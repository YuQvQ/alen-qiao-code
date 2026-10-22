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

/// <summary>坐标系 / 3x3 变换矩阵（行优先 m[0..8]，默认单位阵）。</summary>
[StructLayout(LayoutKind.Sequential)]
public struct VzCoordSystem3x3
{
    public unsafe fixed double m[9];

    public static VzCoordSystem3x3 Identity()
    {
        var r = new VzCoordSystem3x3();
        unsafe
        {
            r.m[0] = 1; r.m[1] = 0; r.m[2] = 0;
            r.m[3] = 0; r.m[4] = 1; r.m[5] = 0;
            r.m[6] = 0; r.m[7] = 0; r.m[8] = 1;
        }
        return r;
    }

    public override string ToString()
    {
        unsafe
        {
            return $"[{m[0]:G6},{m[1]:G6},{m[2]:G6},{m[3]:G6},{m[4]:G6},{m[5]:G6},{m[6]:G6},{m[7]:G6},{m[8]:G6}]";
        }
    }
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
    CoordSys      = 12,   // VzCoordSystem3x3（坐标系/3x3矩阵）
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
