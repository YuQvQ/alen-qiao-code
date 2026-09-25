// grayscale.cpp — 灰度化
#include "grayscale.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::toCvMat; using detail::toVzImage;
int GrayscaleAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int GrayscaleAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    int c = input_.channels();
    if (c == 1) output_ = input_.clone();
    else if (c == 3) cv::cvtColor(input_, output_, cv::COLOR_BGR2GRAY);
    else if (c == 4) cv::cvtColor(input_, output_, cv::COLOR_BGRA2GRAY);
    else output_ = input_.clone();
    return VZ_OK;
}
int GrayscaleAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void GrayscaleAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* GrayscaleAlgo::describe() const {
    return R"JSON({
  "type_id":"Grayscale","display_name":"灰度化","category":"预处理",
  "algo_binding":{"dll":"vision_algo","algo_name":"Grayscale"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"灰度图像"}],
  "parameters":[]
})JSON";
}
std::unique_ptr<AlgoBase> GrayscaleAlgo::create() { return std::make_unique<GrayscaleAlgo>(); }
} // namespace vz
