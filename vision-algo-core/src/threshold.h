// threshold.h — 全局阈值二值化
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
namespace vz {
class ThresholdAlgo : public AlgoBase {
public:
    int setParam(const std::string&, const std::string&) override;
    int setInput(const std::string&, const void*, int) override;
    int process() override;
    int getOutput(const std::string&, void**, int*, int*) override;
    void releaseOutput(void*) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    cv::Mat input_, output_;
    double thresh_ = 128.0, maxval_ = 255.0;
    int type_ = 0;      // 0=Binary 1=BinaryInv 2=Trunc 3=ToZero 4=Otsu
};
} // namespace vz
