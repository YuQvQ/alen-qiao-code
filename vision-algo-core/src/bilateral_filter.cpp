// bilateral_filter.cpp — 双边滤波
#include "bilateral_filter.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::toCvMat; using detail::toVzImage;
int BilateralFilterAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "d") { d_ = parseJsonInt(v, 9); if (d_ < 1) d_ = 1; return VZ_OK; }
    if (k == "sigma_color") { sigma_color_ = parseJsonDouble(v, 75.0); return VZ_OK; }
    if (k == "sigma_space") { sigma_space_ = parseJsonDouble(v, 75.0); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int BilateralFilterAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int BilateralFilterAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Mat src = input_.channels() == 1 ? input_ : input_;
    cv::bilateralFilter(src, output_, d_, sigma_color_, sigma_space_);
    return VZ_OK;
}
int BilateralFilterAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void BilateralFilterAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* BilateralFilterAlgo::describe() const {
    return R"JSON({
  "type_id":"BilateralFilter","display_name":"双边滤波","category":"预处理",
  "algo_binding":{"dll":"vision_algo","algo_name":"BilateralFilter"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"保边去噪后图像"}],
  "parameters":[
    {"name":"d","type":"Int","default":9,"description":"像素邻域直径"},
    {"name":"sigma_color","type":"Double","default":75.0,"description":"颜色空间标准差"},
    {"name":"sigma_space","type":"Double","default":75.0,"description":"坐标空间标准差"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> BilateralFilterAlgo::create() { return std::make_unique<BilateralFilterAlgo>(); }
} // namespace vz
