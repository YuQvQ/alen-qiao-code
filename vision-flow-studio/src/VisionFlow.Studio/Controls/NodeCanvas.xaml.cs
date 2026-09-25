using System.Collections.Specialized;
using System.Data;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using VisionFlow.Studio.Interop;
using VisionFlow.Studio.Models;
using VisionFlow.Studio.Nodes;
using VisionFlow.Studio.TypeSystem;

namespace VisionFlow.Studio.Controls;

// ============================================================
// 节点画布：承载所有 NodeControl + 连线 Path。
//
// 职责：
//   1. 订阅 Graph.Nodes/Edges 集合变化，自动同步视觉
//   2. 节点拖拽：NodeControl.MouseDown -> BeginDragNode -> MouseMove 更新位置
//   3. 端口连线：NodeControl.Port.MouseDown -> BeginConnect -> MouseMove 画临时线
//      -> MouseUp 在端口圆点上 -> Graph.TryAddEdge（强类型校验）
//   4. 节点选中：单击节点 SelectNode；点空白取消选中
//   5. 连线删除：选中连线 + Delete 键
//
// 状态机：
//   - 空闲：无交互
//   - DragNode：正在拖拽某节点
//   - Connecting：正在画连线，起点是某输出端口
//
// 视觉规则：
//   - 连线用 Bezier 曲线，颜色用源端口类型画刷
//   - 拖拽中的临时线用虚线
//   - 选中节点边框变成 BrandBrush
// ============================================================

public partial class NodeCanvas : Canvas
{
    // ============================================================
    // 依赖属性
    // ============================================================
    public static readonly DependencyProperty GraphProperty =
        DependencyProperty.Register(
            nameof(Graph), typeof(Graph), typeof(NodeCanvas),
            new PropertyMetadata(null, OnGraphChanged));

    public Graph? Graph
    {
        get => (Graph)GetValue(GraphProperty);
        set => SetValue(GraphProperty, value);
    }

    // 当前选中节点
    public static readonly DependencyProperty SelectedNodeProperty =
        DependencyProperty.Register(
            nameof(SelectedNode), typeof(Node), typeof(NodeCanvas),
            new PropertyMetadata(null, OnSelectedNodeChanged));

    public Node? SelectedNode
    {
        get => (Node)GetValue(SelectedNodeProperty);
        set => SetValue(SelectedNodeProperty, value);
    }

    // ============================================================
    // 内部状态
    // ============================================================
    private readonly Dictionary<Node, NodeControl> _nodeCtrls = new();
    private readonly Dictionary<Edge, Path> _edgePaths = new();
    // 悬浮属性标签（Border 标签 + Path 箭头 + 目标锚点）
    private readonly List<FloatingLabelInfo> _floatingLabelInfos = new();

    private enum DragState { None, DragNode, Connecting, DragLabel }
    private DragState _state = DragState.None;
    private NodeControl? _draggingNode;
    private Point _dragOffset;             // 节点内坐标偏移
    private Port? _connectSource;          // 连线起点（输出端口）
    private NodeControl? _connectSourceCtrl;
    private Point _connectMousePos;        // 连线终点（鼠标在画布坐标）
    private const double SnapRadius = 22.0; // 端口吸附半径（像素）
    private Port? _snapTarget;             // 当前吸附中的目标端口
    private Port? _lastSnapHint;           // 上次提示的吸附目标（避免刷屏）
    private Path? _tempPath;               // 拖拽中的临时连线
    // 悬浮标签拖拽
    private FloatingLabelInfo? _draggingLabelInfo;
    private Point _labelDragOffset;

    // === 画布缩放与平移 ===
    private double _scale = 1.0;
    private readonly ScaleTransform _scaleTransform = new(1.0, 1.0);
    private readonly TranslateTransform _translateTransform = new(0, 0);
    private bool _isPanning;
    private Point _panStartScreen;
    private Point _panStartTranslate;

    public NodeCanvas()
    {
        InitializeComponent();
        Background = (Brush)Application.Current.TryFindResource("SurfaceMutedBrush")
                     ?? new SolidColorBrush(Color.FromRgb(0xF4, 0xF4, 0xF5));

        // 变换应用到 ContentCanvas（节点+连线），不影响网格背景层
        var tg = new TransformGroup();
        tg.Children.Add(_scaleTransform);
        tg.Children.Add(_translateTransform);
        ContentCanvas.RenderTransform = tg;
        ContentCanvas.RenderTransformOrigin = new Point(0, 0);

        // 监听键盘删除
        KeyDown += OnKeyDown;
        // 鼠标移动 + 释放（全局）
        MouseMove += OnCanvasMouseMove;
        MouseUp += OnCanvasMouseUp;
        MouseLeftButtonUp += OnCanvasMouseUp;
        // 滚轮缩放
        MouseWheel += OnCanvasMouseWheel;
        // 中键按下开始平移
        MouseDown += OnCanvasMouseDown;
        // 尺寸变化时重绘网格
        SizeChanged += (s, e) => DrawGrid();

        // 允许从左侧节点库拖拽创建节点
        AllowDrop = true;
        DragOver += OnCanvasDragOver;
        Drop += OnCanvasDrop;
    }

    // 屏幕坐标 → 内容层逻辑坐标
    private Point ScreenToContent(Point screen)
    {
        return new Point(
            (screen.X - _translateTransform.X) / _scale,
            (screen.Y - _translateTransform.Y) / _scale);
    }

