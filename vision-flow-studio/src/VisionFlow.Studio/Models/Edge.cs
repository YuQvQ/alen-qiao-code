using VisionFlow.Studio.TypeSystem;

namespace VisionFlow.Studio.Models;

/// <summary>
/// 节点间的连接边：源节点的输出端口 -> 目标节点的输入端口。
/// 构造时做强类型校验（运行时防御）。
///
/// 连接规则：
///   1. From 必须是输出端口，To 必须是输入端口
///   2. 两端类型名必须严格相等
///   3. 输入端口默认单扇入：同输入端口新增边时，旧边被自动替换（由 Graph 负责）
///   4. 输出端口支持多扇出：一个输出可连到多个输入
/// </summary>
public sealed class Edge
{
    public Guid Id { get; } = Guid.NewGuid();
    public Port From { get; }
    public Port To { get; }

    public Edge(Port from, Port to)
    {
        if (from.Direction != PortDirection.Output)
            throw new ArgumentException("Edge.From must be an output port", nameof(from));
        if (to.Direction != PortDirection.Input)
            throw new ArgumentException("Edge.To must be an input port", nameof(to));
        if (!TypeRegistry.CanConnect(from.Type, to.Type))
            throw new InvalidOperationException(
                $"Type mismatch: '{from.Type.Name}' -> '{to.Type.Name}'");
        From = from;
        To = to;
    }

    public override string ToString()
        => $"{From.Owner.DisplayName}.{From.Name} -> {To.Owner.DisplayName}.{To.Name}";
}
