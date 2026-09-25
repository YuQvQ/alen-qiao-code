// image_rotate.cpp — 图像旋转
#include "image_rotate.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <cmath>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::toCvMat; using detail::toVzImage;
int ImageRotateAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "angle") { angle_ = parseJsonDouble(v, 0.0); return VZ_OK; }
    if (k == "scale") { scale_ = parseJsonDouble(v, 1.0); if (scale_ <= 0) scale_ = 1.0; return VZ_OK; }
    if (k == "expand") { expand_ = parseJsonInt(v, 1) ? 1 : 0; return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int ImageRotateAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int ImageRotateAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Point2f center((float)(input_.cols / 2.0), (float)(input_.rows / 2.0));
    cv::Mat M = cv::getRotationMatrix2D(center, angle_, scale_);
    if (expand_) {
        double rad = std::abs(angle_ * CV_PI / 180.0);
        double sinA = std::sin(rad), cosA = std::cos(rad);
        int W = input_.cols, H = input_.rows;
        int nW = (int)(std::abs(W * cosA) + std::abs(H * sinA));
        int nH = (int)(std::abs(W * sinA) + std::abs(H * cosA));
        M.at<double>(0, 2) += (nW / 2.0) - center.x;
        M.at<double>(1, 2) += (nH / 2.0) - center.y;
        cv::warpAffine(input_, output_, M, cv::Size(nW, nH), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    } else {
        cv::warpAffine(input_, output_, M, input_.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    }
    return VZ_OK;
}
int ImageRotateAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void ImageRotateAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* ImageRotateAlgo::describe() const {
    return R"JSON({
  "type_id":"ImageRotate","display_name":"图像旋转","category":"几何",
  "algo_binding":{"dll":"vision_algo","algo_name":"ImageRotate"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"旋转后图像"}],
  "parameters":[
    {"name":"angle","type":"Double","default":0.0,"description":"旋转角度(度,正=逆时针)"},
    {"name":"scale","type":"Double","default":1.0,"description":"缩放比例"},
    {"name":"expand","type":"Int","default":1,"description":"扩展画布完整显示:0=否,1=是","enum":[[0,"保持原尺寸"],[1,"扩展画布"]]}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> ImageRotateAlgo::create() { return std::make_unique<ImageRotateAlgo>(); }
} // namespace vz
