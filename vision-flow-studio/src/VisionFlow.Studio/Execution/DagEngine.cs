using System.Diagnostics;
using System.Runtime.InteropServices;
using VisionFlow.Studio;
using VisionFlow.Studio.Interop;
using VisionFlow.Studio.Models;

namespace VisionFlow.Studio.Execution;

// ============================================================
// DAG 执行引擎：按拓扑序跑完整个图，每节点把结果写回 OutputCache。
//
// 设计：
//   - 不直接调 NativeMethods，全部走 VzAlgoInterop 高层封装
//   - 输入数据来自上游节点 OutputCache，按 Port.Type.Name 分发到 SetInputImage 等
//   - 输出数据按 Port.Type.Name 分发到 GetOutput* 后写入 OutputCache
//   - 单节点失败默认停止下游执行（避免垃圾数据传播）；上游结果仍保留
//   - 图像内存由 OutputCache 持有，下次执行该节点前释放上一帧
//
// 扩展点：
//   - 增加新输入类型：在 ResolveInputType 中加 case
//   - 增加新输出类型：在 CollectOutput 中加 case
//   - 异步/并行：RunAsync 可改为按层并行（MVP 先串行）
// ============================================================

/// <summary>
/// 单个节点的执行结果。三态：
///   - Success=true  → 执行成功
///   - Skipped=true  → 跳过（required 输入端口缺值或上游未就绪；不算失败、不标红）
///   - 其余          → 失败（执行出错，标红）
/// </summary>
public sealed class DagNodeResult
{
    public Node Node { get; init; }
    public bool Success { get; init; }
    public bool Skipped { get; init; }
    public string? Error { get; init; }
    public long ElapsedMs { get; init; }
    public override string ToString()
        => Skipped ? $"{Node.DisplayName} SKIPPED: {Error}"
          : Success ? $"{Node.DisplayName} ok ({ElapsedMs}ms)"
                    : $"{Node.DisplayName} FAIL: {Error}";
}

/// <summary>
/// 整图执行结果。
/// </summary>
public sealed class DagRunResult
{
    public IReadOnlyList<DagNodeResult> Results { get; init; } = Array.Empty<DagNodeResult>();
    // 没有"失败"即视为全部成功（skipped 不算失败）
    public bool AllSucceeded => !Results.Any(r => !r.Success && !r.Skipped);
    public int Succeeded => Results.Count(r => r.Success);
    public int Skipped   => Results.Count(r => r.Skipped);
    public int Failed    => Results.Count(r => !r.Success && !r.Skipped);
}

public sealed class DagEngine
{
    private readonly Graph _graph;

    /// <summary>
    /// 失败策略：true=任一节点失败立即停止；false=继续跑下游（已失败的节点下游会因缺数据再次失败）。
    /// </summary>
    public bool StopOnFirstFailure { get; set; } = true;

    public DagEngine(Graph graph)
    {
        _graph = graph ?? throw new ArgumentNullException(nameof(graph));
    }

