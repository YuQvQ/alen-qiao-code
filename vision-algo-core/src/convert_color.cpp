// convert_color.cpp — 颜色空间转换
#include "convert_color.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::parseJsonInt; using detail::toCvMat; using detail::toVzImage;
int ConvertColorAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "code") { code_ = parseJsonInt(v, 0); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int ConvertColorAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int ConvertColorAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    int conv = -1;
    switch (code_) {
        case 0: conv = cv::COLOR_BGR2GRAY; break;   // 灰度
        case 1: conv = cv::COLOR_BGR2HSV; break;    // HSV
        case 2: conv = cv::COLOR_BGR2RGB; break;    // RGB
        case 3: conv = cv::COLOR_BGR2Lab; break;    // Lab
        case 4: conv = cv::COLOR_BGR2YCrCb; break;  // YCrCb
        case 5: conv = cv::COLOR_HSV2BGR; break;
        case 6: conv = cv::COLOR_RGB2BGR; break;
        case 7: conv = cv::COLOR_GRAY2BGR; break;
        default: conv = cv::COLOR_BGR2GRAY;
    }
    try { cv::cvtColor(input_, output_, conv); }
    catch (...) { output_ = input_.clone(); }
    return VZ_OK;
}
int ConvertColorAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "image") { *o = toVzImage(output_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void ConvertColorAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* ConvertColorAlgo::describe() const {
    return R"JSON({
  "type_id":"ConvertColor","display_name":"颜色转换","category":"预处理",
  "algo_binding":{"dll":"vision_algo","algo_name":"ConvertColor"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"转换后图像"}],
  "parameters":[{"name":"code","type":"Int","default":0,"description":"转换方向","enum":[[0,"BGR->灰度"],[1,"BGR->HSV"],[2,"BGR->RGB"],[3,"BGR->Lab"],[4,"BGR->YCrCb"],[5,"HSV->BGR"],[6,"RGB->BGR"],[7,"灰度->BGR"]]}]
})JSON";
}
std::unique_ptr<AlgoBase> ConvertColorAlgo::create() { return std::make_unique<ConvertColorAlgo>(); }
} // namespace vz
