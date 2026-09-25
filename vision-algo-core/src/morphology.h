// morphology.h — 形态学操作
// 输入：image
// 输出：image
// 参数：operation(0=腐蚀,1=膨胀,2=开,3=闭,4=梯度,5=顶帽,6=黑帽), kernel_size, iterations, kernel_shape(0=矩形,1=十字,2=椭圆)
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>

namespace vz {

class MorphologyAlgo : public AlgoBase {
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
    int operation_ = 0;   // 0=erode,1=dilate,2=open,3=close,4=gradient,5=tophat,6=blackhat
    int kernel_size_ = 3;
    int iterations_ = 1;
    int kernel_shape_ = 0; // 0=rect,1=cross,2=ellipse
};

} // namespace vz
