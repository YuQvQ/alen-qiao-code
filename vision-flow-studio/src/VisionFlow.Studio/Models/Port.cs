using VisionFlow.Studio.TypeSystem;

namespace VisionFlow.Studio.Models;

public enum PortDirection
{
    Input,
    Output
}

/// <summary>
/// 节点端口实例（运行时数据）。
/// 不直接持有数据；数据由 DagEngine 在执行时通过 PortCache 字典临时挂载。
/// </summary>
public sealed class Port
{
    public string Name { get; }
    public PortDirection Direction { get; }
    public PortType Type { get; }
    public bool Required { get; }
    public string Description { get; }
    public Node Owner { get; internal set; } = null!;

    public Port(string name, PortDirection dir, PortType type, bool required = true, string? description = null)
    {
        Name = name ?? throw new ArgumentNullException(nameof(name));
        Direction = dir;
        Type = type ?? throw new ArgumentNullException(nameof(type));
        Required = required;
        Description = description ?? "";
    }

    /// <summary>端口的完整提示文本（名称/类型/说明/是否必须）</summary>
    public string ToolTipText
    {
        get
        {
            var req = Required ? "（必须）" : "（可选）";
            var desc = string.IsNullOrEmpty(Description) ? "" : $"\n说明：{Description}";
            return $"{Name} : {Type.Name}{req}{desc}";
        }
    }

    public override string ToString() => $"{Direction}:{Name}:{Type.Name}";
}
