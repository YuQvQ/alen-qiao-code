using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using VisionFlow.Studio.Interop;
using VisionFlow.Studio.Models;

namespace VisionFlow.Studio;

/// <summary>
/// UI 层统一覆盖绘制：从节点 OutputCache 读取算法返回的纯数据，
/// 在 WPF 矢量层绘制圆/线/点/框/多边形/文字。
/// C++ 算法层不再做任何标注绘制，result_image 仅为原图透传。
/// </summary>
public static class OverlayRenderer
{
    private static readonly Typeface Font = new("Consolas");

    // 记录本帧是否真正绘制了覆盖内容（无绘制时跳过昂贵的 RenderTargetBitmap）
    [ThreadStatic] private static bool _drew;

    /// <summary>把 base 图像与节点的覆盖标注合成为一张可显示的 BitmapSource。
    /// 若该节点没有可绘制的数据，则直接返回原图（零额外开销）。</summary>
    public static BitmapSource Render(BitmapSource baseImg, Node node)
    {
        _drew = false;
        double w = baseImg.PixelWidth;
        double h = baseImg.PixelHeight;

        var dv = new DrawingVisual();
        using (var dc = dv.RenderOpen())
        {
            if (w > 0 && h > 0)
            {
                try { DrawNode(dc, node, w, h); }
                catch { /* 覆盖绘制失败不应影响主图显示 */ }
            }
        }

        // 没有任何覆盖标注：直接用原图，避免全图重光栅化导致卡顿
        if (!_drew) return baseImg;

        using (var dc2 = dv.RenderOpen())
        {
            dc2.DrawImage(baseImg, new Rect(0, 0, w, h));
            try { DrawNode(dc2, node, w, h); } catch { }
        }

        var rtb = new RenderTargetBitmap(
            (int)Math.Max(1, w), (int)Math.Max(1, h),
            baseImg.DpiX, baseImg.DpiY, PixelFormats.Pbgra32);
        rtb.Render(dv);
        rtb.Freeze();
        return rtb;
    }

