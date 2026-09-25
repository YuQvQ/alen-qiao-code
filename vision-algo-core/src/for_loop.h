// for_loop.h — For 循环控制节点
// 作用：标记一段流程需要循环执行 count 次；每次迭代输出当前 index(0..count-1)。
// 真正的循环执行由 C# 端 DagEngine 识别本节点后完成（多次跑整张图）。
// 本节点的 process() 仅在单节点运行时输出 index=0。
#pragma once
#include "algo_base.h"
namespace vz {
class ForLoopAlgo : public AlgoBase {
public:
    int setParam(const std::string&, const std::string&) override;
    int setInput(const std::string&, const void*, int) override;
    int process() override;
    int getOutput(const std::string&, void**, int*, int*) override;
    void releaseOutput(void*) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    int count_ = 1;
    int interval_ms_ = 0;
};
} // namespace vz
