namespace VisionFlow.Studio.TypeSystem;

/// <summary>
/// 端口类型描述：用于强类型连接校验。
/// 类型名严格相等才允许连接（与 C 端 VzPortType 枚举一一对应）。
///
/// 扩展点：
///   - 子类化重写 CanConnectTo 即可支持隐式转换（如 Int -> Double）
///   - 静态/动态注册新类型：调用 TypeRegistry.Register
/// </summary>
public class PortType
{
    public string Name { get; }
    public bool IsList { get; }
    public bool IsScalar { get; }
    public string Category { get; }
    public string? ColorBrushKey { get; }    // 在 App.xaml 中查找画刷的 key

    public PortType(string name, bool isList = false, bool isScalar = true,
                    string category = "Other", string? colorBrushKey = null)
    {
        Name = name ?? throw new ArgumentNullException(nameof(name));
        IsList = isList;
        IsScalar = isScalar;
        Category = category;
        ColorBrushKey = colorBrushKey ?? $"PortBrush.{name}";
    }

    /// <summary>
    /// 校验是否可连。默认规则：类型名严格相等。
    /// 子类重写可加入"隐式转换"等扩展规则。
    /// </summary>
    public virtual bool CanConnectTo(PortType? other)
    {
        if (other is null) return false;
        return string.Equals(Name, other.Name, StringComparison.Ordinal);
    }

    public override string ToString() => Name;
    public override bool Equals(object? obj) => obj is PortType t && Name == t.Name;
    public override int GetHashCode() => Name.GetHashCode(StringComparison.Ordinal);

    public static bool operator ==(PortType? a, PortType? b) =>
        a?.Name == b?.Name;
    public static bool operator !=(PortType? a, PortType? b) =>
        !(a == b);
}

/// <summary>
/// 数值标量类型：支持 Int → Double 的隐式转换（Int 可连到 Double 端口）。
/// </summary>
public sealed class NumericPortType : PortType
{
    public NumericPortType(string name) : base(name, isList: false, isScalar: true, category: "Scalar") { }

    public override bool CanConnectTo(PortType? other)
    {
        if (other is null) return false;
        if (Name == other.Name) return true;
        // Int 可以隐式转换为 Double
        if (Name == "Int" && other.Name == "Double") return true;
        return false;
    }
}

/// <summary>
/// 通配类型：可与任意类型互连。用于类型透传节点（如 If：输入任意类型，输出原样透传）。
/// 连接放行由 TypeRegistry.CanConnect 统一处理（任一端为 Any 即允许）。
/// </summary>
public sealed class AnyPortType : PortType
{
    public AnyPortType() : base("Any", isList: false, isScalar: false,
        category: "Any", colorBrushKey: "PortBrush.Any") { }

    public override bool CanConnectTo(PortType? other) => other is not null;
}
