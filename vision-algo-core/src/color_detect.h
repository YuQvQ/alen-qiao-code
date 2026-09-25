// color_detect.h — 颜色检测(HSV阈值分割,找最大色块)
// 输入：image (Image)
// 输出：result_image, center (Pose2D), area (Double), count (Int)
// 参数：h_min,h_max,s_min,s_max,v_min,v_max, min_area, draw_color...
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
namespace vz {
class ColorDetectAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string&, void**, int*, int*) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    cv::Mat input_;
    int h_min_ = 0, h_max_ = 10, s_min_ = 100, s_max_ = 255, v_min_ = 100, v_max_ = 255;
    double min_area_ = 100.0;
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);
    int draw_thickness_ = 2;
    int point_size_ = 5;
    VzPose2D out_center_{};
    double out_area_ = 0;
    int out_count_ = 0;
    cv::Mat result_image_;
};
} // namespace vz
