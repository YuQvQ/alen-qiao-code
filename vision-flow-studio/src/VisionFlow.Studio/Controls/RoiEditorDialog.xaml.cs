using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using VisionFlow.Studio.Interop;

namespace VisionFlow.Studio.Controls;

/// <summary>
/// ROI 编辑对话框：支持旋转矩形的移动 / 旋转 / 缩放，以及视图缩放/平移。
///
/// 坐标体系：
///   - ROI 内部存储：原图像素坐标 (cx, cy, w, h, angle°)
///   - 显示坐标 = 图像坐标 * scale
///   - ImageView/RoiCanvas 尺寸 = 图像像素尺寸 * scale
///   - 通过 Canvas.SetLeft/Top 定位实现平移
/// </summary>
public partial class RoiEditorDialog : Window
{
    private BitmapSource? _source;
    private int _imageWidth, _imageHeight;

    // ROI 状态（原图像素坐标）
    private double _cx, _cy, _w, _h, _angle;
    private bool _hasRoi;

    // 交互状态
    private enum Action { None, Move, Rotate, ScaleCorner, ScaleEdge, Pan }
    private Action _action = Action.None;
    private Point _mouseStart;       // 图像坐标
    private double _startCx, _startCy, _startW, _startH, _startAngle;

    // 视图缩放与平移
    private double _viewScale = 1.0;
    private double _viewOffsetX, _viewOffsetY;
    private Point _panStart;
    private double _panStartOffsetX, _panStartOffsetY;

    private int _handleIndex = -1;
    private const double HandleRadius = 6;

    private Polygon? _poly;
    private readonly List<Ellipse> _handles = new();
    private Line? _rotLine;
    private Ellipse? _rotHandle;

    public (double cx, double cy, double w, double h, double angle) RoiResult { get; private set; }
    public bool HasResult { get; private set; }

    public RoiEditorDialog()
    {
        InitializeComponent();
        ImageGrid.SizeChanged += (s, e) => Redraw();
    }

    public RoiEditorDialog(ref VzImage img) : this()
    {
        _source = VzImageConverter.ToBitmapSource(ref img);
        if (_source is null)
        {
            HintText.Text = "图像数据为空，无法框选";
            return;
        }
        _imageWidth = _source.PixelWidth;
        _imageHeight = _source.PixelHeight;
        ImageView.Source = _source;
        HintText.Text = $"图像 {_imageWidth}×{_imageHeight}：拖拽创建 ROI，滚轮缩放，中键平移，右键重置";
        Loaded += (s, e) => Redraw();
    }

    // ============================================================
    // 视图变换：scale = 适应窗口比例 * 用户缩放因子
    // ============================================================
    private (double scale, double offsetX, double offsetY) GetTransform()
    {
        double availW = ImageGrid.ActualWidth;
        double availH = ImageGrid.ActualHeight;
        if (availW <= 0 || availH <= 0) return (1.0, 0, 0);
        double baseScale = Math.Min(availW / _imageWidth, availH / _imageHeight);
        double scale = baseScale * _viewScale;
        double dispW = _imageWidth * scale;
        double dispH = _imageHeight * scale;
        double offsetX = (availW - dispW) / 2 + _viewOffsetX;
        double offsetY = (availH - dispH) / 2 + _viewOffsetY;
        return (scale, offsetX, offsetY);
    }

    // 屏幕坐标 → 图像坐标
    private Point ScreenToImage(Point screen)
    {
        var (s, ox, oy) = GetTransform();
        if (s <= 0) return screen;
        return new Point((screen.X - ox) / s, (screen.Y - oy) / s);
    }

    // 图像坐标 → 显示坐标（相对于 RoiCanvas 左上角）
    private Point ImageToDisplay(double ix, double iy)
    {
        var (s, _, _) = GetTransform();
        return new Point(ix * s, iy * s);
    }

    // ============================================================
    // 旋转矩形顶点（图像坐标）
    // ============================================================
    private Point[] GetCorners()
    {
        double a = _angle * Math.PI / 180.0;
        double cos = Math.Cos(a), sin = Math.Sin(a);
        double hw = _w / 2, hh = _h / 2;
        double[][] rel = { new[] { -hw, -hh }, new[] { hw, -hh }, new[] { hw, hh }, new[] { -hw, hh } };
        var pts = new Point[4];
        for (int i = 0; i < 4; i++)
        {
            pts[i] = new Point(
                _cx + rel[i][0] * cos - rel[i][1] * sin,
                _cy + rel[i][0] * sin + rel[i][1] * cos);
        }
        return pts;
    }