    // ---------------- 矢量层绘制（叠加到 RoiCanvas，随图像缩放，零全图重编码） ----------------
    /// <summary>把节点的检测标注作为 WPF Shape 画到 canvas 上（显示坐标 = 像素 × scale）。</summary>
    public static void RenderToCanvas(Canvas canvas, Node node, double scale)
    {
        if (node is null || scale <= 0) return;
        Color c = GetColor(node, "#00FF00");
        double thick = GetThickness(node);
        double ptSize = GetPointSize(node) * scale;
        var stroke = new SolidColorBrush(c); stroke.Freeze();
        var brush = new SolidColorBrush(c); brush.Freeze();

        switch (node.AlgoName)
        {
            case "CircleFind":
                if (CountOf(node) > 0 && GetPose(node, "point", out var cp) && GetD(node, "radius", out var r) && r > 0)
                {
                    AddCircle(canvas, cp.point, r, scale, stroke, thick);
                    AddCross(canvas, cp.point, ptSize, Brushes.Red);
                    GetD(node, "score", out var s);
                    AddText(canvas, $"R={r:F1} S={s:F2}", (cp.point.x + r) * scale, (cp.point.y - r) * scale, Brushes.Lime);
                }
                break;
            case "CircleFit":
                if (GetPose(node, "center", out var cf) && GetD(node, "radius", out var rf) && rf > 0)
                {
                    AddCircle(canvas, cf.point, rf, scale, stroke, thick);
                    AddCross(canvas, cf.point, ptSize, Brushes.Red);
                    AddText(canvas, $"R={rf:F1}", (cf.point.x + rf) * scale, (cf.point.y - rf) * scale, brush);
                }
                break;
            case "LineFind":
                if (GetPoints(node, "lines", out var lp) && lp.Length >= 2)
                {
                    AddSeg(canvas, lp[0], lp[1], scale, stroke, thick);
                    var mid = Mid(lp[0], lp[1]);
                    AddCross(canvas, mid, ptSize, Brushes.Red);
                    AddText(canvas, $"L={Dist(lp[0], lp[1]):F1}", mid.x * scale, mid.y * scale - 12, brush);
                }
                break;
            case "LineFit":
                if (GetPose(node, "point", out var lfp) && GetD(node, "angle", out var ang))
                {
                    AddAngleLine(canvas, lfp.point, ang, scale, stroke, thick);
                    AddCross(canvas, lfp.point, ptSize, Brushes.Red);
                    AddText(canvas, $"A={ang:F2}°", lfp.point.x * scale, lfp.point.y * scale - 12, brush);
                }
                break;
            case "LineAngle":
                if (GetPoints(node, "intersection", out var ip) && ip.Length >= 1 && GetD(node, "angle", out var la))
                {
                    AddCross(canvas, ip[0], ptSize, brush);
                    AddText(canvas, $"Angle={la:F2}°", ip[0].x * scale, ip[0].y * scale - 12, brush);
                }
                break;
            case "LineIntersect":
                if (GetPoints(node, "intersection", out var iip) && iip.Length >= 1)
                {
                    AddCross(canvas, iip[0], ptSize, Brushes.Red);
                    if (GetD(node, "angle", out var ia))
                        AddText(canvas, $"Angle={ia:F2}°", iip[0].x * scale, iip[0].y * scale - 12, brush);
                }
                break;
            case "DistanceMeasure":
                if (GetD(node, "distance", out var dist))
                    AddText(canvas, $"D={dist:F2}px", 8, 8, Brushes.Yellow);
                break;
            case "CornerDetect":
                if (GetPoints(node, "points", out var cp2))
                    foreach (var p in cp2) AddCross(canvas, p, ptSize, brush);
                AddText(canvas, $"corners={CountOf(node)}", 8, 8, brush);
                break;
            case "MinMaxLoc":
                if (GetPoints(node, "max_loc", out var mx) && mx.Length >= 1) AddCross(canvas, mx[0], ptSize, Brushes.Red);
                if (GetPoints(node, "min_loc", out var mn) && mn.Length >= 1) AddCross(canvas, mn[0], ptSize, Brushes.DodgerBlue);
                break;
            case "EdgeFinder":
                if (GetPose(node, "point", out var ep))
                {
                    AddCross(canvas, ep.point, ptSize, brush);
                    if (GetD(node, "score", out var es))
                        AddText(canvas, $"score={es:F3}", ep.point.x * scale, ep.point.y * scale - 12, brush);
                }
                break;
            case "FindContours":
            case "ConvexHull":
                {
                    var key = node.AlgoName == "ConvexHull" ? "hull" : "points";
                    if (GetPoints(node, key, out var hp) && hp.Length >= 2)
                    {
                        AddPoly(canvas, hp, scale, stroke, thick, closed: true);
                        foreach (var p in hp) AddCross(canvas, p, ptSize, brush);
                    }
                }
                break;
            case "BlobAnalysis":
                if (GetRects(node, "blobs", out var bl))
                    foreach (var b in bl) AddRotRect(canvas, b, scale, stroke, thick);
                if (GetPoints(node, "centers", out var bc))
                    foreach (var p in bc) AddCross(canvas, p, ptSize, brush);
                break;
            case "RectangleDetect":
                if (GetPoints(node, "corners", out var rc) && rc.Length >= 4)
                {
                    AddPoly(canvas, rc, scale, stroke, thick, closed: true);
                    if (GetPose(node, "center", out var rcp)) AddCross(canvas, rcp.point, ptSize, Brushes.Red);
                    if (GetD(node, "width", out var rw) && GetD(node, "height", out var rh))
                        AddText(canvas, $"{rw:F0}x{rh:F0}", rc[0].x * scale, rc[0].y * scale - 10, brush);
                }
                break;
            case "QrCodeDetect":
                if (GetPoints(node, "points", out var qp) && qp.Length >= 4)
                    AddPoly(canvas, qp, scale, stroke, thick, closed: true);
                if (GetS(node, "content", out var qc) && !string.IsNullOrEmpty(qc))
                    AddText(canvas, qc, 8, 8, brush);
                break;
            case "ColorDetect":
                if (GetPose(node, "center", out var colp))
                {
                    AddCross(canvas, colp.point, ptSize, brush);
                    if (GetD(node, "area", out var area))
                        AddText(canvas, $"area={area:F0}", colp.point.x * scale, colp.point.y * scale - 12, brush);
                }
                break;
            case "ShapeMatch":
                if (GetD(node, "score", out var sm) && GetB(node, "match", out var ok))
                    AddText(canvas, (ok ? "MATCH " : "NO MATCH ") + $"S={sm:F2}", 8, 8, ok ? Brushes.Lime : Brushes.Red);
                break;
            case "FeatureMatch":
                if (GetPoints(node, "target_points", out var ft))
                    foreach (var p in ft) AddCross(canvas, p, ptSize, brush);
                break;
            case "DnnDetect":
                if (GetRects(node, "boxes", out var boxes))
                {
                    string? labels = GetS(node, "labels", out var ls) ? ls : null;
                    var parts = labels?.Split(',');
                    for (int i = 0; i < boxes.Length; i++)
                    {
                        AddRotRect(canvas, boxes[i], scale, stroke, thick);
                        if (parts is { Length: > 0 })
                            AddText(canvas, parts[i % parts.Length],
                                (boxes[i].x - boxes[i].w / 2) * scale,
                                (boxes[i].y - boxes[i].h / 2) * scale - 14, brush);
                    }
                }
                break;
            case "DnnClassify":
                if (GetS(node, "label", out var cl) && GetD(node, "confidence", out var conf))
                    AddText(canvas, $"{cl} {conf:P1}", 8, 8, Brushes.Yellow);
                break;
            case "PixelStats":
                if (GetD(node, "mean", out var mean) && GetD(node, "stddev", out var std))
                    AddText(canvas, $"mean={mean:F1} std={std:F1}", 8, 8, Brushes.Yellow);
                break;
        }
    }

