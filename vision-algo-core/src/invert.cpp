// invert.cpp — 图像反色
#include "invert.h"
#include "algo_utils.h"
#include <opencv2/core.hpp>
namespace vz {
using detail::toCvMat; using detail::toVzImage;
int InvertAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int InvertAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::bitwise_not(input_, output_);
    return VZ_OK;
}
int InvertAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void InvertAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* InvertAlgo::describe() const {
    return R"JSON({
  "type_id":"Invert","display_name":"图像反色","category":"预处理",
  "algo_binding":{"dll":"vision_algo","algo_name":"Invert"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"反色后图像(255-原值)"}],
  "parameters":[]
})JSON";
}
std::unique_ptr<AlgoBase> InvertAlgo::create() { return std::make_unique<InvertAlgo>(); }
} // namespace vz
