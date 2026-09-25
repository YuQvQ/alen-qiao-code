using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using VisionFlow.Studio.Models;

namespace VisionFlow.Studio.Controls;

/// <summary>
/// 节点视觉控件。DataContext = Node 实例。
///
/// 交互转发：
///   - 鼠标左键按下节点本体：转发给 NodeCanvas 启动"节点拖拽"
///   - 鼠标左键按下端口圆点：转发给 NodeCanvas 启动"连线拖拽"
///   - 鼠标左键按下节点（非端口）：设为选中节点
///
/// 错误态视觉：
///   - 监听 Node.HasError 变化 → IsError 同步 → HeaderBrush/NodeBorderBrush 重算
///   - 标题栏在 HasError=true 时强制使用 ErrorBrush（红色 #DC2626）
///   - 外边框在 HasError=true 时强制使用 ErrorBorderBrush（#B91C1C）
///   - 标题栏右侧显示 ⚠ 图标，ToolTip 给完整 LastError
///
/// 不持有状态，全部委托 NodeCanvas 处理（避免每个节点控件各自维护鼠标捕获冲突）
/// </summary>
public partial class NodeControl : UserControl, INotifyPropertyChanged
{
    public NodeControl()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    /// <summary>当前绑定的 Node 实例（DataContext 即 Node）</summary>
    public Node? Node => DataContext as Node;

    // ============================================================
    // 依赖属性：IsError
    // ============================================================
    /// <summary>
    /// 错误态：true 时标题栏+外边框变红，标题栏右侧显示 ⚠ 图标。
    /// 由 Node.HasError 同步而来（监听 Node.PropertyChanged）。
    /// </summary>
    public static readonly DependencyProperty IsErrorProperty =
        DependencyProperty.Register(
            nameof(IsError), typeof(bool), typeof(NodeControl),
            new PropertyMetadata(false, OnIsErrorChanged));

    public bool IsError
    {
        get => (bool)GetValue(IsErrorProperty);
        set => SetValue(IsErrorProperty, value);
    }

