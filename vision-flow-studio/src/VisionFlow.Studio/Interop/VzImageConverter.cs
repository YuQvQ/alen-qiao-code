using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace VisionFlow.Studio.Interop;

/// <summary>
/// VzImage -> BitmapSource 转换器。
/// OpenCV cv::Mat 的数据按 cv::Mat.step 排列，WPF WriteableBitmap 直接拷贝即可。
/// BGR24 / Gray8 / BGRA32 三种像素格式对应通道数 3/1/4。
/// </summary>
public static class VzImageConverter
{
    public static BitmapSource? ToBitmapSource(ref VzImage img)
    {
        if (img.data == IntPtr.Zero) return null;
        PixelFormat fmt = img.channels switch
        {
            1 => PixelFormats.Gray8,
            3 => PixelFormats.Bgr24,
            4 => PixelFormats.Bgra32,
            _ => PixelFormats.Bgr24
        };
        int width = img.width;
        int height = img.height;
        int bytesPerPixel = (fmt.BitsPerPixel + 7) / 8;
        int srcStride = (int)img.step;
        if (srcStride <= 0) srcStride = width * bytesPerPixel;

        var wb = new WriteableBitmap(width, height, 96, 96, fmt, null);
        int dstStride = wb.BackBufferStride;
        int rowBytes = width * bytesPerPixel;
        Execution.Logger.Info("VzImageConverter",
            $"ToBitmapSource: img={width}x{height}x{img.channels}, step={img.step}, srcStride={srcStride}, dstStride={dstStride}, bytesPerPixel={bytesPerPixel}");

        // 使用 Lock/Unlock + Marshal.Copy（与 DrawRectsOnBitmap 的 Lock/Unlock 兼容）
        wb.Lock();
        try
        {
            unsafe
            {
                byte* dst = (byte*)wb.BackBuffer;
                if (srcStride == dstStride)
                {
                    // stride 相同：用 Marshal.Copy 经托管数组中转（对大图更可靠）
                    long totalSize = (long)srcStride * height;
                    byte[] buffer = new byte[totalSize];
                    Marshal.Copy(img.data, buffer, 0, (int)totalSize);
                    // 托管数组 → BackBuffer
                    Marshal.Copy(buffer, 0, (IntPtr)dst, (int)totalSize);
                }
                else
                {
                    // stride 不同：逐行拷贝
                    for (int y = 0; y < height; y++)
                    {
                        byte* srcRow = (byte*)img.data + y * srcStride;
                        byte* dstRow = dst + y * dstStride;
                        System.Buffer.MemoryCopy(srcRow, dstRow, rowBytes, rowBytes);
                    }
                }
            }
            wb.AddDirtyRect(new Int32Rect(0, 0, width, height));
        }
        finally
        {
            wb.Unlock();
        }
        return wb;
    }

    /// <summary>
    /// 从 WriteableBitmap 构造 VzImage（.NET 端分配内存，owns_data=0）。
    /// 调用方后续用 VzAlgoInterop.FreeVzImageData 释放。
    /// </summary>
    public static VzImage FromBitmapSource(BitmapSource src)
    {
        int w = src.PixelWidth;
        int h = src.PixelHeight;
        int channels = src.Format.BitsPerPixel switch
        {
            8 => 1,
            24 => 3,
            32 => 4,
            _ => 3,
        };
        int bytesPerPixel = (src.Format.BitsPerPixel + 7) / 8;
        int stride = w * bytesPerPixel;
        long totalSize = (long)stride * h;

        IntPtr data = Marshal.AllocHGlobal((IntPtr)totalSize);
        try
        {
            // 从 BitmapSource 拷贝像素到非托管内存
            src.CopyPixels(new Int32Rect(0, 0, w, h), data, (int)totalSize, stride);
        }
        catch
        {
            Marshal.FreeHGlobal(data);
            throw;
        }

        return new VzImage
        {
            width = w,
            height = h,
            channels = channels,
            pixel_type = (int)VzPixelType.UInt8,
            data = data,
            step = (ulong)stride,
            owns_data = 0,   // .NET 分配，用 FreeHGlobal 释放
            magic = 0x565A494D,   // 'VZIM'
        };
    }

    /// <summary>
    /// 从源 VzImage 裁剪矩形区域，返回新的 VzImage（.NET 端分配内存，owns_data=0）。
    /// roi = [x, y, w, h]（左上角 + 宽高，像素坐标）。
    /// </summary>
    public static VzImage CropRegion(ref VzImage src, int x, int y, int cropW, int cropH)
    {
        if (src.data == IntPtr.Zero || src.width <= 0 || src.height <= 0)
            return default;
        // 裁剪边界约束
        x = Math.Max(0, Math.Min(x, src.width - 1));
        y = Math.Max(0, Math.Min(y, src.height - 1));
        cropW = Math.Max(1, Math.Min(cropW, src.width - x));
        cropH = Math.Max(1, Math.Min(cropH, src.height - y));

        int bpp = src.channels;
        int srcStride = (int)src.step;
        if (srcStride <= 0) srcStride = src.width * bpp;
        int dstStride = cropW * bpp;
        long dstSize = (long)dstStride * cropH;

        IntPtr dstData = Marshal.AllocHGlobal((IntPtr)dstSize);
        try
        {
            unsafe
            {
                byte* sBase = (byte*)src.data;
                byte* dBase = (byte*)dstData;
                for (int row = 0; row < cropH; row++)
                {
                    byte* sRow = sBase + (y + row) * srcStride + x * bpp;
                    byte* dRow = dBase + row * dstStride;
                    System.Buffer.MemoryCopy(sRow, dRow, dstStride, dstStride);
                }
            }
        }
        catch
        {
            Marshal.FreeHGlobal(dstData);
            throw;
        }

        return new VzImage
        {
            width = cropW,
            height = cropH,
            channels = src.channels,
            pixel_type = src.pixel_type,
            data = dstData,
            step = (ulong)dstStride,
            owns_data = 0,
            magic = 0x565A494D,
        };
    }
}
