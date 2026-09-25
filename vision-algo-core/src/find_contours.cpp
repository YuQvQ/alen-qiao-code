// find_contours.cpp — 轮廓查找
#include "find_contours.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::parseColor;
using detail::parseThickness; using detail::toCvMat; using detail::toVzImage;
int FindContoursAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "mode") { mode_ = parseJsonInt(v, 0); if (mode_ < 0) mode_ = 0; if (mode_ > 2) mode_ = 2; return VZ_OK; }
    if (k == "method") { method_ = parseJsonInt(v, 1); if (method_ < 0) method_ = 0; if (method_ > 1) method_ = 1; return VZ_OK; }
    if (k == "min_area") { min_area_ = parseJsonDouble(v, 0); return VZ_OK; }
    if (k == "draw_color") { draw_color_ = parseColor(v, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (k == "draw_thickness") { draw_thickness_ = parseThickness(v, 2); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int FindContoursAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int FindContoursAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Mat gray; int c = input_.channels();
    if (c == 1) gray = input_; else if (c == 3) cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    else if (c == 4) cv::cvtColor(input_, gray, cv::COLOR_BGRA2GRAY); else gray = input_;
    int m = (mode_ == 0) ? cv::RETR_EXTERNAL : (mode_ == 1 ? cv::RETR_LIST : cv::RETR_TREE);
    int meth = method_ ? cv::CHAIN_APPROX_SIMPLE : cv::CHAIN_APPROX_NONE;
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(gray, contours, m, meth);
    // 过滤
    std::vector<std::vector<cv::Point>> filtered;
    for (auto& cnt : contours) if (cv::contourArea(cnt) >= min_area_) filtered.push_back(cnt);
    out_count_ = (int)filtered.size();
    // 取最大轮廓输出点集
    out_points_.clear();
    if (!filtered.empty()) {
        size_t best = 0; double bestArea = -1;
        for (size_t i = 0; i < filtered.size(); ++i) { double a = cv::contourArea(filtered[i]); if (a > bestArea) { bestArea = a; best = i; } }
        for (auto& pt : filtered[best]) { VzPoint2D p{}; p.x = pt.x; p.y = pt.y; out_points_.push_back(p); }
    }
    // 结果图：输入图像透传（标注由 UI 层绘制）
    result_image_ = input_.clone();
    return VZ_OK;
}
int FindContoursAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "result_image") { *o = toVzImage(result_image_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    if (p == "points") {
        int sz = (int)out_points_.size();
        auto* arr = (VzPoint2D*)std::malloc(sizeof(VzPoint2D) * (sz ? sz : 1));
        for (int i = 0; i < sz; ++i) arr[i] = out_points_[i];
        *o = arr; *n = sz; *tt = VZ_TYPE_POINT2D_LIST; return VZ_OK;
    }
    if (p == "count") { auto* p2 = (int*)std::malloc(sizeof(int)); *p2 = out_count_; *o = p2; *n = 1; *tt = VZ_TYPE_INT; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void FindContoursAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* FindContoursAlgo::describe() const {
    return R"JSON({
  "type_id":"FindContours","display_name":"轮廓查找","category":"分析",
  "algo_binding":{"dll":"vision_algo","algo_name":"FindContours"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入二值/灰度图"}],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"绘制轮廓的结果图"},
    {"name":"points","type":"Point2DList","required":true,"description":"最大轮廓的点集"},
    {"name":"count","type":"Int","required":true,"description":"轮廓数量"}
  ],
  "parameters":[
    {"name":"mode","type":"Int","default":0,"description":"检索模式","enum":[[0,"仅外部"],[1,"列表"],[2,"树形"]]},
    {"name":"method","type":"Int","default":1,"description":"近似方法","enum":[[0,"保存所有点"],[1,"压缩水平/垂直/对角段"]]},
    {"name":"min_area","type":"Double","default":0,"description":"最小面积过滤(像素)"},
    {"name":"draw_color","type":"String","default":"#00FF00","description":"轮廓颜色(#RRGGBB 或 B,G,R)"},
    {"name":"draw_thickness","type":"Int","default":2,"min":1,"max":20,"description":"线宽(1-20)"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> FindContoursAlgo::create() { return std::make_unique<FindContoursAlgo>(); }
} // namespace vz
