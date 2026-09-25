// min_max_loc.h — 查找图像最亮/最暗点位置
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
namespace vz {
class MinMaxLocAlgo : public AlgoBase {
public:
    int setParam(const std::string&, const std::string&) override;
    int setInput(const std::string&, const void*, int) override;
    int process() override;
    int getOutput(const std::string&, void**, int*, int*) override;
    void releaseOutput(void*) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    cv::Mat input_, result_;
    double minVal_ = 0, maxVal_ = 0;
    cv::Point minLoc_, maxLoc_;
    int point_size_ = 8;
    cv::Scalar max_color_ = cv::Scalar(0, 0, 255);
    cv::Scalar min_color_ = cv::Scalar(255, 0, 0);
    bool has_result_ = false;
};
} // namespace vz
