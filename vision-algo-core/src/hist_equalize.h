// hist_equalize.h — 直方图均衡化
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
namespace vz {
class HistEqualizeAlgo : public AlgoBase {
public:
    int setParam(const std::string&, const std::string&) override { return VZ_OK; }
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string&, void**, int*, int*) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    cv::Mat input_, output_;
};
} // namespace vz