    // ============================================================
    // 画布缩放与平移
    // ============================================================
    private void OnCanvasMouseWheel(object sender, MouseWheelEventArgs e)
    {
        var mouseScreen = e.GetPosition(this);  // 相对于 NodeCanvas 的屏幕坐标
        double oldScale = _scale;
        double factor = e.Delta > 0 ? 1.15 : 1.0 / 1.15;
        double newScale = Math.Max(0.2, Math.Min(5.0, oldScale * factor));

        // 鼠标对应的逻辑坐标
        double logicalX = (mouseScreen.X - _translateTransform.X) / oldScale;
        double logicalY = (mouseScreen.Y - _translateTransform.Y) / oldScale;

        _scale = newScale;
        _scaleTransform.ScaleX = newScale;
        _scaleTransform.ScaleY = newScale;

        // 保持鼠标下的逻辑点在同一屏幕位置
        _translateTransform.X = mouseScreen.X - logicalX * newScale;
        _translateTransform.Y = mouseScreen.Y - logicalY * newScale;

        DrawGrid();
        e.Handled = true;
    }

    /// <summary>
    /// 一键适应内容：把所有节点缩放到整个视野内并居中（工具栏「适应」按钮）。
    /// </summary>
    public void FitToContent()
    {
        if (_nodeCtrls.Count == 0) { ResetView(); return; }

        double minX = double.MaxValue, minY = double.MaxValue;
        double maxX = double.MinValue, maxY = double.MinValue;
        foreach (var ctrl in _nodeCtrls.Values)
        {
            double x = Canvas.GetLeft(ctrl), y = Canvas.GetTop(ctrl);
            double w = ctrl.ActualWidth > 0 ? ctrl.ActualWidth : 220;
            double h = ctrl.ActualHeight > 0 ? ctrl.ActualHeight : 140;
            if (x < minX) minX = x;
            if (y < minY) minY = y;
            if (x + w > maxX) maxX = x + w;
            if (y + h > maxY) maxY = y + h;
        }
        if (!double.IsFinite(minX)) return;

        double pad = 80;
        double contentW = maxX - minX + pad * 2;
        double contentH = maxY - minY + pad * 2;
        double viewW = ActualWidth > 0 ? ActualWidth : 1200;
        double viewH = ActualHeight > 0 ? ActualHeight : 700;

        double newScale = Math.Min(viewW / contentW, viewH / contentH);
        newScale = Math.Max(0.2, Math.Min(2.0, newScale));

        _scale = newScale;
        _scaleTransform.ScaleX = newScale;
        _scaleTransform.ScaleY = newScale;
        // 居中：让内容包围盒中心对齐视口中心
        _translateTransform.X = viewW / 2 - (minX + (maxX - minX) / 2) * newScale;
        _translateTransform.Y = viewH / 2 - (minY + (maxY - minY) / 2) * newScale;
        DrawGrid();
    }

    /// <summary>重置为 1:1 且回到原点。</summary>
    public void ResetView()
    {
        _scale = 1.0;
        _scaleTransform.ScaleX = 1.0;
        _scaleTransform.ScaleY = 1.0;
        _translateTransform.X = 0;
        _translateTransform.Y = 0;
        DrawGrid();
    }

    private void OnCanvasMouseDown(object sender, MouseButtonEventArgs e)
    {
        // 中键拖拽平移画布
        if (e.ChangedButton == MouseButton.Middle)
        {
            _isPanning = true;
            _panStartScreen = e.GetPosition(this);
            _panStartTranslate = new Point(_translateTransform.X, _translateTransform.Y);
            CaptureMouse();
            Cursor = Cursors.SizeAll;
            e.Handled = true;
        }
    }

    // 点阵网格背景：在 GridLayer 上绘制，不受变换影响，始终铺满可视区域
    private void DrawGrid()
    {
        GridLayer.Children.Clear();
        var brush = new SolidColorBrush(Color.FromRgb(0xCB, 0xCB, 0xD5)) { Opacity = 0.4 };
        double step = 16;
        double w = ActualWidth;
        double h = ActualHeight;
        if (w <= 0 || h <= 0) return;

        // 网格点也要跟随缩放和平移（在屏幕坐标空间绘制）
        // 屏幕坐标 = 逻辑坐标 * scale + translate
        // 所以逻辑网格点 (ix, iy) 对应的屏幕坐标 = (ix*scale+tx, iy*scale+ty)
        double s = _scale;
        double tx = _translateTransform.X;
        double ty = _translateTransform.Y;

        // 计算屏幕可视区域内需要绘制的逻辑点范围
        double startIX = Math.Floor((0 - tx) / (step * s)) * step;
        double startIY = Math.Floor((0 - ty) / (step * s)) * step;
        double endIX = Math.Ceiling((w - tx) / (step * s)) * step;
        double endIY = Math.Ceiling((h - ty) / (step * s)) * step;

        var ellipseSize = Math.Max(1.0, 1.5 * s);
        for (double iy = startIY; iy <= endIY; iy += step)
        {
            for (double ix = startIX; ix <= endIX; ix += step)
            {
                double sx = ix * s + tx;
                double sy = iy * s + ty;
                if (sx < -5 || sx > w + 5 || sy < -5 || sy > h + 5) continue;
                var ell = new Ellipse
                {
                    Width = ellipseSize,
                    Height = ellipseSize,
                    Fill = brush,
                    IsHitTestVisible = false,
                };
                Canvas.SetLeft(ell, sx - ellipseSize / 2);
                Canvas.SetTop(ell, sy - ellipseSize / 2);
                GridLayer.Children.Add(ell);
            }
        }
    }

