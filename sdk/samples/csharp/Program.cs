// Program.cs —— 第三方用 C# 调用 vision_algo.dll 的最小示例
//
// 运行：把 sdk\bin\*.dll 拷到本目录（或加入 PATH），然后
//   dotnet run -c Release
//
// 演示：create -> set_param -> set_input -> process -> get_output -> free -> release

using System;
using System.Runtime.InteropServices;

internal static class Native
{
    const string DLL = "vision_algo";

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vz_algo_create([MarshalAs(UnmanagedType.LPUTF8Str)] string name, out IntPtr ctx);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vz_algo_set_param(IntPtr ctx,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string key,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string valueJson);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vz_algo_set_input(IntPtr ctx,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string port, ref VzImage img, int typeTag);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vz_algo_process(IntPtr ctx);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vz_algo_get_output(IntPtr ctx,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string port,
        out IntPtr outData, out int count, out int typeTag);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vz_free(IntPtr ptr);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vz_algo_release(IntPtr ctx);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr vz_algo_version();

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr vz_algo_last_error(IntPtr ctx);
}

[StructLayout(LayoutKind.Sequential)]
internal struct VzImage
{
    public int width, height, channels, pixel_type;
    public IntPtr data;
    public ulong step;
    public int owns_data;
    public uint magic;
}

internal static class Program
{
    const int VZ_TYPE_IMAGE = 1;
    const uint VZ_IMAGE_MAGIC = 0x565A494D; // "VZIM"

    static void Main()
    {
        Console.WriteLine("version: " + Utf8(Native.vz_algo_version()));

        IntPtr ctx;
        int rc = Native.vz_algo_create("SobelEdge", out ctx);
        if (rc != 0) { Console.WriteLine("create: " + Utf8(Native.vz_algo_last_error(IntPtr.Zero))); return; }

        Native.vz_algo_set_param(ctx, "ksize", "3");
        Native.vz_algo_set_param(ctx, "dx", "1");
        Native.vz_algo_set_param(ctx, "dy", "0");

        // 合成测试图（左暗右亮）
        int w = 320, h = 240;
        byte[] pixels = new byte[w * h];
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                pixels[y * w + x] = (byte)(x > w / 2 ? 220 : 30);

        IntPtr buf = Marshal.AllocHGlobal(pixels.Length);
        Marshal.Copy(pixels, 0, buf, pixels.Length);

        var img = new VzImage
        {
            width = w, height = h, channels = 1, pixel_type = 0,
            data = buf, step = (ulong)w, owns_data = 0, magic = VZ_IMAGE_MAGIC
        };

        rc = Native.vz_algo_set_input(ctx, "image", ref img, VZ_TYPE_IMAGE);
        if (rc != 0) { Console.WriteLine("set_input: " + Utf8(Native.vz_algo_last_error(ctx))); return; }

        rc = Native.vz_algo_process(ctx); // ← 直接调用 process
        if (rc != 0) { Console.WriteLine("process: " + Utf8(Native.vz_algo_last_error(ctx))); return; }

        IntPtr outPtr; int count, type;
        rc = Native.vz_algo_get_output(ctx, "image", out outPtr, out count, out type);
        if (rc != 0 || outPtr == IntPtr.Zero) { Console.WriteLine("get_output failed"); return; }

        var res = Marshal.PtrToStructure<VzImage>(outPtr);
        Console.WriteLine($"result: {res.width}x{res.height} ch={res.channels} type={type}");

        Native.vz_free(outPtr);
        Native.vz_algo_release(ctx);
        Marshal.FreeHGlobal(buf);
        Console.WriteLine("done.");
    }

    static string Utf8(IntPtr p) => p == IntPtr.Zero ? "" : Marshal.PtrToStringUTF8(p) ?? "";
}
