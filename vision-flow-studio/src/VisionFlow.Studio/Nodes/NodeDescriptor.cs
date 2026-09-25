using System.Text.Json;
using System.Text.Json.Serialization;
using System.Windows;
using System.Windows.Media;
using VisionFlow.Studio.TypeSystem;

namespace VisionFlow.Studio.Nodes;

// ============================================================
// 节点描述模型：JSON 反序列化的目标类型。
// 文件位于 nodes/*.json，由 NodeRegistry.LoadFromDirectory 扫描装载。
// 字段严格对应 C++ 端 TemplateMatchAlgo::describe() 的 JSON 输出契约，
// 见 vision-algo-core/src/template_match.cpp 中的 describe()。
//
// JSON 示例（ImageSource 节点）：
//   {
//     "type_id": "ImageSource",
//     "display_name": "图像源",
//     "category": "源",
//     "icon": "icons/image_source.svg",
//     "algo_binding": { "dll": "vision_algo", "algo_name": "ImageSource" },
//     "inputs": [],
//     "outputs": [ { "name": "image", "type": "Image", "required": true } ],
//     "parameters": [
//       { "name": "path", "type": "String", "default": "", "description": "图片文件路径" }
//     ]
//   }
// ============================================================

/// <summary>
/// 节点类型描述（不可变模板）。一个 NodeDescriptor 对应画布上可放置的"一类节点"。
/// </summary>
public sealed class NodeDescriptor
{
    [JsonPropertyName("type_id")]
    public string TypeId { get; set; } = string.Empty;

    [JsonPropertyName("display_name")]
    public string DisplayName { get; set; } = string.Empty;

    [JsonPropertyName("category")]
    public string Category { get; set; } = "Other";

    [JsonPropertyName("icon")]
    public string? Icon { get; set; }

    [JsonPropertyName("algo_binding")]
    public AlgoBindingDescriptor AlgoBinding { get; set; } = new();

    [JsonPropertyName("inputs")]
    public List<PortDescriptor> Inputs { get; set; } = new();

    [JsonPropertyName("outputs")]
    public List<PortDescriptor> Outputs { get; set; } = new();

    [JsonPropertyName("parameters")]
    public List<ParameterDescriptor> Parameters { get; set; } = new();

    /// <summary>
    /// 计算属性：依据 Category 在 App.xaml 查找 NodeCategoryBrush.{Category} 画刷。
    /// 不参与 JSON 序列化，仅给节点库 ListBox 的 DataTemplate 用。
    /// </summary>
    [JsonIgnore]
    public Brush CategoryBrush
    {
        get
        {
            var key = $"NodeCategoryBrush.{Category}";
            return Application.Current?.TryFindResource(key) as Brush
                   ?? (Application.Current?.TryFindResource("BrandBrush") as Brush)
                   ?? Brushes.Purple;
        }
    }

    public override string ToString() => $"{DisplayName} ({TypeId})";
}

/// <summary>
/// 算法绑定：节点与算法 DLL 中某个算法的映射关系。
/// </summary>
public sealed class AlgoBindingDescriptor
{
    [JsonPropertyName("dll")]
    public string Dll { get; set; } = "vision_algo";

    [JsonPropertyName("algo_name")]
    public string AlgoName { get; set; } = string.Empty;
}

/// <summary>
/// 端口描述：节点的输入或输出端口定义。
/// </summary>
public sealed class PortDescriptor
{
    [JsonPropertyName("name")]
    public string Name { get; set; } = string.Empty;

    [JsonPropertyName("type")]
    public string Type { get; set; } = "Any";

    [JsonPropertyName("required")]
    public bool Required { get; set; } = true;

    [JsonPropertyName("description")]
    public string? Description { get; set; }

    /// <summary>
    /// 解析类型字符串为 PortType。失败抛异常。
    /// </summary>
    public PortType ResolveType() => TypeRegistry.Require(Type);
}

/// <summary>
/// 参数描述：节点的可配置参数元数据。
/// </summary>
public sealed class ParameterDescriptor
{
    [JsonPropertyName("name")]
    public string Name { get; set; } = string.Empty;

    /// <summary>Double / Int / Bool / String / DoubleRange</summary>
    [JsonPropertyName("type")]
    public string Type { get; set; } = "String";

    [JsonPropertyName("default")]
    public JsonElement? Default { get; set; }

    [JsonPropertyName("min")]
    public double? Min { get; set; }

    [JsonPropertyName("max")]
    public double? Max { get; set; }

    [JsonPropertyName("description")]
    public string? Description { get; set; }

    /// <summary>
    /// ROI 形状（仅当 Type == "Roi" 时有效）。
    /// 取值：rect（轴对齐矩形 [x,y,w,h]）/ rotated_rect（旋转矩形 [cx,cy,w,h,angle]）/ circle（圆形 [cx,cy,r]）。
    /// </summary>
    [JsonPropertyName("shape")]
    public string? Shape { get; set; }

    /// <summary>
    /// 枚举选项列表（可选）。每个选项是 [value, label]，如 [[0,"任意"],[1,"白到黑"],[2,"黑到白"]]
    /// 非空时参数对话框用 ComboBox 渲染。
    /// </summary>
    [JsonPropertyName("enum")]
    public List<List<object>>? EnumValues { get; set; }

    /// <summary>
    /// 返回规范化的 ROI 形状（rect / rotated_rect / circle）。
    /// 未知值或空时默认 rect。
    /// </summary>
    public string GetRoiShape()
    {
        var s = (Shape ?? "rect").Trim().ToLowerInvariant();
        return s switch
        {
            "rotated_rect" or "rotatedrect" or "rot" => "rotated_rect",
            "circle" or "ellipse" => "circle",
            _ => "rect",
        };
    }

    /// <summary>
    /// 将 JsonElement Default 转成 .NET 强类型对象（依据 Type 字段）。
    /// 用于初始化 Node.Parameters 字典。
    /// </summary>
    public object? CoerceDefault()
    {
        if (Default is null || Default.Value.ValueKind == JsonValueKind.Null)
        {
            return Type switch
            {
                "Double" => 0.0,
                "Int" => 0,
                "Bool" => false,
                "String" => string.Empty,
                "Roi" => string.Empty,
                _ => null,
            };
        }

        var v = Default.Value;
        try
        {
            return Type switch
            {
                "Double" => v.GetDouble(),
                "Int" => v.GetInt32(),
                "Bool" => v.GetBoolean(),
                "String" => v.GetString() ?? string.Empty,
                "DoubleRange" => v.GetRawText(),
                _ => v.GetRawText(),
            };
        }
        catch
        {
            // 默认值类型不匹配时回退到类型默认值，避免加载失败
            return Type switch
            {
                "Double" => 0.0,
                "Int" => 0,
                "Bool" => false,
                "String" => string.Empty,
                "Roi" => string.Empty,
                _ => null,
            };
        }
    }
}
