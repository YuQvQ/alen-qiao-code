// line_fit.cpp — 点集最小二乘直线拟合
#include "line_fit.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

namespace vz {

using detail::parseColor;
using detail::parseThickness;
using detail::parsePointSize;
using detail::toCvMat;
using detail::toVzImage;

int LineFitAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "draw_color") { draw_color_ = parseColor(value_json, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (key == "draw_thickness") { draw_thickness_ = parseThickness(value_json, 2); return VZ_OK; }
    if (key == "point_size") { point_size_ = parsePointSize(value_json, 4); return VZ_OK; }
    setLastError("unknown parameter: " + key); return VZ_ERR_INVALID_ARG;
}

int LineFitAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "points") {
        if (type_tag != VZ_TYPE_POINT2D_LIST) return VZ_ERR_TYPE_MISMATCH;
        auto* arr = (const VzPoint2DArray*)data;
        points_.clear();
        if (arr && arr->data && arr->count > 0)
            points_.assign(arr->data, arr->data + arr->count);
        return VZ_OK;
    }
    if (port_name == "image") {
        if (type_tag != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH;
        input_image_ = toCvMat((const VzImage*)data);
        has_image_ = true;
        return VZ_OK;
    }
    setLastError("unknown input: " + port_name); return VZ_ERR_INVALID_ARG;
}

int LineFitAlgo::process() {
    if (points_.size() < 2) { setLastError("need at least 2 points"); return VZ_ERR_PROCESS; }
    double sx=0, sy=0, sxx=0, sxy=0;
    int n = (int)points_.size();
    for (auto& p : points_) { sx += p.x; sy += p.y; sxx += p.x*p.x; sxy += p.x*p.y; }
    double mx = sx / n, my = sy / n;
    double denom = n * sxx - sx * sx;
    double a, b;
    if (std::abs(denom) < 1e-12) { a = 1e12; b = mx; }
    else { a = (n * sxy - sx * sy) / denom; b = (sy - a * sx) / n; }
    out_angle_ = std::atan(a) * 180.0 / CV_PI;
    out_intercept_ = b;
    out_point_.point.x = mx;
    out_point_.point.y = my;
    out_point_.angle = out_angle_;

    double sumRes = 0;
    double len = std::sqrt(a*a + 1);
    for (auto& p : points_) {
        double d = std::abs(a * p.x - p.y + b) / len;
        sumRes += d * d;
    }
    out_residuals_ = std::sqrt(sumRes / n);

    result_image_ = input_image_.clone();
    return VZ_OK;
}

int LineFitAlgo::getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "result_image") { VzImage* img=toVzImage(result_image_); *out_data=img;*out_count=1;*out_type_tag=VZ_TYPE_IMAGE;return VZ_OK; }
    if (port_name == "angle") { auto* p=(double*)std::malloc(sizeof(double)); *p=out_angle_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_DOUBLE;return VZ_OK; }
    if (port_name == "intercept") { auto* p=(double*)std::malloc(sizeof(double)); *p=out_intercept_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_DOUBLE;return VZ_OK; }
    if (port_name == "residuals") { auto* p=(double*)std::malloc(sizeof(double)); *p=out_residuals_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_DOUBLE;return VZ_OK; }
    if (port_name == "point") { auto* p=(VzPose2D*)std::malloc(sizeof(VzPose2D)); *p=out_point_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_POSE2D;return VZ_OK; }
    setLastError("unknown output: " + port_name); return VZ_ERR_INVALID_ARG;
}

void LineFitAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* LineFitAlgo::describe() const {
    return R"JSON({
  "type_id":"LineFit","display_name":"直线拟合","category":"对位",
  "algo_binding":{"dll":"vision_algo","algo_name":"LineFit"},
  "inputs":[
    {"name":"points","type":"Point2DList","required":true,"description":"输入点集"},
    {"name":"image","type":"Image","required":false,"description":"输入图像(可选,用于绘制)"}
  ],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"结果图(绿色=拟合直线,红色=输入点,青色=中心点)"},
    {"name":"angle","type":"Double","required":true,"description":"直线角度(度)"},
    {"name":"intercept","type":"Double","required":true,"description":"Y轴截距"},
    {"name":"point","type":"Pose2D","required":true,"description":"直线中心点+角度"},
    {"name":"residuals","type":"Double","required":true,"description":"拟合残差(RMS)"}
  ],
  "parameters":[
    {"name":"draw_color","type":"String","default":"#00FF00","description":"拟合线颜色(#RRGGBB 或 B,G,R)"},
    {"name":"draw_thickness","type":"Int","default":2,"min":1,"max":20,"description":"线宽(1-20)"},
    {"name":"point_size","type":"Int","default":4,"min":1,"max":50,"description":"输入点标记半径(1-50)"}
  ]
})JSON";
}

std::unique_ptr<AlgoBase> LineFitAlgo::create() { return std::make_unique<LineFitAlgo>(); }

} // namespace vz
