using System;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using VisionFlow.Studio.Interop;

namespace VisionFlow.Studio.Templates;

public partial class TemplateEditorWindow : Window
{
    private VzImage _srcImage;
    private BitmapSource? _srcBitmap;
    private WriteableBitmap? _maskBitmap;
    private WriteableBitmap? _maskOverlay;

    private double _roiX, _roiY, _roiW, _roiH;
    private bool _hasRoi;

    private enum Tool { Roi, RectMask, CircleMask, Brush, Eraser }
    private Tool _tool = Tool.Roi;
    private bool _drawing;
    private Point _startPt;
    private Shape? _currentShape;
    private System.Windows.Point? _lastBrushPt;

    public VzImage ResultImage { get; private set; }
    public VzImage ResultMask { get; private set; }
    public double[]? ResultRoi { get; private set; }

    private double _zoom = 1.0;

    public TemplateEditorWindow(VzImage srcImage)
    {
        InitializeComponent();
        _srcImage = srcImage;
        Loaded += OnLoaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        _srcBitmap = VzImageConverter.ToBitmapSource(ref _srcImage);
        if (_srcBitmap == null) { MessageBox.Show("无法加载图像"); return; }

        _maskBitmap = new WriteableBitmap(_srcBitmap.PixelWidth, _srcBitmap.PixelHeight,
            96, 96, PixelFormats.Gray8, null);
        _maskBitmap.Lock();
        unsafe {
            byte* p = (byte*)_maskBitmap.BackBuffer;
            for (int i = 0; i < _srcBitmap.PixelWidth * _srcBitmap.PixelHeight; i++) p[i] = 255;
        }
        _maskBitmap.AddDirtyRect(new Int32Rect(0, 0, _maskBitmap.PixelWidth, _maskBitmap.PixelHeight));
        _maskBitmap.Unlock();

        UpdateMaskOverlay();
        ShowOriginal();
        UpdateImageSize();
    }

    private void ShowOriginal() { ImgDisplay.Source = _srcBitmap; UpdateImageSize(); }

    private void ShowGrayscale()
    {
        if (_srcBitmap == null) return;
        ImgDisplay.Source = ConvertToGrayscale(_srcBitmap);
        UpdateImageSize();
    }

    private void ShowBinary(int threshold)
    {
        if (_srcBitmap == null) return;
        ImgDisplay.Source = Threshold(ConvertToGrayscale(_srcBitmap), threshold);
        UpdateImageSize();
    }

    private void UpdateImageSize()
    {
        if (_srcBitmap == null) return;
        double w = _srcBitmap.PixelWidth * _zoom, h = _srcBitmap.PixelHeight * _zoom;
        ImgDisplay.Width = w; ImgDisplay.Height = h;
        Overlay.Width = w; Overlay.Height = h;
        MaskOverlay.Width = w; MaskOverlay.Height = h;
        ImageHost.Width = w; ImageHost.Height = h;
    }

    private static WriteableBitmap ConvertToGrayscale(BitmapSource src)
    {
        int w = src.PixelWidth, h = src.PixelHeight;
        var wb = new WriteableBitmap(w, h, 96, 96, PixelFormats.Gray8, null);
        int stride = w;
        byte[] pixels = new byte[stride * h];
        if (src.Format == PixelFormats.Gray8) { src.CopyPixels(pixels, stride, 0); }
        else
        {
            int bpp = src.Format.BitsPerPixel / 8;
            byte[] sp = new byte[w * h * bpp];
            src.CopyPixels(sp, w * bpp, 0);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                {
                    int idx = (y * w + x) * bpp;
                    pixels[y * stride + x] = (byte)(sp[idx + 2] * 0.299 + sp[idx + 1] * 0.587 + sp[idx] * 0.114);
                }
        }
        wb.Lock();
        wb.WritePixels(new Int32Rect(0, 0, w, h), pixels, stride, 0);
        wb.Unlock();
        return wb;
    }

