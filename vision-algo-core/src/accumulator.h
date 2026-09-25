// accumulator.h — 累加器(跨次运行累加输入值)
// 输入：value (Double), reset (Int/Bool 可选)
// 输出：sum (Double), count (Int)
#pragma once
#include "algo_base.h"
namespace vz {
class AccumulatorAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string&, void**, int*, int*) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    double sum_ = 0.0;
    long long count_ = 0;
    double value_in_ = 0.0;
    bool has_value_ = false;
    int reset_in_ = 0;
    bool has_reset_ = false;
};
} // namespace vz
