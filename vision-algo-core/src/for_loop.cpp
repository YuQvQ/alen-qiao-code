// for_loop.cpp — For 循环控制节点（循环执行由 C# DagEngine 完成）
#include "for_loop.h"
#include "algo_utils.h"
namespace vz {
using detail::parseJsonInt;
int ForLoopAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "count") { count_ = parseJsonInt(v, 1); if (count_ < 1) count_ = 1; return VZ_OK; }
    if (k == "interval_ms") { interval_ms_ = parseJsonInt(v, 0); if (interval_ms_ < 0) interval_ms_ = 0; return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int ForLoopAlgo::setInput(const std::string&, const void*, int) { return VZ_OK; }
int ForLoopAlgo::process() { return VZ_OK; }
int ForLoopAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "index") { auto* x = (int*)std::malloc(sizeof(int)); *x = 0; *o = x; *n = 1; *tt = VZ_TYPE_INT; return VZ_OK; }
    if (p == "count") { auto* x = (int*)std::malloc(sizeof(int)); *x = count_; *o = x; *n = 1; *tt = VZ_TYPE_INT; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void ForLoopAlgo::releaseOutput(void* p) { if (p) std::free(p); }
const char* ForLoopAlgo::describe() const {
    return R"JSON({
  "type_id":"ForLoop","display_name":"For循环","category":"逻辑",
  "algo_binding":{"dll":"vision_algo","algo_name":"ForLoop"},
  "inputs":[],
  "outputs":[
    {"name":"index","type":"Int","required":true,"description":"当前迭代序号(0..count-1)"},
    {"name":"count","type":"Int","required":true,"description":"总循环次数"}
  ],
  "parameters":[
    {"name":"count","type":"Int","default":1,"description":"循环次数(>=1)"},
    {"name":"interval_ms","type":"Int","default":0,"description":"每次迭代间隔(毫秒),用于定时连续采集"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> ForLoopAlgo::create() { return std::make_unique<ForLoopAlgo>(); }
} // namespace vz
