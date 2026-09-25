using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using VisionFlow.Studio.Models;
using VisionFlow.Studio.Nodes;

namespace VisionFlow.Studio.Controls;

/// <summary>
/// 节点使用说明窗口：展示输入端口、输出端口、参数的完整说明，
/// 帮助用户理解怎么连接、怎么传值。
/// </summary>
public partial class NodeHelpDialog : Window
{
    public NodeHelpDialog(Node node)
    {
        InitializeComponent();
        var desc = NodeRegistry.Instance.Get(node.TypeId);
        TitleText.Text = $"{node.DisplayName}（{node.TypeId}）";
        CategoryText.Text = $"类别：{node.Category}";

        if (desc is null) return;

        var muted = (Brush)Application.Current.TryFindResource("TextMutedBrush")!;
        var primary = (Brush)Application.Current.TryFindResource("TextPrimaryBrush") ?? Brushes.Black;
        var accent = (Brush)Application.Current.TryFindResource("BrandBrush") ?? Brushes.RoyalBlue;

        // ========== 输入端口 ==========
        ContentPanel.Children.Add(MakeSectionTitle("📥 输入端口"));
        if (desc.Inputs.Count == 0)
            ContentPanel.Children.Add(MakeGrayText("（无输入端口）"));
        else
            foreach (var p in desc.Inputs)
            {
                var req = p.Required ? "必须" : "可选";
                ContentPanel.Children.Add(MakePortRow(p.Name, p.Type, req, p.Description, Colors.LightGreen));
            }

        // ========== 输出端口 ==========
        ContentPanel.Children.Add(MakeSectionTitle("📤 输出端口"));
        if (desc.Outputs.Count == 0)
            ContentPanel.Children.Add(MakeGrayText("（无输出端口）"));
        else
            foreach (var p in desc.Outputs)
            {
                ContentPanel.Children.Add(MakePortRow(p.Name, p.Type, "输出", p.Description, Colors.LightSkyBlue));
            }

        // ========== 参数 ==========
        ContentPanel.Children.Add(MakeSectionTitle("⚙️ 参数"));
        if (desc.Parameters.Count == 0)
            ContentPanel.Children.Add(MakeGrayText("（无可编辑参数）"));
        else
            foreach (var p in desc.Parameters)
            {
                ContentPanel.Children.Add(MakeParamRow(p));
            }

        // ========== 使用提示 ==========
        ContentPanel.Children.Add(MakeSectionTitle("💡 使用提示"));
        ContentPanel.Children.Add(MakeTipText(
            "• 端口圆点颜色代表数据类型，相同颜色才能连接\n" +
            "• 双击节点打开参数设置；拖拽端口圆点进行连线\n" +
            "• 连接结果图(result_image)到下游可查看可视化结果\n" +
            "• 鼠标悬停端口可查看详细说明"));
    }

    private static TextBlock MakeSectionTitle(string text) => new()
    {
        Text = text,
        FontSize = 14,
        FontWeight = FontWeights.Bold,
        Margin = new Thickness(0, 12, 0, 6),
        Foreground = (Brush)Application.Current.TryFindResource("BrandBrush") ?? Brushes.RoyalBlue,
    };

    private static TextBlock MakeGrayText(string text) => new()
    {
        Text = text,
        FontSize = 11,
        Foreground = (Brush)Application.Current.TryFindResource("TextMutedBrush")!,
        Margin = new Thickness(0, 0, 0, 4),
    };

    private static TextBlock MakeTipText(string text) => new()
    {
        Text = text,
        FontSize = 11,
        Foreground = (Brush)Application.Current.TryFindResource("TextMutedBrush")!,
        Margin = new Thickness(0, 0, 0, 4),
        TextWrapping = TextWrapping.Wrap,
    };

