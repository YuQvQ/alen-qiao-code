using System.Runtime.InteropServices;
using System.Text.Encodings.Web;
using System.Text.Json;

namespace VisionFlow.Studio.Interop;

/// <summary>
/// 算法层互操作异常：包含错误码 + 上一次 C 端错误字符串。
/// </summary>
public sealed class VzInteropException : Exception
{
    public int ErrorCode { get; }
    public VzInteropException(string message, int rc = 0) : base(message) { ErrorCode = rc; }
}

/// <summary>
/// 算法实例的高层封装。
/// 职责：
///   1. 调用 vz_algo_create 创建上下文，Dispose 时 vz_algo_release
///   2. SetParam：将 .NET 对象序列化为 JSON 字符串传给 C 端
///   3. SetInput(ref VzImage)：通过 fixed 指针把结构体传给 C
///   4. Process：执行算法
///   5. GetOutput*：把 C 端 malloc 出来的数组解码为 .NET 数组，并 vz_free
///
/// 线程模型：每个实例非线程安全；可创建多实例并行。
/// DLL 加载：P/Invoke 由 .NET 运行时按 vision_algo.dll 名查找，路径策略：
///   1) 应用基目录
///   2) runtimes\win-x64\native\
///   3) %PATH%
/// 若找不到，首次调用会抛 DllNotFoundException。
/// </summary>
public sealed class VzAlgoInterop : IDisposable
{
    private IntPtr _ctx;
    public string AlgoName { get; }
    public bool IsLoaded => _ctx != IntPtr.Zero;

