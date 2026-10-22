// ort_embed_test.cpp — 用 Python embed 调用 onnxruntime
// 完全绕过 OpenCV DNN 的 YOLO end-to-end forward bug
#include <Python.h>
#include <cstdio>
#include <vector>
int main() {
    printf("Python embed test start...\n");
    Py_Initialize();
    PyRun_SimpleString("import sys; print('Python:', sys.version)");
    PyRun_SimpleString("import onnxruntime as ort; print('ORT:', ort.__version__)");
    PyRun_SimpleString(
        "import numpy as np, cv2, onnxruntime as ort\n"
        "img = cv2.imread(r'c:\\Users\\qiaoy\\Desktop\\frame0.jpg')\n"
        "r = 640/max(img.shape[:2])\n"
        "nh,nw = int(img.shape[0]*r), int(img.shape[1]*r)\n"
        "res = cv2.resize(img,(nw,nh))\n"
        "dh,dw=640-nh,640-nw\n"
        "lb = cv2.copyMakeBorder(res,dh//2,dh-dh//2,dw//2,dw-dw//2,cv2.BORDER_CONSTANT,(114,114,114))\n"
        "blob = cv2.dnn.blobFromImage(lb,1/255.,(640,640),swapRB=True)\n"
        "sess = ort.InferenceSession(r'c:\\Users\\qiaoy\\Desktop\\yolo26n.onnx', providers=['CPUExecutionProvider'])\n"
        "out = sess.run(None, {sess.get_inputs()[0].name: blob})[0][0]\n"
        "c0,c1,c2 = float(out[0,4]), float(out[1,4]), float(out[2,4])\n"
        "print(f'conf[0..2]={c0:.6f} {c1:.6f} {c2:.6f} allSame={abs(c0-c1)<1e-6}')\n"
    );
    Py_Finalize();
    printf("Done\n");
    return 0;
}