    private static Border MakePortRow(string name, string type, string tag, string? desc, Color color)
    {
        var panel = new StackPanel { Margin = new Thickness(4, 0, 4, 6) };
        var header = new StackPanel { Orientation = Orientation.Horizontal };
        header.Children.Add(new TextBlock
        {
            Text = name,
            FontSize = 12,
            FontWeight = FontWeights.SemiBold,
            Foreground = (Brush)Application.Current.TryFindResource("TextPrimaryBrush") ?? Brushes.Black,
        });
        header.Children.Add(new TextBlock
        {
            Text = $"  ({type})",
            FontSize = 10,
            Foreground = Brushes.Gray,
            VerticalAlignment = VerticalAlignment.Center,
        });
        header.Children.Add(new Border
        {
            Background = new SolidColorBrush(color) { Opacity = 0.6 },
            CornerRadius = new CornerRadius(3),
            Padding = new Thickness(4, 0, 4, 0),
            Margin = new Thickness(6, 0, 0, 0),
            Child = new TextBlock { Text = tag, FontSize = 9, VerticalAlignment = VerticalAlignment.Center },
        });
        panel.Children.Add(header);
        if (!string.IsNullOrEmpty(desc))
        {
            panel.Children.Add(new TextBlock
            {
                Text = desc,
                FontSize = 10,
                Foreground = (Brush)Application.Current.TryFindResource("TextMutedBrush")!,
                Margin = new Thickness(0, 1, 0, 0),
                TextWrapping = TextWrapping.Wrap,
            });
        }
        return new Border
        {
            Background = new SolidColorBrush(color) { Opacity = 0.08 },
            CornerRadius = new CornerRadius(4),
            Padding = new Thickness(8, 6, 8, 6),
            Margin = new Thickness(0, 0, 0, 4),
            Child = panel,
        };
    }

    private static Border MakeParamRow(ParameterDescriptor p)
    {
        var panel = new StackPanel { Margin = new Thickness(4, 0, 4, 6) };
        var header = new StackPanel { Orientation = Orientation.Horizontal };
        header.Children.Add(new TextBlock
        {
            Text = p.Name,
            FontSize = 12,
            FontWeight = FontWeights.SemiBold,
            Foreground = (Brush)Application.Current.TryFindResource("TextPrimaryBrush") ?? Brushes.Black,
        });
        header.Children.Add(new TextBlock
        {
            Text = $"  ({p.Type})",
            FontSize = 10,
            Foreground = Brushes.Gray,
            VerticalAlignment = VerticalAlignment.Center,
        });
        var defStr = p.Default?.ToString() ?? "";
        if (!string.IsNullOrEmpty(defStr))
        {
            header.Children.Add(new TextBlock
            {
                Text = $"   默认: {defStr}",
                FontSize = 10,
                Foreground = Brushes.Gray,
                VerticalAlignment = VerticalAlignment.Center,
            });
        }
        panel.Children.Add(header);
        if (!string.IsNullOrEmpty(p.Description))
        {
            panel.Children.Add(new TextBlock
            {
                Text = p.Description,
                FontSize = 10,
                Foreground = (Brush)Application.Current.TryFindResource("TextMutedBrush")!,
                Margin = new Thickness(0, 1, 0, 0),
                TextWrapping = TextWrapping.Wrap,
            });
        }
        if (p.EnumValues is { Count: > 0 })
        {
            var enumText = string.Join("、", p.EnumValues
                .Where(o => o.Count >= 2)
                .Select(o => $"{o[0]}={o[1]}"));
            panel.Children.Add(new TextBlock
            {
                Text = $"可选值：{enumText}",
                FontSize = 10,
                Foreground = Brushes.DarkOrange,
                Margin = new Thickness(0, 1, 0, 0),
            });
        }
        return new Border
        {
            Background = new SolidColorBrush(Colors.Lavender) { Opacity = 0.4 },
            CornerRadius = new CornerRadius(4),
            Padding = new Thickness(8, 6, 8, 6),
            Margin = new Thickness(0, 0, 0, 4),
            Child = panel,
        };
    }

    private void Close_Click(object sender, RoutedEventArgs e) => Close();
}
