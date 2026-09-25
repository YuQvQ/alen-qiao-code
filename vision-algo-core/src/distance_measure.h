// distance_measure.h — 距离测量
// 输入：a(Pose2D), b(Pose2D), image(可选,用于绘制)
// 输出：result_image(Image), distance(Double)
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>

namespace vz {

class DistanceMeasureAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    double ax_=0, ay_=0, bx_=0, by_=0;
    bool has_a_=false, has_b_=false;
    cv::Mat input_image_;
    bool has_image_ = false;
    cv::Mat result_image_;
    double out_distance_=0;
    // 绘制参数
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);
    int draw_thickness_ = 2;
    int point_size_ = 6;
};

} // namespace vz