    private static void AddCircle(Canvas cv, VzPoint2D c, double r, double sc, Brush stroke, double thick)
    {
        var d = 2 * r * sc;
        if (d <= 0) return;
        var e = new Ellipse { Width = d, Height = d, Stroke = stroke, StrokeThickness = thick, IsHitTestVisible = false };
        Canvas.SetLeft(e, (c.x - r) * sc);
        Canvas.SetTop(e, (c.y - r) * sc);
        cv.Children.Add(e);
    }
    private static void AddCross(Canvas cv, VzPoint2D p, double s, Brush b)
    {
        double x = p.x, y = p.y;
        var l1 = new Line { X1 = (x - s), Y1 = y, X2 = (x + s), Y2 = y, Stroke = b, StrokeThickness = 1.5, IsHitTestVisible = false };
        var l2 = new Line { X1 = x, Y1 = (y - s), X2 = x, Y2 = (y + s), Stroke = b, StrokeThickness = 1.5, IsHitTestVisible = false };
        // 坐标已是显示坐标（调用方已乘 scale）
        cv.Children.Add(l1); cv.Children.Add(l2);
    }
    private static void AddSeg(Canvas cv, VzPoint2D a, VzPoint2D b, double sc, Brush stroke, double thick)
    {
        var l = new Line { X1 = a.x * sc, Y1 = a.y * sc, X2 = b.x * sc, Y2 = b.y * sc, Stroke = stroke, StrokeThickness = thick, IsHitTestVisible = false };
        cv.Children.Add(l);
    }
    private static void AddAngleLine(Canvas cv, VzPoint2D p, double angleDeg, double sc, Brush stroke, double thick)
    {
        double a = angleDeg * Math.PI / 180.0;
        double dx = Math.Cos(a), dy = Math.Sin(a);
        double len = 4096;
        var l = new Line
        {
            X1 = (p.x - dx * len) * sc, Y1 = (p.y - dy * len) * sc,
            X2 = (p.x + dx * len) * sc, Y2 = (p.y + dy * len) * sc,
            Stroke = stroke, StrokeThickness = thick, IsHitTestVisible = false
        };
        cv.Children.Add(l);
    }
    private static void AddPoly(Canvas cv, VzPoint2D[] pts, double sc, Brush stroke, double thick, bool closed)
    {
        var pc = new PointCollection();
        foreach (var p in pts) pc.Add(new Point(p.x * sc, p.y * sc));
        Shape pg = closed
            ? new Polygon { Stroke = stroke, StrokeThickness = thick, Points = pc, IsHitTestVisible = false }
            : new Polyline { Stroke = stroke, StrokeThickness = thick, Points = pc, IsHitTestVisible = false };
        cv.Children.Add(pg);
    }
    private static void AddRotRect(Canvas cv, VzRect r, double sc, Brush stroke, double thick)
    {
        double a = r.angle * Math.PI / 180.0;
        double ca = Math.Cos(a), sa = Math.Sin(a);
        double hw = r.w / 2, hh = r.h / 2;
        (double dx, double dy)[] offs = { (-hw, -hh), (hw, -hh), (hw, hh), (-hw, hh) };
        var pts = new VzPoint2D[4];
        for (int i = 0; i < 4; i++)
            pts[i] = new VzPoint2D { x = r.x + offs[i].dx * ca - offs[i].dy * sa, y = r.y + offs[i].dx * sa + offs[i].dy * ca };
        AddPoly(cv, pts, sc, stroke, thick, closed: true);
    }
    private static void AddText(Canvas cv, string s, double x, double y, Brush b)
    {
        var tb = new TextBlock
        {
            Text = s,
            FontSize = 13,
            Foreground = b,
            Background = new SolidColorBrush(Color.FromArgb(160, 0, 0, 0)),
            Padding = new Thickness(3, 1, 3, 1),
            IsHitTestVisible = false,
        };
        Canvas.SetLeft(tb, x);
        Canvas.SetTop(tb, y);
        cv.Children.Add(tb);
    }

