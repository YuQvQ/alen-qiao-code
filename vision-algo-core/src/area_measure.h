// area_measure.h — 面积/周长测量(输入点集构成的多边形)
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <vector>
namespace vz {
class AreaMeasureAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string& port_name, void**, int*, int*) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    std::vector<VzPoint2D> points_;
    double out_area_ = 0, out_perimeter_ = 0;
};
} // namespace vz
