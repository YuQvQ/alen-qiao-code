// counter.h — 计数器(每次运行自增,可复位)
// 输入：reset (Int/Bool, 可选, 非零时复位到initial)
// 输出：count (Int)
// 参数：initial(初始值), step(步长)
#pragma once
#include "algo_base.h"
namespace vz {
class CounterAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string&, void**, int*, int*) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    long long count_ = 0;
    long long initial_ = 0;
    long long step_ = 1;
    int reset_in_ = 0;
    bool has_reset_ = false;
};
} // namespace vz