    // ---------------- 按节点类型分发 ----------------
    private static void DrawNode(DrawingContext dc, Node node, double w, double h)
    {
        Color c = GetColor(node, "#00FF00");
        double thick = GetThickness(node);
        double ptSize = GetPointSize(node);
        var pen = new Pen(new SolidColorBrush(c), thick);
        var brush = new SolidColorBrush(c);
        pen.Freeze(); brush.Freeze();

        switch (node.AlgoName)
        {
            case "CircleFind":
                if (CountOf(node) > 0 && GetPose(node, "point", out var cp) && GetD(node, "radius", out var r) && r > 0)
                {
                    DrawCircle(dc, cp.point, r, pen);
                    DrawCross(dc, cp.point, ptSize, Brushes.Red);
                    GetD(node, "score", out var s);
                    Text(dc, $"R={r:F1} S={s:F2}", cp.point.x + r, cp.point.y - r, Brushes.Lime);
                }
                break;
            case "CircleFit":
                if (GetPose(node, "center", out var cf) && GetD(node, "radius", out var rf) && rf > 0)
                {
                    DrawCircle(dc, cf.point, rf, pen);
                    DrawCross(dc, cf.point, ptSize, Brushes.Red);
                    Text(dc, $"R={rf:F1}", cf.point.x + rf, cf.point.y - rf, brush);
                }
                break;
            case "LineFind":
                if (GetPoints(node, "lines", out var lp) && lp.Length >= 2)
                {
                    DrawSeg(dc, lp[0], lp[1], pen);
                    var mid = Mid(lp[0], lp[1]);
                    DrawCross(dc, mid, ptSize, Brushes.Red);
                    double lineLen = Dist(lp[0], lp[1]);
                    Text(dc, $"L={lineLen:F1}", mid.x, mid.y - 12, brush);
                }
                break;
            case "LineFit":
                if (GetPose(node, "point", out var lfp) && GetD(node, "angle", out var ang))
                {
                    DrawAngleLine(dc, lfp.point, ang, w, h, pen);
                    DrawCross(dc, lfp.point, ptSize, Brushes.Red);
                    Text(dc, $"A={ang:F2}°", lfp.point.x, lfp.point.y - 12, brush);
                }
                break;
            case "LineAngle":
                if (GetPoints(node, "intersection", out var ip) && ip.Length >= 1
                    && GetD(node, "angle", out var la))
                {
                    DrawCross(dc, ip[0], ptSize, brush);
                    Text(dc, $"Angle={la:F2}°", ip[0].x, ip[0].y - 12, brush);
                }
                break;
            case "LineIntersect":
                if (GetPoints(node, "intersection", out var iip) && iip.Length >= 1)
                {
                    DrawCross(dc, iip[0], ptSize, Brushes.Red);
                    if (GetD(node, "angle", out var ia))
                        Text(dc, $"Angle={ia:F2}°", iip[0].x, iip[0].y - 12, brush);
                }
                break;
            case "DistanceMeasure":
                if (GetD(node, "distance", out var dist))
                    Text(dc, $"D={dist:F2}px", 8, 8, Brushes.Yellow);
                break;
            case "CornerDetect":
                if (GetPoints(node, "points", out var cp2))
                    foreach (var p in cp2) DrawCross(dc, p, ptSize, brush);
                Text(dc, $"corners={CountOf(node)}", 8, 8, brush);
                break;
            case "MinMaxLoc":
                if (GetPoints(node, "max_loc", out var mx) && mx.Length >= 1)
                    DrawCross(dc, mx[0], ptSize, Brushes.Red);
                if (GetPoints(node, "min_loc", out var mn) && mn.Length >= 1)
                    DrawCross(dc, mn[0], ptSize, Brushes.DodgerBlue);
                break;
            case "EdgeFinder":
                if (GetPose(node, "point", out var ep))
                {
                    DrawCross(dc, ep.point, ptSize, brush);
                    if (GetD(node, "score", out var es))
                        Text(dc, $"score={es:F3}", ep.point.x, ep.point.y - 12, brush);
                }
                break;
            case "FindContours":
            case "ConvexHull":
                {
                    var key = node.AlgoName == "ConvexHull" ? "hull" : "points";
                    if (GetPoints(node, key, out var hp) && hp.Length >= 2)
                    {
                        DrawPoly(dc, hp, pen, closed: true);
                        foreach (var p in hp) DrawCross(dc, p, ptSize, brush);
                    }
                }
                break;
            case "BlobAnalysis":
                if (GetRects(node, "blobs", out var bl))
                    foreach (var b in bl) DrawRotRect(dc, b, pen);
                if (GetPoints(node, "centers", out var bc))
                    foreach (var p in bc) DrawCross(dc, p, ptSize, brush);
                break;
            case "RectangleDetect":
                if (GetPoints(node, "corners", out var rc) && rc.Length >= 4)
                {
                    DrawPoly(dc, rc, pen, closed: true);
                    if (GetPose(node, "center", out var rcp)) DrawCross(dc, rcp.point, ptSize, Brushes.Red);
                    if (GetD(node, "width", out var rw) && GetD(node, "height", out var rh))
                        Text(dc, $"{rw:F0}x{rh:F0}", rc[0].x, rc[0].y - 10, brush);
                }
                break;
            case "QrCodeDetect":
                if (GetPoints(node, "points", out var qp) && qp.Length >= 4)
                    DrawPoly(dc, qp, pen, closed: true);
                if (GetS(node, "content", out var qc) && !string.IsNullOrEmpty(qc))
                    Text(dc, qc, 8, 8, brush);
                break;
            case "ColorDetect":
                if (GetPose(node, "center", out var colp))
                {
                    DrawCross(dc, colp.point, ptSize, brush);
                    if (GetD(node, "area", out var area))
                        Text(dc, $"area={area:F0}", colp.point.x, colp.point.y - 12, brush);
                }
                break;
            case "ShapeMatch":
                if (GetD(node, "score", out var sm) && GetB(node, "match", out var ok))
                    Text(dc, (ok ? "MATCH " : "NO MATCH ") + $"S={sm:F2}", 8, 8,
                        ok ? Brushes.Lime : Brushes.Red);
                break;
            case "FeatureMatch":
                if (GetPoints(node, "target_points", out var ft))
                    foreach (var p in ft) DrawCross(dc, p, ptSize, brush);
                break;
            case "DnnDetect":
                if (GetRects(node, "boxes", out var boxes))
                {
                    string? labels = GetS(node, "labels", out var ls) ? ls : null;
                    var parts = labels?.Split(',');
                    for (int i = 0; i < boxes.Length; i++)
                    {
                        DrawRotRect(dc, boxes[i], pen);
                        if (parts is { Length: > 0 })
                            Text(dc, parts[i % parts.Length], boxes[i].x - boxes[i].w / 2, boxes[i].y - boxes[i].h / 2 - 14, brush);
                    }
                }
                break;
            case "DnnClassify":
                if (GetS(node, "label", out var cl) && GetD(node, "confidence", out var conf))
                    Text(dc, $"{cl} {conf:P1}", 8, 8, Brushes.Yellow);
                break;
            case "PixelStats":
                if (GetD(node, "mean", out var mean) && GetD(node, "stddev", out var std))
                    Text(dc, $"mean={mean:F1} std={std:F1}", 8, 8, Brushes.Yellow);
                break;
            // TemplateMatch 已由 ShowImageWithMatches 处理
            default:
                break;
        }
    }