    // ============================================================
    // 主入口：执行整张图
    // ============================================================
    public DagRunResult Run(CancellationToken ct = default)
    {
        IReadOnlyList<Node> order;
        try { order = _graph.TopologicalOrder(); }
        catch (Exception ex)
        {
            Logger.Error("DagEngine", $"拓扑排序失败：{ex.Message}");
            return new DagRunResult
            {
                Results = new[]
                {
                    new DagNodeResult
                    {
                        Node = null!,
                        Success = false,
                        Error = $"Topological sort failed: {ex.Message}"
                    }
                }
            };
        }
        Logger.Info("DagEngine", $"拓扑序：[{string.Join(" → ", order.Select(n => n.DisplayName))}]");

        // 检测 For 循环：图中任一 ForLoop 节点的 count 决定整张图重复执行次数，
        // interval_ms 决定每次迭代之间的等待（用于定时连续采集）。
        int loopCount = 1;
        int intervalMs = 0;
        foreach (var ln in _graph.Nodes)
        {
            if (ln.AlgoName != "ForLoop") continue;
            if (ln.Parameters.TryGetValue("count", out var cv) && cv is not null
                && int.TryParse(cv.ToString(), out var c) && c > 1 && c > loopCount)
                loopCount = c;
            if (ln.Parameters.TryGetValue("interval_ms", out var iv) && iv is not null
                && int.TryParse(iv.ToString(), out var im) && im > intervalMs)
                intervalMs = im;
        }
        if (loopCount > 1)
            Logger.Info("DagEngine", $"检测到 ForLoop(count={loopCount}, interval={intervalMs}ms)，整图将循环执行 {loopCount} 次");

        var results = new List<DagNodeResult>(order.Count * loopCount);

        for (int iter = 0; iter < loopCount; iter++)
        {
            if (loopCount > 1)
                Logger.Info("DagEngine", $"━━━ 迭代 {iter + 1}/{loopCount} (index={iter}) ━━━");

            // blocked = failed + skipped；下游节点只要上游在 blocked 中就跳过
            // （保证 B 等 A 和 C 都成功产出数据后才能执行）
            var blocked = new HashSet<Node>();

            foreach (var node in order)
            {
                if (ct.IsCancellationRequested) break;

                // 托管节点（C# 端执行，不走 C++ interop）：ForLoop / TCP 服务端 / 客户端
                if (node.AlgoName is "ForLoop" or "TcpServer" or "TcpClient" or "If")
                {
                    results.Add(RunManagedNode(node, iter, loopCount));
                    continue;
                }

                // 上游任一未就绪（失败或跳过）→ 本节点跳过（不标红，不执行）
                var upstreamBlocked = _graph.EdgesInto(node)
                    .Any(e => blocked.Contains(e.From.Owner));
                if (upstreamBlocked)
                {
                    Logger.Warn("DagEngine", $"跳过 '{node.DisplayName}'：上游未就绪/已失败");
                    results.Add(new DagNodeResult
                    {
                        Node = node,
                        Skipped = true,
                        Error = "upstream not ready"
                    });
                    blocked.Add(node);
                    continue;
                }

                var r = RunNode(node);
                results.Add(r);
                if (r.Skipped)
                {
                    // 缺值跳过：不算失败、不标红，但下游也要跳过
                    blocked.Add(node);
                    continue;
                }
                if (!r.Success)
                {
                    blocked.Add(node);
                    if (StopOnFirstFailure)
                    {
                        Logger.Warn("DagEngine", "StopOnFirstFailure=true，停止后续节点");
                        break;
                    }
                }
            }

            // 迭代间隔（用于定时连续采集），最后一次迭代后不再等待；可被取消
            if (ct.IsCancellationRequested) break;
            if (intervalMs > 0 && iter < loopCount - 1)
            {
                var sw = System.Diagnostics.Stopwatch.StartNew();
                while (sw.ElapsedMilliseconds < intervalMs)
                {
                    if (ct.IsCancellationRequested) break;
                    System.Threading.Thread.Sleep(10);
                }
            }
        }

        return new DagRunResult { Results = results };
    }

    // ============================================================
    // 托管节点（C# 端执行）：ForLoop / TCP 服务端 / 客户端
    // ============================================================
    private DagNodeResult RunManagedNode(Node node, int iter, int loopCount)
    {
        try
        {
            switch (node.AlgoName)
            {
                case "ForLoop":
                    node.OutputCache["index"] = iter;
                    node.OutputCache["count"] = loopCount;
                    break;
                case "TcpServer":
                    EnsureServerListening(node);
                    // 输出端口 = 接收到的内容（最近一条完整消息）
                    node.OutputCache["message"] = TcpNodeRuntime.LatestMessage(node.InstanceId);
                    break;
                case "TcpClient":
                    EnsureClientConnected(node);
                    var toSend = GetInputString(node, "send");
                    if (!string.IsNullOrEmpty(toSend))
                        TcpNodeRuntime.SendClient(node.InstanceId, toSend);
                    // 输出端口 = 接收到的内容（最近一条完整消息）
                    node.OutputCache["message"] = TcpNodeRuntime.LatestMessage(node.InstanceId);
                    break;
                case "If":
                    var obj = GetInputValue(node, "obj");
                    node.InputCache["obj"] = obj;
                    bool cond = EvalIfCondition(node, obj);
                    // 按判断结果路由：true 分支成立 → 数据进 true 端口；否则进 false 端口。
                    // 另一个分支输出 null（阻断该支下游），实现 if/else 分流。
                    node.OutputCache["true"] = cond ? obj : null;
                    node.OutputCache["false"] = cond ? null : obj;
                    Logger.Info("DagEngine", $"  If 条件={cond} → 数据进入{(cond ? "true" : "false")} 分支");
                    break;
            }
            node.HasError = false;
            node.LastError = null;
            return new DagNodeResult { Node = node, Success = true };
        }
        catch (Exception ex)
        {
            node.HasError = true;
            node.LastError = ex.Message;
            return new DagNodeResult { Node = node, Success = false, Error = ex.Message };
        }
    }

