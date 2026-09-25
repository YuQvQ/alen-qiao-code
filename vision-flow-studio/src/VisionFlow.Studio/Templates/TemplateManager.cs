using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Windows.Media.Imaging;
using VisionFlow.Studio.Interop;

namespace VisionFlow.Studio.Templates;

/// <summary>
/// 模板文件（.tpl）管理：新建 / 导入 / 导出。
/// .tpl 文件格式为 JSON，包含模板图像的 PNG base64 编码 + 元信息。
/// </summary>
public static class TemplateManager
{
    /// <summary>模板目录（程序目录/templates）。</summary>
    public static string TemplatesDir => Path.Combine(AppContext.BaseDirectory, "templates");

    /// <summary>
    /// 从源图像裁剪 ROI 区域生成模板。
    /// roi = [x, y, w, h]（左上角 + 宽高，像素坐标）。
    /// 返回的 VzImage 由 .NET 端分配内存（owns_data=0）。
    /// </summary>
    public static VzImage CreateTemplate(ref VzImage src, double[] roi)
    {
        if (roi is null || roi.Length < 4)
            return CropRegion(ref src, 0, 0, src.width, src.height);
        int x = (int)Math.Round(roi[0]);
        int y = (int)Math.Round(roi[1]);
        int w = (int)Math.Round(roi[2]);
        int h = (int)Math.Round(roi[3]);
        return VzImageConverter.CropRegion(ref src, x, y, w, h);
    }

    /// <summary>
    /// 加载 .tpl 文件，返回模板图像。
    /// </summary>
    public static (VzImage image, VzImage mask) LoadTemplate(string tplPath)
    {
        if (!File.Exists(tplPath))
            throw new FileNotFoundException($"模板文件不存在：{tplPath}");

        var json = File.ReadAllText(tplPath);
        var doc = JsonSerializer.Deserialize<TplDocument>(json)
            ?? throw new InvalidDataException("模板文件格式无效");

        if (string.IsNullOrEmpty(doc.ImagePngBase64))
            throw new InvalidDataException("模板文件缺少图像数据");

        var pngBytes = Convert.FromBase64String(doc.ImagePngBase64);
        using var ms = new MemoryStream(pngBytes);
        var decoder = new PngBitmapDecoder(ms, BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad);
        var bmp = decoder.Frames[0];
        var img = VzImageConverter.FromBitmapSource(bmp);

        VzImage mask = default;
        if (!string.IsNullOrEmpty(doc.MaskPngBase64))
        {
            var maskBytes = Convert.FromBase64String(doc.MaskPngBase64);
            using var mms = new MemoryStream(maskBytes);
            var mdec = new PngBitmapDecoder(mms, BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad);
            mask = VzImageConverter.FromBitmapSource(mdec.Frames[0]);
        }
        return (img, mask);
    }

    /// <summary>
    /// 把当前模板图像 + ROI 保存为 .tpl 文件。
    /// </summary>
    public static void SaveTemplate(string tplPath, ref VzImage img, double[]? roi, VzImage? mask = null)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(tplPath)!);

        // VzImage → PNG base64
        var bmp = VzImageConverter.ToBitmapSource(ref img)
            ?? throw new InvalidOperationException("无法将模板图像转换为位图");
        using var ms = new MemoryStream();
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bmp));
        encoder.Save(ms);
        var pngBase64 = Convert.ToBase64String(ms.ToArray());

        string? maskBase64 = null;
        if (mask.HasValue && mask.Value.data != IntPtr.Zero)
        {
            var m = mask.Value;
            var mbmp = VzImageConverter.ToBitmapSource(ref m);
            if (mbmp is not null)
            {
                using var mms = new MemoryStream();
                var menc = new PngBitmapEncoder();
                menc.Frames.Add(BitmapFrame.Create(mbmp));
                menc.Save(mms);
                maskBase64 = Convert.ToBase64String(mms.ToArray());
            }
        }

        var doc = new TplDocument
        {
            Version = 2,
            Name = Path.GetFileNameWithoutExtension(tplPath),
            CreatedAt = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"),
            Roi = roi,
            ImageWidth = img.width,
            ImageHeight = img.height,
            ImagePngBase64 = pngBase64,
            MaskPngBase64 = maskBase64,
        };

        var options = new JsonSerializerOptions
        {
            WriteIndented = true,
            Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        };
        File.WriteAllText(tplPath, JsonSerializer.Serialize(doc, options));
    }

    // .tpl 文件 JSON 文档
    private sealed class TplDocument
    {
        public int Version { get; set; }
        public string Name { get; set; } = "";
        public string CreatedAt { get; set; } = "";
        public double[]? Roi { get; set; }
        public int ImageWidth { get; set; }
        public int ImageHeight { get; set; }
        public string ImagePngBase64 { get; set; } = "";
        public string? MaskPngBase64 { get; set; }
    }

    // 本地引用避免顶层 using 冲突
    private static VzImage CropRegion(ref VzImage src, int x, int y, int w, int h)
        => VzImageConverter.CropRegion(ref src, x, y, w, h);
}
