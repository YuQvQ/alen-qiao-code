// rectangle_detect.h — 矩形检测
// 输入：image (Image)
// 输出：
//   result_image : Image         叠加了检测矩形的结果图
//   corners      : Point2DList   矩形4个角点(顺时针:左上,右上,右下,左下)
//   center       : Pose2D        矩形中心+角度
//   width        : Double        矩形宽(像素)
//   height       : Double        矩形高(像素)
//   count        : Int           检测到的矩形数量
// 参数：
//   search_roi     : 搜索区 [cx,cy,w,h,angle]（可选）
//   min_area       : 最小面积(像素)
//   max_area_ratio : 最大面积占比(相对图像, 0-1)
//   approx_eps     : 多边形近似精度(占周长比例)
//   draw_color / draw_thickness / point_size
//
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <vector>

namespace vz {

class RectangleDetectAlgo : public AlgoBase {
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
    double min_area_ = 100.0;
    double max_area_ratio_ = 0.95;
    double approx_eps_ = 0.02;
    int    enable_binary_ = 1;
    double bin_threshold_ = 128.0;
    int    polarity_ = 0;   // 0=任意,1=亮目标,2=暗目标
    int    equalize_hist_ = 1;
    int    enable_blur_ = 1;
    int    gaussian_ksize_ = 3;

    // 搜索区 ROI
    bool   has_search_roi_ = false;
    double roi_cx_ = 0, roi_cy_ = 0, roi_w_ = 0, roi_h_ = 0, roi_angle_ = 0;

    // 绘制参数
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);
    int draw_thickness_ = 2;
    int point_size_ = 5;

    // 输出
    std::vector<VzPoint2D> out_corners_;
    VzPose2D out_center_{};
    double out_width_ = 0, out_height_ = 0;
    int out_count_ = 0;
    cv::Mat result_image_;
};

} // namespace vz
