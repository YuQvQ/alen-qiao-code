// line_find.h — 直线查找算法（梯度扫描 + 直线拟合，输出黑白交界单条直线）
//
// 输入：image (Image)
// 输出：
//   result_image : Image         叠加了检测直线的结果图
//   lines        : Point2DList   直线端点对（每条直线2个点：起点+终点，坐标为原图坐标）
//   count        : Int           直线数量（找到=1，未找到=0）
// 参数：
//   search_roi     : 搜索区 [cx, cy, w, h, angle]（带方向矩形，可选）
//   bin_threshold  : 二值化阈值（0-255），用于区分亮/暗
//   polarity       : 搜索方向枚举（0=任意, 1=白到黑, 2=黑到白）
//   min_line_length: 最小直线长度（像素）
//   max_line_gap   : 最大间隙（像素，大于此值则视为断开）
//
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <vector>

namespace vz {

class LineFindAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string& port_name,
                  void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;

    static std::unique_ptr<AlgoBase> create();

private:
    cv::Mat input_;

    // 参数
    double bin_threshold_ = 128.0;     // 二值化阈值（用于 polarity 非0时的 threshold）
    int    enable_binary_ = 1;         // 二值化开关（0=否,1=是）；polarity 非0时若关闭则纯 Canny/Sobel 找边缘
    int    polarity_ = 0;              // 0=任意, 1=白到黑, 2=黑到白
    double min_line_length_ = 30.0;
    double max_line_gap_ = 10.0;
    double canny_low_ = 50.0;          // Canny 低阈值
    double canny_high_ = 150.0;        // Canny 高阈值
    int    hough_threshold_ = 15;      // 霍夫累加器阈值
    int    enable_blur_ = 1;           // 高斯模糊开关 0=否 1=是
    int    gaussian_ksize_ = 3;        // 高斯模糊核大小（奇数）
    int    equalize_hist_ = 1;         // 直方图均衡化开关（0=否,1=是）
    int    edge_method_ = 0;           // 边缘检测方法：0=Canny, 1=Sobel
    int    segment_count_ = 10;        // 子框数量（将ROI切成N个子框找点）

    // 搜索区 ROI（带方向）
    bool   has_search_roi_ = false;
    double roi_cx_ = 0, roi_cy_ = 0, roi_w_ = 0, roi_h_ = 0, roi_angle_ = 0;

    // 输出
    std::vector<VzPoint2D> out_lines_;   // 2*N 个点（每条直线2个点）
    int out_count_ = 0;
    cv::Mat result_image_;               // 叠加了检测直线的结果图

    // 绘制参数
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);   // 拟合直线颜色（默认绿）
    int draw_thickness_ = 2;
    int point_size_ = 3;
};

} // namespace vz
