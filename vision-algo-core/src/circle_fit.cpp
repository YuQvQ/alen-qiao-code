// circle_fit.cpp — 点集最小二乘圆拟合
#include "circle_fit.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

namespace vz {

using detail::parseColor;
using detail::parseThickness;
using detail::parsePointSize;
using detail::toCvMat;
using detail::toVzImage;

int CircleFitAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "draw_color") { draw_color_ = parseColor(value_json, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (key == "draw_thickness") { draw_thickness_ = parseThickness(value_json, 2); return VZ_OK; }
    if (key == "point_size") { point_size_ = parsePointSize(value_json, 4); return VZ_OK; }
    setLastError("unknown parameter: " + key); return VZ_ERR_INVALID_ARG;
}

int CircleFitAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "points") {
        if (type_tag != VZ_TYPE_POINT2D_LIST) return VZ_ERR_TYPE_MISMATCH;
        auto* arr = (const VzPoint2DArray*)data;
        points_.clear();
        if (arr && arr->data && arr->count > 0) points_.assign(arr->data, arr->data + arr->count);
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

int CircleFitAlgo::process() {
    if (points_.size() < 3) { setLastError("need at least 3 points"); return VZ_ERR_PROCESS; }
    double sx=0, sy=0, sxx=0, syy=0, sxy=0, sxxx=0, syyy=0, sxxy=0, sxyy=0;
    int n = (int)points_.size();
    for (auto& p : points_) {
        double x=p.x, y=p.y;
        sx+=x; sy+=y; sxx+=x*x; syy+=y*y; sxy+=x*y;
        sxxx+=x*x*x; syyy+=y*y*y; sxxy+=x*x*y; sxyy+=x*y*y;
    }
    double A[3][3] = {{2*sx, 2*sy, (double)n}, {2*sxx, 2*sxy, sx}, {2*sxy, 2*syy, sy}};
    double B[3] = {sxx + syy, sxxx + sxyy, syyy + sxxy};
    for (int i = 0; i < 3; ++i) {
        for (int k = i + 1; k < 3; ++k) {
            double f = A[k][i] / A[i][i];
            for (int j = i; j < 3; ++j) A[k][j] -= f * A[i][j];
            B[k] -= f * B[i];
        }
    }
    double x[3];
    for (int i = 2; i >= 0; --i) {
        x[i] = B[i];
        for (int j = i + 1; j < 3; ++j) x[i] -= A[i][j] * x[j];
        x[i] /= A[i][i];
    }
    out_cx_ = x[0]; out_cy_ = x[1];
    out_radius_ = std::sqrt(x[2] + out_cx_*out_cx_ + out_cy_*out_cy_);
    double sumRes = 0;
    for (auto& p : points_) {
        double r = std::sqrt((p.x-out_cx_)*(p.x-out_cx_) + (p.y-out_cy_)*(p.y-out_cy_));
        sumRes += (r - out_radius_) * (r - out_radius_);
    }
    out_residuals_ = std::sqrt(sumRes / n);
    out_count_ = n;

    result_image_ = input_image_.clone();
    return VZ_OK;
}

int CircleFitAlgo::getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "result_image") { VzImage* img=toVzImage(result_image_); *out_data=img;*out_count=1;*out_type_tag=VZ_TYPE_IMAGE;return VZ_OK; }
    if (port_name == "center") {
        auto* p = (VzPose2D*)std::malloc(sizeof(VzPose2D));
        p->point.x = out_cx_; p->point.y = out_cy_; p->angle = 0;
        *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_POSE2D;return VZ_OK;
    }
    if (port_name == "radius") { auto* p=(double*)std::malloc(sizeof(double)); *p=out_radius_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_DOUBLE;return VZ_OK; }
    if (port_name == "residuals") { auto* p=(double*)std::malloc(sizeof(double)); *p=out_residuals_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_DOUBLE;return VZ_OK; }
    if (port_name == "count") { auto* p=(int*)std::malloc(sizeof(int)); *p=out_count_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_INT;return VZ_OK; }
    setLastError("unknown output: " + port_name); return VZ_ERR_INVALID_ARG;
}

void CircleFitAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* CircleFitAlgo::describe() const {
    return R"JSON({
  "type_id":"CircleFit","display_name":"圆拟合","category":"对位",
  "algo_binding":{"dll":"vision_algo","algo_name":"CircleFit"},
  "inputs":[
    {"name":"points","type":"Point2DList","required":true,"description":"输入点集"},
    {"name":"image","type":"Image","required":false,"description":"输入图像(可选,用于绘制)"}
  ],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"结果图(绿色=拟合圆,红色=圆心,黄色=输入点)"},
    {"name":"center","type":"Pose2D","required":true,"description":"拟合圆心"},
    {"name":"radius","type":"Double","required":true,"description":"拟合半径"},
    {"name":"residuals","type":"Double","required":true,"description":"拟合残差(RMS)"},
    {"name":"count","type":"Int","required":true,"description":"参与拟合的点数"}
  ],
  "parameters":[
    {"name":"draw_color","type":"String","default":"#00FF00","description":"拟合圆颜色(#RRGGBB 或 B,G,R)"},
    {"name":"draw_thickness","type":"Int","default":2,"min":1,"max":20,"description":"线宽(1-20)"},
    {"name":"point_size","type":"Int","default":4,"min":1,"max":50,"description":"输入点/圆心标记半径(1-50)"}
  ]
})JSON";
}

std::unique_ptr<AlgoBase> CircleFitAlgo::create() { return std::make_unique<CircleFitAlgo>(); }

} // namespace vz
