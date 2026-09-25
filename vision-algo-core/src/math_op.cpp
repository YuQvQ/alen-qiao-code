// math_op.cpp — 数学运算(双目)
#include "math_op.h"
#include "algo_utils.h"
#include <cmath>
#include <algorithm>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble;
int MathOpAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "op") { op_ = parseJsonInt(v, 0); if (op_ < 0) op_ = 0; if (op_ > 7) op_ = 7; return VZ_OK; }
    if (k == "a") { a_ = parseJsonDouble(v, 0.0); has_a_ = true; return VZ_OK; }
    if (k == "b") { b_ = parseJsonDouble(v, 0.0); has_b_ = true; return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int MathOpAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "a") {
        if (t == VZ_TYPE_INT) a_ = d ? *(const int*)d : 0;
        else if (t == VZ_TYPE_DOUBLE) a_ = d ? *(const double*)d : 0;
        else { setLastError("input a expects Int/Double"); return VZ_ERR_TYPE_MISMATCH; }
        has_a_ = true; return VZ_OK;
    }
    if (p == "b") {
        if (t == VZ_TYPE_INT) b_ = d ? *(const int*)d : 0;
        else if (t == VZ_TYPE_DOUBLE) b_ = d ? *(const double*)d : 0;
        else { setLastError("input b expects Int/Double"); return VZ_ERR_TYPE_MISMATCH; }
        has_b_ = true; return VZ_OK;
    }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int MathOpAlgo::process() {
    if (!has_a_ || !has_b_) { setLastError("inputs a and b required"); return VZ_ERR_PROCESS; }
    switch (op_) {
        case 0: result_ = a_ + b_; break;
        case 1: result_ = a_ - b_; break;
        case 2: result_ = a_ * b_; break;
        case 3: result_ = (std::abs(b_) < 1e-12) ? 0.0 : a_ / b_; break;
        case 4: result_ = (std::abs(b_) < 1e-12) ? 0.0 : std::fmod(a_, b_); break;
        case 5: result_ = std::pow(a_, b_); break;
        case 6: result_ = std::min(a_, b_); break;
        case 7: result_ = std::max(a_, b_); break;
        default: result_ = 0;
    }
    return VZ_OK;
}
int MathOpAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "result") { auto* x = (double*)std::malloc(sizeof(double)); *x = result_; *o = x; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void MathOpAlgo::releaseOutput(void* p) { if (p) std::free(p); }
const char* MathOpAlgo::describe() const {
    return R"JSON({
  "type_id":"MathOp","display_name":"数学运算","category":"数学",
  "algo_binding":{"dll":"vision_algo","algo_name":"MathOp"},
  "inputs":[
    {"name":"a","type":"Double","required":false,"description":"操作数A(可由参数提供)"},
    {"name":"b","type":"Double","required":false,"description":"操作数B(可由参数提供)"}
  ],
  "outputs":[{"name":"result","type":"Double","required":true,"description":"运算结果"}],
  "parameters":[
    {"name":"op","type":"Int","default":0,"description":"运算符","enum":[[0,"a+b 加"],[1,"a-b 减"],[2,"a*b 乘"],[3,"a/b 除"],[4,"a%b 取余"],[5,"a^b 幂"],[6,"min(a,b)"],[7,"max(a,b)"]]},
    {"name":"a","type":"Double","default":0.0,"description":"操作数A"},
    {"name":"b","type":"Double","default":0.0,"description":"操作数B"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> MathOpAlgo::create() { return std::make_unique<MathOpAlgo>(); }
} // namespace vz
