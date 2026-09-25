using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using VisionFlow.Studio;
using VisionFlow.Studio.Interop;
using VisionFlow.Studio.Models;
using VisionFlow.Studio.Nodes;

namespace VisionFlow.Studio.Controls;

/// <summary>
/// 双击节点弹出的参数设置对话框。
/// 不在主界面常驻显示参数，符合"参数双击后弹出"的交互需求。
/// 内部仍可触发 ROI 框选（通过 Owner=MainWindow 调用 OpenRoiEditor）。
/// </summary>
public partial class NodeParameterDialog : Window
{
    public Node Node { get; }
    private bool _confirmed;

    /// <summary>回调：当用户点击 ROI 绘制按钮时，把当前参数名交回 MainWindow 处理。</summary>
    public Action<Node, string>? OnRoiDrawRequested { get; set; }

    public NodeParameterDialog(Node node)
    {
        InitializeComponent();
        Node = node;
        DataContext = node;
        BuildParamPanel();
    }

    private void BuildParamPanel()
    {
        ParamPanel.Children.Clear();
        var desc = NodeRegistry.Instance.Get(Node.TypeId);
        if (desc is null || desc.Parameters.Count == 0)
        {
            ParamPanel.Children.Add(new TextBlock
            {
                Text = "该节点无可编辑参数",
                FontStyle = FontStyles.Italic,
                Foreground = (System.Windows.Media.Brush)Application.Current.TryFindResource("TextMutedBrush")!,
                Margin = new Thickness(0, 8, 0, 0),
            });
            return;
        }

        foreach (var p in desc.Parameters)
        {
            // ROI 参数不在对话框里编辑：改为在右侧图像窗口的 ROI 工具栏上直接绘制。
            // 参数仍保留在节点描述中，以便图像窗口显示工具栏并把绘制结果传给算法。
            if (p.Type == "Roi") continue;

            var row = new StackPanel { Margin = new Thickness(0, 0, 0, 8) };

            // 参数名 + 类型 + 默认值
            var nameRow = new StackPanel { Orientation = Orientation.Horizontal };
            nameRow.Children.Add(new TextBlock
            {
                Text = $"{p.Name}  ({p.Type})",
                FontSize = 12,
                FontWeight = FontWeights.Medium,
                Foreground = (System.Windows.Media.Brush)Application.Current.TryFindResource("TextPrimaryBrush") ?? System.Windows.Media.Brushes.Black,
            });
            var defStr = p.Default?.ToString() ?? "";
            if (!string.IsNullOrEmpty(defStr))
            {
                nameRow.Children.Add(new TextBlock
                {
                    Text = $"   默认: {defStr}",
                    FontSize = 10,
                    Foreground = System.Windows.Media.Brushes.Gray,
                    VerticalAlignment = VerticalAlignment.Center,
                });
            }
            row.Children.Add(nameRow);

            // 参数说明
            if (!string.IsNullOrEmpty(p.Description))
            {
                row.Children.Add(new TextBlock
                {
                    Text = p.Description,
                    FontSize = 10,
                    Foreground = (System.Windows.Media.Brush)Application.Current.TryFindResource("TextMutedBrush")!,
                    Margin = new Thickness(0, 1, 0, 3),
                    TextWrapping = TextWrapping.Wrap,
                });
            }

            var prmName = p.Name;
            var currentVal = Node.Parameters.TryGetValue(p.Name, out var v) ? v?.ToString() ?? "" : "";

            TextBox? box = null;  // 统一声明，供 ROI/path 按钮引用

            // 枚举参数用 ComboBox
            if (p.EnumValues is { Count: > 0 })
            {
                var combo = new ComboBox
                {
                    Padding = new Thickness(4, 2, 4, 2),
                };
                foreach (var opt in p.EnumValues)
                {
                    if (opt.Count >= 2)
                    {
                        var val = opt[0]?.ToString() ?? "";
                        var label = opt[1]?.ToString() ?? val;
                        var item = new ComboBoxItem { Content = label, Tag = val };
                        combo.Items.Add(item);
                        if (val == currentVal)
                            combo.SelectedItem = item;
                    }
                }
                if (combo.SelectedItem is null && combo.Items.Count > 0)
                    combo.SelectedIndex = 0;
                combo.SelectionChanged += (s, _) =>
                {
                    if (s is ComboBox cb && cb.SelectedItem is ComboBoxItem cbi)
                        Node.Parameters[prmName] = cbi.Tag?.ToString() ?? "";
                };
                row.Children.Add(combo);
            }
            else if (p.Type != "Roi")
            {
                box = new TextBox
                {
                    Text = currentVal,
                    Padding = new Thickness(4, 2, 4, 2),
                };
                box.TextChanged += (s, _) =>
                {
                    if (s is TextBox tb)
                        Node.Parameters[prmName] = tb.Text;
                };
                row.Children.Add(box);
            }

            // ROI 参数：只读显示 + 绘制按钮（禁止手动输入文本）
            if (p.Type == "Roi")
            {
                var shape = p.GetRoiShape();
                var shapeLabel = shape switch
                {
                    "circle" => "圆形",
                    "rotated_rect" => "旋转矩形",
                    _ => "矩形",
                };
                var valBox = new TextBlock
                {
                    Text = string.IsNullOrEmpty(currentVal) ? "(未绘制)" : currentVal,
                    FontFamily = new System.Windows.Media.FontFamily("Consolas"),
                    FontSize = 11,
                    Padding = new Thickness(4, 2, 4, 2),
                    Background = System.Windows.Media.Brushes.Black,
                    Foreground = System.Windows.Media.Brushes.LightGreen,
                    TextWrapping = TextWrapping.Wrap,
                };
                row.Children.Add(valBox);

                var drawBtn = new Button
                {
                    Content = $"绘制{shapeLabel}...",
                    Padding = new Thickness(8, 2, 8, 2),
                    Margin = new Thickness(0, 4, 0, 0),
                    HorizontalAlignment = HorizontalAlignment.Left,
                };
                var paramForClosure = prmName;
                drawBtn.Click += (s, _) =>
                {
                    OnRoiDrawRequested?.Invoke(Node, paramForClosure);
                    // 关闭对话框，让用户回到主图像窗口绘制
                    _confirmed = false;
                    DialogResult = false;
                    Close();
                };
                row.Children.Add(drawBtn);

                ParamPanel.Children.Add(row);
                continue;   // 跳过下面的 path 分支
            }

            // path 参数提供"浏览..."按钮（OpenFileDialog）
            if (p.Name == "path")
            {
                var browseBtn = new Button
                {
                    Content = "浏览...",
                    Padding = new Thickness(8, 2, 8, 2),
                    Margin = new Thickness(0, 4, 0, 0),
                    HorizontalAlignment = HorizontalAlignment.Left,
                };
                browseBtn.Click += (s, _) =>
                {
                    var ofd = new OpenFileDialog
                    {
                        Title = "选择图像文件",
                        Filter = "图像文件|*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff|所有文件|*.*",
                        CheckFileExists = true,
                    };
                    if (!string.IsNullOrEmpty(box!.Text))
                    {
                        try
                        {
                            var dir = System.IO.Path.GetDirectoryName(box.Text);
                            if (!string.IsNullOrEmpty(dir) && System.IO.Directory.Exists(dir))
                                ofd.InitialDirectory = dir;
                        }
                        catch { }
                    }
                    if (ofd.ShowDialog() == true)
                        box!.Text = ofd.FileName;
                };
                row.Children.Add(browseBtn);
            }

            // 相机节点的 device_sn：提供「枚举相机」下拉，无需手填序列号
            if (p.Name == "device_sn")
            {
                var enumBtn = new Button
                {
                    Content = "枚举相机...",
                    Padding = new Thickness(8, 2, 8, 2),
                    Margin = new Thickness(0, 4, 0, 0),
                    HorizontalAlignment = HorizontalAlignment.Left,
                };
                var camCombo = new ComboBox
                {
                    Padding = new Thickness(4, 2, 4, 2),
                    Margin = new Thickness(0, 4, 0, 0),
                    Visibility = Visibility.Collapsed,
                };
                var snBox = box;
                enumBtn.Click += (s, _) =>
                {
                    var cams = VzAlgoInterop.EnumCameras(out var err);
                    camCombo.Items.Clear();
                    if (cams.Count == 0)
                    {
                        MessageBox.Show(this,
                            err is null ? "未发现在线相机（请检查相机连接/供电/网卡）" : $"枚举失败：{err}",
                            "枚举相机", MessageBoxButton.OK, MessageBoxImage.Information);
                        return;
                    }
                    foreach (var c in cams)
                        camCombo.Items.Add(new ComboBoxItem { Content = c.Display, Tag = c.Sn });
                    camCombo.Visibility = Visibility.Visible;
                    if (camCombo.Items.Count > 0)
                        camCombo.SelectedIndex = 0;
                };
                camCombo.SelectionChanged += (s, _) =>
                {
                    if (camCombo.SelectedItem is ComboBoxItem cbi && cbi.Tag is string sn)
                    {
                        if (snBox is not null) snBox.Text = sn;
                        Node.Parameters["device_sn"] = sn;
                    }
                };
                row.Children.Add(enumBtn);
                row.Children.Add(camCombo);
            }

            ParamPanel.Children.Add(row);
        }

        AddTcpControls();
    }

