using VisionFlow.Studio.Models;
using VisionFlow.Studio.TypeSystem;

namespace VisionFlow.Studio.Nodes;

// ============================================================
// 节点工厂：根据 NodeDescriptor 创建运行时 Node 实例。
//
// 实例化规则：
//   - Inputs/Outputs 一次性构造（按 descriptor 顺序），运行时不变
//   - Parameters 用 ParameterDescriptor.CoerceDefault() 初始化
//   - 位置默认 (0,0)，由画布拖拽时更新
//   - InstanceId 自动生成
//
// 类型解析失败（节点 JSON 引用未知 PortType）会抛 ArgumentException，
// 在 NodeRegistry 加载期或画布放置期抛出，便于用户及早发现配置错误。
// ============================================================

public static class NodeFactory
{
    public static Node Create(NodeDescriptor d, double x = 0, double y = 0)
    {
        if (d is null) throw new ArgumentNullException(nameof(d));
        if (string.IsNullOrWhiteSpace(d.TypeId))
            throw new ArgumentException("descriptor.TypeId is empty", nameof(d));

        // 构造输入端口
        var inputs = new List<Port>(d.Inputs.Count);
        foreach (var p in d.Inputs)
        {
            var type = p.ResolveType();   // 找不到类型会抛异常
            inputs.Add(new Port(p.Name, PortDirection.Input, type, p.Required, p.Description));
        }

        // 构造输出端口
        var outputs = new List<Port>(d.Outputs.Count);
        foreach (var p in d.Outputs)
        {
            var type = p.ResolveType();
            outputs.Add(new Port(p.Name, PortDirection.Output, type, p.Required, p.Description));
        }

        // 构造节点
        var node = new Node(
            typeId: d.TypeId,
            displayName: d.DisplayName,
            category: d.Category,
            algoName: d.AlgoBinding.AlgoName,
            dllName: d.AlgoBinding.Dll,
            inputs: inputs,
            outputs: outputs)
        {
            X = x,
            Y = y,
        };

        // 初始化默认参数
        foreach (var prm in d.Parameters)
        {
            node.Parameters[prm.Name] = prm.CoerceDefault();
        }

        return node;
    }

    /// <summary>
    /// 按 type_id 从 NodeRegistry 查 descriptor，再实例化。
    /// 查不到时返回 null（画布层负责提示用户）。
    /// </summary>
    public static Node? CreateByTypeId(string typeId, double x = 0, double y = 0)
    {
        var d = NodeRegistry.Instance.Get(typeId);
        return d is null ? null : Create(d, x, y);
    }
}
