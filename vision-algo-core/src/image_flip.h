// image_flip.h — 图像翻转
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
namespace vz {
class ImageFlipAlgo : public AlgoBase {
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
    int flip_code_ = 1; // 0=垂直(上下), 1=水平(左右), -1=两者
};
} // namespace vz
