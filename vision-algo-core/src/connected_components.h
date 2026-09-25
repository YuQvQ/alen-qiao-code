// connected_components.h — 连通域统计（数量+标注图）
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
namespace vz {
class ConnectedComponentsAlgo : public AlgoBase {
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
    int count_ = 0;
};
} // namespace vz