    private Point[] GetEdgeMids()
    {
        var c = GetCorners();
        return new[]
        {
            new Point((c[0].X + c[1].X) / 2, (c[0].Y + c[1].Y) / 2),
            new Point((c[1].X + c[2].X) / 2, (c[1].Y + c[2].Y) / 2),
            new Point((c[2].X + c[3].X) / 2, (c[2].Y + c[3].Y) / 2),
            new Point((c[3].X + c[0].X) / 2, (c[3].Y + c[0].Y) / 2),
        };
    }

    private Point GetRotHandle()
    {
        var up = GetEdgeMids()[0];
        double a = _angle * Math.PI / 180.0;
        double len = Math.Max(_w, _h) * 0.3;
        return new Point(up.X - Math.Sin(a) * len, up.Y - Math.Cos(a) * len);
    }

    // ============================================================
    // 重绘：设置 ImageView/RoiCanvas 尺寸和位置，绘制 ROI 手柄
    // ============================================================
    private void Redraw()
    {
        if (_source is null) return;
        var (s, ox, oy) = GetTransform();

        // ImageView 和 RoiCanvas 尺寸 = 显示尺寸，位置 = offset
        double dispW = _imageWidth * s;
        double dispH = _imageHeight * s;
        ImageView.Width = dispW;
        ImageView.Height = dispH;
        RoiCanvas.Width = dispW;
        RoiCanvas.Height = dispH;
        Canvas.SetLeft(ImageView, ox);
        Canvas.SetTop(ImageView, oy);
        Canvas.SetLeft(RoiCanvas, ox);
        Canvas.SetTop(RoiCanvas, oy);

        RoiCanvas.Children.Clear();
        _handles.Clear();
        _poly = null;
        _rotLine = null;
        _rotHandle = null;
        if (!_hasRoi) return;

        var corners = GetCorners();
        // ROI 手柄用显示坐标
        var dispCorners = corners.Select(c => ImageToDisplay(c.X, c.Y)).ToArray();

        _poly = new Polygon
        {
            Stroke = (Brush)Application.Current.FindResource("BrandBrush"),
            StrokeThickness = 2,
            Fill = new SolidColorBrush(Color.FromArgb(50, 124, 58, 237)),
            Points = new PointCollection(dispCorners)
        };
        RoiCanvas.Children.Add(_poly);

        for (int i = 0; i < 4; i++)
        {
            var h = MakeHandle(dispCorners[i]);
            _handles.Add(h);
            RoiCanvas.Children.Add(h);
        }
        var edges = GetEdgeMids();
        for (int i = 0; i < 4; i++)
        {
            var h = MakeHandle(ImageToDisplay(edges[i].X, edges[i].Y));
            h.Fill = Brushes.White;
            _handles.Add(h);
            RoiCanvas.Children.Add(h);
        }
        var rot = GetRotHandle();
        var rotDisp = ImageToDisplay(rot.X, rot.Y);
        var centerDisp = ImageToDisplay(_cx, _cy);
        _rotLine = new Line
        {
            X1 = centerDisp.X, Y1 = centerDisp.Y,
            X2 = rotDisp.X, Y2 = rotDisp.Y,
            Stroke = (Brush)Application.Current.FindResource("BrandBrush"),
            StrokeThickness = 1
        };
        RoiCanvas.Children.Add(_rotLine);
        _rotHandle = MakeHandle(rotDisp);
        _rotHandle.Fill = Brushes.OrangeRed;
        _handles.Add(_rotHandle);
        RoiCanvas.Children.Add(_rotHandle);
    }

    private Ellipse MakeHandle(Point displayPt)
    {
        var ell = new Ellipse
        {
            Width = HandleRadius * 2,
            Height = HandleRadius * 2,
            Fill = (Brush)Application.Current.FindResource("BrandBrush"),
            Stroke = Brushes.White,
            StrokeThickness = 1,
            Cursor = Cursors.Hand
        };
        Canvas.SetLeft(ell, displayPt.X - HandleRadius);
        Canvas.SetTop(ell, displayPt.Y - HandleRadius);
        return ell;
    }

    // ============================================================
    // 命中测试：imgPt 为图像坐标
    // ============================================================
    private int HitTest(Point imgPt)
    {
        if (!_hasRoi) return -1;

        var rot = GetRotHandle();
        if (Distance(imgPt, rot) <= (HandleRadius + 2) / GetTransform().scale) return 8;

        var corners = GetCorners();
        double hitR = (HandleRadius + 2) / GetTransform().scale;
        for (int i = 0; i < 4; i++)
            if (Distance(imgPt, corners[i]) <= hitR) return i;

        var edges = GetEdgeMids();
        for (int i = 0; i < 4; i++)
            if (Distance(imgPt, edges[i]) <= hitR) return 4 + i;

        double a = _angle * Math.PI / 180.0;
        double cos = Math.Cos(-a), sin = Math.Sin(-a);
        double lx = (imgPt.X - _cx) * cos - (imgPt.Y - _cy) * sin;
        double ly = (imgPt.X - _cx) * sin + (imgPt.Y - _cy) * cos;
        if (Math.Abs(lx) <= _w / 2 && Math.Abs(ly) <= _h / 2) return -2;
        return -1;
    }