    private static string P(Node node, string name, string fallback)
        => node.Parameters.TryGetValue(name, out var v) && v is string s && !string.IsNullOrEmpty(s) ? s : fallback;
    private static int PInt(Node node, string name, int fallback)
    {
        if (node.Parameters.TryGetValue(name, out var v) && v is not null)
        {
            if (v is int i) return i;
            if (int.TryParse(v.ToString(), out var p)) return p;
        }
        return fallback;
    }

    private static void EnsureServerListening(Node node)
    {
        if (TcpNodeRuntime.IsListening(node.InstanceId)) return;
        TcpNodeRuntime.StartServer(node.InstanceId,
            P(node, "ip", "0.0.0.0"), PInt(node, "port", 8080), P(node, "terminator", "\n"));
    }
    private static void EnsureClientConnected(Node node)
    {
        if (TcpNodeRuntime.IsConnected(node.InstanceId)) return;
        TcpNodeRuntime.ConnectClient(node.InstanceId,
            P(node, "ip", "127.0.0.1"), PInt(node, "port", 8080),
            P(node, "trigger", ""), P(node, "terminator", "\n"));
    }

    /// <summary>读取节点某输入端口当前的上游字符串值。</summary>
    private string? GetInputString(Node node, string portName)
    {
        var edge = _graph.EdgesInto(node).FirstOrDefault(e => e.To.Name == portName);
        if (edge is null) return null;
        if (edge.From.Owner.OutputCache.TryGetValue(edge.From.Name, out var v) && v is string s)
            return s;
        return null;
    }

    /// <summary>读取节点某输入端口当前的上游值（任意类型）。</summary>
    private object? GetInputValue(Node node, string portName)
    {
        var edge = _graph.EdgesInto(node).FirstOrDefault(e => e.To.Name == portName);
        if (edge is null) return null;
        return edge.From.Owner.OutputCache.TryGetValue(edge.From.Name, out var v) ? v : null;
    }

    /// <summary>计算 If 节点的条件是否成立。</summary>
    private static bool EvalIfCondition(Node node, object? obj)
    {
        var op = P(node, "op", "nonempty");
        if (op == "nonempty")
        {
            if (obj is null) return false;
            if (obj is string s) return s.Length > 0;
            if (obj is System.Collections.ICollection c) return c.Count > 0;
            return true;
        }
        var cmp = P(node, "value", "");
        var sign = CompareByType(obj, cmp);
        return op switch
        {
            "==" => sign == 0,
            "!=" => sign != 0,
            ">"  => sign > 0,
            ">=" => sign >= 0,
            "<"  => sign < 0,
            "<=" => sign <= 0,
            _ => false,
        };
    }

    /// <summary>按 obj 的运行时类型，把基准字符串解析后比较，返回 (obj - cmp) 的符号。</summary>
    private static int CompareByType(object? obj, string cmp)
    {
        switch (obj)
        {
            case null:
                return cmp.Length == 0 ? 0 : -1;
            case int i:
                return int.TryParse(cmp, out var iv) ? i.CompareTo(iv) : 0;
            case long l:
                return long.TryParse(cmp, out var lv) ? l.CompareTo(lv) : 0;
            case double d:
                return double.TryParse(cmp, out var dv) ? d.CompareTo(dv) : 0;
            case float f:
                return float.TryParse(cmp, out var fv) ? f.CompareTo(fv) : 0;
            case bool b:
                var bv = cmp.Equals("true", StringComparison.OrdinalIgnoreCase) || cmp == "1";
                return b.CompareTo(bv);
            case string st:
                return string.Compare(st, cmp, StringComparison.Ordinal);
            default:
                return 0;
        }
    }

