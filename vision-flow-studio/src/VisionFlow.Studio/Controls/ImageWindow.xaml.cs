using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using VisionFlow.Studio.Execution;
using VisionFlow.Studio.Interop;
using VisionFlow.Studio.Models;
using VisionFlow.Studio.Nodes;
using VisionFlow.Studio;  // OverlayRenderer

namespace VisionFlow.Studio.Controls;

/// <summary>
/// 图像查看窗口：支持平移（鼠标拖拽）、缩放（滚轮）、
/// 右下角显示鼠标位置坐标+灰度值。
/// 所有 ROI 在本窗口的图像上直接绘制（不再弹窗）。
/// </summary>
public partial class ImageWindow : UserControl
{
    private WriteableBitmap? _bmp;

    // === 当前节点 & 图像尺寸 ===
    private Node? _activeNode;
    private double _imgPixelW, _imgPixelH;

    // 当前缩放比例：1.0 = 1:1 原始像素，>1 放大，<1 缩小
    private double _scale = 1.0;

    // === 平移状态 ===
    private bool _isPanning;
    private Point _panStartScreen;
    private Point _panStartTranslate;

    // === ROI 绘制状态机 ===
    private enum RoiDrawMode { None, Rect, RotatedRect, Circle }
    private RoiDrawMode _drawMode = RoiDrawMode.None;
    private string? _drawParamName;        // 当前正在绘制的参数名
    private Point _drawStart;              // 绘制起点（显示坐标）
    private Shape? _drawPreview;          // 绘制中的预览形状

    // === ROI 编辑（拖拽/缩放已有 ROI）状态 ===
    private class RoiEditState
    {
        public string ParamName = "";
        public string Kind = "rect";   // rect / rotated_rect / circle
        public Shape Shape = null!;
        public Point Origin;
        public double StartCx, StartCy, StartW, StartH, StartR, StartAngle;
        public string DragMode = "move";  // move / resize / rotate
    }
    private readonly List<RoiEditState> _roiStates = new();
    private RoiEditState? _activeRoi;
    private bool _roiDragging;
    private Point _roiDragOrigin;

    // 当前匹配结果（用于在 RoiCanvas 上叠加分数标签）
    private VzMatchResult[]? _currentMatches;

    // 当前用于矢量覆盖绘制的节点（检测标注来源）
    private Node? _overlayNode;

    // ROI 颜色调色板（循环分配给不同参数）
    private static readonly Brush[] RoiPalette =
    {
        Brushes.Magenta, Brushes.Cyan, Brushes.Yellow, Brushes.LimeGreen,
        Brushes.Orange, Brushes.DeepPink, Brushes.DodgerBlue,
    };

    public ImageWindow()
    {
        InitializeComponent();
        ImageView.SizeChanged += OnImageViewSizeChanged;
        // ImageArea 尺寸变化时重新适应（解决首次加载时 ActualWidth=0 的问题）
        ImageArea.SizeChanged += (s, e) => {
            if (_imgPixelW > 0 && _imgPixelH > 0)
                FitToWindow();
        };
        RoiCanvas.MouseLeftButtonDown += OnRoiCanvasMouseDown;
        RoiCanvas.MouseMove += OnRoiCanvasMouseMove;
        RoiCanvas.MouseLeftButtonUp += OnRoiCanvasMouseUp;
    }

    public Node? ActiveNode
    {
        get => _activeNode;
        set
        {
            _activeNode = value;
            UpdateToolbars();
            RenderRoiOverlays();
        }
    }

    /// <summary>更新工具栏可见性：依据 ActiveNode 的参数类型。</summary>
    private void UpdateToolbars()
    {
        bool hasRoi = false;
        bool isTemplate = false;
        if (_activeNode is not null)
        {
            var desc = NodeRegistry.Instance.Get(_activeNode.TypeId);
            if (desc is not null)
            {
                hasRoi = desc.Parameters.Any(p => p.Type == "Roi");
                isTemplate = _activeNode.TypeId == "TemplateMatch";
            }
        }
        RoiToolbarHost.Visibility = hasRoi ? Visibility.Visible : Visibility.Collapsed;
        TemplateToolbarHost.Visibility = isTemplate ? Visibility.Visible : Visibility.Collapsed;
    }

    // ============================================================
    // 显示图像
    // ============================================================
    public void ShowImage(ref VzImage img, string sourceLabel = "")
    {
        if (img.data == IntPtr.Zero || img.width <= 0 || img.height <= 0)
        {
            ImageInfoText.Text = $"(无效图像 from {sourceLabel})";
            Logger.Warn("ImageWindow", $"ShowImage 收到空图像（来源：{sourceLabel}）");
            return;
        }
        var bmp = VzImageConverter.ToBitmapSource(ref img);
        _bmp = bmp as WriteableBitmap;
        _currentMatches = null;
        ImageView.Source = bmp;
        _imgPixelW = img.width;
        _imgPixelH = img.height;
        ImageInfoText.Text = $"{img.width}×{img.height}×{img.channels} 来自 {sourceLabel}";
        Logger.Info("ImageWindow", $"显示图像：{img.width}x{img.height}x{img.channels} (来源：{sourceLabel})");
        FitToWindow();
        RenderRoiOverlays();
        RenderMatchLabels();
    }

    public void ShowNodeImage(Node node, ref VzImage img, string sourceLabel = "")
    {
        _activeNode = node;
        UpdateToolbars();
        ShowImage(ref img, sourceLabel);
    }

    /// <summary>
    /// 显示基础图像；节点的检测标注由 OverlayRenderer 作为 WPF 矢量层叠加到 RoiCanvas，
    /// 随图像缩放，不做全图重编码（流畅）。
    /// </summary>
    public void ShowImageWithOverlay(ref VzImage img, Node node, string sourceLabel = "")
    {
        if (img.data == IntPtr.Zero || img.width <= 0 || img.height <= 0)
        {
            ImageInfoText.Text = $"(无效图像 from {sourceLabel})";
            return;
        }
        var baseBmp = VzImageConverter.ToBitmapSource(ref img);
        _bmp = baseBmp as WriteableBitmap;
        _currentMatches = null;
        _overlayNode = node;
        ImageView.Source = baseBmp;
        _imgPixelW = img.width;
        _imgPixelH = img.height;
        ImageInfoText.Text = $"{img.width}×{img.height}×{img.channels} 来自 {sourceLabel} (矢量覆盖)";
        FitToWindow();
        RenderRoiOverlays();
        RenderMatchLabels();
    }

