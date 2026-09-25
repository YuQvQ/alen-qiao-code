// image_display.h — 图像显示节点（透传）
// 输入：image
// 输出：image（透传，便于在 UI 图像窗口中查看上游图像）
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>

namespace vz {

class ImageDisplayAlgo : public AlgoBase {
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
};

} // namespace vz