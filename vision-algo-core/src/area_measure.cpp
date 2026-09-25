// area_measure.cpp — 面积/周长测量
#include "area_measure.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
namespace vz {
using detail::toCvMat;
int AreaMeasureAlgo::setParam(const std::string&, const std::string&) { return VZ_OK; }
int AreaMeasureAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "points") {
        if (t != VZ_TYPE_POINT2D_LIST) return VZ_ERR_TYPE_MISMATCH;
        auto* arr = (const VzPoint2DArray*)d; points_.clear();
        if (arr && arr->data && arr->count > 0) points_.assign(arr->data, arr->data + arr->count);
        return VZ_OK;
    }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int AreaMeasureAlgo::process() {
    if (points_.size() < 3) { setLastError("need at least 3 points"); return VZ_ERR_PROCESS; }
    std::vector<cv::Point> pts;
    for (auto& p : points_) pts.emplace_back((int)p.x, (int)p.y);
    out_area_ = cv::contourArea(pts);
    out_perimeter_ = cv::arcLength(pts, true);
    return VZ_OK;
}
int AreaMeasureAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "area") { auto* p2 = (double*)std::malloc(sizeof(double)); *p2 = out_area_; *o = p2; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    if (p == "perimeter") { auto* p2 = (double*)std::malloc(sizeof(double)); *p2 = out_perimeter_; *o = p2; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void AreaMeasureAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* AreaMeasureAlgo::describe() const {
    return R"JSON({
  "type_id":"AreaMeasure","display_name":"面积测量","category":"测量",
  "algo_binding":{"dll":"vision_algo","algo_name":"AreaMeasure"},
  "inputs":[{"name":"points","type":"Point2DList","required":true,"description":"多边形顶点(>=3)"}],
  "outputs":[
    {"name":"area","type":"Double","required":true,"description":"面积(像素²)"},
    {"name":"perimeter","type":"Double","required":true,"description":"周长(像素)"}
  ],
  "parameters":[]
})JSON";
}
std::unique_ptr<AlgoBase> AreaMeasureAlgo::create() { return std::make_unique<AreaMeasureAlgo>(); }
} // namespace vz
