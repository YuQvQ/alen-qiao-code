// adaptive_threshold.h — 自适应阈值
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
namespace vz {
class AdaptiveThresholdAlgo : public AlgoBase {
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
    int block_size_ = 11;
    double c_ = 2.0;
    int method_ = 0; // 0=高斯均值, 1=均值
    int invert_ = 0;
};
} // namespace vz
