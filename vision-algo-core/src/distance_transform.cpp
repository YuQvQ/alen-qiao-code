// distance_transform.cpp
#include "distance_transform.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::parseJsonInt; using detail::toCvMat; using detail::toVzImage;
int DistanceTransformAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "mask_size") { mask_size_ = parseJsonInt(v, 5); if (mask_size_ != 0 && mask_size_ != 3 && mask_size_ != 5) mask_size_ = 5; return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int DistanceTransformAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int DistanceTransformAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Mat gray; if (input_.channels() == 1) gray = input_; else cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    cv::Mat dist; cv::distanceTransform(gray, dist, cv::DIST_L2, mask_size_);
    double mx; cv::minMaxLoc(dist, nullptr, &mx);
    max_dist_ = mx;
    if (mx > 0) dist.convertTo(output_, CV_8U, 255.0 / mx); else output_ = cv::Mat::zeros(gray.size(), CV_8U);
    return VZ_OK;
}
int DistanceTransformAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    if (p == "max_distance") { auto* x = (double*)std::malloc(sizeof(double)); *x = max_dist_; *o = x; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void DistanceTransformAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* DistanceTransformAlgo::describe() const {
    return R"JSON({
  "type_id":"DistanceTransform","display_name":"距离变换","category":"分析",
  "algo_binding":{"dll":"vision_algo","algo_name":"DistanceTransform"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入二值/灰度图"}],
  "outputs":[
    {"name":"image","type":"Image","required":true,"description":"距离归一化灰度图"},
    {"name":"max_distance","type":"Double","required":true,"description":"最大距离(像素)"}
  ],
  "parameters":[{"name":"mask_size","type":"Int","default":5,"description":"掩膜尺寸(0/3/5)"}]
})JSON";
}
std::unique_ptr<AlgoBase> DistanceTransformAlgo::create() { return std::make_unique<DistanceTransformAlgo>(); }
} // namespace vz
