// min_max_loc.cpp — 查找最亮/最暗点
#include "min_max_loc.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::parseJsonInt; using detail::toCvMat; using detail::toVzImage; using detail::parseColor; using detail::parsePointSize;
int MinMaxLocAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "point_size") { point_size_ = parsePointSize(v, 8); return VZ_OK; }
    if (k == "max_color") { max_color_ = parseColor(v, cv::Scalar(0, 0, 255)); return VZ_OK; }
    if (k == "min_color") { min_color_ = parseColor(v, cv::Scalar(255, 0, 0)); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int MinMaxLocAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int MinMaxLocAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    has_result_ = false;
    cv::Mat gray; if (input_.channels() == 1) gray = input_; else cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    cv::minMaxLoc(gray, &minVal_, &maxVal_, &minLoc_, &maxLoc_);
    // 结果图：输入图像透传（标注由 UI 层绘制）
    result_ = input_.clone();
    has_result_ = true;
    return VZ_OK;
}
static VzPoint2D toPt(cv::Point p) { VzPoint2D r{}; r.x = p.x; r.y = p.y; return r; }
int MinMaxLocAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "result_image") { *o = toVzImage(result_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    if (p == "max_loc") { auto* x = (VzPoint2D*)std::malloc(sizeof(VzPoint2D)); *x = toPt(maxLoc_); *o = x; *n = 1; *tt = VZ_TYPE_POINT2D_LIST; return VZ_OK; }
    if (p == "min_loc") { auto* x = (VzPoint2D*)std::malloc(sizeof(VzPoint2D)); *x = toPt(minLoc_); *o = x; *n = 1; *tt = VZ_TYPE_POINT2D_LIST; return VZ_OK; }
    if (p == "max_val") { auto* x = (double*)std::malloc(sizeof(double)); *x = maxVal_; *o = x; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    if (p == "min_val") { auto* x = (double*)std::malloc(sizeof(double)); *x = minVal_; *o = x; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void MinMaxLocAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* MinMaxLocAlgo::describe() const {
    return R"JSON({
  "type_id":"MinMaxLoc","display_name":"最值点定位","category":"分析",
  "algo_binding":{"dll":"vision_algo","algo_name":"MinMaxLoc"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"标注最值点的结果图(红=最亮,蓝=最暗)"},
    {"name":"max_loc","type":"Point2DList","required":true,"description":"最亮点坐标"},
    {"name":"min_loc","type":"Point2DList","required":true,"description":"最暗点坐标"},
    {"name":"max_val","type":"Double","required":true,"description":"最大灰度值"},
    {"name":"min_val","type":"Double","required":true,"description":"最小灰度值"}
  ],
  "parameters":[
    {"name":"point_size","type":"Int","default":8,"description":"标注点大小(1-50)"},
    {"name":"max_color","type":"String","default":"#FF0000","description":"最亮点颜色(#RRGGBB)"},
    {"name":"min_color","type":"String","default":"#0000FF","description":"最暗点颜色(#RRGGBB)"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> MinMaxLocAlgo::create() { return std::make_unique<MinMaxLocAlgo>(); }
} // namespace vz
