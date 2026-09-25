// gaussian_blur.cpp — 高斯模糊
#include "gaussian_blur.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::toCvMat; using detail::toVzImage;
int GaussianBlurAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "ksize") { ksize_ = parseJsonInt(v, 5); if (ksize_ < 1) ksize_ = 1; if (ksize_ % 2 == 0) ksize_++; return VZ_OK; }
    if (k == "sigma") { sigma_ = parseJsonDouble(v, 0.0); if (sigma_ < 0) sigma_ = 0; return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int GaussianBlurAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int GaussianBlurAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::GaussianBlur(input_, output_, cv::Size(ksize_, ksize_), sigma_, sigma_);
    return VZ_OK;
}
int GaussianBlurAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void GaussianBlurAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* GaussianBlurAlgo::describe() const {
    return R"JSON({
  "type_id":"GaussianBlur","display_name":"高斯模糊","category":"预处理",
  "algo_binding":{"dll":"vision_algo","algo_name":"GaussianBlur"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"模糊后图像"}],
  "parameters":[
    {"name":"ksize","type":"Int","default":5,"description":"核大小(奇数,越大越模糊)"},
    {"name":"sigma","type":"Double","default":0.0,"description":"高斯标准差(0=自动由核大小计算)"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> GaussianBlurAlgo::create() { return std::make_unique<GaussianBlurAlgo>(); }
} // namespace vz
