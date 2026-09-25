// bilateral_filter.h — 双边滤波（保边去噪）
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
namespace vz {
class BilateralFilterAlgo : public AlgoBase {
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
    int d_ = 9;
    double sigma_color_ = 75.0, sigma_space_ = 75.0;
};
} // namespace vz
