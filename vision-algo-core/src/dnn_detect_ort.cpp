// dnn_detect_ort.cpp — Python embed + onnxruntime ONNX 推理
// 绕过 OpenCV 4.13 对 YOLO end-to-end ONNX 的 forward bug
// 零拷贝：PyArray_SimpleNewFromData 把 C++ blob 直接变成 numpy array
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace vz {

static std::mutex g_py_mtx;
static bool g_py_ok = false;

// ---------- 轻量检查点日志（写到 DLL 同目录 ort_ckpt.log，排查卡死用） ----------
static void ckptf(const char* fmt, ...) {
    // 定位 vision_algo.dll 自身所在目录（而不是 exe 目录，防止加载了别的副本）
    HMODULE hmod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&ckptf, &hmod);
    if (!hmod) return;
    wchar_t dll_path[MAX_PATH] = {};
    GetModuleFileNameW(hmod, dll_path, MAX_PATH);
    wchar_t* slash = wcsrchr(dll_path, L'\\');
    if (!slash) return;
    *slash = 0;
    wchar_t log_path[MAX_PATH];
    swprintf_s(log_path, L"%s\\ort_ckpt.log", dll_path);
    FILE* f = nullptr;
    _wfopen_s(&f, log_path, L"a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d][tid=%u] ", st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, GetCurrentThreadId());
    va_list args; va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

static bool ensurePy() {
    std::lock_guard<std::mutex> lk(g_py_mtx);
    if (g_py_ok) return true;

    ckptf("ensurePy: start");

    static const wchar_t* kPythonHome = L"C:\\Users\\qiaoy\\AppData\\Local\\Programs\\Python\\Python314";
    SetEnvironmentVariableW(L"PYTHONHOME", kPythonHome);

    if (Py_IsInitialized()) {
        ckptf("ensurePy: python already inited (host)");
        g_py_ok = true;
        return true;
    }

    Py_SetPythonHome(kPythonHome);
    ckptf("ensurePy: calling Py_Initialize ...");
    Py_Initialize();
    ckptf("ensurePy: Py_Initialize OK");

    import_array();
    ckptf("ensurePy: import_array (numpy) OK");

    // GUI 进程没有控制台，stdout/stderr 可能是无效句柄，重定向到 devnull 防止 print 出问题
    PyRun_SimpleString(
        "import sys, os\n"
        "try:\n"
        "    _f = open(os.devnull, 'w')\n"
        "    sys.stdout = _f\n"
        "    sys.stderr = _f\n"
        "except Exception:\n"
        "    pass\n");
    ckptf("ensurePy: stdio redirected to devnull");

    // ★★★ 关键修复：释放初始化线程持有的 GIL ★★★
    // Py_Initialize 之后本线程一直持有 GIL；WPF 的 DAG 每次执行可能用不同线程，
    // 其他线程 PyGILState_Ensure() 会永远等待 → 卡死。必须 SaveThread 释放。
    PyEval_SaveThread();
    ckptf("ensurePy: GIL released via PyEval_SaveThread");

    g_py_ok = true;
    return true;
}

// ORT forward：blob(N,3,H,W) float32 → output(N,6) float32
// 零拷贝传入 blob，forward 结果写进 out_raw。返回行数（失败 0）
int ortForwardOnnx(const char* model_path,
                   const float* blob, int n, int c, int h, int w,
                   std::vector<float>& out_raw) {
    if (!ensurePy()) return 0;
    ckptf("forward: acquiring GIL ...");
    PyGILState_STATE gs = PyGILState_Ensure();
    ckptf("forward: GIL acquired");

    int ret_rows = 0;
    PyObject* mainMod = PyImport_ImportModule("__main__");   // new ref
    if (!mainMod) { PyGILState_Release(gs); ckptf("forward: __main__ import FAILED"); return 0; }
    PyObject* g = PyModule_GetDict(mainMod);                 // borrowed ref，绝不 DECREF！

    // 1. C++ blob → numpy array（零拷贝）
    npy_intp dims[4] = {n, c, h, w};
    PyArrayObject* blob_arr = (PyArrayObject*)PyArray_SimpleNewFromData(
        4, dims, NPY_FLOAT32, (void*)blob);                  // new ref
    if (!blob_arr) { Py_DECREF(mainMod); PyGILState_Release(gs); ckptf("forward: SimpleNewFromData FAILED"); return 0; }
    // 放入 globals（SetItemString 会 INCREF，随后 DECREF 我们自己的）
    PyDict_SetItemString(g, "_vz_blob", (PyObject*)blob_arr);
    Py_DECREF(blob_arr);

    // 2. Python 代码：session 按 model_path 缓存在 __main__._vz_sessions
    std::string pyCode =
        std::string(
        "import numpy as np, onnxruntime as ort\n"
        "try:\n"
        "    if '_vz_sessions' not in globals(): globals()['_vz_sessions'] = {}\n"
        "    _ss = globals()['_vz_sessions']\n"
        "    _mp = r'") + model_path + "'\n"
        "    if _mp not in _ss:\n"
        "        _ss[_mp] = ort.InferenceSession(_mp, providers=['CPUExecutionProvider'])\n"
        "    sess = _ss[_mp]\n"
        "    inp_name = sess.get_inputs()[0].name\n"
        "    blob_np = np.ascontiguousarray(globals()['_vz_blob'], dtype=np.float32)\n"
        "    out0 = sess.run(None, {inp_name: blob_np})[0]\n"
        "    out_flat = np.ascontiguousarray(out0.reshape(-1, out0.shape[-1]), dtype=np.float32)\n"
        "    globals()['_vz_result'] = out_flat\n"
        "    globals()['_vz_shape'] = out_flat.shape\n"
        "except Exception as e:\n"
        "    globals()['_vz_result'] = None\n"
        "    globals()['_vz_error'] = str(e)\n";

    PyObject* code = Py_CompileString(pyCode.c_str(), "<ort>", Py_file_input);  // new ref
    if (code) {
        PyObject* result = PyEval_EvalCode(code, g, g);  // new ref / NULL on error
        Py_DECREF(code);
        Py_XDECREF(result);
    }
    ckptf("forward: python eval done");

    // 3. 取结果（borrowed refs）
    PyObject* res = PyDict_GetItemString(g, "_vz_result");
    PyObject* shp = PyDict_GetItemString(g, "_vz_shape");
    if (res && res != Py_None && shp && PyTuple_Check(shp) && PyTuple_Size(shp) >= 2) {
        long rows = PyLong_AsLong(PyTuple_GetItem(shp, 0));
        long cols = PyLong_AsLong(PyTuple_GetItem(shp, 1));
        const float* p = (const float*)PyArray_DATA((PyArrayObject*)res);
        out_raw.assign(p, p + (size_t)rows * cols);
        ret_rows = (int)rows;
    } else {
        PyObject* err = PyDict_GetItemString(g, "_vz_error");
        if (err) ckptf("forward: PYTHON ERROR: %s", PyUnicode_AsUTF8(err));
    }

    // 4. 清掉 _vz_blob（它引用的 C++ blob 内存马上释放，绝不能留引用）
    PyDict_SetItemString(g, "_vz_blob", Py_None);
    PyDict_SetItemString(g, "_vz_result", Py_None);

    Py_DECREF(mainMod);
    PyGILState_Release(gs);
    ckptf("forward: done, rows=%d", ret_rows);
    return ret_rows;
}

} // namespace vz
