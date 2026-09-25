// image_resize.h — 图像缩放
// 输入：image
// 输出：image
// 参数：scale(缩放比例), width(目标宽,0=用scale), height(目标高,0=用scale), interpolation(0=最近邻,1=线性,2=立方,3=面积,4=Lanczos)
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>

namespace vz {

class ImageResizeAlgo : public AlgoBase {
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
    cv::Mat output_;
    double scale_ = 1.0;
    int width_ = 0;
    int height_ = 0;
    int interpolation_ = 1;
};

} // namespace vz