    // ============================================================
    // 单节点执行（含上游依赖）
    // ============================================================
    /// <summary>
    /// 运行指定节点及其所有上游依赖（按拓扑序先跑上游再跑本节点）。
    /// 用于节点上的"▶ 运行"按钮：单点触发，自动补齐所需输入。
    /// </summary>
    public DagRunResult RunNodeWithUpstream(Node target)
    {
        // 收集上游链（含 target 自身）
        var visited = new HashSet<Node>();
        var stack = new Stack<Node>();
        stack.Push(target);
        while (stack.Count > 0)
        {
            var n = stack.Pop();
            if (!visited.Add(n)) continue;
            foreach (var e in _graph.EdgesInto(n))
                stack.Push(e.From.Owner);
        }

        // 在子图上做拓扑序
        var sub = visited.ToList();
        var inDeg = sub.ToDictionary(n => n, _ => 0);
        foreach (var e in _graph.Edges)
            if (visited.Contains(e.From.Owner) && visited.Contains(e.To.Owner))
                inDeg[e.To.Owner]++;
        var q = new Queue<Node>(inDeg.Where(kv => kv.Value == 0).Select(kv => kv.Key));
        var order = new List<Node>();
        while (q.Count > 0)
        {
            var n = q.Dequeue();
            order.Add(n);
            foreach (var e in _graph.EdgesOutOf(n))
                if (visited.Contains(e.To.Owner) && --inDeg[e.To.Owner] == 0)
                    q.Enqueue(e.To.Owner);
        }

        var results = new List<DagNodeResult>(order.Count);
        var blocked = new HashSet<Node>();
        foreach (var n in order)
        {
            var upstreamBlocked = _graph.EdgesInto(n)
                .Any(e => blocked.Contains(e.From.Owner));
            if (upstreamBlocked)
            {
                Logger.Warn("DagEngine", $"跳过 '{n.DisplayName}'：上游未就绪/已失败");
                results.Add(new DagNodeResult
                {
                    Node = n, Skipped = true,
                    Error = "upstream not ready"
                });
                blocked.Add(n);
                continue;
            }
            if (n.AlgoName is "ForLoop" or "TcpServer" or "TcpClient" or "If")
            {
                results.Add(RunManagedNode(n, 0, 1));
                continue;
            }
            var r = RunNode(n);
            results.Add(r);
            if (r.Skipped) { blocked.Add(n); continue; }
            if (!r.Success) blocked.Add(n);
        }
        return new DagRunResult { Results = results };
    }

