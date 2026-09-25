// accumulator.cpp — 累加器
#include "accumulator.h"
#include "algo_utils.h"
namespace vz {
using detail::parseJsonInt;
int AccumulatorAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "reset") { sum_ = 0.0; count_ = 0; return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int AccumulatorAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "value") {
        if (t == VZ_TYPE_INT) value_in_ = d ? *(const int*)d : 0;
        else if (t == VZ_TYPE_DOUBLE) value_in_ = d ? *(const double*)d : 0;
        else { setLastError("input value expects Int/Double"); return VZ_ERR_TYPE_MISMATCH; }
        has_value_ = true; return VZ_OK;
    }
    if (p == "reset") {
        if (t == VZ_TYPE_INT) reset_in_ = d ? *(const int*)d : 0;
        else if (t == VZ_TYPE_BOOL) reset_in_ = d ? *(const int*)d : 0;
        else if (t == VZ_TYPE_DOUBLE) reset_in_ = (d && *(const double*)d != 0) ? 1 : 0;
        else { setLastError("input reset expects Int/Bool/Double"); return VZ_ERR_TYPE_MISMATCH; }
        has_reset_ = true; return VZ_OK;
    }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int AccumulatorAlgo::process() {
    if (has_reset_ && reset_in_ != 0) { sum_ = 0.0; count_ = 0; }
    if (has_value_) { sum_ += value_in_; count_++; }
    has_value_ = false; has_reset_ = false;
    return VZ_OK;
}
int AccumulatorAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "sum") { auto* x = (double*)std::malloc(sizeof(double)); *x = sum_; *o = x; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    if (p == "count") { auto* x = (int*)std::malloc(sizeof(int)); *x = (int)count_; *o = x; *n = 1; *tt = VZ_TYPE_INT; return VZ_OK; }
    if (p == "average") { auto* x = (double*)std::malloc(sizeof(double)); *x = count_ > 0 ? sum_ / count_ : 0.0; *o = x; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void AccumulatorAlgo::releaseOutput(void* p) { if (p) std::free(p); }
const char* AccumulatorAlgo::describe() const {
    return R"JSON({
  "type_id":"Accumulator","display_name":"累加器","category":"逻辑",
  "algo_binding":{"dll":"vision_algo","algo_name":"Accumulator"},
  "inputs":[
    {"name":"value","type":"Double","required":false,"description":"待累加的值"},
    {"name":"reset","type":"Int","required":false,"description":"复位(非零清零)"}
  ],
  "outputs":[
    {"name":"sum","type":"Double","required":true,"description":"累加和"},
    {"name":"count","type":"Int","required":true,"description":"累加次数"},
    {"name":"average","type":"Double","required":true,"description":"平均值"}
  ],
  "parameters":[]
})JSON";
}
std::unique_ptr<AlgoBase> AccumulatorAlgo::create() { return std::make_unique<AccumulatorAlgo>(); }
} // namespace vz
