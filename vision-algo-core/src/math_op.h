// math_op.h — 数学运算(双目)
// 输入：a (Double), b (Double)
// 输出：result (Double)
// 参数：op (运算符)
#pragma once
#include "algo_base.h"
namespace vz {
class MathOpAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string&, void**, int*, int*) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    double a_ = 0.0, b_ = 0.0, result_ = 0.0;
    bool has_a_ = false, has_b_ = false;
    int op_ = 0; // 0=+,1=-,2=*,3=/,4=%,5=pow,6=min,7=max
};
} // namespace vz