    // ============================================================
    // 单节点执行
    // ============================================================
    public DagNodeResult RunNode(Node node)
    {
        var sw = Stopwatch.StartNew();
        // 预检查：所有 required 输入端口必须已连线且上游已产出数据。
        // 缺值则跳过（不执行、不标红），符合"每个端口都得有值才执行"。
        // 这保证：B 有两个输入端口时，必须 A 和 C 都跑完产出数据后 B 才执行。
        foreach (var port in node.Inputs)
        {
            if (!port.Required) continue;
            var edge = _graph.EdgesInto(node).FirstOrDefault(e => e.To.Name == port.Name);
            if (edge == null)
            {
                Logger.Warn("DagEngine", $"跳过 '{node.DisplayName}'：required 输入端口 '{port.Name}' 未连接");
                return new DagNodeResult
                {
                    Node = node, Skipped = true,
                    Error = $"required input '{port.Name}' not connected"
                };
            }
            if (!edge.From.Owner.OutputCache.TryGetValue(edge.From.Name, out var d) || d is null)
            {
                Logger.Warn("DagEngine", $"跳过 '{node.DisplayName}'：上游 '{edge.From.Owner.DisplayName}.{edge.From.Name}' 尚无数据（需先运行上游）");
                return new DagNodeResult
                {
                    Node = node, Skipped = true,
                    Error = $"upstream '{edge.From.Owner.DisplayName}.{edge.From.Name}' has no data"
                };
            }
        }
        Logger.Info("DagEngine", $"▶ 开始执行 '{node.DisplayName}' (algo={node.AlgoName})");
        try
        {
            // 释放上一帧的图像输出（如果有的话）
            CleanupImageOutputs(node);

            using var interop = new VzAlgoInterop(node.AlgoName);

            // --- 1) 推入参数 ---
            if (node.Parameters.Count > 0)
                Logger.Info("DagEngine", $"  推入 {node.Parameters.Count} 个参数：[{string.Join(", ", node.Parameters.Where(kv => kv.Value is not null).Select(kv => $"{kv.Key}={kv.Value}"))}]");
            foreach (var (key, value) in node.Parameters)
            {
                if (value is null) continue;
                // 空字符串参数跳过（ROI 等可选参数为空表示未设置，C++ 端也做了兼容）
                if (value is string s && s.Length == 0) continue;
                interop.SetParam(key, value);
            }

            // --- 2) 推入输入端口 ---
            foreach (var port in node.Inputs)
            {
                // 查找连到这个输入端口的边
                var edge = _graph.EdgesInto(node)
                    .FirstOrDefault(e => e.To.Name == port.Name);
                if (edge == null)
                {
                    if (port.Required)
                        throw new InvalidOperationException(
                            $"missing required input '{port.Name}'");
                    // 可选端口未连接：检查是否有外部预置的输入值（如模板导入/新建）
                    if (node.InputCache.TryGetValue(port.Name, out var preset) && preset is not null)
                    {
                        Logger.Info("DagEngine", $"  输入端口 '{port.Name}' ({port.Type.Name}) 使用预置值（无上游连接）");
                        PushInput(interop, port, preset);
                    }
                    else
                    {
                        Logger.Info("DagEngine", $"  输入端口 '{port.Name}' ({port.Type.Name}) 未连接（可选，跳过）");
                    }
                    continue;
                }

                var upstreamNode = edge.From.Owner;
                var outName = edge.From.Name;
                if (!upstreamNode.OutputCache.TryGetValue(outName, out var data) || data is null)
                    throw new InvalidOperationException(
                        $"upstream '{upstreamNode.DisplayName}.{outName}' has no data");

                Logger.Info("DagEngine", $"  输入端口 '{port.Name}' ({port.Type.Name}) ← '{upstreamNode.DisplayName}.{outName}'");
                // 把上游传过来的值同步写入本节点的 InputCache，
                // 让 UI 可以在节点上显示"B 的输入端口当前值是 A 传过来的什么"。
                node.InputCache[port.Name] = data;
                PushInput(interop, port, data);
            }

            // --- 3) 执行 ---
            Logger.Info("DagEngine", "  调用 vz_algo_process() ...");
            interop.Process();

            // --- 4) 拉取输出 ---
            foreach (var port in node.Outputs)
            {
                var outValue = PullOutput(interop, port);
                node.OutputCache[port.Name] = outValue;
                // 简短描述输出值
                string desc = outValue switch
                {
                    VzImage img => $"Image {img.width}x{img.height}x{img.channels}",
                    System.Collections.ICollection c => $"{c.Count} 项",
                    _ => outValue?.ToString() ?? "null",
                };
                Logger.Info("DagEngine", $"  输出端口 '{port.Name}' ({port.Type.Name}) → {desc}");
            }

            sw.Stop();
            // 成功：清空错误态（如有）
            node.HasError = false;
            node.LastError = null;
            Logger.Success("DagEngine", $"✔ '{node.DisplayName}' 完成 ({sw.ElapsedMilliseconds}ms)");
            return new DagNodeResult
            {
                Node = node,
                Success = true,
                ElapsedMs = sw.ElapsedMilliseconds,
            };
        }
        catch (Exception ex)
        {
            sw.Stop();
            // 失败：写入错误态，UI 立刻把节点变红
            node.HasError = true;
            node.LastError = ex.Message;
            Logger.Error("DagEngine", $"✘ '{node.DisplayName}' 失败 ({sw.ElapsedMilliseconds}ms): {ex.Message}");
            return new DagNodeResult
            {
                Node = node,
                Success = false,
                Error = ex.Message,
                ElapsedMs = sw.ElapsedMilliseconds,
            };
        }
    }

