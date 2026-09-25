// image_math.cpp
#include "image_math.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::toCvMat; using detail::toVzImage;
int ImageMathAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "op") { op_ = parseJsonInt(v, 0); return VZ_OK; }
    if (k == "alpha") { alpha_ = parseJsonDouble(v, 0.5); return VZ_OK; }
    if (k == "beta") { beta_ = parseJsonDouble(v, 0.5); return VZ_OK; }
    if (k == "gamma") { gamma_ = parseJsonDouble(v, 0.0); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int ImageMathAlgo::setInput(const std::string& p, const void* d, int t) {
    if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH;
    if (p == "image_a") { a_ = toCvMat((const VzImage*)d); return VZ_OK; }
    if (p == "image_b") { b_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
static void matchSizeType(cv::Mat& a, cv::Mat& b) {
    if (a.size() != b.size()) cv::resize(b, b, a.size());
    if (a.channels() != b.channels()) {
        if (a.channels() == 1 && b.channels() == 3) cv::cvtColor(a, a, cv::COLOR_GRAY2BGR);
        else if (a.channels() == 3 && b.channels() == 1) cv::cvtColor(b, b, cv::COLOR_GRAY2BGR);
    }
}
int ImageMathAlgo::process() {
    if (a_.empty() || b_.empty()) { setLastError("missing image_a or image_b"); return VZ_ERR_PROCESS; }
    matchSizeType(a_, b_);
    switch (op_) {
        case 1: cv::subtract(a_, b_, output_); break;
        case 2: cv::absdiff(a_, b_, output_); break;
        case 3: cv::addWeighted(a_, alpha_, b_, beta_, gamma_, output_); break;
        default: cv::add(a_, b_, output_); break;
    }
    return VZ_OK;
}
int ImageMathAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void ImageMathAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* ImageMathAlgo::describe() const {
    return R"JSON({
  "type_id":"ImageMath","display_name":"图像运算","category":"几何",
  "algo_binding":{"dll":"vision_algo","algo_name":"ImageMath"},
  "inputs":[
    {"name":"image_a","type":"Image","required":true,"description":"图像A"},
    {"name":"image_b","type":"Image","required":true,"description":"图像B"}
  ],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"运算结果"}],
  "parameters":[
    {"name":"op","type":"Int","default":0,"description":"运算","enum":[[0,"相加 A+B"],[1,"相减 A-B"],[2,"绝对差 |A-B|"],[3,"加权混合 alpha*A+beta*B+gamma"]]},
    {"name":"alpha","type":"Double","default":0.5,"description":"A 权重(仅混合)"},
    {"name":"beta","type":"Double","default":0.5,"description":"B 权重(仅混合)"},
    {"name":"gamma","type":"Double","default":0.0,"description":"偏移(仅混合)"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> ImageMathAlgo::create() { return std::make_unique<ImageMathAlgo>(); }
} // namespace vz