    // ============================================================
    // Graph 同步
    // ============================================================
    private static void OnGraphChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NodeCanvas c)
        {
            if (e.OldValue is Graph oldGraph)
            {
                oldGraph.Nodes.CollectionChanged -= c.OnNodesChanged;
                oldGraph.Edges.CollectionChanged -= c.OnEdgesChanged;
                c.ClearAll();
            }
            if (e.NewValue is Graph newGraph)
            {
                newGraph.Nodes.CollectionChanged += c.OnNodesChanged;
                newGraph.Edges.CollectionChanged += c.OnEdgesChanged;
                foreach (var n in newGraph.Nodes) c.AddNodeVisual(n);
                foreach (var ed in newGraph.Edges) c.AddEdgeVisual(ed);
            }
        }
    }

    private void OnNodesChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems is not null)
            foreach (Node n in e.OldItems) RemoveNodeVisual(n);
        if (e.NewItems is not null)
            foreach (Node n in e.NewItems) AddNodeVisual(n);
    }

    private void OnEdgesChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems is not null)
            foreach (Edge ed in e.OldItems) RemoveEdgeVisual(ed);
        if (e.NewItems is not null)
            foreach (Edge ed in e.NewItems) AddEdgeVisual(ed);
    }

    // ============================================================
    // Node / Edge 视觉管理
    // ============================================================
    private void AddNodeVisual(Node node)
    {
        if (_nodeCtrls.ContainsKey(node)) return;
        var ctrl = new NodeControl { DataContext = node };
        // 订阅子控件上报事件 → 转发给 NodeCanvas 的订阅者
        ctrl.NodeRunRequested += c => NodeRunRequested?.Invoke(c);
        ctrl.NodeDoubleClicked += c => NodeDoubleClicked?.Invoke(c);
        ctrl.NodeHelpRequested += c => NodeHelpRequested?.Invoke(c);
        ContentCanvas.Children.Add(ctrl);
        SetLeft(ctrl, node.X);
        SetTop(ctrl, node.Y);
        SetZIndex(ctrl, 10);
        _nodeCtrls[node] = ctrl;
    }

    private void RemoveNodeVisual(Node node)
    {
        if (_nodeCtrls.TryGetValue(node, out var ctrl))
        {
            ContentCanvas.Children.Remove(ctrl);
            _nodeCtrls.Remove(node);
        }
        if (ReferenceEquals(SelectedNode, node)) SelectedNode = null;
    }

    private void AddEdgeVisual(Edge edge)
    {
        if (_edgePaths.ContainsKey(edge)) return;
        var path = new Path
        {
            Stroke = ResolvePortBrush(edge.From.Type.Name),
            StrokeThickness = 2,
            Data = BuildEdgeGeometry(edge),
            Cursor = Cursors.Hand,
        };
        // 路径点击选中（高亮 + 准备 Delete）
        path.MouseLeftButtonDown += (s, e) =>
        {
            _selectedEdge = edge;
            path.StrokeThickness = 4;
            InvalidateVisual();
            e.Handled = true;
        };
        ContentCanvas.Children.Insert(0, path); // 连线置于节点下方
        SetZIndex(path, 1);
        _edgePaths[edge] = path;
    }

    private void RemoveEdgeVisual(Edge edge)
    {
        if (_edgePaths.TryGetValue(edge, out var path))
        {
            ContentCanvas.Children.Remove(path);
            _edgePaths.Remove(edge);
        }
        if (ReferenceEquals(_selectedEdge, edge)) _selectedEdge = null;
    }

    private void ClearAll()
    {
        foreach (var ctrl in _nodeCtrls.Values.ToList()) ContentCanvas.Children.Remove(ctrl);
        foreach (var path in _edgePaths.Values.ToList()) ContentCanvas.Children.Remove(path);
        _nodeCtrls.Clear();
        _edgePaths.Clear();
        _selectedEdge = null;
    }

    private Edge? _selectedEdge;

    // ============================================================
    // 节点拖拽
    // ============================================================
    internal void BeginDragNode(NodeControl ctrl, Point mouseInCanvas)
    {
        if (ctrl.Node is null) return;
        _state = DragState.DragNode;
        _draggingNode = ctrl;
        var node = ctrl.Node;
        var p = ScreenToContent(mouseInCanvas);
        _dragOffset = new Point(p.X - node.X, p.Y - node.Y);
        CaptureMouse();
    }

    // ============================================================
    // 连线拖拽
    // ============================================================
    internal void BeginConnect(NodeControl ctrl, Port port, Point mouseInCanvas)
    {
        if (port.Direction == PortDirection.Input)
        {
            // 反向连线：从输入端口开始 -> 寻找目标输出
            // MVP 仅支持从输出端起始的连线
            return;
        }
        _state = DragState.Connecting;
        _connectSource = port;
        _connectSourceCtrl = ctrl;
        _connectMousePos = ScreenToContent(mouseInCanvas);
        EnsureTempPath();
        CaptureMouse();
    }

    private void EnsureTempPath()
    {
        if (_tempPath is null)
        {
            _tempPath = new Path
            {
                Stroke = (Brush)Application.Current.TryFindResource("BrandBrush")
                          ?? Brushes.Purple,
                StrokeThickness = 2,
                StrokeDashArray = new DoubleCollection { 3, 3 },
            };
            ContentCanvas.Children.Add(_tempPath);
            SetZIndex(_tempPath, 50);
        }
        _tempPath.Visibility = Visibility.Visible;
    }

    // ============================================================
    // 鼠标移动
    // ============================================================
    private void OnCanvasMouseMove(object sender, MouseEventArgs e)
    {
        // 中键平移画布
        if (_isPanning)
        {
            var panPos = e.GetPosition(this);
            double dx = panPos.X - _panStartScreen.X;
            double dy = panPos.Y - _panStartScreen.Y;
            _translateTransform.X = _panStartTranslate.X + dx;
            _translateTransform.Y = _panStartTranslate.Y + dy;
            DrawGrid();
            e.Handled = true;
            return;
        }

        var pos = ScreenToContent(e.GetPosition(this));
        switch (_state)
        {
            case DragState.DragNode:
                if (_draggingNode?.Node is { } node)
                {
                    node.X = pos.X - _dragOffset.X;
                    node.Y = pos.Y - _dragOffset.Y;
                    SetLeft(_draggingNode, node.X);
                    SetTop(_draggingNode, node.Y);
                    // 重画相关连线
                    foreach (var ed in Graph!.Edges
                        .Where(e2 => e2.From.Owner == node || e2.To.Owner == node))
                    {
                        if (_edgePaths.TryGetValue(ed, out var p))
                            p.Data = BuildEdgeGeometry(ed);
                    }
                    // 同步刷新悬浮属性标签位置
                    if (ReferenceEquals(SelectedNode, node))
                        BuildFloatingLabels(node);
                }
                break;
            case DragState.DragLabel:
                if (_draggingLabelInfo is { } li)
                {
                    SetLeft(li.Label, pos.X - _labelDragOffset.X);
                    SetTop(li.Label, pos.Y - _labelDragOffset.Y);
                    UpdateLabelArrow(li);
                }
                break;
            case DragState.Connecting:
                _connectMousePos = pos;
                if (_tempPath is not null && _connectSource is not null)
                {
                    var start = GetPortAnchor(_connectSourceCtrl!, _connectSource);
                    // 吸附：找半径内最近的、类型匹配的输入端口
                    var snap = FindNearestInputPort(pos, SnapRadius / _scale, _connectSource.Type);
                    _snapTarget = snap;
                    var end = snap is not null
                        ? GetPortAnchor(_nodeCtrls[snap.Owner], snap)
                        : pos;
                    _tempPath.Data = BuildBezier(start, end);
                    // 状态栏提示当前可吸附目标（让用户知道松开会连到哪）
                    if (snap is not null && snap != _lastSnapHint)
                    {
                        _lastSnapHint = snap;
                        StatusText?.Invoke($"吸附到：{snap.Owner.DisplayName} / {snap.Name} ({snap.Type.Name})");
                    }
                    else if (snap is null && _lastSnapHint is not null)
                    {
                        _lastSnapHint = null;
                    }
                }
                break;
        }
    }

    // ============================================================
    // 鼠标释放
    // ============================================================
    private void OnCanvasMouseUp(object sender, MouseButtonEventArgs e)
    {
        // 中键平移结束
        if (_isPanning)
        {
            _isPanning = false;
            Cursor = Cursors.Arrow;
            ReleaseMouseCapture();
            return;
        }

        if (_state == DragState.Connecting)
        {
            // 优先使用吸附目标；没有吸附时退化到 VisualHitTest
            Port? targetPort = _snapTarget ?? VisualHitTest(ScreenToContent(e.GetPosition(this)));
            if (targetPort is Port tp && tp.Direction == PortDirection.Input
                && _connectSource is not null && Graph is not null)
            {
                bool ok = Graph.TryAddEdge(_connectSource, tp);
                if (!ok)
                    StatusText?.Invoke($"连接失败：{_connectSource.Type.Name} → {tp.Type.Name} 类型不一致");
                else
                    StatusText?.Invoke($"已连接 {_connectSource.Owner.DisplayName}.{_connectSource.Name} → {tp.Owner.DisplayName}.{tp.Name}");
            }
            if (_tempPath is not null) _tempPath.Visibility = Visibility.Collapsed;
            _snapTarget = null;
            _lastSnapHint = null;
        }
        _state = DragState.None;
        _draggingNode = null;
        _draggingLabelInfo = null;
        _connectSource = null;
        _connectSourceCtrl = null;
        ReleaseMouseCapture();
    }

    // ============================================================
    // 选中
    // ============================================================
    internal void SelectNode(NodeControl ctrl)
    {
        if (ctrl.Node is null) return;
        SelectedNode = ctrl.Node;
    }

    private static void OnSelectedNodeChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is NodeCanvas c)
        {
            // 更新节点选中边框
            foreach (var (node, ctrl) in c._nodeCtrls)
            {
                var border = FindVisualChild<Border>(ctrl);
                if (border is null) continue;
                border.BorderBrush = ReferenceEquals(node, e.NewValue)
                    ? (Brush)Application.Current.TryFindResource("BrandBrush") ?? Brushes.Purple
                    : (Brush)Application.Current.TryFindResource("BorderBrush") ?? Brushes.Gray;
                border.BorderThickness = new Thickness(ReferenceEquals(node, e.NewValue) ? 2 : 1);
            }
            // 选中节点 -> 画悬浮属性标签；取消选中 -> 清除
            if (e.NewValue is Node sel)
                c.BuildFloatingLabels(sel);
            else
                c.ClearFloatingLabels();
            c.SelectedNodeChanged?.Invoke(e.NewValue as Node);
        }
    }

    public event Action<Node?>? SelectedNodeChanged;
    public event Action<string>? StatusText;

    // ============================================================
    // 悬浮属性标签：选中节点时，在画布上绘制该节点的
    //   参数 + 输入端口当前值 + 输出端口当前值
    // 用箭头从标签指向对应端口圆点。
    // 标签可拖拽（避免重叠），拖拽时箭头自动跟随。
    // 取消选中时 ClearFloatingLabels() 清除全部。
    // ============================================================

    /// <summary>标签+箭头+目标锚点的绑定信息</summary>
    private sealed class FloatingLabelInfo
    {
        public Border Label = null!;
        public Path Arrow = null!;
        public Point TargetAnchor;   // 箭头终点（端口圆点或节点顶部）
        public string Kind = "";     // "param" / "input" / "output"
        public Node? Node;            // 所属节点
        public string PortName = "";  // 端口名（input/output 标签用）
    }

    /// <summary>刷新选中节点的悬浮属性标签（参数变化后由外部调用）。</summary>
    public void RefreshFloatingLabels()
    {
        if (SelectedNode is Node sel)
            BuildFloatingLabels(sel);
        else
            ClearFloatingLabels();
    }

    private void ClearFloatingLabels()
    {
        foreach (var info in _floatingLabelInfos)
        {
            ContentCanvas.Children.Remove(info.Label);
            ContentCanvas.Children.Remove(info.Arrow);
        }
        _floatingLabelInfos.Clear();
    }

    private void BuildFloatingLabels(Node node)
    {
        ClearFloatingLabels();
        if (!_nodeCtrls.TryGetValue(node, out var ctrl)) return;
        ctrl.UpdateLayout();

        var labelFont = new FontFamily("Consolas");
        var brandBrush = (Brush)Application.Current.TryFindResource("BrandBrush") ?? Brushes.Purple;

        // === 参数标签（放在节点上方，箭头指向节点顶部） ===
        var desc = NodeRegistry.Instance.Get(node.TypeId);
        if (desc is not null && desc.Parameters.Count > 0)
        {
            var lines = new List<string> { "参数" };
            foreach (var p in desc.Parameters)
            {
                var v = node.Parameters.TryGetValue(p.Name, out var pv) ? pv?.ToString() ?? "" : "";
                lines.Add($"  {p.Name} = {(string.IsNullOrEmpty(v) ? "(空)" : v)}");
            }
            var label = CreateFloatingLabel(string.Join("\n", lines), labelFont, brandBrush);
            var labelW = label.DesiredSize.Width;
            var labelH = label.DesiredSize.Height;
            double nodeX = GetLeft(ctrl);
            double nodeY = GetTop(ctrl);
            SetLeft(label, nodeX);
            SetTop(label, Math.Max(0, nodeY - labelH - 6));
            var targetAnchor = new Point(nodeX + ctrl.ActualWidth / 2, nodeY);
            var arrow = CreateArrow(
                new Point(nodeX + Math.Min(labelW, ctrl.ActualWidth) / 2, nodeY - 6),
                targetAnchor, brandBrush);
            var info = new FloatingLabelInfo
            {
                Label = label, Arrow = arrow,
                TargetAnchor = targetAnchor, Kind = "param",
                Node = node,
            };
            AddFloatingLabel(info);
        }

        // === 输入端口标签（放在节点左侧，箭头指向端口圆点） ===
        foreach (var p in node.Inputs)
        {
            var anchor = GetPortAnchor(ctrl, p);
            string valStr;
            if (node.InputCache.TryGetValue(p.Name, out var val) && val is not null)
            {
                valStr = DescribePortValue(val);
            }
            else
            {
                // 本节点尚未运行（InputCache 空）：回查连线上游的 OutputCache，
                // 若上游已产出值则显示出来（标注"上游"），避免误以为没连线。
                var edge = Graph?.EdgesInto(node).FirstOrDefault(e => e.To.Name == p.Name);
                if (edge is not null
                    && edge.From.Owner.OutputCache.TryGetValue(edge.From.Name, out var upVal)
                    && upVal is not null)
                {
                    valStr = $"上游:{DescribePortValue(upVal)}";
                }
                else if (edge is not null)
                {
                    valStr = "(已连接,上游未运行)";
                }
                else
                {
                    valStr = "(未传值)";
                }
            }

            var text = $"{p.Name} : {p.Type.Name}\n  ← {valStr}";
            var label = CreateFloatingLabel(text, labelFont, brandBrush);
            var labelW = label.DesiredSize.Width;
            var labelH = label.DesiredSize.Height;
            SetLeft(label, Math.Max(0, anchor.X - labelW - 24));
            SetTop(label, anchor.Y - labelH / 2);
            var arrow = CreateArrow(
                new Point(anchor.X - 24, anchor.Y),
                anchor, brandBrush);
            var info = new FloatingLabelInfo
            {
                Label = label, Arrow = arrow,
                TargetAnchor = anchor, Kind = "input",
                Node = node, PortName = p.Name,
            };
            AddFloatingLabel(info);
        }

        // === 输出端口标签（放在节点右侧，箭头指向端口圆点） ===
        foreach (var p in node.Outputs)
        {
            var anchor = GetPortAnchor(ctrl, p);
            string valStr;
            if (!node.OutputCache.TryGetValue(p.Name, out var val) || val is null)
                valStr = "(未运行)";
            else
                valStr = DescribePortValue(val);

            var text = $"{p.Name} : {p.Type.Name}\n  → {valStr}";
            var label = CreateFloatingLabel(text, labelFont, brandBrush);
            var labelH = label.DesiredSize.Height;
            SetLeft(label, anchor.X + 24);
            SetTop(label, anchor.Y - labelH / 2);
            var arrow = CreateArrow(
                new Point(anchor.X + 24, anchor.Y),
                anchor, brandBrush);
            var info = new FloatingLabelInfo
            {
                Label = label, Arrow = arrow,
                TargetAnchor = anchor, Kind = "output",
                Node = node, PortName = p.Name,
            };
            AddFloatingLabel(info);
        }
    }

    /// <summary>把标签+箭头加入画布并注册拖拽+双击事件。</summary>
    private void AddFloatingLabel(FloatingLabelInfo info)
    {
        ContentCanvas.Children.Add(info.Arrow);
        SetZIndex(info.Arrow, 19);
        ContentCanvas.Children.Add(info.Label);
        SetZIndex(info.Label, 20);
        // 标签可拖拽：按下时进入 DragLabel 状态；双击弹出详细数据
        info.Label.MouseLeftButtonDown += (s, e) =>
        {
            if (e.ClickCount >= 2)
            {
                // 双击：弹出详细数据窗口
                ShowLabelDetail(info);
                e.Handled = true;
                return;
            }
            // 单击：进入拖拽状态
            _state = DragState.DragLabel;
            _draggingLabelInfo = info;
            var mousePos = ScreenToContent(e.GetPosition(this));
            _labelDragOffset = new Point(
                mousePos.X - GetLeft(info.Label),
                mousePos.Y - GetTop(info.Label));
            CaptureMouse();
            e.Handled = true;   // 阻止冒泡到画布（避免取消选中）
        };
        _floatingLabelInfos.Add(info);
    }

    /// <summary>双击悬浮标签：弹出详细数据窗口。</summary>
    private static void ShowLabelDetail(FloatingLabelInfo info)
    {
        if (info.Node is null) return;
        var node = info.Node;
        string title = info.Kind switch
        {
            "output" => $"{node.DisplayName} → 输出：{info.PortName}",
            "input"  => $"{node.DisplayName} → 输入：{info.PortName}",
            _         => $"{node.DisplayName} → 参数",
        };

        object? data = null;
        if (info.Kind == "output" && node.OutputCache.TryGetValue(info.PortName, out var outVal))
            data = outVal;
        else if (info.Kind == "input" && node.InputCache.TryGetValue(info.PortName, out var inVal))
            data = inVal;
        else if (info.Kind == "param")
            data = node.Parameters;

        var dlg = new Window
        {
            Title = title,
            Width = 520,
            Height = 420,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            Background = new SolidColorBrush(Color.FromRgb(0xFA, 0xFA, 0xFA)),
        };

        var scroll = new ScrollViewer
        {
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            Padding = new Thickness(12),
        };

        UIElement content = BuildDetailContent(data);
        scroll.Content = content;
        dlg.Content = scroll;
        dlg.ShowDialog();
    }

    /// <summary>根据数据类型构建详细内容 UI。</summary>
    private static UIElement BuildDetailContent(object? data)
    {
        // MatchResult 数组 → 表格
        if (data is VzMatchResult[] matches)
        {
            var dt = new System.Data.DataTable();
            dt.Columns.Add("#", typeof(int));
            dt.Columns.Add("中心X", typeof(double));
            dt.Columns.Add("中心Y", typeof(double));
            dt.Columns.Add("宽", typeof(double));
            dt.Columns.Add("高", typeof(double));
            dt.Columns.Add("角度", typeof(double));
            dt.Columns.Add("分数", typeof(double));
            for (int i = 0; i < matches.Length; i++)
            {
                var m = matches[i];
                dt.Rows.Add(i + 1, m.rect.x, m.rect.y, m.rect.w, m.rect.h, m.rect.angle, m.score);
            }
            return MakeDataGrid(dt, $"共 {matches.Length} 条匹配结果");
        }

        // Point2D 数组 → 表格
        if (data is VzPoint2D[] pts)
        {
            var dt = new System.Data.DataTable();
            dt.Columns.Add("#", typeof(int));
            dt.Columns.Add("X", typeof(double));
            dt.Columns.Add("Y", typeof(double));
            for (int i = 0; i < pts.Length; i++)
                dt.Rows.Add(i + 1, pts[i].x, pts[i].y);
            return MakeDataGrid(dt, $"共 {pts.Length} 个点");
        }

        // Rect 数组 → 表格
        if (data is VzRect[] rects)
        {
            var dt = new System.Data.DataTable();
            dt.Columns.Add("#", typeof(int));
            dt.Columns.Add("中心X", typeof(double));
            dt.Columns.Add("中心Y", typeof(double));
            dt.Columns.Add("宽", typeof(double));
            dt.Columns.Add("高", typeof(double));
            dt.Columns.Add("角度", typeof(double));
            for (int i = 0; i < rects.Length; i++)
                dt.Rows.Add(i + 1, rects[i].x, rects[i].y, rects[i].w, rects[i].h, rects[i].angle);
            return MakeDataGrid(dt, $"共 {rects.Length} 个矩形");
        }

        // 参数字典 → 键值表
        if (data is System.Collections.Generic.Dictionary<string, object?> dict)
        {
            var dt = new System.Data.DataTable();
            dt.Columns.Add("参数名", typeof(string));
            dt.Columns.Add("值", typeof(string));
            foreach (var kv in dict)
                dt.Rows.Add(kv.Key, kv.Value?.ToString() ?? "(空)");
            return MakeDataGrid(dt, $"共 {dict.Count} 个参数");
        }

        // 简单值
        var str = data?.ToString() ?? "(无数据)";
        return new TextBlock
        {
            Text = str,
            FontSize = 13,
            FontFamily = new FontFamily("Consolas"),
            TextWrapping = TextWrapping.Wrap,
        };
    }

    /// <summary>构建 DataGrid 并标注标题。</summary>
    private static UIElement MakeDataGrid(System.Data.DataTable dt, string summary)
    {
        var sp = new StackPanel();
        sp.Children.Add(new TextBlock
        {
            Text = summary,
            FontWeight = FontWeights.SemiBold,
            FontSize = 13,
            Margin = new Thickness(0, 0, 0, 8),
        });
        var dg = new DataGrid
        {
            ItemsSource = dt.DefaultView,
            AutoGenerateColumns = true,
            IsReadOnly = true,
            HeadersVisibility = DataGridHeadersVisibility.Column,
            FontSize = 12,
        };
        sp.Children.Add(dg);
        return sp;
    }

    /// <summary>拖拽标签后重算箭头：从标签矩形最近的边缘点到目标锚点。</summary>
    private static void UpdateLabelArrow(FloatingLabelInfo info)
    {
        double x = GetLeft(info.Label);
        double y = GetTop(info.Label);
        double w = info.Label.DesiredSize.Width;
        double h = info.Label.DesiredSize.Height;
        // 标签矩形上离 TargetAnchor 最近的点
        double cx = Math.Max(x, Math.Min(info.TargetAnchor.X, x + w));
        double cy = Math.Max(y, Math.Min(info.TargetAnchor.Y, y + h));
        info.Arrow.Data = CreateArrowGeometry(new Point(cx, cy), info.TargetAnchor);
    }

    /// <summary>创建一个悬浮标签 Border（白底、品牌色边框、圆角、可拖拽）。</summary>
    private static Border CreateFloatingLabel(string text, FontFamily font, Brush borderBrush)
    {
        var tb = new TextBlock
        {
            Text = text,
            FontSize = 10,
            FontFamily = font,
            Foreground = Brushes.Black,
            TextWrapping = TextWrapping.NoWrap,
        };
        var border = new Border
        {
            Background = new SolidColorBrush(Color.FromArgb(232, 255, 255, 255)),
            BorderBrush = borderBrush,
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(4),
            Padding = new Thickness(6, 3, 6, 3),
            Child = tb,
            Cursor = Cursors.SizeAll,
            Effect = new System.Windows.Media.Effects.DropShadowEffect
            {
                BlurRadius = 4,
                ShadowDepth = 1,
                Opacity = 0.15,
                Color = Colors.Black,
            },
        };
        border.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        return border;
    }

    /// <summary>箭头几何（线段 + 三角箭头），供 CreateArrow 和拖拽更新共用。</summary>
    private static Geometry CreateArrowGeometry(Point from, Point to)
    {
        var fig = new PathFigure { StartPoint = from, IsClosed = false };
        fig.Segments.Add(new LineSegment(to, true));

        // 箭头三角
        double angle = Math.Atan2(to.Y - from.Y, to.X - from.X);
        double arrowLen = 7;
        double arrowAngle = 0.5; // ~28.6 度
        var p1 = new Point(
            to.X - arrowLen * Math.Cos(angle - arrowAngle),
            to.Y - arrowLen * Math.Sin(angle - arrowAngle));
        var p2 = new Point(
            to.X - arrowLen * Math.Cos(angle + arrowAngle),
            to.Y - arrowLen * Math.Sin(angle + arrowAngle));
        var headFig = new PathFigure { StartPoint = p1, IsClosed = true };
        headFig.Segments.Add(new LineSegment(to, true));
        headFig.Segments.Add(new LineSegment(p2, true));
        headFig.Segments.Add(new LineSegment(p1, true));

        var geo = new PathGeometry();
        geo.Figures.Add(fig);
        geo.Figures.Add(headFig);
        return geo;
    }

    /// <summary>创建一条带箭头的线段（从 from 到 to，箭头在 to 端）。</summary>
    private static Path CreateArrow(Point from, Point to, Brush stroke)
    {
        return new Path
        {
            Stroke = stroke,
            StrokeThickness = 1.5,
            Data = CreateArrowGeometry(from, to),
            Fill = stroke,
        };
    }

    /// <summary>端口值的简短描述（用于悬浮标签显示）。</summary>
    private static string DescribePortValue(object val) => val switch
    {
        VzImage img => $"Image {img.width}x{img.height}x{img.channels}",
        VzMatchResult[] arr => $"{arr.Length} 项匹配" +
            (arr.Length > 0 ? $" (分数={arr[0].score:F2})" : ""),
        VzPoint2D[] arr => $"{arr.Length} 个点",
        VzRect[] arr => $"{arr.Length} 个矩形",
        VzPose2D[] arr => $"{arr.Length} 个位姿",
        VzPose2D p => $"Pose({p.point.x:F1},{p.point.y:F1})∠{p.angle:F1}",
        double d => d.ToString("F3"),
        int i => i.ToString(),
        bool b => b ? "true" : "false",
        string s => s.Length > 30 ? s[..30] + "…" : s,
        _ => val.ToString() ?? "(null)",
    };

    /// <summary>用户点击节点上的"▶ 运行"按钮时触发（参数：被点击的 NodeControl）</summary>
    public event Action<NodeControl>? NodeRunRequested;

    /// <summary>用户双击节点本体时触发（参数：被双击的 NodeControl）</summary>
    public event Action<NodeControl>? NodeDoubleClicked;

    /// <summary>用户点击节点上的"?"帮助按钮时触发</summary>
    public event Action<NodeControl>? NodeHelpRequested;

    /// <summary>用户从节点库拖拽 NodeDescriptor 到画布时触发</summary>
    public event Action<Nodes.NodeDescriptor, Point>? NodeDropped;

    // ============================================================
    // 节点库拖拽接收
    // ============================================================
    private static readonly string NodeDescriptorDataFormat =
        $"VisionFlow:{typeof(Nodes.NodeDescriptor).FullName}";

    /// <summary>启动一次从节点库到画布的拖拽（由 MainWindow 调用）</summary>
    public static void BeginDragNodeDescriptor(System.Windows.DependencyObject source,
                                                Nodes.NodeDescriptor desc)
    {
        var data = new System.Windows.DataObject(NodeDescriptorDataFormat, desc);
        System.Windows.DragDrop.DoDragDrop(source, data, System.Windows.DragDropEffects.Copy);
    }

    private void OnCanvasDragOver(object sender, System.Windows.DragEventArgs e)
    {
        e.Effects = e.Data.GetDataPresent(NodeDescriptorDataFormat)
                    ? System.Windows.DragDropEffects.Copy
                    : System.Windows.DragDropEffects.None;
        e.Handled = true;
    }

    private void OnCanvasDrop(object sender, System.Windows.DragEventArgs e)
    {
        if (e.Data.GetData(NodeDescriptorDataFormat) is Nodes.NodeDescriptor desc)
        {
            var pos = ScreenToContent(e.GetPosition(this));
            NodeDropped?.Invoke(desc, pos);
        }
        e.Handled = true;
    }

    // ============================================================
    // 键盘：删除选中节点 / 连线
    // ============================================================
    private void OnKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Delete)
        {
            if (_selectedEdge is not null && Graph is not null)
            {
                Graph.RemoveEdge(_selectedEdge);
                _selectedEdge = null;
                e.Handled = true;
            }
            else if (SelectedNode is not null && Graph is not null)
            {
                Graph.RemoveNode(SelectedNode);
                SelectedNode = null;
                e.Handled = true;
            }
        }
    }

    protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
    {
        base.OnMouseLeftButtonDown(e);
        // 点空白取消选中 + 取消连线选中
        SelectedNode = null;
        if (_selectedEdge is not null && _edgePaths.TryGetValue(_selectedEdge, out var p))
            p.StrokeThickness = 2;
        _selectedEdge = null;
        Focus();
    }

    // ============================================================
    // 几何辅助
    // ============================================================
    /// <summary>计算端口圆点在画布坐标系的锚点。</summary>
    private Point GetPortAnchor(NodeControl ctrl, Port port)
    {
        ctrl.UpdateLayout();
        // 节点左上角的画布坐标
        double x = GetLeft(ctrl), y = GetTop(ctrl);
        // 端口圆点相对节点左上的偏移：MVP 简化为按 index 估算
        // 真实场景中应通过 VisualTreeHelper 找到 Ellipse 元素再 PointToScreen
        var ellipse = FindPortEllipse(ctrl, port);
        if (ellipse is not null)
        {
            // 转换到内容层逻辑坐标（连线在 ContentCanvas 中绘制）
            var p = ellipse.TransformToAncestor(ContentCanvas).Transform(new Point(0, 0));
            return new Point(p.X + ellipse.ActualWidth / 2,
                              p.Y + ellipse.ActualHeight / 2);
        }
        // 后备：节点中线
        return new Point(x + ctrl.ActualWidth / 2, y + ctrl.ActualHeight / 2);
    }

    /// <summary>
    /// 在半径 radius 内查找最近的、类型与 sourceType 兼容的输入端口。
    /// 用于连线拖拽时的吸附。
    /// </summary>
    private Port? FindNearestInputPort(Point mouse, double radius, PortType sourceType)
    {
        Port? best = null;
        double bestDist = radius;
        foreach (var (node, ctrl) in _nodeCtrls)
        {
            foreach (var port in node.Inputs)
            {
                // 强类型预校验：类型不匹配的端口不吸附
                if (!sourceType.CanConnectTo(port.Type)) continue;
                var anchor = GetPortAnchor(ctrl, port);
                var d = (anchor - mouse).Length;
                if (d < bestDist)
                {
                    bestDist = d;
                    best = port;
                }
            }
        }
        return best;
    }

    private static Ellipse? FindPortEllipse(NodeControl ctrl, Port port)
    {
        var inputList = FindVisualChild<ItemsControl>(ctrl);
        // 直接枚举所有 Ellipse，按 DataContext 匹配
        Ellipse? found = null;
        EnumVisualChildren(ctrl, el =>
        {
            if (el is Ellipse e && e.DataContext is Port p && p == port)
                found = e;
        });
        return found;
    }

    private static void EnumVisualChildren(DependencyObject d, Action<DependencyObject> action)
    {
        action(d);
        int n = VisualTreeHelper.GetChildrenCount(d);
        for (int i = 0; i < n; i++)
        {
            var child = VisualTreeHelper.GetChild(d, i);
            EnumVisualChildren(child, action);
        }
    }

    private static T? FindVisualChild<T>(DependencyObject d) where T : DependencyObject
    {
        int n = VisualTreeHelper.GetChildrenCount(d);
        for (int i = 0; i < n; i++)
        {
            var child = VisualTreeHelper.GetChild(d, i);
            if (child is T t) return t;
            var sub = FindVisualChild<T>(child);
            if (sub is not null) return sub;
        }
        return null;
    }

    /// <summary>命中测试：返回鼠标位置下的 Port（若有）。pt 为内容层逻辑坐标。</summary>
    private Port? VisualHitTest(Point pt)
    {
        var hitResult = VisualTreeHelper.HitTest(ContentCanvas, pt);
        if (hitResult is null) return null;
        var dep = hitResult.VisualHit;
        while (dep is not null)
        {
            if (dep is Ellipse e && e.DataContext is Port p) return p;
            dep = VisualTreeHelper.GetParent(dep);
        }
        return null;
    }

    private Geometry BuildEdgeGeometry(Edge e)
    {
        var fromCtrl = _nodeCtrls.GetValueOrDefault(e.From.Owner);
        var toCtrl = _nodeCtrls.GetValueOrDefault(e.To.Owner);
        if (fromCtrl is null || toCtrl is null) return Geometry.Empty;
        var s = GetPortAnchor(fromCtrl, e.From);
        var t = GetPortAnchor(toCtrl, e.To);
        return BuildBezier(s, t);
    }

    private static Geometry BuildBezier(Point s, Point t)
    {
        double dx = Math.Max(40, Math.Abs(t.X - s.X) * 0.5);
        var fig = new PathFigure { StartPoint = s, IsClosed = false };
        fig.Segments.Add(new BezierSegment(
            new Point(s.X + dx, s.Y),
            new Point(t.X - dx, t.Y),
            t, true));
        var geo = new PathGeometry();
        geo.Figures.Add(fig);
        return geo;
    }

    private static Brush ResolvePortBrush(string typeName)
    {
        var key = $"PortBrush.{typeName}";
        return Application.Current.TryFindResource(key) as Brush ?? Brushes.Gray;
    }
}
