// math_func.h — 数学函数(单目)
// 输入：x (Double)
// 输出：result (Double)
// 参数：func
#pragma once
#include "algo_base.h"
namespace vz {
class MathFuncAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string&, void**, int*, int*) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    double x_ = 0.0, result_ = 0.0;
    bool has_x_ = false;
    int func_ = 0; // 见枚举
    double clamp_lo_ = 0.0, clamp_hi_ = 1.0;
};
} // namespace vz