    private static void OnIsErrorChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        // IsError 变化 → 通知 HeaderBrush/NodeBorderBrush/ErrorToolTip 重新求值
        var nc = (NodeControl)d;
        nc.RaisePropertyChanged(nameof(HeaderBrush));
        nc.RaisePropertyChanged(nameof(NodeBorderBrush));
        nc.RaisePropertyChanged(nameof(ErrorToolTip));
    }

    // ============================================================
    // 标题栏背景画刷（CLR 属性，由 INPC 通知变化）
    // 正常=CategoryBrush，错误=ErrorBrush
    // ============================================================
    public System.Windows.Media.Brush HeaderBrush
    {
        get
        {
            if (IsError)
                return (System.Windows.Media.Brush)Application.Current.TryFindResource("ErrorBrush")!;
            return CategoryBrush;
        }
    }

    // ============================================================
    // 外边框画刷（CLR 属性，由 INPC 通知变化）
    // 正常=BorderBrush，错误=ErrorBorderBrush
    // ============================================================
    public System.Windows.Media.Brush NodeBorderBrush
    {
        get
        {
            if (IsError)
                return (System.Windows.Media.Brush)Application.Current.TryFindResource("ErrorBorderBrush")!;
            return (System.Windows.Media.Brush)Application.Current.TryFindResource("BorderBrush")!;
        }
    }

    // ============================================================
    // 错误提示文本（ToolTip）：LastError
    // ============================================================
    public string? ErrorToolTip => Node?.LastError;

    /// <summary>节点类别画刷（由 NodeControl 模板内 Binding 使用）</summary>
    public System.Windows.Media.Brush CategoryBrush
    {
        get
        {
            if (Node?.Category is string c)
            {
                var key = $"NodeCategoryBrush.{c}";
                if (Application.Current.TryFindResource(key) is System.Windows.Media.Brush b)
                    return b;
            }
            return (System.Windows.Media.Brush)Application.Current.TryFindResource("BrandBrush");
        }
    }

    // ============================================================
    // 监听 Node 的 PropertyChanged：把 HasError/LastError 同步到 IsError
    // ============================================================
    private void OnDataContextChanged(object sender, DependencyPropertyChangedEventArgs e)
    {
        if (e.OldValue is Node oldNode)
            oldNode.PropertyChanged -= OnNodePropertyChanged;
        if (e.NewValue is Node newNode)
            newNode.PropertyChanged += OnNodePropertyChanged;
        // 初次绑定：同步一次
        SyncErrorState();
    }

    private void OnNodePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(Node.HasError) or nameof(Node.LastError))
            SyncErrorState();
    }

    private void SyncErrorState()
    {
        var node = Node;
        if (node is null) return;
        // SetCurrentValue 不覆盖绑定，但这里 IsError 没有外部 Binding，是纯 DP 写入
        SetCurrentValue(IsErrorProperty, node.HasError);
    }

    // ============================================================
    // 上报事件：转发给 NodeCanvas 处理（避免控件自身持有引用）
    // ============================================================
    /// <summary>用户点击节点本体的"▶ 运行"按钮时触发</summary>
    public event Action<NodeControl>? NodeRunRequested;

    /// <summary>用户双击节点本体（非端口、非运行按钮）时触发</summary>
    public event Action<NodeControl>? NodeDoubleClicked;

    /// <summary>用户点击"?"帮助按钮时触发</summary>
    public event Action<NodeControl>? NodeHelpRequested;

    private void RunButton_Click(object sender, RoutedEventArgs e)
    {
        NodeRunRequested?.Invoke(this);
        e.Handled = true;     // 阻止冒泡到节点本体（避免同时触发拖拽/双击）
    }

    private void HelpButton_Click(object sender, RoutedEventArgs e)
    {
        NodeHelpRequested?.Invoke(this);
        e.Handled = true;
    }

    protected override void OnMouseDoubleClick(MouseButtonEventArgs e)
    {
        base.OnMouseDoubleClick(e);
        if (e.Handled) return;
        // 双击节点本体：上报 NodeCanvas → 由 MainWindow 触发参数面板
        NodeDoubleClicked?.Invoke(this);
        e.Handled = true;
    }

    /// <summary>
    /// 端口圆点按下：上报 NodeCanvas，由画布决定是连线还是其他。
    /// 用 InputBubbles=false 防止节点拖拽冲突。
    /// </summary>
    private void Port_MouseDown(object sender, MouseButtonEventArgs e)
    {
        if (sender is FrameworkElement fe && fe.DataContext is Port port)
        {
            var canvas = FindAncestor<NodeCanvas>(this);
            canvas?.BeginConnect(this, port, e.GetPosition(canvas));
            e.Handled = true;     // 阻止节点本体进入拖拽
        }
    }

    /// <summary>节点本体按下：启动拖拽 + 选中</summary>
    protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
    {
        base.OnMouseLeftButtonDown(e);
        if (e.Handled) return;
        var canvas = FindAncestor<NodeCanvas>(this);
        canvas?.BeginDragNode(this, e.GetPosition(canvas));
        canvas?.SelectNode(this);
        e.Handled = true;
    }

    private static T? FindAncestor<T>(DependencyObject d) where T : DependencyObject
    {
        while (d is not null)
        {
            if (d is T t) return t;
            d = System.Windows.Media.VisualTreeHelper.GetParent(d);
        }
        return null;
    }

    // ============================================================
    // INotifyPropertyChanged
    // ============================================================
    public event PropertyChangedEventHandler? PropertyChanged;

    private void RaisePropertyChanged([CallerMemberName] string? name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

/// <summary>
/// 把端口类型上的 ColorBrushKey（字符串）解析为 App.xaml 中的画刷资源。
/// </summary>
public sealed class BrushKeyConverter : System.Windows.Data.IValueConverter
{
    public static BrushKeyConverter Instance { get; } = new();

    public object Convert(object value, Type targetType, object parameter,
                          System.Globalization.CultureInfo culture)
    {
        if (value is string key)
        {
            if (Application.Current.TryFindResource(key) is System.Windows.Media.Brush b)
                return b;
        }
        return System.Windows.Media.Brushes.Gray;
    }

    public object ConvertBack(object value, Type targetType, object parameter,
                              System.Globalization.CultureInfo culture)
        => throw new NotImplementedException();
}

/// <summary>
/// bool -> Visibility 转换器：true=Visible，false=Collapsed。
/// 用于错误标识 TextBlock 的显示控制。
/// </summary>
public sealed class BoolToVisibilityConverter : System.Windows.Data.IValueConverter
{
    public static BoolToVisibilityConverter Instance { get; } = new();

    public object Convert(object value, Type targetType, object parameter,
                          System.Globalization.CultureInfo culture)
        => value is bool b && b ? Visibility.Visible : Visibility.Collapsed;

    public object ConvertBack(object value, Type targetType, object parameter,
                              System.Globalization.CultureInfo culture)
        => throw new NotImplementedException();
}
