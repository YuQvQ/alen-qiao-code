// convex_hull.h — 凸包(输入点集,输出凸包点集+结果图)
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <vector>
namespace vz {
class ConvexHullAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string& port_name, void**, int*, int*) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    std::vector<VzPoint2D> in_points_;
    cv::Mat input_image_;
    bool has_image_ = false;
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);
    int draw_thickness_ = 2;
    int point_size_ = 3;
    std::vector<VzPoint2D> out_hull_;
    double out_area_ = 0;
    cv::Mat result_image_;
};
} // namespace vz
