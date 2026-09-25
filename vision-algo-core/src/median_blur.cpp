// median_blur.cpp — 中值滤波(去椒盐噪声)
#include "median_blur.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::parseJsonInt; using detail::toCvMat; using detail::toVzImage;
int MedianBlurAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "ksize") { ksize_ = parseJsonInt(v, 5); if (ksize_ < 1) ksize_ = 1; if (ksize_ % 2 == 0) ksize_++; return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int MedianBlurAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int MedianBlurAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::medianBlur(input_, output_, ksize_);
    return VZ_OK;
}
int MedianBlurAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void MedianBlurAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* MedianBlurAlgo::describe() const {
    return R"JSON({
  "type_id":"MedianBlur","display_name":"中值滤波","category":"预处理",
  "algo_binding":{"dll":"vision_algo","algo_name":"MedianBlur"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"滤波后图像"}],
  "parameters":[{"name":"ksize","type":"Int","default":5,"description":"核大小(奇数,越大去噪越强)"}]
})JSON";
}
std::unique_ptr<AlgoBase> MedianBlurAlgo::create() { return std::make_unique<MedianBlurAlgo>(); }
} // namespace vz