    public void ClearImage()
    {
        ImageView.Source = null;
        _bmp = null;
        _currentMatches = null;
        ImageInfoText.Text = "(无图像)";
        PixelInfoText.Text = "X=0, Y=0, Gray=—";
    }

    public void ShowImageWithMatches(ref VzImage img, VzMatchResult[] matches, string sourceLabel = "")
    {
        if (img.data == IntPtr.Zero || img.width <= 0 || img.height <= 0)
        {
            ImageInfoText.Text = $"(无效图像 from {sourceLabel})";
            return;
        }
        var bmp = VzImageConverter.ToBitmapSource(ref img) as WriteableBitmap;
        if (bmp is null) return;
        bmp = EnsureBgr(bmp);
        _bmp = bmp;
        _currentMatches = matches;
        if (matches is { Length: > 0 })
            DrawRectsOnBitmap(bmp, matches);
        ImageView.Source = bmp;
        _imgPixelW = img.width;
        _imgPixelH = img.height;
        ImageInfoText.Text = $"{img.width}×{img.height}×{img.channels} + {matches?.Length ?? 0} 匹配 来自 {sourceLabel}";
        Logger.Info("ImageWindow", $"显示带标注图像：{img.width}x{img.height}x{img.channels}，叠加 {matches?.Length ?? 0} 个匹配 (来源：{sourceLabel})");
        FitToWindow();
        RenderRoiOverlays();
        RenderMatchLabels();
    }

    public void ShowImageWithRects(ref VzImage img, VzRect[] rects, string sourceLabel = "")
    {
        if (img.data == IntPtr.Zero || img.width <= 0 || img.height <= 0)
        {
            ImageInfoText.Text = $"(无效图像 from {sourceLabel})";
            return;
        }
        var bmp = VzImageConverter.ToBitmapSource(ref img) as WriteableBitmap;
        if (bmp is null) return;
        bmp = EnsureBgr(bmp);
        _bmp = bmp;
        if (rects is { Length: > 0 })
        {
            var matches = new VzMatchResult[rects.Length];
            for (int i = 0; i < rects.Length; i++)
                matches[i] = new VzMatchResult { rect = rects[i], score = 0, template_id = 0 };
            DrawRectsOnBitmap(bmp, matches, rectColor: 0x00FFFF, crossColor: 0x00FFFF00);
        }
        ImageView.Source = bmp;
        _imgPixelW = img.width;
        _imgPixelH = img.height;
        ImageInfoText.Text = $"{img.width}×{img.height}×{img.channels} + {rects?.Length ?? 0} 区域 来自 {sourceLabel}";
        FitToWindow();
        RenderRoiOverlays();
    }

    /// <summary>灰度图(Gray8)转 Bgr24，使彩色标注（绿色框等）可见。</summary>
    private static unsafe WriteableBitmap EnsureBgr(WriteableBitmap src)
    {
        if (src.Format == PixelFormats.Bgr24 || src.Format == PixelFormats.Bgr32)
            return src;
        if (src.Format != PixelFormats.Gray8)
            return src;
        int w = src.PixelWidth, h = src.PixelHeight;
        var dst = new WriteableBitmap(w, h, 96, 96, PixelFormats.Bgr24, null);
        src.Lock();
        dst.Lock();
        try
        {
            byte* sPtr = (byte*)src.BackBuffer;
            byte* dPtr = (byte*)dst.BackBuffer;
            int sStride = src.BackBufferStride;
            int dStride = dst.BackBufferStride;
            for (int y = 0; y < h; y++)
            {
                byte* sRow = sPtr + y * sStride;
                byte* dRow = dPtr + y * dStride;
                for (int x = 0; x < w; x++)
                {
                    byte g = sRow[x];
                    dRow[x * 3]     = g;
                    dRow[x * 3 + 1] = g;
                    dRow[x * 3 + 2] = g;
                }
            }
            dst.AddDirtyRect(new Int32Rect(0, 0, w, h));
        }
        finally
        {
            src.Unlock();
            dst.Unlock();
        }
        return dst;
    }

    // ============================================================
    // 缩放与平移
    // ============================================================

    /// <summary>更新缩放：通过设置 ImageView.Width/Height = pixelW*scale 实现。
    /// ROI 形状用显示坐标（图像像素坐标 × scale），RoiCanvas 与 ImageView 同尺寸。</summary>
    private void UpdateImageSize()
    {
        if (_imgPixelW <= 0 || _imgPixelH <= 0) return;
        ImageView.Width = _imgPixelW * _scale;
        ImageView.Height = _imgPixelH * _scale;
        RoiCanvas.Width = _imgPixelW * _scale;
        RoiCanvas.Height = _imgPixelH * _scale;
    }

    /// <summary>适应窗口：计算缩放比例使图像完整显示在视口内，居中。</summary>
    private void FitToWindow()
    {
        if (_imgPixelW <= 0 || _imgPixelH <= 0) return;
        double vw = ImageArea.ActualWidth;
        double vh = ImageArea.ActualHeight;
        if (vw <= 0 || vh <= 0)
        {
            Logger.Warn("ImageWindow", $"FitToWindow 跳过：ImageArea 尺寸=({vw:F0}x{vh:F0})，图像=({_imgPixelW}x{_imgPixelH})");
            return;
        }
        double sx = vw / _imgPixelW;
        double sy = vh / _imgPixelH;
        _scale = Math.Min(sx, sy);
        if (_scale <= 0) _scale = 1.0;
        UpdateImageSize();
        ImgTranslate.X = (vw - _imgPixelW * _scale) / 2.0;
        ImgTranslate.Y = (vh - _imgPixelH * _scale) / 2.0;
        UpdateZoomText();
        Logger.Info("ImageWindow", $"FitToWindow: 图像={_imgPixelW}x{_imgPixelH}, 视口={vw:F0}x{vh:F0}, 缩放={_scale:F4}");
    }

    private void BtnFit_Click(object sender, RoutedEventArgs e) => FitToWindow();

    private void BtnActualSize_Click(object sender, RoutedEventArgs e)
    {
        _scale = 1.0;
        UpdateImageSize();
        double vw = ImageArea.ActualWidth;
        double vh = ImageArea.ActualHeight;
        ImgTranslate.X = Math.Max(0, (vw - _imgPixelW * _scale) / 2.0);
        ImgTranslate.Y = Math.Max(0, (vh - _imgPixelH * _scale) / 2.0);
        UpdateZoomText();
        RenderRoiOverlays();
        RenderMatchLabels();
        Logger.Info("ImageWindow", "切换到 1:1 模式");
    }