    /// <summary>TCP 节点：参数下方追加 启动监听/连接/停止 按钮与状态。</summary>
    private void AddTcpControls()
    {
        if (Node.TypeId is not ("TcpServer" or "TcpClient")) return;

        bool isServer = Node.TypeId == "TcpServer";
        string PStr(string name, string fb)
            => Node.Parameters.TryGetValue(name, out var v) && v is string s && !string.IsNullOrEmpty(s) ? s : fb;
        int PInt(string name, int fb)
        {
            if (Node.Parameters.TryGetValue(name, out var v) && v is not null)
            {
                if (v is int i) return i;
                if (int.TryParse(v.ToString(), out var p)) return p;
            }
            return fb;
        }

        var panel = new StackPanel { Margin = new Thickness(0, 10, 0, 0) };
        var btnRow = new StackPanel { Orientation = Orientation.Horizontal };
        var startBtn = new Button
        {
            Content = isServer ? "启动监听" : "连接",
            Padding = new Thickness(12, 5, 12, 5),
            Margin = new Thickness(0, 0, 8, 0),
        };
        var stopBtn = new Button { Content = "停止", Padding = new Thickness(12, 5, 12, 5) };
        var status = new TextBlock { Margin = new Thickness(0, 6, 0, 0), FontSize = 12 };

        void Refresh()
        {
            string st = isServer
                ? (TcpNodeRuntime.IsListening(Node.InstanceId) ? "● 监听中" : "○ 未监听")
                : (TcpNodeRuntime.IsConnected(Node.InstanceId) ? "● 已连接" : "○ 未连接");
            var err = TcpNodeRuntime.LastError(Node.InstanceId);
            if (!string.IsNullOrEmpty(err)) st += "   错误: " + err;
            status.Text = st;
        }

        startBtn.Click += (_, _) =>
        {
            TcpNodeRuntime.Stop(Node.InstanceId);
            if (isServer)
                TcpNodeRuntime.StartServer(Node.InstanceId, PStr("ip", "0.0.0.0"), PInt("port", 8080), PStr("terminator", "\n"));
            else
                TcpNodeRuntime.ConnectClient(Node.InstanceId, PStr("ip", "127.0.0.1"), PInt("port", 8080),
                    PStr("trigger", ""), PStr("terminator", "\n"));
            Refresh();
        };
        stopBtn.Click += (_, _) => { TcpNodeRuntime.Stop(Node.InstanceId); Refresh(); };

        btnRow.Children.Add(startBtn);
        btnRow.Children.Add(stopBtn);
        panel.Children.Add(btnRow);
        panel.Children.Add(status);
        ParamPanel.Children.Add(panel);
        Refresh();
    }

    private void OK_Click(object sender, RoutedEventArgs e)
    {
        _confirmed = true;
        DialogResult = true;
        Close();
    }

    /// <summary>用户是否点击了确定（TextChanged 已实时同步参数到 Node.Parameters）。</summary>
    public bool Confirmed => _confirmed;
}
