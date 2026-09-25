namespace VisionFlow.Studio.TypeSystem;

/// <summary>
/// 端口类型目录：单例 + 启动初始化 + 运行时可扩展。
/// 节点 JSON 中以字符串名引用类型，TypeRegistry 负责解析为 PortType 对象。
///
/// 与 C 端 vision_types.h 中的 VzPortType 枚举严格保持名称映射。
/// </summary>
public static class TypeRegistry
{
    private static readonly Dictionary<string, PortType> _types =
        new(StringComparer.Ordinal);

    static TypeRegistry()
    {
        // === 通配类型 ===
        // Any：可与任意类型互连（用于 If 等“类型透传”节点：输入 obj 任意类型，输出原样透传）。
        Register(new AnyPortType());

        // === 标量 ===
        Register(new PortType("Image",       isList: false, isScalar: false, category: "Image"));
        Register(new PortType("Region",      isList: false, isScalar: false, category: "Image"));
        Register(new NumericPortType("Int"));
        Register(new NumericPortType("Double"));
        Register(new PortType("Bool",         isList: false, isScalar: true,  category: "Scalar"));
        Register(new PortType("String",      isList: false, isScalar: true,  category: "Scalar"));
        Register(new PortType("Pose2D",      isList: false, isScalar: false, category: "Geometry"));
        Register(new PortType("DoubleRange", isList: false, isScalar: false, category: "Scalar"));

        // === 列表 ===
        Register(new PortType("MatchResultList", isList: true, isScalar: false, category: "Result"));
        Register(new PortType("Point2DList",     isList: true, isScalar: false, category: "Geometry"));
        Register(new PortType("RectList",        isList: true, isScalar: false, category: "Geometry"));
        Register(new PortType("Pose2DList",     isList: true, isScalar: false, category: "Geometry"));
    }

    public static void Register(PortType type)
    {
        if (type == null) throw new ArgumentNullException(nameof(type));
        _types[type.Name] = type;
    }

    public static PortType? Get(string? name)
        => name is null || !_types.TryGetValue(name, out var t) ? null : t;

    public static IReadOnlyCollection<PortType> All => _types.Values;

    /// <summary>
    /// 强类型连接校验（核心规则）：
    ///   - 任一端为 Any（通配）→ 允许连接；
    ///   - 否则两端类型名严格相等（或源端支持隐式转换）。
    /// </summary>
    public static bool CanConnect(PortType? from, PortType? to)
    {
        if (from is null || to is null) return false;
        if (from.Name == "Any" || to.Name == "Any") return true;
        return from.CanConnectTo(to);
    }

    /// <summary>
    /// 解析类型字符串。找不到时抛 ArgumentException（防止节点描述引用未知类型）。
    /// </summary>
    public static PortType Require(string name)
        => Get(name) ?? throw new ArgumentException($"Unknown port type: {name}", nameof(name));
}
