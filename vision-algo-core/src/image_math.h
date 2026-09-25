// image_math.h — 两图像运算（加/减/绝对差/加权混合）
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
namespace vz {
class ImageMathAlgo : public AlgoBase {
public:
    int setParam(const std::string&, const std::string&) override;
    int setInput(const std::string&, const void*, int) override;
    int process() override;
    int getOutput(const std::string&, void**, int*, int*) override;
    void releaseOutput(void*) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    cv::Mat a_, b_, output_;
    int op_ = 0;          // 0=add 1=subtract 2=absdiff 3=blend
    double alpha_ = 0.5, beta_ = 0.5, gamma_ = 0.0;
};
} // namespace vz
