// hist_equalize.cpp — 直方图均衡化(增强对比度)
#include "hist_equalize.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::toCvMat; using detail::toVzImage;
int HistEqualizeAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int HistEqualizeAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    int c = input_.channels();
    if (c == 1) { cv::equalizeHist(input_, output_); }
    else if (c == 3) {
        cv::Mat ycrcb; cv::cvtColor(input_, ycrcb, cv::COLOR_BGR2YCrCb);
        std::vector<cv::Mat> ch; cv::split(ycrcb, ch);
        cv::equalizeHist(ch[0], ch[0]); cv::merge(ch, ycrcb);
        cv::cvtColor(ycrcb, output_, cv::COLOR_YCrCb2BGR);
    } else if (c == 4) {
        cv::Mat bgr; cv::cvtColor(input_, bgr, cv::COLOR_BGRA2BGR);
        cv::Mat ycrcb; cv::cvtColor(bgr, ycrcb, cv::COLOR_BGR2YCrCb);
        std::vector<cv::Mat> ch; cv::split(ycrcb, ch);
        cv::equalizeHist(ch[0], ch[0]); cv::merge(ch, ycrcb);
        cv::cvtColor(ycrcb, output_, cv::COLOR_YCrCb2BGR);
    } else output_ = input_.clone();
    return VZ_OK;
}
int HistEqualizeAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void HistEqualizeAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* HistEqualizeAlgo::describe() const {
    return R"JSON({
  "type_id":"HistEqualize","display_name":"直方图均衡","category":"预处理",
  "algo_binding":{"dll":"vision_algo","algo_name":"HistEqualize"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"对比度增强后图像"}],
  "parameters":[]
})JSON";
}
std::unique_ptr<AlgoBase> HistEqualizeAlgo::create() { return std::make_unique<HistEqualizeAlgo>(); }
} // namespace vz