    private void BtnClearImage_Click(object sender, RoutedEventArgs e) => ClearImage();

    private void UpdateZoomText()
    {
        ZoomInfoText.Text = $"{_scale * 100:F0}%";
    }

    /// <summary>滚轮缩放：以鼠标位置为中心缩放。</summary>
    private void OnImageMouseWheel(object sender, MouseWheelEventArgs e)
    {
        if (_imgPixelW <= 0) return;
        var mousePos = e.GetPosition(ImageArea);
        double oldScale = _scale;
        double factor = e.Delta > 0 ? 1.15 : 1.0 / 1.15;
        double newScale = oldScale * factor;
        newScale = Math.Max(0.05, Math.Min(50.0, newScale));
        double imgX = (mousePos.X - ImgTranslate.X) / oldScale;
        double imgY = (mousePos.Y - ImgTranslate.Y) / oldScale;
        _scale = newScale;
        UpdateImageSize();
        ImgTranslate.X = mousePos.X - imgX * newScale;
        ImgTranslate.Y = mousePos.Y - imgY * newScale;
        UpdateZoomText();
        RenderRoiOverlays();
        RenderMatchLabels();
        e.Handled = true;
    }

    /// <summary>鼠标左键按下：若处于 ROI 绘制模式则由 RoiCanvas 处理；
    /// 若点中已有 ROI 形状也由其处理；否则开始平移。</summary>
    private void OnImageAreaLeftDown(object sender, MouseButtonEventArgs e)
    {
        // 处于绘制模式时不平移（RoiCanvas 会接管）
        if (_drawMode != RoiDrawMode.None) return;
        _isPanning = true;
        _panStartScreen = e.GetPosition(ImageArea);
        _panStartTranslate = new Point(ImgTranslate.X, ImgTranslate.Y);
        ImageArea.CaptureMouse();
        e.Handled = true;
    }

    private void OnImageAreaLeftUp(object sender, MouseButtonEventArgs e)
    {
        if (_isPanning)
        {
            _isPanning = false;
            ImageArea.ReleaseMouseCapture();
            e.Handled = true;
        }
    }

    /// <summary>鼠标移动：平移或显示像素信息。</summary>
    private void OnImageAreaMouseMove(object sender, MouseEventArgs e)
    {
        var pos = e.GetPosition(ImageArea);
        if (_isPanning)
        {
            ImgTranslate.X = _panStartTranslate.X + (pos.X - _panStartScreen.X);
            ImgTranslate.Y = _panStartTranslate.Y + (pos.Y - _panStartScreen.Y);
            e.Handled = true;
        }
        UpdatePixelInfo(pos);
    }

    private void OnImageAreaMouseLeave(object sender, MouseEventArgs e)
    {
        PixelInfoText.Text = "X=—, Y=—, Gray=—";
    }

    /// <summary>根据鼠标在视口中的位置，计算对应图像像素坐标和灰度值。</summary>
    private unsafe void UpdatePixelInfo(Point screenPos)
    {
        if (_bmp is null || _imgPixelW <= 0 || _imgPixelH <= 0)
        {
            PixelInfoText.Text = "X=—, Y=—, Gray=—";
            return;
        }
        double imgX = (screenPos.X - ImgTranslate.X) / _scale;
        double imgY = (screenPos.Y - ImgTranslate.Y) / _scale;
        int px = (int)Math.Round(imgX);
        int py = (int)Math.Round(imgY);
        if (px < 0 || px >= _imgPixelW || py < 0 || py >= _imgPixelH)
        {
            PixelInfoText.Text = $"X={imgX:F1}, Y={imgY:F1}, Gray=(越界)";
            return;
        }
        string grayStr;
        try
        {
            _bmp.Lock();
            byte* ptr = (byte*)_bmp.BackBuffer;
            int stride = _bmp.BackBufferStride;
            int bpp = _bmp.Format.BitsPerPixel / 8;
            byte* pixel = ptr + py * stride + px * bpp;
            if (bpp == 1)
                grayStr = $"Gray={*pixel}";
            else if (bpp == 3 || bpp == 4)
            {
                byte b = pixel[0], g = pixel[1], r = pixel[2];
                grayStr = $"B={b}, G={g}, R={r}";
            }
            else
                grayStr = "Gray=?";
        }
        finally
        {
            _bmp.Unlock();
        }
        PixelInfoText.Text = $"X={px}, Y={py}, {grayStr}";
    }

    private void OnImageViewSizeChanged(object sender, SizeChangedEventArgs e)
    {
        // RoiCanvas 与 ImageView 同尺寸（显示尺寸 = pixelW × scale）
        if (_imgPixelW > 0 && _imgPixelH > 0)
        {
            RoiCanvas.Width = _imgPixelW * _scale;
            RoiCanvas.Height = _imgPixelH * _scale;
        }
        RenderRoiOverlays();
    }

    // ============================================================
    // 在 WriteableBitmap 上画旋转矩形 + 中心点（结果标注，非 ROI）
    // ============================================================
    private static unsafe void DrawRectsOnBitmap(WriteableBitmap bmp, VzMatchResult[] matches,
                                                  uint rectColor = 0x00FF00, uint crossColor = 0x0000FF)
    {
        bmp.Lock();
        try
        {
            byte* ptr = (byte*)bmp.BackBuffer;
            int stride = bmp.BackBufferStride;
            int w = bmp.PixelWidth;
            int h = bmp.PixelHeight;
            int bpp = bmp.Format.BitsPerPixel / 8;
            foreach (var m in matches)
            {
                var r = m.rect;
                var corners = ComputeRotatedRectCorners(r.x, r.y, r.w, r.h, r.angle);
                for (int i = 0; i < 4; i++)
                {
                    var p1 = corners[i];
                    var p2 = corners[(i + 1) % 4];
                    DrawLine(ptr, stride, w, h, bpp, p1.x, p1.y, p2.x, p2.y, rectColor);
                }
                DrawCross(ptr, stride, w, h, bpp, r.x, r.y, halfLen: 10, crossColor);
            }
            bmp.AddDirtyRect(new Int32Rect(0, 0, w, h));
        }
        finally
        {
            bmp.Unlock();
        }
    }

    private static (double x, double y)[] ComputeRotatedRectCorners(
        double cx, double cy, double w, double h, double angleDeg)
    {
        double a = angleDeg * Math.PI / 180.0;
        double cos = Math.Cos(a), sin = Math.Sin(a);
        double hw = w / 2.0, hh = h / 2.0;
        double[][] rel = { new[] { -hw, -hh }, new[] { hw, -hh }, new[] { hw, hh }, new[] { -hw, hh } };
        var result = new (double, double)[4];
        for (int i = 0; i < 4; i++)
        {
            double x = rel[i][0], y = rel[i][1];
            result[i] = (cx + x * cos - y * sin, cy + x * sin + y * cos);
        }
        return result;
    }

