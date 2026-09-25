// edge_finder.h — 沿扫描线亚像素边缘查找
// 输入：image, scan_roi(可选)
// 输出：point(Pose2D), direction(Int), score(Double)
// 参数：scan_roi, polarity, threshold, subpixel
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>

namespace vz {

class EdgeFinderAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    cv::Mat input_;
    int polarity_ = 0;        // 0=任意,1=白到黑,2=黑到白
    double threshold_ = 30.0; // 边缘梯度阈值
    int subpixel_ = 1;        // 亚像素精度 0=否,1=是
    double roi_cx_=0, roi_cy_=0, roi_w_=0, roi_h_=0, roi_angle_=0;
    bool has_roi_ = false;
    double out_x_=0, out_y_=0, out_angle_=0, out_score_=0;
    int out_dir_ = 0;
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);
    int draw_thickness_ = 2;
    int point_size_ = 6;
    cv::Mat result_image_;
};

} // namespace vz
