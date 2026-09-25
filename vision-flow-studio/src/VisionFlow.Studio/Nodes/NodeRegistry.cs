using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using VisionFlow.Studio.Interop;

namespace VisionFlow.Studio.Nodes;

// ============================================================
// 节点注册表：单例。负责加载节点描述（JSON）并提供按 type_id 查询。
//
// 加载策略（重要）：
//   1) LoadFromDirectory: 启动时扫描 nodes/*.json 离线装载（默认值/兜底）
//   2) LoadFromAlgoInterop: 启动后调用 vz_algo_describe(name) 拿到 C 端权威
//      描述并覆盖离线 JSON（同 type_id 时以 C 端为准）
//   3) 运行时仍可调用 Register 追加自定义节点
//
// 这样保证：算法层新增算法时，应用层只需把对应 JSON 放进 nodes/ 目录，
// 或运行时从 DLL 自描述拉取，UI 代码不需要任何改动即可在节点库显示新节点。
// ============================================================

public sealed class NodeRegistry
{
    private readonly Dictionary<string, NodeDescriptor> _byTypeId =
        new(StringComparer.Ordinal);

    // 可选：节点类别顺序，用于左侧节点库分组排序
    private readonly List<string> _categoryOrder = new()
    {
        "源", "图像处理", "定位", "测量", "几何", "逻辑", "结果", "Other"
    };

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        PropertyNameCaseInsensitive = false,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
    };

    public static NodeRegistry Instance { get; } = new();

    public IReadOnlyCollection<NodeDescriptor> All => _byTypeId.Values;

    public IReadOnlyList<string> Categories
    {
        get
        {
            var present = _byTypeId.Values
                .Select(d => d.Category)
                .Distinct(StringComparer.Ordinal)
                .ToList();
            // 已知顺序优先；未知类别按字母排序追加在末尾
            var ordered = _categoryOrder.Where(present.Contains).ToList();
            ordered.AddRange(present.Except(_categoryOrder)
                .OrderBy(c => c, StringComparer.Ordinal));
            return ordered;
        }
    }

    public NodeDescriptor? Get(string? typeId)
        => typeId is null || !_byTypeId.TryGetValue(typeId, out var d) ? null : d;

    public void Register(NodeDescriptor d)
    {
        if (d is null) throw new ArgumentNullException(nameof(d));
        if (string.IsNullOrWhiteSpace(d.TypeId))
            throw new ArgumentException("NodeDescriptor.TypeId is empty", nameof(d));
        _byTypeId[d.TypeId] = d;
    }

    // ----- 离线 JSON 加载 -----

    /// <summary>
    /// 扫描指定目录下所有 *.json 装载为 NodeDescriptor。
    /// 单个文件失败不中断整体加载（记录后跳过），便于调试。
    /// 返回成功加载数量。
    /// </summary>
    public int LoadFromDirectory(string directory)
    {
        if (!Directory.Exists(directory))
        {
            Trace.WriteLine($"[NodeRegistry] dir not found: {directory}");
            return 0;
        }

        int ok = 0;
        foreach (var path in Directory.EnumerateFiles(directory, "*.json"))
        {
            try
            {
                var text = File.ReadAllText(path);
                var d = JsonSerializer.Deserialize<NodeDescriptor>(text, JsonOpts);
                if (d is null || string.IsNullOrWhiteSpace(d.TypeId))
                {
                    Trace.WriteLine($"[NodeRegistry] skip (empty): {path}");
                    continue;
                }
                Register(d);
                ok++;
            }
            catch (Exception ex)
            {
                Trace.WriteLine($"[NodeRegistry] failed {path}: {ex.Message}");
            }
        }
        return ok;
    }

    // ----- C 端自描述加载（运行时权威） -----

    /// <summary>
    /// 调用 vz_algo_list() 拿到算法列表，再对每个算法调用 vz_algo_describe()
    /// 反序列化为 NodeDescriptor 并 Register。C 端缺失某算法时跳过不报错。
    /// 同名节点以 C 端为准（覆盖离线 JSON）。
    /// </summary>
    public int LoadFromAlgoInterop(VzAlgoInterop interop)
    {
        if (interop is null) throw new ArgumentNullException(nameof(interop));

        string[] algoNames;
        try { algoNames = VzAlgoInterop.ListAlgos(); }
        catch (Exception ex)
        {
            Trace.WriteLine($"[NodeRegistry] vz_algo_list failed: {ex.Message}");
            return 0;
        }

        int ok = 0;
        foreach (var name in algoNames)
        {
            string json;
            try { json = VzAlgoInterop.DescribeAlgo(name); }
            catch (Exception ex)
            {
                Trace.WriteLine($"[NodeRegistry] vz_algo_describe({name}) failed: {ex.Message}");
                continue;
            }
            try
            {
                var d = JsonSerializer.Deserialize<NodeDescriptor>(json, JsonOpts);
                if (d is null || string.IsNullOrWhiteSpace(d.TypeId))
                {
                    // C 端返回的 type_id 未填时，回退用 algo_name 作为 type_id
                    d ??= new NodeDescriptor { TypeId = name };
                    if (string.IsNullOrWhiteSpace(d.TypeId)) d.TypeId = name;
                }
                Register(d);
                ok++;
            }
            catch (Exception ex)
            {
                Trace.WriteLine($"[NodeRegistry] parse describe({name}) failed: {ex.Message}");
            }
        }
        return ok;
    }
}