    private static (double x, double y)[] RotatedRectDisplayCorners(
        double dcx, double dcy, double dw, double dh, double angleDeg)
    {
        double a = angleDeg * Math.PI / 180.0;
        double cos = Math.Cos(a), sin = Math.Sin(a);
        double hw = dw / 2.0, hh = dh / 2.0;
        double[][] rel = { new[] { -hw, -hh }, new[] { hw, -hh }, new[] { hw, hh }, new[] { -hw, hh } };
        var result = new (double, double)[4];
        for (int i = 0; i < 4; i++)
        {
            double x = rel[i][0], y = rel[i][1];
            result[i] = (dcx + x * cos - y * sin, dcy + x * sin + y * cos);
        }
        return result;
    }

    private static unsafe void DrawLine(byte* ptr, int stride, int w, int h, int bpp,
                                         double x0d, double y0d, double x1d, double y1d, uint color)
    {
        int x0 = (int)Math.Round(x0d), y0 = (int)Math.Round(y0d);
        int x1 = (int)Math.Round(x1d), y1 = (int)Math.Round(y1d);
        int dx = Math.Abs(x1 - x0), dy = Math.Abs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;
        byte b = (byte)(color & 0xFF);
        byte g = (byte)((color >> 8) & 0xFF);
        byte r = (byte)((color >> 16) & 0xFF);
        if (bpp == 1)
        {
            byte gray = (byte)((b + g + r) / 3);
            while (true)
            {
                if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h)
                    *(ptr + y0 * stride + x0 * 1) = gray;
                if (x0 == x1 && y0 == y1) break;
                int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x0 += sx; }
                if (e2 < dx) { err += dx; y0 += sy; }
            }
            return;
        }
        while (true)
        {
            if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h)
            {
                byte* p = ptr + y0 * stride + x0 * bpp;
                p[0] = b; p[1] = g; p[2] = r;
                if (bpp == 4) p[3] = 0xFF;
            }
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
    }

    private static unsafe void DrawCross(byte* ptr, int stride, int w, int h, int bpp,
                                          double cx, double cy, int halfLen, uint color)
    {
        DrawLine(ptr, stride, w, h, bpp, cx - halfLen, cy, cx + halfLen, cy, color);
        DrawLine(ptr, stride, w, h, bpp, cx, cy - halfLen, cx, cy + halfLen, color);
    }

    // ============================================================
    // ROI 覆盖层：通用化，从节点描述读取所有 Roi 参数
    // ============================================================
    public void RenderRoiOverlays()
    {
        RoiCanvas.Children.Clear();
        _roiStates.Clear();
        if (_activeNode is null || _imgPixelW <= 0) return;

        // RoiCanvas 使用显示尺寸（与 ImageView 同尺寸 = pixelW × scale）
        // ROI 形状用显示坐标（图像像素坐标 × scale）
        RoiCanvas.Width = _imgPixelW * _scale;
        RoiCanvas.Height = _imgPixelH * _scale;

        var node = _activeNode;
        var desc = NodeRegistry.Instance.Get(node.TypeId);
        if (desc is null) return;

        int colorIdx = 0;
        foreach (var p in desc.Parameters)
        {
            if (p.Type != "Roi") continue;
            var shape = p.GetRoiShape();
            var stroke = RoiPalette[colorIdx % RoiPalette.Length];
            colorIdx++;
            switch (shape)
            {
                case "rect":
                    AddRectRoi(node, p.Name, stroke);
                    break;
                case "rotated_rect":
                    AddRotatedRectRoi(node, p.Name, stroke);
                    break;
                case "circle":
                    AddCircleRoi(node, p.Name, stroke);
                    break;
            }
        }

        // 节点检测结果的矢量覆盖标注（随缩放刷新）
        RenderNodeOverlays();
    }

    /// <summary>把当前节点的检测结果作为矢量形状叠加到 RoiCanvas。</summary>
    private void RenderNodeOverlays()
    {
        if (_overlayNode is null || _imgPixelW <= 0) return;
        OverlayRenderer.RenderToCanvas(RoiCanvas, _overlayNode, _scale);
    }

    // ============================================================
    // 匹配分数标签
    // ============================================================
    private void RenderMatchLabels()
    {
        if (_currentMatches is null || _currentMatches.Length == 0) return;
        if (_imgPixelW <= 0) return;

        foreach (var m in _currentMatches)
        {
            var r = m.rect;
            // 坐标使用显示坐标（图像像素坐标 × scale）
            double dx = r.x * _scale;
            double dy = r.y * _scale;

            var label = new TextBlock
            {
                Text = $"{m.score:F3}",
                FontSize = 14,
                FontWeight = FontWeights.Bold,
                Foreground = Brushes.Yellow,
                Background = new SolidColorBrush(Color.FromArgb(180, 0, 0, 0)),
                Padding = new Thickness(3, 1, 3, 1),
                IsHitTestVisible = false,
            };
            Canvas.SetLeft(label, dx - r.w * _scale / 2.0);
            Canvas.SetTop(label, dy - r.h * _scale / 2.0 - 20);
            RoiCanvas.Children.Add(label);
        }
    }

    // ============================================================
    // 矩形 ROI（轴对齐 [x,y,w,h]，内部按 [cx,cy,w,h,0] 存储）
    // ============================================================
    private void AddRectRoi(Node node, string paramName, Brush stroke)
    {
        if (!node.Parameters.TryGetValue(paramName, out var pv) || pv is not string s || string.IsNullOrWhiteSpace(s))
            return;
        s = s.Trim();
        if (s == "\"\"" || s == "") return;
        var parts = ParseDoubleArray(s);
        double cx, cy, w, h;
        if (parts.Length >= 5) { cx = parts[0]; cy = parts[1]; w = parts[2]; h = parts[3]; }
        else if (parts.Length >= 4) { cx = parts[0] + parts[2] / 2.0; cy = parts[1] + parts[3] / 2.0; w = parts[2]; h = parts[3]; }
        else return;
        if (w <= 0 || h <= 0) return;

        // 显示坐标 = 图像像素坐标 × scale
        double dcx = cx * _scale, dcy = cy * _scale;
        double dw = w * _scale, dh = h * _scale;

        var rect = new Rectangle
        {
            Stroke = stroke,
            StrokeThickness = 1.5,
            StrokeDashArray = new DoubleCollection { 4, 2 },
            Fill = new SolidColorBrush(Color.FromArgb(40, 255, 255, 255)),
            Tag = paramName,
            Cursor = Cursors.SizeAll,
            ToolTip = $"{paramName}: [cx={cx:F1}, cy={cy:F1}, w={w:F1}, h={h:F1}]",
        };
        Canvas.SetLeft(rect, dcx - dw / 2.0);
        Canvas.SetTop(rect, dcy - dh / 2.0);
        rect.Width = dw;
        rect.Height = dh;
        RoiCanvas.Children.Add(rect);

        var state = new RoiEditState
        {
            ParamName = paramName,
            Kind = "rect",
            Shape = rect,
            StartCx = cx, StartCy = cy, StartW = w, StartH = h,
        };
        rect.MouseLeftButtonDown += (s, e) => OnRoiShapeMouseDown(state, e);
        _roiStates.Add(state);
        AddResizeHandle(state, stroke, dcx + dw / 2.0, dcy + dh / 2.0);
    }

    // ============================================================
    // 旋转矩形 ROI（[cx,cy,w,h,angle]）
    // ============================================================
    private void AddRotatedRectRoi(Node node, string paramName, Brush stroke)
    {
        if (!node.Parameters.TryGetValue(paramName, out var pv) || pv is not string s || string.IsNullOrWhiteSpace(s))
            return;
        s = s.Trim();
        if (s == "\"\"" || s == "") return;
        var parts = ParseDoubleArray(s);
        if (parts.Length < 5) return;
        double cx = parts[0], cy = parts[1], w = parts[2], h = parts[3], angle = parts[4];
        if (w <= 0 || h <= 0) return;

        // 显示坐标 = 图像像素坐标 × scale
        double dcx = cx * _scale, dcy = cy * _scale;
        double dw = w * _scale, dh = h * _scale;
        var corners = RotatedRectDisplayCorners(dcx, dcy, dw, dh, angle);

        var poly = new Polygon
        {
            Stroke = stroke,
            StrokeThickness = 1.5,
            StrokeDashArray = new DoubleCollection { 4, 2 },
            Fill = new SolidColorBrush(Color.FromArgb(40, 255, 255, 255)),
            Tag = paramName,
            Cursor = Cursors.SizeAll,
            ToolTip = $"{paramName}: [cx={cx:F1}, cy={cy:F1}, w={w:F1}, h={h:F1}, angle={angle:F2}]",
        };
        foreach (var (px, py) in corners)
            poly.Points.Add(new Point(px, py));
        RoiCanvas.Children.Add(poly);

        var state = new RoiEditState
        {
            ParamName = paramName,
            Kind = "rotated_rect",
            Shape = poly,
            StartCx = cx, StartCy = cy, StartW = w, StartH = h, StartAngle = angle,
        };
        poly.MouseLeftButtonDown += (s, e) => OnRoiShapeMouseDown(state, e);
        _roiStates.Add(state);

        // 旋转手柄：放在右上角顶点外侧
        var (rx, ry) = corners[1];
        AddRotateHandle(state, stroke, rx, ry);
        // 缩放手柄：放在右下角顶点外侧
        var (sx, sy) = corners[2];
        AddResizeHandle(state, stroke, sx, sy);
    }

    // ============================================================
    // 圆形 ROI（[cx,cy,r]）
    // ============================================================
    private void AddCircleRoi(Node node, string paramName, Brush stroke)
    {
        if (!node.Parameters.TryGetValue(paramName, out var pv) || pv is not string s || string.IsNullOrWhiteSpace(s))
            return;
        s = s.Trim();
        if (s == "\"\"" || s == "") return;
        var parts = ParseDoubleArray(s);
        if (parts.Length < 3) return;
        double cx = parts[0], cy = parts[1], r = parts[2];
        if (r <= 0) return;

        // 显示坐标 = 图像像素坐标 × scale
        double dcx = cx * _scale, dcy = cy * _scale, dr = r * _scale;

        var ell = new Ellipse
        {
            Stroke = stroke,
            StrokeThickness = 1.5,
            StrokeDashArray = new DoubleCollection { 4, 2 },
            Fill = new SolidColorBrush(Color.FromArgb(40, 255, 255, 255)),
            Tag = paramName,
            Cursor = Cursors.SizeAll,
            ToolTip = $"{paramName}: [cx={cx:F1}, cy={cy:F1}, r={r:F1}]",
        };
        Canvas.SetLeft(ell, dcx - dr);
        Canvas.SetTop(ell, dcy - dr);
        ell.Width = dr * 2;
        ell.Height = dr * 2;
        RoiCanvas.Children.Add(ell);

        var state = new RoiEditState
        {
            ParamName = paramName,
            Kind = "circle",
            Shape = ell,
            StartCx = cx, StartCy = cy, StartR = r, StartW = r * 2, StartH = r * 2,
        };
        ell.MouseLeftButtonDown += (s, e) => OnRoiShapeMouseDown(state, e);
        _roiStates.Add(state);
        AddResizeHandle(state, stroke, dcx + dr, dcy + dr);
    }

    // ============================================================
    // ROI 手柄
    // ============================================================
    private void AddResizeHandle(RoiEditState state, Brush stroke, double x, double y)
    {
        var handle = new Rectangle
        {
            Width = 8, Height = 8,
            Fill = stroke,
            Stroke = Brushes.Black,
            StrokeThickness = 0.5,
            Cursor = Cursors.SizeNWSE,
            Tag = ("resize_handle", state),
        };
        Canvas.SetLeft(handle, x - 4);
        Canvas.SetTop(handle, y - 4);
        handle.MouseLeftButtonDown += (s, e) =>
        {
            state.DragMode = "resize";
            OnRoiShapeMouseDown(state, e);
            e.Handled = true;
        };
        RoiCanvas.Children.Add(handle);
    }

    private void AddRotateHandle(RoiEditState state, Brush stroke, double x, double y)
    {
        var handle = new Ellipse
        {
            Width = 8, Height = 8,
            Fill = stroke,
            Stroke = Brushes.Black,
            StrokeThickness = 0.5,
            Cursor = Cursors.Hand,
            Tag = ("rotate_handle", state),
        };
        Canvas.SetLeft(handle, x - 4);
        Canvas.SetTop(handle, y - 4);
        handle.MouseLeftButtonDown += (s, e) =>
        {
            state.DragMode = "rotate";
            OnRoiShapeMouseDown(state, e);
            e.Handled = true;
        };
        RoiCanvas.Children.Add(handle);
    }

    private void SyncHandlePosition(RoiEditState st)
    {
        var children = RoiCanvas.Children.OfType<Shape>().ToList();
        foreach (var child in children)
        {
            if (child.Tag is System.ValueTuple<string, RoiEditState> t && t.Item2 == st)
            {
                double left, top;
                if (st.Kind == "rect" || st.Kind == "circle")
                {
                    left = Canvas.GetLeft(st.Shape) + st.Shape.Width;
                    top = Canvas.GetTop(st.Shape) + st.Shape.Height;
                }
                else // rotated_rect
                {
                    var poly = (Polygon)st.Shape;
                    if (poly.Points.Count >= 3)
                    {
                        if (t.Item1 == "resize_handle")
                        {
                            left = poly.Points[2].X;
                            top = poly.Points[2].Y;
                        }
                        else // rotate_handle
                        {
                            left = poly.Points[1].X;
                            top = poly.Points[1].Y;
                        }
                    }
                    else continue;
                }
                Canvas.SetLeft(child, left - 4);
                Canvas.SetTop(child, top - 4);
            }
        }
    }

    private static double[] ParseDoubleArray(string s)
    {
        var lst = new System.Collections.Generic.List<double>();
        var body = s.Trim().Trim('[', ']', '"', ' ', '\t');
        var tokens = body.Split(new[] { ',', ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
        foreach (var t in tokens)
        {
            if (double.TryParse(t, System.Globalization.NumberStyles.Any,
                System.Globalization.CultureInfo.InvariantCulture, out var v))
                lst.Add(v);
        }
        return lst.ToArray();
    }

    // ============================================================
    // ROI 绘制模式：进入 / 退出
    // ============================================================

    /// <summary>进入 ROI 绘制模式。由工具栏按钮或 NodeParameterDialog 触发。</summary>
    /// <param name="paramName">要写入的参数名</param>
    /// <param name="shape">rect / rotated_rect / circle</param>
    public void StartDrawMode(string paramName, string shape)
    {
        _drawParamName = paramName;
        _drawMode = shape switch
        {
            "circle" => RoiDrawMode.Circle,
            "rotated_rect" => RoiDrawMode.RotatedRect,
            _ => RoiDrawMode.Rect,
        };
        RoiCanvas.Cursor = Cursors.Cross;
        RoiModeText.Text = $"绘制中：{paramName} ({shape}) — 在图像上拖拽";
        Logger.Info("ImageWindow", $"进入 ROI 绘制模式：param={paramName}, shape={shape}");
    }

    /// <summary>退出绘制模式。</summary>
    private void ExitDrawMode()
    {
        _drawMode = RoiDrawMode.None;
        _drawParamName = null;
        _drawPreview = null;
        RoiCanvas.Cursor = null;
        RoiModeText.Text = "";
    }

    // ============================================================
    // 工具栏按钮
    // ============================================================
    private void BtnDrawRect_Click(object sender, RoutedEventArgs e) => RequestDraw("rect");
    private void BtnDrawRotRect_Click(object sender, RoutedEventArgs e) => RequestDraw("rotated_rect");
    private void BtnDrawCircle_Click(object sender, RoutedEventArgs e) => RequestDraw("circle");

    /// <summary>根据当前选中节点的 ROI 参数，找到第一个匹配 shape 的参数并进入绘制模式。</summary>
    private void RequestDraw(string shape)
    {
        if (_activeNode is null)
        {
            RoiModeText.Text = "请先选中一个节点";
            return;
        }
        var desc = NodeRegistry.Instance.Get(_activeNode.TypeId);
        if (desc is null) return;
        var param = desc.Parameters.FirstOrDefault(p => p.Type == "Roi" && p.GetRoiShape() == shape);
        if (param is null)
        {
            // 该节点没有此形状的 ROI 参数，找第一个 Roi 参数
            param = desc.Parameters.FirstOrDefault(p => p.Type == "Roi");
            if (param is null)
            {
                RoiModeText.Text = "该节点没有可绘制的 ROI 参数";
                return;
            }
        }
        StartDrawMode(param.Name, param.GetRoiShape());
    }

    private void BtnClearRoi_Click(object sender, RoutedEventArgs e)
    {
        if (_activeNode is null) return;
        var desc = NodeRegistry.Instance.Get(_activeNode.TypeId);
        if (desc is null) return;
        foreach (var p in desc.Parameters.Where(p => p.Type == "Roi"))
            _activeNode.Parameters[p.Name] = "";
        RenderRoiOverlays();
        RoiChanged?.Invoke();
        Logger.Info("ImageWindow", $"已清除节点 '{_activeNode.DisplayName}' 的所有 ROI 参数");
    }

    // ============================================================
    // 模板管理按钮（转发给 MainWindow 处理）
    // ============================================================
    private void BtnNewTemplate_Click(object sender, RoutedEventArgs e)
    {
        if (_activeNode is null || _activeNode.TypeId != "TemplateMatch")
        {
            RoiModeText.Text = "请先选中模板匹配节点";
            return;
        }
        // 打开独立的模板编辑器窗口
        NewTemplateEditorRequested?.Invoke(_activeNode);
    }

    private void BtnImportTemplate_Click(object sender, RoutedEventArgs e)
        => ImportTemplateRequested?.Invoke(_activeNode);

    private void BtnExportTemplate_Click(object sender, RoutedEventArgs e)
        => ExportTemplateRequested?.Invoke(_activeNode);

    /// <summary>模板导入请求（MainWindow 订阅）。</summary>
    public event Action<Node?>? ImportTemplateRequested;
    /// <summary>模板导出请求（MainWindow 订阅）。</summary>
    public event Action<Node?>? ExportTemplateRequested;

    // ============================================================
    // ROI 鼠标交互（绘制 + 拖拽编辑）
    // ============================================================
    private void OnRoiCanvasMouseDown(object sender, MouseButtonEventArgs e)
    {
        var pos = e.GetPosition(RoiCanvas);

        // --- 绘制模式 ---
        if (_drawMode != RoiDrawMode.None)
        {
            _drawStart = pos;
            _drawPreview = _drawMode switch
            {
                RoiDrawMode.Circle => (Shape)new Ellipse
                {
                    Stroke = Brushes.Orange,
                    StrokeThickness = 1.5,
                    StrokeDashArray = new DoubleCollection { 3, 2 },
                    Fill = new SolidColorBrush(Color.FromArgb(40, 255, 165, 0)),
                },
                _ => new Rectangle
                {
                    Stroke = Brushes.Orange,
                    StrokeThickness = 1.5,
                    StrokeDashArray = new DoubleCollection { 3, 2 },
                    Fill = new SolidColorBrush(Color.FromArgb(40, 255, 165, 0)),
                },
            };
            Canvas.SetLeft(_drawPreview, pos.X);
            Canvas.SetTop(_drawPreview, pos.Y);
            RoiCanvas.Children.Add(_drawPreview);
            RoiCanvas.CaptureMouse();
            e.Handled = true;
            return;
        }

        // --- 编辑模式：点空白取消选中 ---
        _activeRoi = null;
    }

    private void OnRoiCanvasMouseMove(object sender, MouseEventArgs e)
    {
        var pos = e.GetPosition(RoiCanvas);

        // --- 绘制中：更新预览 ---
        if (_drawMode != RoiDrawMode.None && _drawPreview is not null && _drawParamName is not null)
        {
            double x = Math.Min(_drawStart.X, pos.X);
            double y = Math.Min(_drawStart.Y, pos.Y);
            double w = Math.Abs(pos.X - _drawStart.X);
            double h = Math.Abs(pos.Y - _drawStart.Y);
            if (_drawMode == RoiDrawMode.Circle)
            {
                double r = Math.Min(w, h) / 2.0;
                Canvas.SetLeft(_drawPreview, _drawStart.X - r);
                Canvas.SetTop(_drawPreview, _drawStart.Y - r);
                _drawPreview.Width = r * 2;
                _drawPreview.Height = r * 2;
            }
            else
            {
                Canvas.SetLeft(_drawPreview, x);
                Canvas.SetTop(_drawPreview, y);
                _drawPreview.Width = w;
                _drawPreview.Height = h;
            }
            e.Handled = true;
            return;
        }

        // --- 拖拽编辑已有 ROI ---
        if (!_roiDragging || _activeRoi is null || _activeNode is null) return;
        // RoiCanvas 使用显示坐标，鼠标 delta 是显示 delta，需 /_scale 转换为图像像素 delta
        double sxInv = _scale > 0 ? 1.0 / _scale : 1.0;
        double dxPx = (pos.X - _roiDragOrigin.X) * sxInv;
        double dyPx = (pos.Y - _roiDragOrigin.Y) * sxInv;
        var st = _activeRoi;

        if (st.Kind == "rect")
        {
            if (st.DragMode == "move")
            {
                double ncx = st.StartCx + dxPx;
                double ncy = st.StartCy + dyPx;
                double w = st.StartW, h = st.StartH;
                Canvas.SetLeft(st.Shape, ncx * _scale - w * _scale / 2.0);
                Canvas.SetTop(st.Shape, ncy * _scale - h * _scale / 2.0);
                SyncHandlePosition(st);
                WriteRectParam(_activeNode, st.ParamName, ncx, ncy, w, h, 0);
            }
            else if (st.DragMode == "resize")
            {
                double nw = Math.Max(4, st.StartW + dxPx);
                double nh = Math.Max(4, st.StartH + dyPx);
                st.Shape.Width = nw * _scale;
                st.Shape.Height = nh * _scale;
                SyncHandlePosition(st);
                WriteRectParam(_activeNode, st.ParamName, st.StartCx, st.StartCy, nw, nh, 0);
            }
        }
        else if (st.Kind == "rotated_rect")
        {
            var poly = (Polygon)st.Shape;
            if (st.DragMode == "move")
            {
                double ncx = st.StartCx + dxPx;
                double ncy = st.StartCy + dyPx;
                var corners = RotatedRectDisplayCorners(ncx * _scale, ncy * _scale,
                    st.StartW * _scale, st.StartH * _scale, st.StartAngle);
                poly.Points.Clear();
                foreach (var (px, py) in corners)
                    poly.Points.Add(new Point(px, py));
                SyncHandlePosition(st);
                WriteRectParam(_activeNode, st.ParamName, ncx, ncy, st.StartW, st.StartH, st.StartAngle);
            }
            else if (st.DragMode == "resize")
            {
                double nw = Math.Max(4, st.StartW + dxPx);
                double nh = Math.Max(4, st.StartH + dyPx);
                var corners = RotatedRectDisplayCorners(st.StartCx * _scale, st.StartCy * _scale,
                    nw * _scale, nh * _scale, st.StartAngle);
                poly.Points.Clear();
                foreach (var (px, py) in corners)
                    poly.Points.Add(new Point(px, py));
                SyncHandlePosition(st);
                WriteRectParam(_activeNode, st.ParamName, st.StartCx, st.StartCy, nw, nh, st.StartAngle);
            }
            else if (st.DragMode == "rotate")
            {
                // 以中心为原点，鼠标角度 - 起始鼠标角度 = 旋转增量
                // 中心和鼠标位置都用显示坐标（缩放比例一致，角度计算结果相同）
                double cxDisp = st.StartCx * _scale;
                double cyDisp = st.StartCy * _scale;
                double curAngle = Math.Atan2(pos.Y - cyDisp, pos.X - cxDisp) * 180.0 / Math.PI;
                double startMouseAngle = Math.Atan2(_roiDragOrigin.Y - cyDisp, _roiDragOrigin.X - cxDisp) * 180.0 / Math.PI;
                double newAngle = st.StartAngle + (curAngle - startMouseAngle);
                var corners = RotatedRectDisplayCorners(cxDisp, cyDisp,
                    st.StartW * _scale, st.StartH * _scale, newAngle);
                poly.Points.Clear();
                foreach (var (px, py) in corners)
                    poly.Points.Add(new Point(px, py));
                SyncHandlePosition(st);
                WriteRectParam(_activeNode, st.ParamName, st.StartCx, st.StartCy, st.StartW, st.StartH, newAngle);
            }
        }
        else // circle
        {
            if (st.DragMode == "move")
            {
                double ncx = st.StartCx + dxPx;
                double ncy = st.StartCy + dyPx;
                double r = st.StartR;
                Canvas.SetLeft(st.Shape, ncx * _scale - r * _scale);
                Canvas.SetTop(st.Shape, ncy * _scale - r * _scale);
                SyncHandlePosition(st);
                WriteCircleParam(_activeNode, st.ParamName, ncx, ncy, r);
            }
            else if (st.DragMode == "resize")
            {
                double dr = (dxPx + dyPx) / 2.0;
                double nr = Math.Max(2, st.StartR + dr);
                st.Shape.Width = nr * 2 * _scale;
                st.Shape.Height = nr * 2 * _scale;
                double cx = st.StartCx, cy = st.StartCy;
                Canvas.SetLeft(st.Shape, cx * _scale - nr * _scale);
                Canvas.SetTop(st.Shape, cy * _scale - nr * _scale);
                SyncHandlePosition(st);
                WriteCircleParam(_activeNode, st.ParamName, cx, cy, nr);
            }
        }
    }

    private void OnRoiCanvasMouseUp(object sender, MouseButtonEventArgs e)
    {
        var pos = e.GetPosition(RoiCanvas);

        // --- 完成绘制 ---
        if (_drawMode != RoiDrawMode.None && _drawPreview is not null && _drawParamName is not null)
        {
            RoiCanvas.ReleaseMouseCapture();
            double x1 = Math.Min(_drawStart.X, pos.X);
            double y1 = Math.Min(_drawStart.Y, pos.Y);
            double w = Math.Abs(pos.X - _drawStart.X);
            double h = Math.Abs(pos.Y - _drawStart.Y);

            // RoiCanvas 使用显示坐标，需 /_scale 转换为图像像素坐标
            double sxInv = _scale > 0 ? 1.0 / _scale : 1.0;
            double pxStartX = _drawStart.X * sxInv;
            double pxStartY = _drawStart.Y * sxInv;
            double pxEndX = pos.X * sxInv;
            double pxEndY = pos.Y * sxInv;

            if (_drawParamName == "__new_template__")
            {
                // 模板新建：交给 MainWindow 处理裁剪
                double px = Math.Min(pxStartX, pxEndX);
                double py = Math.Min(pxStartY, pxEndY);
                double pw = Math.Abs(pxEndX - pxStartX);
                double ph = Math.Abs(pxEndY - pxStartY);
                ExitDrawMode();
                RenderRoiOverlays();
                NewTemplateDrawn?.Invoke(px, py, pw, ph);
                e.Handled = true;
                return;
            }

            if (_drawMode == RoiDrawMode.Circle)
            {
                double r = Math.Min(w, h) / 2.0 * sxInv;
                double cx = pxStartX;
                double cy = pxStartY;
                if (r > 1)
                {
                    WriteCircleParam(_activeNode!, _drawParamName, cx, cy, r);
                    Logger.Info("ImageWindow", $"绘制圆形 ROI：{_drawParamName} = [{cx:F1},{cy:F1},{r:F1}]");
                }
            }
            else if (_drawMode == RoiDrawMode.RotatedRect)
            {
                double cx = (pxStartX + pxEndX) / 2.0;
                double cy = (pxStartY + pxEndY) / 2.0;
                double rw = Math.Abs(pxEndX - pxStartX);
                double rh = Math.Abs(pxEndY - pxStartY);
                if (rw > 1 && rh > 1)
                {
                    WriteRectParam(_activeNode!, _drawParamName, cx, cy, rw, rh, 0);
                    Logger.Info("ImageWindow", $"绘制旋转矩形 ROI：{_drawParamName} = [{cx:F1},{cy:F1},{rw:F1},{rh:F1},0]");
                }
            }
            else // Rect
            {
                double cx = (pxStartX + pxEndX) / 2.0;
                double cy = (pxStartY + pxEndY) / 2.0;
                double rw = Math.Abs(pxEndX - pxStartX);
                double rh = Math.Abs(pxEndY - pxStartY);
                if (rw > 1 && rh > 1)
                {
                    WriteRectParam(_activeNode!, _drawParamName, cx, cy, rw, rh, 0);
                    Logger.Info("ImageWindow", $"绘制矩形 ROI：{_drawParamName} = [{cx:F1},{cy:F1},{rw:F1},{rh:F1},0]");
                }
            }
            ExitDrawMode();
            RenderRoiOverlays();
            RoiChanged?.Invoke();
            e.Handled = true;
            return;
        }

        // --- 完成拖拽编辑 ---
        if (_roiDragging)
        {
            _roiDragging = false;
            RoiCanvas.ReleaseMouseCapture();
            var st = _activeRoi;
            if (st is not null) st.DragMode = "move";
            _activeRoi = null;
            RoiChanged?.Invoke();
        }
    }

    /// <summary>已有 ROI 形状被按下：进入拖拽编辑。</summary>
    private void OnRoiShapeMouseDown(RoiEditState state, MouseButtonEventArgs e)
    {
        if (_activeNode is null) return;
        // 绘制模式下不响应已有形状的按下
        if (_drawMode != RoiDrawMode.None) return;
        _activeRoi = state;
        var pos = e.GetPosition(RoiCanvas);
        _roiDragOrigin = pos;
        var cur = ReadRoiParam(_activeNode, state.ParamName);
        state.StartCx = cur.cx;
        state.StartCy = cur.cy;
        if (state.Kind == "circle")
        {
            state.StartR = cur.r;
        }
        else
        {
            state.StartW = cur.w;
            state.StartH = cur.h;
            state.StartAngle = cur.angle;
        }
        if (state.DragMode != "resize" && state.DragMode != "rotate")
            state.DragMode = "move";
        _roiDragging = true;
        RoiCanvas.CaptureMouse();
        e.Handled = true;
    }

    public event Action? RoiChanged;

    /// <summary>模板新建：用户在图像上框选矩形后触发（MainWindow 订阅）。</summary>
    public event Action<double, double, double, double>? NewTemplateDrawn;
    public event Action<Node>? NewTemplateEditorRequested;

    // === 参数读写辅助 ===
    private static (double cx, double cy, double w, double h, double r, double angle) ReadRoiParam(Node node, string name)
    {
        if (node.Parameters.TryGetValue(name, out var pv) && pv is string s)
        {
            var parts = ParseDoubleArray(s);
            if (parts.Length >= 5) return (parts[0], parts[1], parts[2], parts[3], 0, parts[4]);
            if (parts.Length >= 4) return (parts[0] + parts[2] / 2.0, parts[1] + parts[3] / 2.0, parts[2], parts[3], 0, 0);
            if (parts.Length >= 3) return (parts[0], parts[1], 0, 0, parts[2], 0);
        }
        return (0, 0, 0, 0, 0, 0);
    }

    private static void WriteRectParam(Node node, string name, double cx, double cy, double w, double h, double angle)
    {
        var json = $"[{cx:F1},{cy:F1},{w:F1},{h:F1},{angle:F2}]";
        node.Parameters[name] = json;
    }

    private static void WriteCircleParam(Node node, string name, double cx, double cy, double r)
    {
        var json = $"[{cx:F1},{cy:F1},{r:F1}]";
        node.Parameters[name] = json;
    }
}
