// if_condition.h — 条件判断节点（IF）
//
// 输入：a (Int/Double), b (Int/Double) — 两个比较操作数
// 参数：op (Int) — 比较运算符：0=>, 1=>=, 2=<, 3=<=, 4===, 5=!=
// 输出：result (Int) — 1 表示条件成立，0 表示不成立
//
#pragma once
#include "algo_base.h"

namespace vz {

class IfConditionAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string& port_name,
                  void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;

    static std::unique_ptr<AlgoBase> create();

private:
    double a_ = 0.0;
    double b_ = 0.0;
    bool has_a_ = false;
    bool has_b_ = false;
    int op_ = 0;  // 0=>, 1=>=, 2=<, 3=<=, 4===, 5=!=
    int result_ = 0;
};

} // namespace vz
