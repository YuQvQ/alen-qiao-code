using System.Runtime.InteropServices;

namespace VisionFlow.Studio.Interop;

/// <summary>
/// P/Invoke 声明：严格对应 vision-algo-core/include/vision_algo.h
/// 字符串采用 UTF-8 编解码（C 端期望 char* 为系统 ANSI 时亦可用 LPStr，
/// 为跨平台统一性此处使用 UTF-8 自定义封送）。
///
/// 内存约定：
///   - 入参 string 由 .NET 自动封送为 ANSI/UTF-8 字符串
///   - 出参 IntPtr 指向算法 DLL 内部静态/堆缓冲，需手动 PtrToStringUTF8
///   - 通过 get_output 拿到的内存指针必须 vz_free 释放
/// </summary>
internal static class NativeMethods
{
    private const string DllName = "vision_algo";

    // === 生命周期 ===
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl,
               CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
    public static extern int vz_algo_create(string algoName, out IntPtr ctx);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vz_algo_release(IntPtr ctx);

    // === 参数 ===
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl,
               CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
    public static extern int vz_algo_set_param(IntPtr ctx, string key, string valueJson);

    // === 输入 ===
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl,
               CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
    public static extern int vz_algo_set_input(IntPtr ctx, string portName,
                                                IntPtr data, int typeTag);

    // === 执行 ===
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vz_algo_process(IntPtr ctx);

    // === 输出 ===
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl,
               CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
    public static extern int vz_algo_get_output(IntPtr ctx, string portName,
                                                 out IntPtr outData, out int outCount,
                                                 out int outTypeTag);

    // === 内存 ===
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vz_free(IntPtr ptr);

    // === 自描述 ===
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr vz_algo_list();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl,
               CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
    public static extern IntPtr vz_algo_describe(string algoName);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr vz_algo_version();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr vz_algo_last_error(IntPtr ctx);

    // === 相机枚举（海康 MVS） ===
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr vz_enum_cameras();
}
