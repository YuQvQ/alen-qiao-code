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

    // ForLoop 在第 0 轮从输入端口解析出的循环次数（数据驱动）。
    private int _resolvedLoopCount;

    /// <summary>每轮迭代（含 ForLoop 的每一次 index）执行完毕后触发，用于 UI 实时刷新显示。
    /// 注意：在后台线程调用，订阅方需自行 Marshal 到 UI 线程。</summary>
    public Action? OnAfterIteration;

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

        // 迭代间隔：取图中所有 ForLoop 节点 interval_ms 的最大值（参数配置）。
        int intervalMs = 0;
        foreach (var ln in _graph.Nodes)
        {
            if (ln.AlgoName != "ForLoop") continue;
            if (ln.Parameters.TryGetValue("interval_ms", out var iv) && iv is not null
                && int.TryParse(iv.ToString(), out var im) && im > intervalMs)
                intervalMs = im;
        }

        // count 由 ForLoop 的输入端口（数据驱动）决定：先跑第 0 轮，ForLoop 解析出 count，
        // 写入 _resolvedLoopCount；随后再跑剩余迭代。未连接 count 输入时退化为参数默认值。
        _resolvedLoopCount = 0;
        var results = new List<DagNodeResult>(order.Count);

        void RunIteration(int iter)
        {
            // blocked = failed + skipped；下游节点只要上游在 blocked 中就跳过
            // （保证 B 等 A 和 C 都成功产出数据后才能执行）
            var blocked = new HashSet<Node>();
            foreach (var node in order)
            {
                if (ct.IsCancellationRequested) break;

                // 被用户禁用的节点直接跳过（不执行，下游也视为未就绪）
                if (!node.Enabled)
                {
                    Logger.Info("DagEngine", $"跳过 '{node.DisplayName}'：节点已禁用");
                    results.Add(new DagNodeResult { Node = node, Skipped = true, Error = "node disabled" });
                    blocked.Add(node);
                    continue;
                }

                // 托管节点（C# 端执行，不走 C++ interop）：ForLoop / TCP 服务端 / 客户端 / PointSort
                if (node.AlgoName is "ForLoop" or "TcpServer" or "TcpClient" or "If" or "PointSort"
                    or "ConstString" or "ConstNumber" or "ConstInteger")
                {
                    results.Add(RunManagedNode(node, iter));
                    continue;
                }

                var upstreamBlocked = _graph.EdgesInto(node)
                    .Any(e => blocked.Contains(e.From.Owner));
                if (upstreamBlocked)
                {
                    Logger.Warn("DagEngine", $"跳过 '{node.DisplayName}'：上游未就绪/已失败");
                    results.Add(new DagNodeResult { Node = node, Skipped = true, Error = "upstream not ready" });
                    blocked.Add(node);
                    continue;
                }

                var r = RunNode(node);
                results.Add(r);
                if (r.Skipped) { blocked.Add(node); continue; }
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
        }

        // 第 0 轮：执行全图，ForLoop 在此轮从输入解析 count 并输出 index=0
        RunIteration(0);
        OnAfterIteration?.Invoke();
        int loopCount = Math.Max(1, _resolvedLoopCount);
        if (loopCount > 1)
            Logger.Info("DagEngine", $"检测到 ForLoop(count={loopCount}, interval={intervalMs}ms)，整图将循环执行 {loopCount} 次");

        for (int iter = 1; iter < loopCount; iter++)
        {
            if (ct.IsCancellationRequested) break;
            Logger.Info("DagEngine", $"━━━ 迭代 {iter + 1}/{loopCount} (index={iter}) ━━━");

            // 迭代间隔（最后一次迭代后不再等待）；可被取消
            if (intervalMs > 0)
            {
                var sw = System.Diagnostics.Stopwatch.StartNew();
                while (sw.ElapsedMilliseconds < intervalMs)
                {
                    if (ct.IsCancellationRequested) break;
                    System.Threading.Thread.Sleep(10);
                }
            }
            if (ct.IsCancellationRequested) break;
            RunIteration(iter);
            OnAfterIteration?.Invoke();
        }

        return new DagRunResult { Results = results };
    }

    // ============================================================
    // 托管节点（C# 端执行）：ForLoop / TCP 服务端 / 客户端
    // ============================================================
    private DagNodeResult RunManagedNode(Node node, int iter)
    {
        try
        {
            switch (node.AlgoName)
            {
                case "ForLoop":
                {
                    // count 优先取输入端口（数据驱动，如模板匹配输出的数量），
                    // 未连接时退化为参数 count 的默认值。
                    int cnt = -1;
                    var inCnt = GetInputValue(node, "count");
                    if (inCnt is int ic && ic > 0) cnt = ic;
                    else if (inCnt is long lc && lc > 0) cnt = (int)lc;
                    if (cnt <= 0 && node.Parameters.TryGetValue("count", out var cv) && cv is not null
                        && int.TryParse(cv.ToString(), out var pc) && pc > 0) cnt = pc;
                    if (cnt <= 0) cnt = 1;
                    node.OutputCache["index"] = iter;
                    node.OutputCache["count"] = cnt;
                    if (cnt > _resolvedLoopCount) _resolvedLoopCount = cnt;
                    Logger.Info("DagEngine", $"  ForLoop: index={iter}, count={cnt}");
                    break;
                }
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
                case "ConstString":
                {
                    var sv = node.Parameters.TryGetValue("value", out var svv) && svv is not null
                        ? svv.ToString() ?? "" : "";
                    node.OutputCache["value"] = sv;
                    Logger.Info("DagEngine", $"  ConstString = '{sv}'");
                    break;
                }
                case "ConstNumber":
                {
                    double dv = 0;
                    if (node.Parameters.TryGetValue("value", out var nv) && nv is not null)
                        double.TryParse(nv.ToString(), System.Globalization.NumberStyles.Any,
                            System.Globalization.CultureInfo.InvariantCulture, out dv);
                    node.OutputCache["value"] = dv;
                    Logger.Info("DagEngine", $"  ConstNumber = {dv}");
                    break;
                }
                case "ConstInteger":
                {
                    int iv = 0;
                    if (node.Parameters.TryGetValue("value", out var nv) && nv is not null)
                        int.TryParse(nv.ToString(), System.Globalization.NumberStyles.Any,
                            System.Globalization.CultureInfo.InvariantCulture, out iv);
                    node.OutputCache["value"] = iv;
                    Logger.Info("DagEngine", $"  ConstInteger = {iv}");
                    break;
                }
                case "PointSort":
                {
                    // 点排序：先按 Y 分行，再按 X 升序。
                    // 分行用“Y 间隙聚类”：按 Y 排序后，相邻点 Y 差超过容差则另起一行。
                    // 比 round(y/tol) 更抗透视/漂移，避免行被错误合并或拆分。
                    var raw = GetInputValue(node, "points");
                    var pts = (raw as VzPoint2D[] ?? Array.Empty<VzPoint2D>()).ToList();
                    node.InputCache["points"] = pts.ToArray();
                    double tol = PDouble(node, "row_tolerance", 20.0);

                    VzPoint2D[] sorted;
                    if (pts.Count == 0)
                    {
                        sorted = Array.Empty<VzPoint2D>();
                    }
                    else if (tol <= 0)
                    {
                        sorted = pts.OrderBy(p => p.y).ThenBy(p => p.x).ToArray();
                    }
                    else
                    {
                        var byY = pts.OrderBy(p => p.y).ToList();
                        // 自动估计典型行距：相邻点 Y 差中大于 minGap 的那些差值取中位数。
                        // 同行内相邻点的 Y 抖动通常 < minGap 被过滤，剩下的多为换行差，中位数即行距。
                        double minGap = tol;
                        var gaps = new List<double>();
                        for (int i = 1; i < byY.Count; i++)
                        {
                            double d = byY[i].y - byY[i - 1].y;
                            if (d > minGap) gaps.Add(d);
                        }
                        double rowGap = gaps.Count > 0
                            ? gaps.OrderBy(x => x).ElementAt(gaps.Count / 2)
                            : minGap * 2.0;
                        double threshold = rowGap * 0.5;

                        // 分行：按 Y 排序后，当前点与当前行最大 Y 的差 > threshold 则另起一行。
                        // 用 maxY（而非平均Y）更稳定，避免透视下某点拉偏均值。
                        var rows = new List<List<VzPoint2D>>();
                        double rowMaxY = double.NegativeInfinity;
                        foreach (var p in byY)
                        {
                            if (rows.Count == 0 || p.y - rowMaxY > threshold)
                            {
                                rows.Add(new List<VzPoint2D> { p });
                                rowMaxY = p.y;
                            }
                            else
                            {
                                rows[rows.Count - 1].Add(p);
                                if (p.y > rowMaxY) rowMaxY = p.y;
                            }
                        }
                        sorted = rows.SelectMany(r => r.OrderBy(p => p.x)).ToArray();
                    }
                    node.OutputCache["points"] = sorted;
                    node.OutputCache["count"] = sorted.Length;
                    Logger.Info("DagEngine", $"  PointSort: {pts.Count} 点 → 排序后 {sorted.Length} (row_tolerance={tol})");
                    break;
                }
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
    private static double PDouble(Node node, string name, double fallback)
    {
        if (node.Parameters.TryGetValue(name, out var v) && v is not null)
        {
            if (v is double d) return d;
            if (v is float f) return f;
            if (double.TryParse(v.ToString(), System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out var p)) return p;
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

    // 仅引擎消费、不下推给 native 的输入端口（用于数据驱动地构造 search_roi）。
    private static readonly HashSet<string> EngineOnlyPorts =
        new() { "roi_center", "roi_size", "roi_width", "roi_height", "roi_radius" };

    private static bool IsEngineOnlyPort(string name) => EngineOnlyPorts.Contains(name);

    private static bool TryGetPoint(object? v, out double x, out double y)
    {
        x = 0; y = 0;
        switch (v)
        {
            case VzPoint2D p: x = p.x; y = p.y; return true;
            case VzPose2D pose: x = pose.point.x; y = pose.point.y; return true;
            case VzPoint2D[] arr when arr.Length > 0: x = arr[0].x; y = arr[0].y; return true;
            default: return false;
        }
    }

    private static bool TryGetDouble(object? v, out double d)
    {
        d = 0;
        switch (v)
        {
            case double dd: d = dd; return true;
            case float f: d = f; return true;
            case int i: d = i; return true;
            case long l: d = l; return true;
            case string s when double.TryParse(s, System.Globalization.NumberStyles.Any,
                System.Globalization.CultureInfo.InvariantCulture, out var parsed): d = parsed; return true;
            default: return false;
        }
    }

    /// <summary>
    /// 数据驱动 ROI：若节点连了 roi_center（+ 尺寸）输入端口，则用它们构造 search_roi 参数，
    /// 覆盖在图像上手绘的 ROI。仅对带 search_roi 参数的检测节点生效。
    /// </summary>
    private void InjectRoiFromInputs(Node node)
    {
        bool HasPort(string name) => node.Inputs.Any(p => p.Name == name);
        if (!HasPort("roi_center")) return;
        var cv = GetInputValue(node, "roi_center");
        if (cv is null || !TryGetPoint(cv, out double cx, out double cy)) return;

        if (HasPort("roi_width") && HasPort("roi_height"))
        {
            if (!TryGetDouble(GetInputValue(node, "roi_width"), out double w) || w <= 0) return;
            if (!TryGetDouble(GetInputValue(node, "roi_height"), out double h) || h <= 0) return;
            node.Parameters["search_roi"] = $"[{cx:F4},{cy:F4},{w:F4},{h:F4},0]";
            Logger.Info("DagEngine", $"  ROI(输入) 矩形中心=({cx:F1},{cy:F1}) 尺寸={w:F1}x{h:F1}");
        }
        else if (HasPort("roi_size"))
        {
            if (!TryGetPoint(GetInputValue(node, "roi_size"), out double w, out double h)) return;
            if (w <= 0 || h <= 0) return;
            node.Parameters["search_roi"] = $"[{cx:F4},{cy:F4},{w:F4},{h:F4},0]";
            Logger.Info("DagEngine", $"  ROI(输入) 矩形中心=({cx:F1},{cy:F1}) 尺寸={w:F1}x{h:F1}");
        }
        else if (HasPort("roi_radius"))
        {
            if (!TryGetDouble(GetInputValue(node, "roi_radius"), out double r) || r <= 0) return;
            node.Parameters["search_roi"] = $"[{cx:F4},{cy:F4},{r:F4}]";
            Logger.Info("DagEngine", $"  ROI(输入) 圆形中心=({cx:F1},{cy:F1}) r={r:F1}");
        }
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
            if (n.AlgoName is "ForLoop" or "TcpServer" or "TcpClient" or "If" or "PointSort"
                or "ConstString" or "ConstNumber" or "ConstInteger")
            {
                results.Add(RunManagedNode(n, 0));
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

            // --- 0) 数据驱动 ROI：用输入端口覆盖 search_roi 参数 ---
            InjectRoiFromInputs(node);

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
                // 引擎专用端口（roi_center/roi_size/roi_width/roi_height/roi_radius）
                // 只用于构造 search_roi，不下推给 native（否则 native 报 unknown input）。
                if (IsEngineOnlyPort(port.Name)) continue;

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
