# VisionFlow 算法 SDK

给第三方调用机器视觉算法的**原生接口**。核心是 `vision_algo.dll` 导出的一套稳定
C ABI，第三方用 **C / C++ / C# / Python** 都能直接调用算法 `process`。

## 目录结构

```
sdk/
├─ include/        头文件（C/C++ 用）
│   ├─ vision_algo.h     函数声明：create / set_param / set_input / process / get_output ...
│   └─ vision_types.h    数据结构：VzImage / VzPoint2D / VzPose2D ... 与类型枚举
├─ lib/
│   └─ vision_algo.lib   导入库（MSVC/MinGW 链接用）
├─ bin/                 运行时
│   ├─ vision_algo.dll        算法 DLL
│   └─ opencv_world4130.dll   唯一运行时依赖（OpenCV，64MB）
└─ samples/
    ├─ c/sample_sobel.c        C 示例
    ├─ csharp/                 C# 示例（dotnet run）
    └─ python/sample_sobel.py  Python 示例（ctypes，无第三方依赖）
```

> 分发时把 `bin/` 里的两个 DLL 放到调用方 exe 同目录（或加入 `PATH`）即可。
> 除相机相关算法外，**不需要安装海康 MVS**（MvCameraControl 为延迟加载）。

## 调用流程（所有语言通用）

```
create(algo_name) → set_param(...) → set_input(...) → process() → get_output(...) → vz_free → release
```

- 算法名、端口名、参数名：用 `vz_algo_list()` 取全部算法名，
  `vz_algo_describe("SobelEdge")` 取某算法的输入/输出端口与参数 JSON。
- 参数值一律传 **JSON 字符串**（数字 `"3"`、字符串 `"\"abc\""`、数组 `"[1,2]"`）。
- 内存：`get_output` 返回的指针由 DLL 分配，**必须 `vz_free`**；`const char*` 返回值
  指向静态缓冲，下次同线程调用即失效，需立即拷贝。

## 内存与线程

- 每个 `VzAlgoCtx` 实例非线程安全；可创建多个实例并行。
- 图像输入：填好 `VzImage` 结构（务必设 `magic = VZ_IMAGE_MAGIC`），`data` 指向的像素
  缓冲在调用期间由调用方持有（`owns_data = 0`）。

## 重新编译 DLL

```
cmake -S vision-algo-core -B vision-algo-core/build
cmake --build vision-algo-core/build --config Release --target vision_algo
```
产物：`build/bin/Release/vision_algo.dll` + `build/lib/Release/vision_algo.lib`。
若安装了海康 MVS SDK，相机会话自动启用（延迟加载，不影响无相机环境分发）。
