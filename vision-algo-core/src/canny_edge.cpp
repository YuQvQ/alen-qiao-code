// canny_edge.cpp — Canny 边缘检测
#include "canny_edge.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::toCvMat; using detail::toVzImage;
int CannyEdgeAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "low_threshold") { low_ = parseJsonDouble(v, 50.0); return VZ_OK; }
    if (k == "high_threshold") { high_ = parseJsonDouble(v, 150.0); return VZ_OK; }
    if (k == "aperture") { aperture_ = parseJsonInt(v, 3); if (aperture_ < 3) aperture_ = 3; if (aperture_ > 7) aperture_ = 7; if (aperture_ % 2 == 0) aperture_++; return VZ_OK; }
    if (k == "l2_gradient") { l2_ = parseJsonInt(v, 0) ? 1 : 0; return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int CannyEdgeAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int CannyEdgeAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Mat gray; int c = input_.channels();
    if (c == 1) gray = input_; else if (c == 3) cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    else if (c == 4) cv::cvtColor(input_, gray, cv::COLOR_BGRA2GRAY); else gray = input_;
    cv::Canny(gray, output_, low_, high_, aperture_, l2_ != 0);
    return VZ_OK;
}
int CannyEdgeAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void CannyEdgeAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* CannyEdgeAlgo::describe() const {
    return R"JSON({
  "type_id":"CannyEdge","display_name":"Canny边缘","category":"边缘",
  "algo_binding":{"dll":"vision_algo","algo_name":"CannyEdge"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"边缘二值图"}],
  "parameters":[
    {"name":"low_threshold","type":"Double","default":50.0,"description":"低阈值"},
    {"name":"high_threshold","type":"Double","default":150.0,"description":"高阈值(建议为低阈值2-3倍)"},
    {"name":"aperture","type":"Int","default":3,"description":"Sobel核大小(3,5,7)"},
    {"name":"l2_gradient","type":"Int","default":0,"description":"L2梯度(更精确):0=否,1=是","enum":[[0,"否"],[1,"是"]]}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> CannyEdgeAlgo::create() { return std::make_unique<CannyEdgeAlgo>(); }
} // namespace vz
