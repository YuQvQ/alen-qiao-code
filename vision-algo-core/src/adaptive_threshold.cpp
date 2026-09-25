// adaptive_threshold.cpp — 自适应阈值(光照不均时效果好)
#include "adaptive_threshold.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::toCvMat; using detail::toVzImage;
int AdaptiveThresholdAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "block_size") { block_size_ = parseJsonInt(v, 11); if (block_size_ < 3) block_size_ = 3; if (block_size_ % 2 == 0) block_size_++; return VZ_OK; }
    if (k == "c") { c_ = parseJsonDouble(v, 2.0); return VZ_OK; }
    if (k == "method") { method_ = parseJsonInt(v, 0); if (method_ < 0) method_ = 0; if (method_ > 1) method_ = 1; return VZ_OK; }
    if (k == "invert") { invert_ = parseJsonInt(v, 0) ? 1 : 0; return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int AdaptiveThresholdAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int AdaptiveThresholdAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Mat gray; int c = input_.channels();
    if (c == 1) gray = input_; else if (c == 3) cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    else if (c == 4) cv::cvtColor(input_, gray, cv::COLOR_BGRA2GRAY); else gray = input_;
    int m = (method_ == 0) ? cv::ADAPTIVE_THRESH_GAUSSIAN_C : cv::ADAPTIVE_THRESH_MEAN_C;
    int t = invert_ ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY;
    cv::adaptiveThreshold(gray, output_, 255, m, t, block_size_, c_);
    return VZ_OK;
}
int AdaptiveThresholdAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void AdaptiveThresholdAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* AdaptiveThresholdAlgo::describe() const {
    return R"JSON({
  "type_id":"AdaptiveThreshold","display_name":"自适应阈值","category":"预处理",
  "algo_binding":{"dll":"vision_algo","algo_name":"AdaptiveThreshold"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"二值图"}],
  "parameters":[
    {"name":"block_size","type":"Int","default":11,"description":"邻域块大小(奇数,越大越平滑)"},
    {"name":"c","type":"Double","default":2.0,"description":"从均值中减去的常数"},
    {"name":"method","type":"Int","default":0,"description":"方法","enum":[[0,"高斯加权"],[1,"均值"]]},
    {"name":"invert","type":"Int","default":0,"description":"反相:0=否,1=是","enum":[[0,"否"],[1,"是"]]}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> AdaptiveThresholdAlgo::create() { return std::make_unique<AdaptiveThresholdAlgo>(); }
} // namespace vz
