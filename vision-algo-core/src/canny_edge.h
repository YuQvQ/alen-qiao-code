// canny_edge.h — Canny 边缘检测
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
namespace vz {
class CannyEdgeAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string&, void**, int*, int*) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    cv::Mat input_, output_;
    double low_ = 50.0, high_ = 150.0;
    int aperture_ = 3;
    int l2_ = 0;
};
} // namespace vz
