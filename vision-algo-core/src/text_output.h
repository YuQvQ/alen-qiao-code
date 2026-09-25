// text_output.h — 文本输出节点
//
// 输入：value (String) — 上游传入的文本
// 输出：text (String) — 拼接 prefix + value + suffix 后的文本
// 参数：prefix, suffix
//
#pragma once
#include "algo_base.h"
#include <string>

namespace vz {

class TextOutputAlgo : public AlgoBase {
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
    std::string input_value_;
    std::string prefix_;
    std::string suffix_;
    std::string output_text_;
};

} // namespace vz
