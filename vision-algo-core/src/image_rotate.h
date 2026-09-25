// image_rotate.h — 图像旋转(任意角度,可扩展画布)
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
namespace vz {
class ImageRotateAlgo : public AlgoBase {
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
    double angle_ = 0.0;
    double scale_ = 1.0;
    int expand_ = 1; // 1=扩展画布完整显示, 0=保持原尺寸
};
} // namespace vz
