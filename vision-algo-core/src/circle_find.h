// circle_find.h — 圆查找算法（基于边缘检测，通过黑白交接找圆）
//
// 输入：image (Image)
// 输出：
//   point        : Pose2D  找到的圆心（angle=0）
//   radius       : Double  圆半径
//   count        : Int     找到的圆数量（0 则 NG）
//   result_image : Image   叠加检测结果的原
// 参数：
//   search_roi       : [cx, cy, r] 圆形搜索区，留空搜索整图
//   target_radius    : 目标半径（像素），告诉算法要找多大的圆
//   radius_tolerance : 半径容差比例 0.0~1.0（默认 0.2 = ±20%）
//   polarity         : 边缘极性 0=任意 1=亮圆暗底 2=暗圆亮底
//   edge_threshold   : 二值化阈值 0-255（区分亮/暗区域）
//   max_count        : 最多找几个圆（默认 1）
//   blur_ksize       : 高斯模糊核大小（奇数，0=不滤波）
//   dp / min_dist / param1 / param2 : HoughCircles 高级参数
//
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <vector>

namespace vz {

class CircleFindAlgo : public AlgoBase {
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

    // 用户参数
    double target_radius_   = 0;     // 目标半径（像素），0=自动
    double radius_tolerance_ = 0.2;  // 半径容差比例
    int    polarity_        = 0;     // 0=任意 1=亮圆暗底 2=暗圆亮底
    int    edge_threshold_  = 128;   // 二值化阈值
    int    enable_binary_   = 1;     // 二值化开关（0=否,1=是）；polarity 非0时若关闭则直接用灰度图做霍夫圆
    int    max_count_       = 1;     // 最多找几个圆
    int    enable_blur_     = 1;     // 高斯模糊开关 0=否 1=是
    int    blur_ksize_      = 5;     // 高斯模糊核大小

    // HoughCircles 高级参数
    double dp_       = 1.0;
    double min_dist_ = 20.0;
    double param1_   = 100.0;   // Canny 高阈值
    double param2_   = 30.0;    // 累加器阈值

    // 圆形搜索区 [cx, cy, r]
    bool   has_search_roi_ = false;
    double roi_cx_ = 0, roi_cy_ = 0, roi_r_ = 0;

    // 绘制参数（0 = 自动按图像尺寸计算）
    cv::Scalar draw_color_   = cv::Scalar(0, 255, 0);
    cv::Scalar center_color_ = cv::Scalar(0, 0, 255);
    cv::Scalar roi_color_    = cv::Scalar(255, 0, 255);
    int draw_thickness_ = 0;
    int point_size_ = 0;

    // 标签：在找到的圆旁绘制文字（如半径）
    int enable_label_ = 1;             // 0=否 1=是
    cv::Scalar label_color_ = cv::Scalar(0, 255, 255);  // 标签颜色(默认黄)

    // 精修：用边缘点最小二乘拟合优化霍夫结果
    int    enable_refine_ = 1;   // 0=只用霍夫, 1=最小二乘精修
    double fit_threshold_ = 0;   // 内点距离阈值(像素),0=自动(半径的2%)
    double min_score_     = 0.2; // 最低得分(内点比例),低于此值判未找到(防止把噪声拟合成圆)

    // 输出
    VzPose2D out_point_{};
    double   out_radius_ = 0;
    int      out_count_ = 0;
    double   out_score_ = 0;     // 得分: 内点比例 0~1
    double   out_fit_error_ = 0; // 拟合误差: 内点 RMS(像素)
    cv::Mat  result_image_;
};

} // namespace vz