    // ---------------- 几何图元 ----------------
    private static void DrawCircle(DrawingContext dc, VzPoint2D c, double r, Pen pen)
    { _drew = true; dc.DrawEllipse(null, pen, new Point(c.x, c.y), r, r); }

    private static void DrawCross(DrawingContext dc, VzPoint2D p, double s, Brush b)
    {
        _drew = true;
        var pen = new Pen(b, Math.Max(1, s / 2));
        dc.DrawLine(pen, new Point(p.x - s, p.y), new Point(p.x + s, p.y));
        dc.DrawLine(pen, new Point(p.x, p.y - s), new Point(p.x, p.y + s));
    }

    private static void DrawSeg(DrawingContext dc, VzPoint2D a, VzPoint2D b, Pen pen)
    { _drew = true; dc.DrawLine(pen, P(a), P(b)); }

    private static void DrawPoly(DrawingContext dc, VzPoint2D[] pts, Pen pen, bool closed)
    {
        _drew = true;
        var geo = new StreamGeometry();
        using (var ctx = geo.Open())
        {
            ctx.BeginFigure(new Point(pts[0].x, pts[0].y), false, closed);
            for (int i = 1; i < pts.Length; i++)
                ctx.LineTo(new Point(pts[i].x, pts[i].y), true, false);
        }
        dc.DrawGeometry(null, pen, geo);
    }

