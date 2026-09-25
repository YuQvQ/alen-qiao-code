// pixel_stats.h — 区域灰度统计
// 输入：image
// 输出：mean(Double), stddev(Double), min_val(Double), max_val(Double), count(Int)
// 参数：roi(可选矩形[x,y,w,h])
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>

namespace vz {

class PixelStatsAlgo : public AlgoBase {
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
    double roi_x_=0, roi_y_=0, roi_w_=0, roi_h_=0;
    bool has_roi_=false;
    double out_mean_=0, out_stddev_=0, out_min_=0, out_max_=0;
    int out_count_=0;
    cv::Scalar draw_color_ = cv::Scalar(255, 0, 255);
    int draw_thickness_ = 2;
    cv::Mat result_image_;
};

} // namespace vz
