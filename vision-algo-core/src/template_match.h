// template_match.h — 模板匹配算法（多角度、多尺度、金字塔、NMS、子像素）
//
// 端口契约：
//   输入：image (Image) [必]、template (Image) [可选]、region (Region) [可选]
//   输出：result_image (Image)、matches (MatchResultList)、
//         centers (Point2DList)、rects (RectList)、count (Int)
//   参数：
//     threshold       : 匹配置信度阈值 [0,1]
//     max_count       : 最大匹配数量
//     angle_range     : 角度搜索范围 [a_min, a_max]（度）
//     angle_step      : 角度搜索步长（度，默认 5）
//     scale_range     : 缩放搜索范围 [s_min, s_max]
//     scale_step      : 缩放搜索步长（默认 0.05）
//     pyramid_levels  : 金字塔层数（0=不使用金字塔，默认 3，最大 4）
//     nms_overlap     : NMS IoU 阈值（0~1，默认 0.5）
//     subpixel        : 子像素精度 0=否 1=是（默认 1）
//     enable_binary   : 二值化预处理 0=否 1=是（默认 0）
//     binary_threshold: 二值化阈值 0-255（仅在 enable_binary=1 时有效）
//     template_roi    : 模板 ROI [cx,cy,w,h,angle] 或 [x,y,w,h]
//     search_roi      : 搜索区 ROI [x,y,w,h] 或 [cx,cy,w,h,angle]
//     search_circle_roi: 拟合圆搜索区 [cx,cy,radius]
//
#pragma once

#include "algo_base.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vz {

class TemplateMatchAlgo : public AlgoBase {
public:
    TemplateMatchAlgo() = default;
    ~TemplateMatchAlgo() override = default;

    int  setParam(const std::string& key, const std::string& value_json) override;
    int  setInput(const std::string& port_name, const void* data, int type_tag) override;
    int  process() override;
    int  getOutput(const std::string& port_name,
                   void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;

    static std::unique_ptr<AlgoBase> create();

private:
    // === 输入 ===
    cv::Mat image_;          // 必填
    cv::Mat template_;       // 可选
    cv::Mat region_mask_;    // 可选
    cv::Mat template_mask_;  // 可选：模板掩膜（255=有效, 0=排除）

    // === 参数 ===
    double threshold_       = 0.5;
    int    max_count_       = 1;
    double angle_min_       = -5.0, angle_max_ = 5.0;
    double angle_step_      = 5.0;       // 角度搜索步长（度）
    double scale_min_       = 0.95, scale_max_ = 1.05;
    double scale_step_      = 0.05;      // 尺度搜索步长
    int    pyramid_levels_  = 3;         // 金字塔层数
    double nms_overlap_     = 0.5;       // NMS IoU 阈值
    int    subpixel_        = 1;         // 子像素精度开关
    int    enable_binary_   = 0;         // 二值化预处理开关
    int    binary_threshold_ = 128;     // 二值化阈值
    int    refine_boundary_  = 1;       // 边缘检测精化边界 0=否 1=是（默认1）
    int    display_mode_     = 0;       // 结果图显示模式 0=检测结果 1=预处理灰度图 2=预处理二值图

    // 模板 ROI（中心 x,y + 宽高 + 角度）：当 template_ 端口未连线时
    bool   has_template_roi_ = false;
    double roi_x_ = 0, roi_y_ = 0, roi_w_ = 0, roi_h_ = 0, roi_angle_ = 0;

    // 搜索区 ROI
    bool   has_search_roi_ = false;
    double search_x_ = 0, search_y_ = 0, search_w_ = 0, search_h_ = 0;

    // 拟合圆搜索区
    bool   has_search_circle_roi_ = false;
    double scr_cx_ = 0, scr_cy_ = 0, scr_r_ = 0;

    // 绘制参数
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);
    int draw_thickness_ = 2;
    int point_size_ = 5;

    // === 输出 ===
    cv::Mat result_image_;
    cv::Mat debug_image_;     // 预处理后的图像（灰度/二值），供调试调参
    std::vector<VzMatchResult> matches_;
    std::vector<VzPoint2D>     centers_;
    std::vector<VzRect>        rects_;
    int    count_ = 0;
};

} // namespace vz
