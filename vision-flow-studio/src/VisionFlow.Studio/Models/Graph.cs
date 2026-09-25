using System.Collections.ObjectModel;

namespace VisionFlow.Studio.Models;

/// <summary>
/// 节点流程图：节点 + 边的集合，提供增删查改 + 拓扑排序。
/// 持有两个 ObservableCollection，UI 直接绑定以同步刷新。
///
/// 不变量：
///   - 任何 Edge.From / Edge.To 的 Owner 必须在 Nodes 中
///   - 同一输入端口在 Edges 中至多出现一次（单扇入）
/// </summary>
public sealed class Graph
{
    public ObservableCollection<Node> Nodes { get; } = new();
    public ObservableCollection<Edge> Edges { get; } = new();

    public event EventHandler<Node>? NodeAdded;
    public event EventHandler<Node>? NodeRemoved;
    public event EventHandler<Edge>? EdgeAdded;
    public event EventHandler<Edge>? EdgeRemoved;

    // === 节点 ===
    public void AddNode(Node node)
    {
        ArgumentNullException.ThrowIfNull(node);
        Nodes.Add(node);
        NodeAdded?.Invoke(this, node);
    }

    public bool RemoveNode(Node node)
    {
        if (!Nodes.Remove(node)) return false;
        // 同步移除该节点上的所有边
        var connected = Edges.Where(e => e.From.Owner == node || e.To.Owner == node).ToList();
        foreach (var e in connected) Edges.Remove(e);
        NodeRemoved?.Invoke(this, node);
        return true;
    }

    // === 边 ===
    public bool TryAddEdge(Port from, Port to)
    {
        if (!TypeSystem.TypeRegistry.CanConnect(from.Type, to.Type)) return false;
        // 输入端口单扇入：先移除已有连接
        var existing = Edges.FirstOrDefault(e => e.To == to);
        if (existing != null)
        {
            Edges.Remove(existing);
            EdgeRemoved?.Invoke(this, existing);
        }
        var edge = new Edge(from, to);
        Edges.Add(edge);
        EdgeAdded?.Invoke(this, edge);
        return true;
    }

    public bool RemoveEdge(Edge edge)
    {
        if (!Edges.Remove(edge)) return false;
        EdgeRemoved?.Invoke(this, edge);
        return true;
    }

    public IEnumerable<Edge> EdgesInto(Node node)
        => Edges.Where(e => e.To.Owner == node);

    public IEnumerable<Edge> EdgesOutOf(Node node)
        => Edges.Where(e => e.From.Owner == node);

    /// <summary>
    /// 拓扑排序（Kahn 算法）。存在环时抛 InvalidOperationException。
    /// </summary>
    public IReadOnlyList<Node> TopologicalOrder()
    {
        var inDegree = Nodes.ToDictionary(n => n, _ => 0);
        foreach (var e in Edges) inDegree[e.To.Owner]++;
        var queue = new Queue<Node>(inDegree
            .Where(kv => kv.Value == 0).Select(kv => kv.Key));
        var result = new List<Node>(Nodes.Count);
        while (queue.Count > 0)
        {
            var n = queue.Dequeue();
            result.Add(n);
            foreach (var e in EdgesOutOf(n))
            {
                if (--inDegree[e.To.Owner] == 0)
                    queue.Enqueue(e.To.Owner);
            }
        }
        if (result.Count != Nodes.Count)
            throw new InvalidOperationException("Graph contains a cycle");
        return result;
    }

    public void Clear()
    {
        foreach (var e in Edges.ToList()) RemoveEdge(e);
        foreach (var n in Nodes.ToList()) RemoveNode(n);
    }
}