    private static void DrawRotRect(DrawingContext dc, VzRect r, Pen pen)
    {
        double a = r.angle * Math.PI / 180.0;
        double ca = Math.Cos(a), sa = Math.Sin(a);
        double hw = r.w / 2, hh = r.h / 2;
        VzPoint2D[] corners = new VzPoint2D[4];
        (double dx, double dy)[] offs = { (-hw, -hh), (hw, -hh), (hw, hh), (-hw, hh) };
        for (int i = 0; i < 4; i++)
        {
            corners[i] = new VzPoint2D
            {
                x = r.x + offs[i].dx * ca - offs[i].dy * sa,
                y = r.y + offs[i].dx * sa + offs[i].dy * ca,
            };
        }
        DrawPoly(dc, corners, pen, closed: true);
    }

    private static void DrawAngleLine(DrawingContext dc, VzPoint2D p, double angleDeg, double w, double h, Pen pen)
    {
        _drew = true;
        double a = angleDeg * Math.PI / 180.0;
        double dx = Math.Cos(a), dy = Math.Sin(a);
        double len = Math.Sqrt(w * w + h * h);
        var p0 = new Point(p.x - dx * len, p.y - dy * len);
        var p1 = new Point(p.x + dx * len, p.y + dy * len);
        dc.DrawLine(pen, p0, p1);
    }

