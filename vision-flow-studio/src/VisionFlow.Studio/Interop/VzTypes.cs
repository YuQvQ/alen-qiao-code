using System.Runtime.InteropServices;

namespace VisionFlow.Studio.Interop;

// ============================================================
// POD 结构：与 vision_types.h 严格一一对应（内存布局必须相同）
// 字段顺序、类型、对齐都不可修改
// ============================================================

[StructLayout(LayoutKind.Sequential)]
public struct VzImage
{
    public int    width;
    public int    height;
    public int    channels;       // 1=灰 3=BGR 4=BGRA
    public int    pixel_type;     // VzPixelType
    public IntPtr data;
    public ulong  step;           // 行字节数
    public int    owns_data;
    public uint   magic;          // VZ_IMAGE_MAGIC (0x565A494D)
}

[StructLayout(LayoutKind.Sequential)]
public struct VzPoint2D
{
    public double x;
    public double y;
}

[StructLayout(LayoutKind.Sequential)]
public struct VzRect
{
    public double x;       // 中心 X
    public double y;       // 中心 Y
    public double w;
    public double h;
    public double angle;   // 度
}

[StructLayout(LayoutKind.Sequential)]
public struct VzPose2D
{
    public VzPoint2D point;
    public double    angle;
}

[StructLayout(LayoutKind.Sequential)]
public struct VzMatchResult
{
    public VzRect rect;
    public double score;
    public int    template_id;
    public int    _reserved;
}

[StructLayout(LayoutKind.Sequential)]
public struct VzDoubleRange
{
    public double low;
    public double high;
}

// ============================================================
// 数组包装（用于 set_input 传递数组给 C 端）
// C 端期望收到 {data指针, count} 结构
// ============================================================
[StructLayout(LayoutKind.Sequential)]
public struct VzPoint2DArray
{
    public IntPtr data;   // 指向 VzPoint2D[] 的非托管内存
    public int    count;
}

// ============================================================
// 枚举
// ============================================================
public enum VzPixelType
{
    UInt8   = 0,
    UInt16  = 1,
    Float32 = 2,
}

public enum VzPortType
{
    Unknown       = 0,
    Image          = 1,
    Region         = 2,
    MatchResult   = 3,
    Point2DList    = 4,
    RectList      = 5,
    Int            = 6,
    Double         = 7,
    Bool           = 8,
    String         = 9,
    Pose2D         = 10,
    DoubleRange   = 11,
}

public enum VzError
{
    Ok              = 0,
    InvalidArg      = -1,
    NotFound        = -2,
    TypeMismatch   = -3,
    Process         = -4,
    NoMemory        = -5,
    Unsupported     = -6,
    Internal        = -7,
}
