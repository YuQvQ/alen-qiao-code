// connected_components.cpp
#include "connected_components.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <vector>
namespace vz {
using detail::toCvMat; using detail::toVzImage;
int ConnectedComponentsAlgo::setParam(const std::string&, const std::string&) { return VZ_OK; }
int ConnectedComponentsAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int ConnectedComponentsAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Mat gray, bin;
    if (input_.channels() == 1) gray = input_; else cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::Mat labels;
    count_ = cv::connectedComponents(bin, labels, 8, CV_32S) - 1;  // 去掉背景
    // 伪彩色标注
    result_ = cv::Mat::zeros(labels.size(), CV_8UC3);
    std::vector<cv::Vec3b> colors(count_ + 1);
    for (int i = 1; i <= count_; ++i) {
        cv::Mat hsv(1, 1, CV_8UC3, cv::Scalar((i * 47) % 180, 200, 200));
        cv::Mat bgr; cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);
        colors[i] = bgr.at<cv::Vec3b>(0, 0);
    }
    for (int y = 0; y < labels.rows; ++y)
        for (int x = 0; x < labels.cols; ++x) {
            int l = labels.at<int>(y, x);
            if (l > 0) result_.at<cv::Vec3b>(y, x) = colors[l];
        }
    return VZ_OK;
}
int ConnectedComponentsAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "result_image") { *o = toVzImage(result_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    if (p == "count") { auto* x = (int*)std::malloc(sizeof(int)); *x = count_; *o = x; *n = 1; *tt = VZ_TYPE_INT; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void ConnectedComponentsAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* ConnectedComponentsAlgo::describe() const {
    return R"JSON({
  "type_id":"ConnectedComponents","display_name":"连通域统计","category":"分析",
  "algo_binding":{"dll":"vision_algo","algo_name":"ConnectedComponents"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"伪彩色标注图"},
    {"name":"count","type":"Int","required":true,"description":"连通域数量"}
  ],
  "parameters":[]
})JSON";
}
std::unique_ptr<AlgoBase> ConnectedComponentsAlgo::create() { return std::make_unique<ConnectedComponentsAlgo>(); }
} // namespace vz
