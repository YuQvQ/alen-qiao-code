// image_flip.cpp — 图像翻转
#include "image_flip.h"
#include "algo_utils.h"
#include <opencv2/core.hpp>
namespace vz {
using detail::parseJsonInt; using detail::toCvMat; using detail::toVzImage;
int ImageFlipAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "flip_code") { flip_code_ = parseJsonInt(v, 1); if (flip_code_ < -1) flip_code_ = -1; if (flip_code_ > 1) flip_code_ = 1; return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int ImageFlipAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int ImageFlipAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::flip(input_, output_, flip_code_);
    return VZ_OK;
}
int ImageFlipAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void ImageFlipAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* ImageFlipAlgo::describe() const {
    return R"JSON({
  "type_id":"ImageFlip","display_name":"图像翻转","category":"几何",
  "algo_binding":{"dll":"vision_algo","algo_name":"ImageFlip"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"翻转后图像"}],
  "parameters":[{"name":"flip_code","type":"Int","default":1,"description":"翻转方向","enum":[[0,"垂直(上下)"],[1,"水平(左右)"],[-1,"水平+垂直"]]}]
})JSON";
}
std::unique_ptr<AlgoBase> ImageFlipAlgo::create() { return std::make_unique<ImageFlipAlgo>(); }
} // namespace vz
