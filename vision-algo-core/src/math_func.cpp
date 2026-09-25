// math_func.cpp — 数学函数(单目)
#include "math_func.h"
#include "algo_utils.h"
#include <cmath>
#include <algorithm>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble;
int MathFuncAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "func") { func_ = parseJsonInt(v, 0); if (func_ < 0) func_ = 0; if (func_ > 13) func_ = 13; return VZ_OK; }
    if (k == "x") { x_ = parseJsonDouble(v, 0.0); has_x_ = true; return VZ_OK; }
    if (k == "clamp_lo") { clamp_lo_ = parseJsonDouble(v, 0.0); return VZ_OK; }
    if (k == "clamp_hi") { clamp_hi_ = parseJsonDouble(v, 1.0); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int MathFuncAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "x") {
        if (t == VZ_TYPE_INT) x_ = d ? *(const int*)d : 0;
        else if (t == VZ_TYPE_DOUBLE) x_ = d ? *(const double*)d : 0;
        else { setLastError("input x expects Int/Double"); return VZ_ERR_TYPE_MISMATCH; }
        has_x_ = true; return VZ_OK;
    }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int MathFuncAlgo::process() {
    if (!has_x_) { setLastError("input x required"); return VZ_ERR_PROCESS; }
    switch (func_) {
        case 0: result_ = x_; break;                       // 恒等
        case 1: result_ = -x_; break;                      // 取反
        case 2: result_ = std::abs(x_); break;             // 绝对值
        case 3: result_ = std::sqrt(std::max(0.0, x_)); break; // 开方
        case 4: result_ = x_ * x_; break;                  // 平方
        case 5: result_ = std::sin(x_); break;             // sin(弧度)
        case 6: result_ = std::cos(x_); break;             // cos
        case 7: result_ = std::tan(x_); break;             // tan
        case 8: result_ = (x_ != 0) ? std::log(std::abs(x_)) : 0; break; // ln
        case 9: result_ = std::exp(x_); break;             // e^x
        case 10: result_ = std::round(x_); break;          // 四舍五入
        case 11: result_ = std::floor(x_); break;          // 向下取整
        case 12: result_ = std::ceil(x_); break;           // 向上取整
        case 13: result_ = std::max(clamp_lo_, std::min(clamp_hi_, x_)); break; // 限幅
        default: result_ = x_;
    }
    return VZ_OK;
}
int MathFuncAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "result") { auto* x = (double*)std::malloc(sizeof(double)); *x = result_; *o = x; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void MathFuncAlgo::releaseOutput(void* p) { if (p) std::free(p); }
const char* MathFuncAlgo::describe() const {
    return R"JSON({
  "type_id":"MathFunc","display_name":"数学函数","category":"数学",
  "algo_binding":{"dll":"vision_algo","algo_name":"MathFunc"},
  "inputs":[{"name":"x","type":"Double","required":false,"description":"输入值(可由参数提供)"}],
  "outputs":[{"name":"result","type":"Double","required":true,"description":"函数结果"}],
  "parameters":[
    {"name":"func","type":"Int","default":0,"description":"函数","enum":[[0,"恒等 x"],[1,"取反 -x"],[2,"绝对值"],[3,"开方 sqrt"],[4,"平方 x²"],[5,"sin"],[6,"cos"],[7,"tan"],[8,"ln"],[9,"e^x"],[10,"四舍五入"],[11,"向下取整"],[12,"向上取整"],[13,"限幅 clamp"]]},
    {"name":"x","type":"Double","default":0.0,"description":"输入值"},
    {"name":"clamp_lo","type":"Double","default":0.0,"description":"限幅下限(func=13时)"},
    {"name":"clamp_hi","type":"Double","default":1.0,"description":"限幅上限(func=13时)"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> MathFuncAlgo::create() { return std::make_unique<MathFuncAlgo>(); }
} // namespace vz