    // ============================================================
    // 类型分发：输入侧
    // ============================================================
    private static void PushInput(VzAlgoInterop interop, Port port, object data)
    {
        switch (port.Type.Name)
        {
            case "Image":
            {
                if (data is not VzImage img)
                    throw new InvalidOperationException(
                        $"input '{port.Name}' expects Image, got {data.GetType().Name}");
                interop.SetInputImage(port.Name, ref img, VzPortType.Image);
                break;
            }
            case "Region":
            {
                // Region 在 ABI 中复用 VzImage 结构（掩码图），但 type_tag 必须是 Region
                if (data is not VzImage img)
                    throw new InvalidOperationException(
                        $"input '{port.Name}' expects Region, got {data.GetType().Name}");
                interop.SetInputImage(port.Name, ref img, VzPortType.Region);
                break;
            }
            case "String":
            {
                string s = data?.ToString() ?? "";
                interop.SetInputString(port.Name, s);
                break;
            }
            case "Point2DList":
            {
                var arr = data as VzPoint2D[] ?? Array.Empty<VzPoint2D>();
                interop.SetInputPoints(port.Name, arr);
                break;
            }
            case "Pose2D":
            {
                if (data is not VzPose2D pose)
                    throw new InvalidOperationException(
                        $"input '{port.Name}' expects Pose2D, got {data.GetType().Name}");
                interop.SetInputPose(port.Name, pose);
                break;
            }
            case "Int":
            {
                int v = data switch
                {
                    int i => i,
                    double d => (int)d,
                    _ => throw new InvalidOperationException(
                        $"input '{port.Name}' expects Int, got {data.GetType().Name}")
                };
                interop.SetInputInt(port.Name, v);
                break;
            }
            case "Double":
            {
                double v = data switch
                {
                    int i => i,
                    double d => d,
                    _ => throw new InvalidOperationException(
                        $"input '{port.Name}' expects Double, got {data.GetType().Name}")
                };
                interop.SetInputDouble(port.Name, v);
                break;
            }
            case "Bool":
            {
                bool b = data switch
                {
                    bool bb => bb,
                    int i => i != 0,
                    _ => throw new InvalidOperationException(
                        $"input '{port.Name}' expects Bool, got {data.GetType().Name}")
                };
                interop.SetInputBool(port.Name, b);
                break;
            }
            default:
                throw new NotSupportedException(
                    $"input type '{port.Type.Name}' not supported by DagEngine");
        }
    }

    // ============================================================
    // 类型分发：输出侧
    // ============================================================
    private static object PullOutput(VzAlgoInterop interop, Port port)
    {
        return port.Type.Name switch
        {
            "Image"           => interop.GetOutputImage(port.Name),
            "MatchResultList" => interop.GetOutputMatches(port.Name),
            "Point2DList"     => interop.GetOutputPoints(port.Name),
            "RectList"        => interop.GetOutputRects(port.Name),
            "Int"             => interop.GetOutputInt(port.Name),
            "Double"          => interop.GetOutputDouble(port.Name),
            "Bool"            => interop.GetOutputBool(port.Name),
            "String"          => interop.GetOutputString(port.Name),
            "Pose2D"          => interop.GetOutputPose(port.Name),
            _ => throw new NotSupportedException(
                    $"output type '{port.Type.Name}' not supported by DagEngine"),
        };
    }

    // ============================================================
    // 内存管理：图像输出生命周期
    // ============================================================
    /// <summary>
    /// 释放上一帧 OutputCache 中 VzImage.data 的非托管内存。
    /// VzImage 外壳结构体本身是值类型不释放；data 是 C 端 malloc 出来的，需 vz_free。
    /// </summary>
    private static void CleanupImageOutputs(Node node)
    {
        foreach (var (key, value) in node.OutputCache.ToList())
        {
            if (value is VzImage img && img.data != IntPtr.Zero)
            {
                VzAlgoInterop.FreeVzImageData(img);
                node.OutputCache[key] = default(VzImage);
            }
        }
    }
}