    private static double Distance(Point a, Point b) =>
        Math.Sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y));

    // ============================================================
    // 鼠标事件
    // ============================================================
    private void OnMouseDown(object sender, MouseButtonEventArgs e)
    {
        if (_source is null) return;

        if (e.ChangedButton == MouseButton.Middle)
        {
            _action = Action.Pan;
            _panStart = e.GetPosition(ImageGrid);
            _panStartOffsetX = _viewOffsetX;
            _panStartOffsetY = _viewOffsetY;
            ImageGrid.CaptureMouse();
            e.Handled = true;
            return;
        }

        if (e.ChangedButton == MouseButton.Right)
        {
            _viewScale = 1.0;
            _viewOffsetX = 0;
            _viewOffsetY = 0;
            Redraw();
            e.Handled = true;
            return;
        }

        // 鼠标在 ImageGrid 上的屏幕坐标 → 图像坐标
        var screenPos = e.GetPosition(ImageGrid);
        var imgPos = ScreenToImage(screenPos);
        int hit = HitTest(imgPos);

        if (hit == -1)
        {
            _action = Action.ScaleCorner;
            _mouseStart = imgPos;
            _cx = imgPos.X; _cy = imgPos.Y; _w = 1; _h = 1; _angle = 0;
            _hasRoi = true;
            _handleIndex = 2;
            _startCx = _cx; _startCy = _cy; _startW = _w; _startH = _h; _startAngle = _angle;
            Redraw();
            RoiCanvas.CaptureMouse();
            e.Handled = true;
            return;
        }

        if (hit == -2) _action = Action.Move;
        else if (hit == 8) _action = Action.Rotate;
        else if (hit < 4) { _action = Action.ScaleCorner; _handleIndex = hit; }
        else { _action = Action.ScaleEdge; _handleIndex = hit - 4; }

        _mouseStart = imgPos;
        _startCx = _cx; _startCy = _cy; _startW = _w; _startH = _h; _startAngle = _angle;
        RoiCanvas.CaptureMouse();
        e.Handled = true;
    }

    private void OnMouseMove(object sender, MouseEventArgs e)
    {
        if (_source is null) return;

        if (_action == Action.Pan)
        {
            var panPos = e.GetPosition(ImageGrid);
            _viewOffsetX = _panStartOffsetX + (panPos.X - _panStart.X);
            _viewOffsetY = _panStartOffsetY + (panPos.Y - _panStart.Y);
            Redraw();
            e.Handled = true;
            return;
        }

        var screenPos = e.GetPosition(ImageGrid);
        var imgPos = ScreenToImage(screenPos);

        if (_action == Action.None)
        {
            int hit = HitTest(imgPos);
            Cursor = hit switch
            {
                -2 => Cursors.SizeAll,
                8 => Cursors.Hand,
                0 or 2 => Cursors.SizeNWSE,
                1 or 3 => Cursors.SizeNESW,
                4 or 6 => Cursors.SizeNS,
                5 or 7 => Cursors.SizeWE,
                _ => Cursors.Cross
            };
            return;
        }

        double dxImg = imgPos.X - _mouseStart.X;
        double dyImg = imgPos.Y - _mouseStart.Y;

        switch (_action)
        {
            case Action.Move:
                _cx = _startCx + dxImg;
                _cy = _startCy + dyImg;
                break;

            case Action.Rotate:
            {
                double a0 = Math.Atan2(_mouseStart.Y - _startCy, _mouseStart.X - _startCx);
                double a1 = Math.Atan2(imgPos.Y - _cy, imgPos.X - _cx);
                _angle = _startAngle + (a1 - a0) * 180.0 / Math.PI;
                break;
            }

            case Action.ScaleCorner:
            {
                int opp = (_handleIndex + 2) % 4;
                var oppCorner = GetCornerAtStart(opp);
                double a = _startAngle * Math.PI / 180.0;
                double cos = Math.Cos(-a), sin = Math.Sin(-a);
                double mx = (imgPos.X - oppCorner.x) * cos - (imgPos.Y - oppCorner.y) * sin;
                double my = (imgPos.X - oppCorner.x) * sin + (imgPos.Y - oppCorner.y) * cos;
                _w = Math.Abs(mx);
                _h = Math.Abs(my);
                _cx = oppCorner.x + mx / 2;
                _cy = oppCorner.y + my / 2;
                break;
            }

            case Action.ScaleEdge:
            {
                int oppEdge = (_handleIndex + 2) % 4;
                var oppMid = GetEdgeMidAtStart(oppEdge);
                double a = _startAngle * Math.PI / 180.0;
                double cos = Math.Cos(-a), sin = Math.Sin(-a);
                double mx = (imgPos.X - oppMid.x) * cos - (imgPos.Y - oppMid.y) * sin;
                double my = (imgPos.X - oppMid.x) * sin + (imgPos.Y - oppMid.y) * cos;
                if (_handleIndex == 0 || _handleIndex == 2) { _h = Math.Abs(my); _cy = oppMid.y + my / 2; }
                else { _w = Math.Abs(mx); _cx = oppMid.x + mx / 2; }
                break;
            }
        }

        _w = Math.Max(4, _w);
        _h = Math.Max(4, _h);
        Redraw();
        HintText.Text = $"ROI：中心=({_cx:F0},{_cy:F0}) 尺寸={_w:F0}×{_h:F0} 角度={_angle:F1}°";
    }

    private (double x, double y) GetCornerAtStart(int idx)
    {
        double a = _startAngle * Math.PI / 180.0;
        double cos = Math.Cos(a), sin = Math.Sin(a);
        double hw = _startW / 2, hh = _startH / 2;
        double[][] rel = { new[] { -hw, -hh }, new[] { hw, -hh }, new[] { hw, hh }, new[] { -hw, hh } };
        return (_startCx + rel[idx][0] * cos - rel[idx][1] * sin,
                _startCy + rel[idx][0] * sin + rel[idx][1] * cos);
    }

    private (double x, double y) GetEdgeMidAtStart(int idx)
    {
        var c0 = GetCornerAtStart(idx);
        var c1 = GetCornerAtStart((idx + 1) % 4);
        return ((c0.x + c1.x) / 2, (c0.y + c1.y) / 2);
    }

    private void OnMouseUp(object sender, MouseButtonEventArgs e)
    {
        if (_action == Action.Pan)
        {
            _action = Action.None;
            ImageGrid.ReleaseMouseCapture();
            Cursor = Cursors.Arrow;
            return;
        }
        if (_action == Action.None) return;
        _action = Action.None;
        RoiCanvas.ReleaseMouseCapture();
        Cursor = Cursors.Arrow;

        if (_hasRoi && _w >= 4 && _h >= 4)
        {
            BtnOk.IsEnabled = true;
            HintText.Text = $"ROI 就绪：中心=({_cx:F0},{_cy:F0}) 尺寸={_w:F0}×{_h:F0} 角度={_angle:F1}°";
        }
        else
        {
            BtnOk.IsEnabled = false;
        }
    }

    // ============================================================
    // 滚轮缩放（以鼠标位置为中心）
    // ============================================================
    private void OnMouseWheel(object sender, MouseWheelEventArgs e)
    {
        if (_source is null) return;
        var mousePos = e.GetPosition(ImageGrid);
        var (s, ox, oy) = GetTransform();

        // 鼠标位置对应的图像坐标
        double imgX = (mousePos.X - ox) / s;
        double imgY = (mousePos.Y - oy) / s;

        double oldScale = _viewScale;
        double factor = e.Delta > 0 ? 1.15 : 1.0 / 1.15;
        double newScale = Math.Max(0.1, Math.Min(20.0, oldScale * factor));
        _viewScale = newScale;

        // 重新计算偏移，使鼠标下的图像点保持不动
        var (newS, _, _) = GetTransform();
        double availW = ImageGrid.ActualWidth;
        double availH = ImageGrid.ActualHeight;
        double newDispW = _imageWidth * newS;
        double newDispH = _imageHeight * newS;
        _viewOffsetX = mousePos.X - imgX * newS - (availW - newDispW) / 2;
        _viewOffsetY = mousePos.Y - imgY * newS - (availH - newDispH) / 2;

        Redraw();
        e.Handled = true;
    }

    // ============================================================
    // 按钮
    // ============================================================
    private void BtnOk_Click(object sender, RoutedEventArgs e)
    {
        if (!_hasRoi) return;
        RoiResult = (_cx, _cy, _w, _h, _angle);
        HasResult = true;
        DialogResult = true;
        Close();
    }

    private void BtnCancel_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
        Close();
    }
}
