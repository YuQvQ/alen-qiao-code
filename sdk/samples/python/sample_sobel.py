# sample_sobel.py —— 第三方用 Python 调用 vision_algo.dll 的最小示例
#
# 依赖：仅标准库 ctypes。运行前把 sdk\bin\vision_algo.dll、opencv_world4130.dll
# 放在本目录或 PATH 中。
#
#   python sample_sobel.py

import os
import sys
import ctypes
from ctypes import c_int, c_uint, c_int64, c_uint64, c_void_p, c_char_p, POINTER, byref, Structure

DLL_NAME = "vision_algo.dll"

# 找到 DLL（同目录优先）
_here = os.path.dirname(os.path.abspath(__file__))
for p in (_here, os.path.join(_here, "..", "..", "bin")):
    full = os.path.abspath(os.path.join(p, DLL_NAME))
    if os.path.exists(full):
        os.add_dll_directory(os.path.dirname(full))  # 让 opencv 依赖也能被找到
        lib = ctypes.CDLL(full)
        break
else:
    lib = ctypes.CDLL(DLL_NAME)

VZ_TYPE_IMAGE = 1
VZ_IMAGE_MAGIC = 0x565A494D  # "VZIM"


class VzImage(Structure):
    _fields_ = [
        ("width", c_int), ("height", c_int),
        ("channels", c_int), ("pixel_type", c_int),
        ("data", c_void_p),
        ("step", c_uint64),
        ("owns_data", c_int),
        ("magic", c_uint),
    ]


# 函数签名
lib.vz_algo_create.argtypes = [c_char_p, POINTER(c_void_p)]
lib.vz_algo_create.restype = c_int
lib.vz_algo_set_param.argtypes = [c_void_p, c_char_p, c_char_p]
lib.vz_algo_set_param.restype = c_int
lib.vz_algo_set_input.argtypes = [c_void_p, c_char_p, POINTER(VzImage), c_int]
lib.vz_algo_set_input.restype = c_int
lib.vz_algo_process.argtypes = [c_void_p]
lib.vz_algo_process.restype = c_int
lib.vz_algo_get_output.argtypes = [c_void_p, c_char_p, POINTER(c_void_p), POINTER(c_int), POINTER(c_int)]
lib.vz_algo_get_output.restype = c_int
lib.vz_free.argtypes = [c_void_p]
lib.vz_algo_release.argtypes = [c_void_p]
lib.vz_algo_version.restype = c_void_p
lib.vz_algo_last_error.argtypes = [c_void_p]
lib.vz_algo_last_error.restype = c_void_p


def _s(ptr):
    return ctypes.string_at(ptr).decode("utf-8", "replace") if ptr else ""


def main():
    print("version:", _s(lib.vz_algo_version()))

    ctx = c_void_p()
    rc = lib.vz_algo_create(b"SobelEdge", byref(ctx))
    if rc != 0:
        print("create:", _s(lib.vz_algo_last_error(None))); return

    lib.vz_algo_set_param(ctx, b"ksize", b"3")
    lib.vz_algo_set_param(ctx, b"dx", b"1")
    lib.vz_algo_set_param(ctx, b"dy", b"0")

    # 合成测试图（左暗右亮）
    w, h = 320, 240
    pixels = bytearray((220 if x > w // 2 else 30) for y in range(h) for x in range(w))
    buf = (ctypes.c_ubyte * len(pixels)).from_buffer(pixels)  # 保持 pixels 存活

    img = VzImage(w, h, 1, 0, ctypes.cast(buf, c_void_p), w, 0, VZ_IMAGE_MAGIC)
    rc = lib.vz_algo_set_input(ctx, b"image", byref(img), VZ_TYPE_IMAGE)
    if rc != 0:
        print("set_input:", _s(lib.vz_algo_last_error(ctx))); return

    rc = lib.vz_algo_process(ctx)  # ← 直接调用 process
    if rc != 0:
        print("process:", _s(lib.vz_algo_last_error(ctx))); return

    out, count, typ = c_void_p(), c_int(), c_int()
    rc = lib.vz_algo_get_output(ctx, b"image", byref(out), byref(count), byref(typ))
    if rc != 0 or not out:
        print("get_output failed"); return

    res = ctypes.cast(out, POINTER(VzImage)).contents
    print(f"result: {res.width}x{res.height} ch={res.channels} type={typ.value}")

    lib.vz_free(out)
    lib.vz_algo_release(ctx)
    print("done.")


if __name__ == "__main__":
    main()