    /// <summary>
    /// JSON 序列化选项：禁用 \uXXXX 转义，让中文字符以 UTF-8 字节直出。
    /// 默认 JsonSerializer.Serialize 会把所有非 ASCII 字符转义成 \uXXXX，
    /// 这会让 C++ 端拿到字面 "\u6DB2\u6001..." 字符串而不是 UTF-8 中文，
    /// 导致 cv::imread 等接收路径的 API 全部失败。
    /// </summary>
    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping
    };

    public VzAlgoInterop(string algoName)
    {
        AlgoName = algoName ?? throw new ArgumentNullException(nameof(algoName));
        var rc = NativeMethods.vz_algo_create(algoName, out _ctx);
        if (rc != 0)
            throw new VzInteropException(
                $"vz_algo_create('{algoName}') failed rc={rc}: {GetLastError()}", rc);
    }

    ~VzAlgoInterop() => Dispose(false);

    // ============================================================
    // 参数
    // ============================================================
    public void SetParam(string key, object? value)
    {
        var json = JsonSerializer.Serialize(value, JsonOpts);
        ThrowIfFailed(NativeMethods.vz_algo_set_param(_ctx, key, json),
                       $"set_param({key}={json})");
    }

    // ============================================================
    // 输入（图像）
    // ============================================================
    /// <summary>
    /// 写入图像输入端口。
    /// 调用方负责构造 VzImage 结构（data 字段为非托管内存，调用期间不可释放）。
    /// 默认 type_tag=Image；Region 端口在 ABI 中复用 VzImage 结构，
    /// 但 type_tag 必须传 Region，否则 C 端校验会拒绝。
    /// </summary>
    public unsafe void SetInputImage(string portName, ref VzImage img,
                                       VzPortType typeTag = VzPortType.Image)
    {
        fixed (VzImage* p = &img)
        {
            var rc = NativeMethods.vz_algo_set_input(_ctx, portName,
                                                      (IntPtr)p, (int)typeTag);
            ThrowIfFailed(rc, $"set_input({portName}:{typeTag})");
        }
    }

    // ============================================================
    // 输入（其它类型）
    // ============================================================
    /// <summary>
    /// 写入 String 输入端口。C 端按 const char* 读取，以 \0 结尾。
    /// </summary>
    public void SetInputString(string portName, string value)
    {
        var bytes = System.Text.Encoding.UTF8.GetBytes(value ?? "");
        // 分配非托管内存（含结尾 \0）
        IntPtr buf = Marshal.AllocHGlobal(bytes.Length + 1);
        try
        {
            Marshal.Copy(bytes, 0, buf, bytes.Length);
            Marshal.WriteByte(buf, bytes.Length, 0);  // \0
            var rc = NativeMethods.vz_algo_set_input(_ctx, portName, buf, (int)VzPortType.String);
            ThrowIfFailed(rc, $"set_input({portName}:String)");
        }
        finally
        {
            // C 端在 process 之前已经拷贝了字符串，可以安全释放
            Marshal.FreeHGlobal(buf);
        }
    }

    /// <summary>
    /// 写入 Point2DList 输入端口。构造 VzPoint2DArray 结构传给 C 端。
    /// C 端在 setInput 内会拷贝数组，调用返回后可释放。
    /// </summary>
    public void SetInputPoints(string portName, VzPoint2D[] points)
    {
        IntPtr arrPtr = IntPtr.Zero;
        IntPtr dataPtr = IntPtr.Zero;
        try
        {
            if (points != null && points.Length > 0)
            {
                int elemSize = Marshal.SizeOf<VzPoint2D>();
                dataPtr = Marshal.AllocHGlobal(elemSize * points.Length);
                for (int i = 0; i < points.Length; i++)
                    Marshal.StructureToPtr(points[i], dataPtr + i * elemSize, false);
            }
            var arr = new VzPoint2DArray { data = dataPtr, count = points?.Length ?? 0 };
            arrPtr = Marshal.AllocHGlobal(Marshal.SizeOf<VzPoint2DArray>());
            Marshal.StructureToPtr(arr, arrPtr, false);

            var rc = NativeMethods.vz_algo_set_input(_ctx, portName,
                                                     arrPtr, (int)VzPortType.Point2DList);
            ThrowIfFailed(rc, $"set_input({portName}:Point2DList)");
        }
        finally
        {
            if (arrPtr != IntPtr.Zero) Marshal.FreeHGlobal(arrPtr);
            if (dataPtr != IntPtr.Zero) Marshal.FreeHGlobal(dataPtr);
        }
    }

    /// <summary>
    /// 写入 Pose2D 输入端口。C 端在 setInput 内拷贝结构。
    /// </summary>
    public void SetInputPose(string portName, VzPose2D pose)
    {
        IntPtr ptr = Marshal.AllocHGlobal(Marshal.SizeOf<VzPose2D>());
        try
        {
            Marshal.StructureToPtr(pose, ptr, false);
            var rc = NativeMethods.vz_algo_set_input(_ctx, portName,
                                                     ptr, (int)VzPortType.Pose2D);
            ThrowIfFailed(rc, $"set_input({portName}:Pose2D)");
        }
        finally
        {
            Marshal.FreeHGlobal(ptr);
        }
    }

    /// <summary>写入 Int 输入端口。</summary>
    public void SetInputInt(string portName, int value)
    {
        IntPtr ptr = Marshal.AllocHGlobal(sizeof(int));
        try
        {
            Marshal.WriteInt32(ptr, value);
            var rc = NativeMethods.vz_algo_set_input(_ctx, portName, ptr, (int)VzPortType.Int);
            ThrowIfFailed(rc, $"set_input({portName}:Int)");
        }
        finally { Marshal.FreeHGlobal(ptr); }
    }

    /// <summary>写入 Double 输入端口。</summary>
    public void SetInputDouble(string portName, double value)
    {
        IntPtr ptr = Marshal.AllocHGlobal(sizeof(double));
        try
        {
            Marshal.StructureToPtr(value, ptr, false);
            var rc = NativeMethods.vz_algo_set_input(_ctx, portName, ptr, (int)VzPortType.Double);
            ThrowIfFailed(rc, $"set_input({portName}:Double)");
        }
        finally { Marshal.FreeHGlobal(ptr); }
    }

    /// <summary>写入 Bool 输入端口（以 int 0/1 传输）。</summary>
    public void SetInputBool(string portName, bool value)
    {
        IntPtr ptr = Marshal.AllocHGlobal(sizeof(int));
        try
        {
            Marshal.WriteInt32(ptr, value ? 1 : 0);
            var rc = NativeMethods.vz_algo_set_input(_ctx, portName, ptr, (int)VzPortType.Bool);
            ThrowIfFailed(rc, $"set_input({portName}:Bool)");
        }
        finally { Marshal.FreeHGlobal(ptr); }
    }

    // ============================================================
    // 执行
    // ============================================================
    public void Process()
    {
        ThrowIfFailed(NativeMethods.vz_algo_process(_ctx), "process");
    }

    // ============================================================
    // 输出（强类型解码）
    // ============================================================
    public VzPoint2D[] GetOutputPoints(string portName)
    {
        var (ptr, count, _) = GetRawOutput(portName);
        if (count == 0 || ptr == IntPtr.Zero) return Array.Empty<VzPoint2D>();
        try
        {
            var arr = new VzPoint2D[count];
            int size = Marshal.SizeOf<VzPoint2D>();
            for (int i = 0; i < count; i++)
                arr[i] = Marshal.PtrToStructure<VzPoint2D>(ptr + i * size);
            return arr;
        }
        finally { NativeMethods.vz_free(ptr); }
    }

    public VzRect[] GetOutputRects(string portName)
    {
        var (ptr, count, _) = GetRawOutput(portName);
        if (count == 0 || ptr == IntPtr.Zero) return Array.Empty<VzRect>();
        try
        {
            var arr = new VzRect[count];
            int size = Marshal.SizeOf<VzRect>();
            for (int i = 0; i < count; i++)
                arr[i] = Marshal.PtrToStructure<VzRect>(ptr + i * size);
            return arr;
        }
        finally { NativeMethods.vz_free(ptr); }
    }

    public VzMatchResult[] GetOutputMatches(string portName)
    {
        var (ptr, count, _) = GetRawOutput(portName);
        if (count == 0 || ptr == IntPtr.Zero) return Array.Empty<VzMatchResult>();
        try
        {
            var arr = new VzMatchResult[count];
            int size = Marshal.SizeOf<VzMatchResult>();
            for (int i = 0; i < count; i++)
                arr[i] = Marshal.PtrToStructure<VzMatchResult>(ptr + i * size);
            return arr;
        }
        finally { NativeMethods.vz_free(ptr); }
    }

    public int GetOutputInt(string portName)
    {
        var (ptr, count, _) = GetRawOutput(portName);
        if (ptr == IntPtr.Zero) return 0;
        try { return Marshal.ReadInt32(ptr); }
        finally { NativeMethods.vz_free(ptr); }
    }

    public bool GetOutputBool(string portName)
    {
        var (ptr, count, _) = GetRawOutput(portName);
        if (ptr == IntPtr.Zero) return false;
        try { return Marshal.ReadInt32(ptr) != 0; }
        finally { NativeMethods.vz_free(ptr); }
    }

    public double GetOutputDouble(string portName)
    {
        var (ptr, count, _) = GetRawOutput(portName);
        if (ptr == IntPtr.Zero) return 0.0;
        try { return Marshal.PtrToStructure<double>(ptr); }
        finally { NativeMethods.vz_free(ptr); }
    }

    public string GetOutputString(string portName)
    {
        var (ptr, count, _) = GetRawOutput(portName);
        if (ptr == IntPtr.Zero) return "";
        try
        {
            // C 端返回 char*，count 是字符串长度（不含 \0）
            var bytes = new byte[count];
            Marshal.Copy(ptr, bytes, 0, count);
            return System.Text.Encoding.UTF8.GetString(bytes);
        }
        finally { NativeMethods.vz_free(ptr); }
    }

    public VzPose2D GetOutputPose(string portName)
    {
        var (ptr, count, _) = GetRawOutput(portName);
        if (ptr == IntPtr.Zero) return default;
        try { return Marshal.PtrToStructure<VzPose2D>(ptr); }
        finally { NativeMethods.vz_free(ptr); }
    }

    /// <summary>
    /// 获取输出图像（VzImage 结构）。返回的 .data 内存由 caller 负责 vz_free 释放。
    /// 简化：返回 VzImage 拷贝 + 原始 data 指针，调用方调 Dispose 输出对象释放。
    /// </summary>
    public VzImage GetOutputImage(string portName)
    {
        var (ptr, count, _) = GetRawOutput(portName);
        if (ptr == IntPtr.Zero) return default;
        // ptr 指向 VzImage 结构；其内部 .data 由 C 端 malloc，需单独释放
        var img = Marshal.PtrToStructure<VzImage>(ptr);
        NativeMethods.vz_free(ptr);          // 释放外层 VzImage 包装
        // 注意：img.data 仍由 C 端持有，需后续 vz_free
        return img;
    }

    /// <summary>
    /// 释放通过 GetOutputImage 拿到的 .data 内存。
    /// owns_data==1 表示 C 端 malloc 分配，用 vz_free 释放；
    /// owns_data==0 表示 .NET 端 Marshal.AllocHGlobal 分配，用 FreeHGlobal 释放。
    /// </summary>
    public static void FreeVzImageData(VzImage img)
    {
        if (img.data != IntPtr.Zero)
        {
            if (img.owns_data == 0)
                Marshal.FreeHGlobal(img.data);
            else
                NativeMethods.vz_free(img.data);
            img.data = IntPtr.Zero;
        }
    }

    // ============================================================
    // Dispose
    // ============================================================
    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    private void Dispose(bool disposing)
    {
        if (_ctx != IntPtr.Zero)
        {
            NativeMethods.vz_algo_release(_ctx);
            _ctx = IntPtr.Zero;
        }
    }

    // ============================================================
    // 静态：自描述 / 版本
    // ============================================================
    public static string GetVersion()
        => PtrToUtf8(NativeMethods.vz_algo_version()) ?? "unknown";

    public static string[] ListAlgos()
    {
        var json = PtrToUtf8(NativeMethods.vz_algo_list()) ?? "[]";
        return JsonSerializer.Deserialize<string[]>(json) ?? Array.Empty<string>();
    }

    public static string DescribeAlgo(string algoName)
        => PtrToUtf8(NativeMethods.vz_algo_describe(algoName)) ?? "";

    /// <summary>枚举到的相机信息。</summary>
    public sealed class CameraDevice
    {
        public string Sn { get; set; } = "";
        public string Model { get; set; } = "";
        public string Vendor { get; set; } = "";
        public string Type { get; set; } = "";
        public string UserName { get; set; } = "";
        public string Display => $"{Type} | {Model} | SN:{Sn}"
                                  + (string.IsNullOrEmpty(UserName) ? "" : $" ({UserName})");
    }

    private class CameraDeviceDto { public string sn { get; set; } = ""; public string model { get; set; } = "";
        public string vendor { get; set; } = ""; public string type { get; set; } = ""; public string user_name { get; set; } = ""; }

    /// <summary>枚举当前在线的海康相机。失败返回空列表（错误见 out 文本）。</summary>
    public static List<CameraDevice> EnumCameras(out string? error)
    {
        error = null;
        try
        {
            var json = PtrToUtf8(NativeMethods.vz_enum_cameras()) ?? "[]";
            var arr = JsonSerializer.Deserialize<CameraDeviceDto[]>(json) ?? Array.Empty<CameraDeviceDto>();
            return arr.Select(d => new CameraDevice
            {
                Sn = d.sn ?? "", Model = d.model ?? "", Vendor = d.vendor ?? "",
                Type = d.type ?? "", UserName = d.user_name ?? "",
            }).ToList();
        }
        catch (Exception ex)
        {
            error = ex.Message;
            return new List<CameraDevice>();
        }
    }

    // ============================================================
    // Helpers
    // ============================================================
    private (IntPtr ptr, int count, int typeTag) GetRawOutput(string portName)
    {
        var rc = NativeMethods.vz_algo_get_output(_ctx, portName,
                                                   out var ptr, out var count, out var tag);
        ThrowIfFailed(rc, $"get_output({portName})");
        return (ptr, count, tag);
    }

    private string GetLastError()
        => PtrToUtf8(NativeMethods.vz_algo_last_error(_ctx)) ?? "";

    private void ThrowIfFailed(int rc, string op)
    {
        if (rc != 0)
            throw new VzInteropException(
                $"vz {op} failed rc={rc}: {GetLastError()}", rc);
    }

    private static string? PtrToUtf8(IntPtr ptr)
    {
        if (ptr == IntPtr.Zero) return null;
        // C 端返回 ANSI 字符串（多数情况下与 UTF-8 兼容 ASCII 段）
        return Marshal.PtrToStringAnsi(ptr);
    }
}