    private static WriteableBitmap Threshold(WriteableBitmap gray, int thresh)
    {
        int w = gray.PixelWidth, h = gray.PixelHeight;
        byte[] pixels = new byte[w * h];
        gray.CopyPixels(pixels, w, 0);
        for (int i = 0; i < pixels.Length; i++) pixels[i] = pixels[i] >= thresh ? (byte)255 : (byte)0;
        var wb = new WriteableBitmap(w, h, 96, 96, PixelFormats.Gray8, null);
        wb.Lock();
        wb.WritePixels(new Int32Rect(0, 0, w, h), pixels, w, 0);
        wb.Unlock();
        return wb;
    }

    private void OnModeChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!IsLoaded) return;
        int idx = ModeCombo.SelectedIndex;
        if (idx == 0) { ShowOriginal(); ThreshSlider.Visibility = Visibility.Collapsed; ThreshLabel.Visibility = Visibility.Collapsed; }
        else if (idx == 1) { ShowGrayscale(); ThreshSlider.Visibility = Visibility.Collapsed; ThreshLabel.Visibility = Visibility.Collapsed; }
        else if (idx == 2) { ShowBinary((int)ThreshSlider.Value); ThreshSlider.Visibility = Visibility.Visible; ThreshLabel.Visibility = Visibility.Visible; }
    }

    private void OnThreshChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (!IsLoaded) return;
        ThreshLabel.Text = $"阈值：{(int)ThreshSlider.Value}";
        if (ModeCombo.SelectedIndex == 2) ShowBinary((int)ThreshSlider.Value);
    }

    private void OnZoomChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (!IsLoaded) return;
        _zoom = ZoomSlider.Value;
        ZoomLabel.Text = $"{(int)(_zoom * 100)}%";
        UpdateImageSize();
    }

    private void SetActiveButton(Button active)
    {
        foreach (var btn in new[] { BtnRoi, BtnRectMask, BtnCircleMask, BtnBrush, BtnEraser })
            btn.Style = (Style)FindResource("ToolBtn");
        active.Style = (Style)FindResource("ToolBtnActive");
    }

    private void OnToolRoi(object sender, RoutedEventArgs e) { _tool = Tool.Roi; SetActiveButton(BtnRoi); InfoText.Text = "拖动绘制模板 ROI 矩形"; Overlay.Cursor = Cursors.Cross; }
    private void OnToolRectMask(object sender, RoutedEventArgs e) { _tool = Tool.RectMask; SetActiveButton(BtnRectMask); InfoText.Text = "拖动绘制排除矩形（掩膜）"; Overlay.Cursor = Cursors.Cross; }
    private void OnToolCircleMask(object sender, RoutedEventArgs e) { _tool = Tool.CircleMask; SetActiveButton(BtnCircleMask); InfoText.Text = "拖动绘制排除圆形（掩膜）"; Overlay.Cursor = Cursors.Cross; }
    private void OnToolBrush(object sender, RoutedEventArgs e) { _tool = Tool.Brush; SetActiveButton(BtnBrush); InfoText.Text = "按住拖动绘制排除区域"; Overlay.Cursor = Cursors.Pen; }
    private void OnToolEraser(object sender, RoutedEventArgs e) { _tool = Tool.Eraser; SetActiveButton(BtnEraser); InfoText.Text = "按住拖动擦除掩膜（恢复有效）"; Overlay.Cursor = Cursors.No; }

    private void OnClearMask(object sender, RoutedEventArgs e)
    {
        if (_maskBitmap == null) return;
        _maskBitmap.Lock();
        unsafe { byte* p = (byte*)_maskBitmap.BackBuffer; for (int i = 0; i < _maskBitmap.PixelWidth * _maskBitmap.PixelHeight; i++) p[i] = 255; }
        _maskBitmap.AddDirtyRect(new Int32Rect(0, 0, _maskBitmap.PixelWidth, _maskBitmap.PixelHeight));
        _maskBitmap.Unlock();
        UpdateMaskOverlay();
        InfoText.Text = "掩膜已清除";
    }

    private void OnClearAll(object sender, RoutedEventArgs e)
    {
        OnClearMask(sender, e);
        _hasRoi = false;
        Overlay.Children.Clear();
        RoiInfo.Text = "";
        InfoText.Text = "已全部清除";
    }

    private Point ToImageCoord(Point p) => new Point(p.X / _zoom, p.Y / _zoom);

    private void OnCanvasMouseDown(object sender, MouseButtonEventArgs e)
    {
        if (_srcBitmap == null) return;
        _drawing = true;
        _startPt = e.GetPosition(Overlay);
        var imgPt = ToImageCoord(_startPt);

        if (_tool == Tool.Roi)
        {
            var rect = new System.Windows.Shapes.Rectangle { Stroke = new SolidColorBrush(Color.FromRgb(0, 255, 0)), StrokeThickness = 2 / _zoom, StrokeDashArray = new DoubleCollection { 4 / _zoom, 4 / _zoom }, Fill = new SolidColorBrush(Color.FromArgb(30, 0, 255, 0)) };
            Canvas.SetLeft(rect, _startPt.X); Canvas.SetTop(rect, _startPt.Y);
            Overlay.Children.Add(rect); _currentShape = rect;
        }
        else if (_tool == Tool.RectMask)
        {
            var rect = new System.Windows.Shapes.Rectangle { Stroke = new SolidColorBrush(Color.FromRgb(255, 80, 80)), StrokeThickness = 2 / _zoom, Fill = new SolidColorBrush(Color.FromArgb(120, 255, 0, 0)) };
            Canvas.SetLeft(rect, _startPt.X); Canvas.SetTop(rect, _startPt.Y);
            Overlay.Children.Add(rect); _currentShape = rect;
        }
        else if (_tool == Tool.CircleMask)
        {
            var ell = new Ellipse { Stroke = new SolidColorBrush(Color.FromRgb(255, 80, 80)), StrokeThickness = 2 / _zoom, Fill = new SolidColorBrush(Color.FromArgb(120, 255, 0, 0)) };
            Canvas.SetLeft(ell, _startPt.X); Canvas.SetTop(ell, _startPt.Y);
            Overlay.Children.Add(ell); _currentShape = ell;
        }
        else if (_tool == Tool.Brush) { DrawBrush(imgPt, 8 / _zoom, 0); _lastBrushPt = imgPt; }
        else if (_tool == Tool.Eraser) { DrawBrush(imgPt, 8 / _zoom, 255); _lastBrushPt = imgPt; }
        Overlay.CaptureMouse();
    }

    private void OnCanvasMouseMove(object sender, MouseEventArgs e)
    {
        if (!_drawing || _srcBitmap == null) return;
        var cur = e.GetPosition(Overlay);
        var imgPt = ToImageCoord(cur);

        if (_tool == Tool.Roi || _tool == Tool.RectMask)
        {
            if (_currentShape is System.Windows.Shapes.Rectangle rect)
            {
                double x = Math.Min(_startPt.X, cur.X), y = Math.Min(_startPt.Y, cur.Y);
                Canvas.SetLeft(rect, x); Canvas.SetTop(rect, y);
                rect.Width = Math.Abs(cur.X - _startPt.X); rect.Height = Math.Abs(cur.Y - _startPt.Y);
            }
        }
        else if (_tool == Tool.CircleMask)
        {
            if (_currentShape is Ellipse ell)
            {
                double x = Math.Min(_startPt.X, cur.X), y = Math.Min(_startPt.Y, cur.Y);
                Canvas.SetLeft(ell, x); Canvas.SetTop(ell, y);
                ell.Width = Math.Abs(cur.X - _startPt.X); ell.Height = Math.Abs(cur.Y - _startPt.Y);
            }
        }
        else if (_tool == Tool.Brush && _lastBrushPt.HasValue) { DrawLineOnMask(_lastBrushPt.Value, imgPt, 8 / _zoom, 0); _lastBrushPt = imgPt; }
        else if (_tool == Tool.Eraser && _lastBrushPt.HasValue) { DrawLineOnMask(_lastBrushPt.Value, imgPt, 8 / _zoom, 255); _lastBrushPt = imgPt; }
    }

    private void OnCanvasMouseUp(object sender, MouseButtonEventArgs e)
    {
        if (!_drawing) return;
        _drawing = false;
        Overlay.ReleaseMouseCapture();
        _lastBrushPt = null;

        if (_tool == Tool.Roi && _currentShape is System.Windows.Shapes.Rectangle rect)
        {
            double x = Canvas.GetLeft(rect), y = Canvas.GetTop(rect), w = rect.Width, h = rect.Height;
            if (w > 2 && h > 2)
            {
                _roiX = x / _zoom; _roiY = y / _zoom; _roiW = w / _zoom; _roiH = h / _zoom;
                _hasRoi = true;
                RoiInfo.Text = $"ROI: ({_roiX:F0}, {_roiY:F0}) {_roiW:F0}×{_roiH:F0}";
                InfoText.Text = "ROI 已设置，可继续绘制掩膜或点击确定";
            }
        }
        else if ((_tool == Tool.RectMask || _tool == Tool.CircleMask) && _currentShape != null)
        {
            RasterizeShapeToMask();
            Overlay.Children.Remove(_currentShape);
        }
        _currentShape = null;
        UpdateMaskOverlay();
    }

    private void OnCanvasMouseLeave(object sender, MouseEventArgs e) { _lastBrushPt = null; }

    private void DrawBrush(Point imgPt, double radius, byte value)
    {
        if (_maskBitmap == null) return;
        int cx = (int)imgPt.X, cy = (int)imgPt.Y, r = (int)Math.Max(1, radius);
        int pw = _maskBitmap.PixelWidth, ph = _maskBitmap.PixelHeight;
        _maskBitmap.Lock();
        unsafe {
            byte* p = (byte*)_maskBitmap.BackBuffer;
            int stride = _maskBitmap.BackBufferStride;
            for (int dy = -r; dy <= r; dy++)
            {
                int yy = cy + dy; if (yy < 0 || yy >= ph) continue;
                for (int dx = -r; dx <= r; dx++)
                {
                    int xx = cx + dx; if (xx < 0 || xx >= pw) continue;
                    if (dx * dx + dy * dy <= r * r) p[yy * stride + xx] = value;
                }
            }
        }
        _maskBitmap.AddDirtyRect(new Int32Rect(Math.Max(0, cx - r), Math.Max(0, cy - r), Math.Min(pw, 2 * r + 1), Math.Min(ph, 2 * r + 1)));
        _maskBitmap.Unlock();
    }

    private void DrawLineOnMask(Point from, Point to, double radius, byte value)
    {
        if (_maskBitmap == null) return;
        double dx = to.X - from.X, dy = to.Y - from.Y;
        double dist = Math.Sqrt(dx * dx + dy * dy);
        int steps = Math.Max(1, (int)(dist / 2));
        for (int i = 0; i <= steps; i++)
        {
            double t = (double)i / steps;
            DrawBrush(new Point(from.X + dx * t, from.Y + dy * t), radius, value);
        }
    }

    private void RasterizeShapeToMask()
    {
        if (_currentShape == null || _maskBitmap == null) return;
        bool isEllipse = _currentShape is Ellipse;
        double x = Canvas.GetLeft(_currentShape) / _zoom, y = Canvas.GetTop(_currentShape) / _zoom;
        double w, h;
        if (_currentShape is System.Windows.Shapes.Rectangle r) { w = r.Width / _zoom; h = r.Height / _zoom; }
        else { var el = (Ellipse)_currentShape; w = el.Width / _zoom; h = el.Height / _zoom; }

        int ix = (int)x, iy = (int)y, iw = (int)w, ih = (int)h;
        int cx2 = ix + iw / 2, cy2 = iy + ih / 2;
        int pw = _maskBitmap.PixelWidth, ph = _maskBitmap.PixelHeight;
        _maskBitmap.Lock();
        unsafe {
            byte* p = (byte*)_maskBitmap.BackBuffer;
            int stride = _maskBitmap.BackBufferStride;
            for (int py = Math.Max(0, iy); py < Math.Min(ph, iy + ih); py++)
                for (int px = Math.Max(0, ix); px < Math.Min(pw, ix + iw); px++)
                {
                    bool inside;
                    if (isEllipse) { double nx = (px - cx2) / (iw / 2.0), ny = (py - cy2) / (ih / 2.0); inside = nx * nx + ny * ny <= 1.0; }
                    else inside = true;
                    if (inside) p[py * stride + px] = 0;
                }
        }
        _maskBitmap.AddDirtyRect(new Int32Rect(Math.Max(0, ix), Math.Max(0, iy), Math.Min(pw, iw), Math.Min(ph, ih)));
        _maskBitmap.Unlock();
    }

    private void UpdateMaskOverlay()
    {
        if (_maskBitmap == null || _srcBitmap == null) return;
        int w = _maskBitmap.PixelWidth, h = _maskBitmap.PixelHeight;
        byte[] maskPx = new byte[w * h];
        _maskBitmap.CopyPixels(maskPx, w, 0);
        var overlay = new WriteableBitmap(w, h, 96, 96, PixelFormats.Bgra32, null);
        byte[] ovPx = new byte[w * h * 4];
        for (int i = 0; i < w * h; i++)
        {
            if (maskPx[i] == 0) { ovPx[i * 4] = 0; ovPx[i * 4 + 1] = 0; ovPx[i * 4 + 2] = 255; ovPx[i * 4 + 3] = 150; }
        }
        overlay.Lock();
        overlay.WritePixels(new Int32Rect(0, 0, w, h), ovPx, w * 4, 0);
        overlay.Unlock();
        _maskOverlay = overlay;
        MaskOverlay.Source = overlay;
    }

    private void OnConfirm(object sender, RoutedEventArgs e)
    {
        if (!_hasRoi) { MessageBox.Show("请先绘制模板 ROI 矩形", "提示"); return; }
        if (_srcBitmap == null) return;

        int x = (int)Math.Round(_roiX), y = (int)Math.Round(_roiY);
        int w = (int)Math.Round(_roiW), h = (int)Math.Round(_roiH);
        x = Math.Max(0, Math.Min(_srcBitmap.PixelWidth - 1, x));
        y = Math.Max(0, Math.Min(_srcBitmap.PixelHeight - 1, y));
        w = Math.Max(1, Math.Min(_srcBitmap.PixelWidth - x, w));
        h = Math.Max(1, Math.Min(_srcBitmap.PixelHeight - y, h));

        var src = _srcImage;
        ResultImage = VzImageConverter.CropRegion(ref src, x, y, w, h);
        ResultRoi = new double[] { x, y, w, h };

        if (_maskBitmap != null)
        {
            var cropMask = new WriteableBitmap(w, h, 96, 96, PixelFormats.Gray8, null);
            byte[] cropPx = new byte[w * h];
            _maskBitmap.CopyPixels(new Int32Rect(x, y, w, h), cropPx, w, 0);
            cropMask.Lock();
            cropMask.WritePixels(new Int32Rect(0, 0, w, h), cropPx, w, 0);
            cropMask.Unlock();
            ResultMask = VzImageConverter.FromBitmapSource(cropMask);
        }

        DialogResult = true;
        Close();
    }

    private void OnCancel(object sender, RoutedEventArgs e) { DialogResult = false; Close(); }
}