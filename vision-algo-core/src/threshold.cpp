// threshold.cpp — 全局阈值二值化
#include "threshold.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::toCvMat; using detail::toVzImage;
int ThresholdAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "thresh") { thresh_ = parseJsonDouble(v, 128.0); return VZ_OK; }
    if (k == "maxval") { maxval_ = parseJsonDouble(v, 255.0); return VZ_OK; }
    if (k == "type") { type_ = parseJsonInt(v, 0); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int ThresholdAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int ThresholdAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Mat gray; if (input_.channels() == 1) gray = input_; else cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    int flags;
    switch (type_) {
        case 1: flags = cv::THRESH_BINARY_INV; break;
        case 2: flags = cv::THRESH_TRUNC; break;
        case 3: flags = cv::THRESH_TOZERO; break;
        case 4: flags = cv::THRESH_BINARY | cv::THRESH_OTSU; break;
        default: flags = cv::THRESH_BINARY; break;
    }
    cv::threshold(gray, output_, thresh_, maxval_, flags);
    return VZ_OK;
}
int ThresholdAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void ThresholdAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* ThresholdAlgo::describe() const {
    return R"JSON({
  "type_id":"Threshold","display_name":"阈值二值化","category":"预处理",
  "algo_binding":{"dll":"vision_algo","algo_name":"Threshold"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"二值图"}],
  "parameters":[
    {"name":"thresh","type":"Double","default":128.0,"description":"阈值"},
    {"name":"maxval","type":"Double","default":255.0,"description":"最大值"},
    {"name":"type","type":"Int","default":0,"description":"类型","enum":[[0,"Binary"],[1,"BinaryInv"],[2,"Trunc"],[3,"ToZero"],[4,"Otsu自动"]]}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> ThresholdAlgo::create() { return std::make_unique<ThresholdAlgo>(); }
} // namespace vz
