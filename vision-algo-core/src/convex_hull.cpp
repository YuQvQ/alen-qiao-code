// convex_hull.cpp — 凸包
#include "convex_hull.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
namespace vz {
using detail::parseColor; using detail::parseThickness; using detail::parsePointSize;
using detail::toDrawCanvas; using detail::toCvMat; using detail::toVzImage;
int ConvexHullAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "draw_color") { draw_color_ = parseColor(v, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (k == "draw_thickness") { draw_thickness_ = parseThickness(v, 2); return VZ_OK; }
    if (k == "point_size") { point_size_ = parsePointSize(v, 3); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int ConvexHullAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "points") {
        if (t != VZ_TYPE_POINT2D_LIST) return VZ_ERR_TYPE_MISMATCH;
        auto* arr = (const VzPoint2DArray*)d; in_points_.clear();
        if (arr && arr->data && arr->count > 0) in_points_.assign(arr->data, arr->data + arr->count);
        return VZ_OK;
    }
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_image_ = toCvMat((const VzImage*)d); has_image_ = true; return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int ConvexHullAlgo::process() {
    if (in_points_.size() < 3) { setLastError("need at least 3 points"); return VZ_ERR_PROCESS; }
    std::vector<cv::Point> pts;
    for (auto& p : in_points_) pts.emplace_back((int)p.x, (int)p.y);
    std::vector<cv::Point> hull;
    cv::convexHull(pts, hull);
    out_hull_.clear();
    for (auto& p : hull) { VzPoint2D vp{}; vp.x = p.x; vp.y = p.y; out_hull_.push_back(vp); }
    out_area_ = cv::contourArea(hull);
    // 结果图：输入图像透传（标注由 UI 层绘制）
    if (has_image_) {
        result_image_ = input_image_.clone();
    } else {
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (auto& p : in_points_) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minY = std::min(minY, p.y); maxY = std::max(maxY, p.y); }
        int pad = 40; int W = std::max(100, (int)(maxX - minX) + pad * 2), H = std::max(100, (int)(maxY - minY) + pad * 2);
        result_image_ = cv::Mat::zeros(H, W, CV_8UC3);
    }
    return VZ_OK;
}
int ConvexHullAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "result_image") { *o = toVzImage(result_image_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    if (p == "hull") {
        int sz = (int)out_hull_.size();
        auto* arr = (VzPoint2D*)std::malloc(sizeof(VzPoint2D) * (sz ? sz : 1));
        for (int i = 0; i < sz; ++i) arr[i] = out_hull_[i];
        *o = arr; *n = sz; *tt = VZ_TYPE_POINT2D_LIST; return VZ_OK;
    }
    if (p == "area") { auto* p2 = (double*)std::malloc(sizeof(double)); *p2 = out_area_; *o = p2; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void ConvexHullAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* ConvexHullAlgo::describe() const {
    return R"JSON({
  "type_id":"ConvexHull","display_name":"凸包","category":"分析",
  "algo_binding":{"dll":"vision_algo","algo_name":"ConvexHull"},
  "inputs":[
    {"name":"points","type":"Point2DList","required":true,"description":"输入点集"},
    {"name":"image","type":"Image","required":false,"description":"输入图像(可选,用于叠加绘制)"}
  ],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"绘制凸包的结果图"},
    {"name":"hull","type":"Point2DList","required":true,"description":"凸包顶点"},
    {"name":"area","type":"Double","required":true,"description":"凸包面积(像素²)"}
  ],
  "parameters":[
    {"name":"draw_color","type":"String","default":"#00FF00","description":"凸包颜色(#RRGGBB 或 B,G,R)"},
    {"name":"draw_thickness","type":"Int","default":2,"min":1,"max":20,"description":"线宽(1-20)"},
    {"name":"point_size","type":"Int","default":3,"min":1,"max":50,"description":"顶点标记半径(1-50)"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> ConvexHullAlgo::create() { return std::make_unique<ConvexHullAlgo>(); }
} // namespace vz
