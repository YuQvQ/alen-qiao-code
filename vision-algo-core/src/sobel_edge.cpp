// sobel_edge.cpp — Sobel 梯度
#include "sobel_edge.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::toCvMat; using detail::toVzImage;
int SobelEdgeAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "dx") { dx_ = parseJsonInt(v, 1); if (dx_ < 0) dx_ = 0; if (dx_ > 2) dx_ = 2; return VZ_OK; }
    if (k == "dy") { dy_ = parseJsonInt(v, 0); if (dy_ < 0) dy_ = 0; if (dy_ > 2) dy_ = 2; return VZ_OK; }
    if (k == "ksize") { ksize_ = parseJsonInt(v, 3); if (ksize_ < 1) ksize_ = 1; if (ksize_ % 2 == 0) ksize_++; if (ksize_ > 7) ksize_ = 7; return VZ_OK; }
    if (k == "scale") { scale_ = parseJsonDouble(v, 1.0); return VZ_OK; }
    if (k == "combine") { combine_ = parseJsonInt(v, 0) ? 1 : 0; return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int SobelEdgeAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int SobelEdgeAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Mat gray; int c = input_.channels();
    if (c == 1) gray = input_; else if (c == 3) cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    else if (c == 4) cv::cvtColor(input_, gray, cv::COLOR_BGRA2GRAY); else gray = input_;
    if (combine_) {
        cv::Mat gx, gy;
        cv::Sobel(gray, gx, CV_32F, 1, 0, ksize_, scale_);
        cv::Sobel(gray, gy, CV_32F, 0, 1, ksize_, scale_);
        cv::Mat mag; cv::magnitude(gx, gy, mag);
        cv::convertScaleAbs(mag, output_);
    } else {
        cv::Sobel(gray, output_, CV_8U, dx_, dy_, ksize_, scale_);
    }
    return VZ_OK;
}
int SobelEdgeAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void SobelEdgeAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* SobelEdgeAlgo::describe() const {
    return R"JSON({
  "type_id":"SobelEdge","display_name":"Sobel梯度","category":"边缘",
  "algo_binding":{"dll":"vision_algo","algo_name":"SobelEdge"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"梯度结果图"}],
  "parameters":[
    {"name":"dx","type":"Int","default":1,"description":"x方向阶数(0-2)"},
    {"name":"dy","type":"Int","default":0,"description":"y方向阶数(0-2)"},
    {"name":"ksize","type":"Int","default":3,"description":"核大小(1,3,5,7)"},
    {"name":"scale","type":"Double","default":1.0,"description":"缩放因子"},
    {"name":"combine","type":"Int","default":0,"description":"输出梯度幅值(忽略dx/dy):0=否,1=是","enum":[[0,"按dx/dy"],[1,"梯度幅值"]]}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> SobelEdgeAlgo::create() { return std::make_unique<SobelEdgeAlgo>(); }
} // namespace vz