    private static void Text(DrawingContext dc, string s, double x, double y, Brush b)
    {
        _drew = true;
        var ft = new FormattedText(s, CultureInfo.InvariantCulture,
            FlowDirection.LeftToRight, Font, 14, b, 1.0);
        dc.DrawText(ft, new Point(x, y));
    }

    // ---------------- 数据读取辅助 ----------------
    private static bool GetPose(Node node, string name, out VzPose2D v)
    {
        if (node.OutputCache.TryGetValue(name, out var o) && o is VzPose2D p) { v = p; return true; }
        v = default; return false;
    }
    private static bool GetD(Node node, string name, out double v)
    {
        if (node.OutputCache.TryGetValue(name, out var o) && o is double d) { v = d; return true; }
        if (node.OutputCache.TryGetValue(name, out var o2) && o2 is int i) { v = i; return true; }
        v = 0; return false;
    }
    private static bool GetB(Node node, string name, out bool v)
    {
        if (node.OutputCache.TryGetValue(name, out var o) && o is bool b) { v = b; return true; }
        v = false; return false;
    }
    private static bool GetS(Node node, string name, out string v)
    {
        if (node.OutputCache.TryGetValue(name, out var o) && o is string s) { v = s; return true; }
        v = ""; return false;
    }
    private static bool GetPoints(Node node, string name, out VzPoint2D[] v)
    {
        if (node.OutputCache.TryGetValue(name, out var o) && o is VzPoint2D[] p) { v = p; return true; }
        v = Array.Empty<VzPoint2D>(); return false;
    }
    private static bool GetRects(Node node, string name, out VzRect[] v)
    {
        if (node.OutputCache.TryGetValue(name, out var o) && o is VzRect[] r) { v = r; return true; }
        v = Array.Empty<VzRect>(); return false;
    }
    private static int CountOf(Node node)
        => node.OutputCache.TryGetValue("count", out var o) && o is int i ? i : -1;

    private static Point P(VzPoint2D p) => new(p.x, p.y);
    private static VzPoint2D Mid(VzPoint2D a, VzPoint2D b) => new() { x = (a.x + b.x) / 2, y = (a.y + b.y) / 2 };
    private static double Dist(VzPoint2D a, VzPoint2D b) => Math.Sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));

    // ---------------- 颜色/线宽（来自节点参数） ----------------
    private static Color GetColor(Node node, string fallback)
    {
        string hex = fallback;
        if (node.Parameters is not null && node.Parameters.TryGetValue("draw_color", out var c) && c is string s && !string.IsNullOrEmpty(s))
            hex = s;
        try
        {
            if (hex.StartsWith("#"))
                return (Color)ColorConverter.ConvertFromString(hex);
            if (hex.Contains(','))
            {
                var p = hex.Split(',');
                if (p.Length >= 3)
                    return Color.FromRgb(byte.Parse(p[2]), byte.Parse(p[1]), byte.Parse(p[0])); // B,G,R
            }
        }
        catch { }
        return Colors.Lime;
    }
    private static double GetThickness(Node node)
    {
        if (node.Parameters is not null && node.Parameters.TryGetValue("draw_thickness", out var t))
        {
            if (t is int ti) return Math.Clamp(ti, 1, 20);
            if (t is string ts && int.TryParse(ts, out var tn)) return Math.Clamp(tn, 1, 20);
        }
        return 2;
    }
    private static double GetPointSize(Node node)
    {
        if (node.Parameters is not null && node.Parameters.TryGetValue("point_size", out var t))
        {
            if (t is int ti) return Math.Clamp(ti, 1, 50);
            if (t is string ts && int.TryParse(ts, out var tn)) return Math.Clamp(tn, 1, 50);
        }
        return 4;
    }
}
