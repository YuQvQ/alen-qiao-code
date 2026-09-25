// distance_transform.h — 距离变换（像素到最近零值像素的距离，归一化显示）
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
namespace vz {
class DistanceTransformAlgo : public AlgoBase {
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
    int mask_size_ = 5;
    double max_dist_ = 0;
};
} // namespace vz
