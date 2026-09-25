// counter.cpp — 计数器
#include "counter.h"
#include "algo_utils.h"
namespace vz {
using detail::parseJsonInt;
int CounterAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "initial") { initial_ = (long long)parseJsonInt(v, 0); count_ = initial_; return VZ_OK; }
    if (k == "step") { step_ = (long long)parseJsonInt(v, 1); if (step_ == 0) step_ = 1; return VZ_OK; }
    if (k == "reset") { count_ = initial_; return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int CounterAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "reset") {
        if (t == VZ_TYPE_INT) reset_in_ = d ? *(const int*)d : 0;
        else if (t == VZ_TYPE_BOOL) reset_in_ = d ? *(const int*)d : 0;
        else if (t == VZ_TYPE_DOUBLE) reset_in_ = (d && *(const double*)d != 0) ? 1 : 0;
        else { setLastError("input reset expects Int/Bool/Double"); return VZ_ERR_TYPE_MISMATCH; }
        has_reset_ = true; return VZ_OK;
    }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int CounterAlgo::process() {
    if (has_reset_ && reset_in_ != 0) count_ = initial_;
    else count_ += step_;
    has_reset_ = false;
    return VZ_OK;
}
int CounterAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "count") { auto* x = (int*)std::malloc(sizeof(int)); *x = (int)count_; *o = x; *n = 1; *tt = VZ_TYPE_INT; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void CounterAlgo::releaseOutput(void* p) { if (p) std::free(p); }
const char* CounterAlgo::describe() const {
    return R"JSON({
  "type_id":"Counter","display_name":"计数器","category":"逻辑",
  "algo_binding":{"dll":"vision_algo","algo_name":"Counter"},
  "inputs":[{"name":"reset","type":"Int","required":false,"description":"复位信号(非零则复位到初始值)"}],
  "outputs":[{"name":"count","type":"Int","required":true,"description":"当前计数值"}],
  "parameters":[
    {"name":"initial","type":"Int","default":0,"description":"初始值"},
    {"name":"step","type":"Int","default":1,"description":"每次运行的步长"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> CounterAlgo::create() { return std::make_unique<CounterAlgo>(); }
} // namespace vz
