// angle_measure.h — 角度测量
// 输入：angle_a(Double), angle_b(Double)
// 输出：angle(Double) - 夹角(度)
#pragma once
#include "algo_base.h"

namespace vz {

class AngleMeasureAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    double a_=0, b_=0;
    bool has_a_=false, has_b_=false;
    double out_angle_=0;
};

} // namespace vz
