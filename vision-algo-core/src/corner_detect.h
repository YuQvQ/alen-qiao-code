// corner_detect.h — 角点检测
// 输入：image
// 输出：points(Point2DList), count(Int), result_image
// 参数：method(0=Harris,1=Shi-Tomasi), max_corners, quality_level, min_distance, block_size, k
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <vector>

namespace vz {

class CornerDetectAlgo : public AlgoBase {
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
    int method_ = 1;          // 0=Harris, 1=Shi-Tomasi
    int max_corners_ = 100;
    double quality_level_ = 0.01;
    double min_distance_ = 10.0;
    int block_size_ = 3;
    double k_ = 0.04;
    cv::Scalar draw_color_ = cv::Scalar(0, 0, 255);
    int draw_thickness_ = 2;
    int point_size_ = 5;
    std::vector<VzPoint2D> out_points_;
    int out_count_ = 0;
    cv::Mat result_image_;
};

} // namespace vz
