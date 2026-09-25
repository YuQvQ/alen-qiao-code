// line_intersect.cpp — 线线交点
#include "line_intersect.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

namespace vz {

using detail::parseColor;
using detail::parseThickness;
using detail::parsePointSize;
using detail::toDrawCanvas;
using detail::toCvMat;
using detail::toVzImage;

int LineIntersectAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "draw_color") { draw_color_ = parseColor(value_json, cv::Scalar(0, 0, 255)); return VZ_OK; }
    if (key == "draw_thickness") { draw_thickness_ = parseThickness(value_json, 2); return VZ_OK; }
    if (key == "point_size") { point_size_ = parsePointSize(value_json, 8); return VZ_OK; }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int LineIntersectAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "line_a" || port_name == "line_b") {
        if (type_tag != VZ_TYPE_POINT2D_LIST) return VZ_ERR_TYPE_MISMATCH;
        auto* arr = (const VzPoint2DArray*)data;
        std::vector<VzPoint2D> target;
        if (arr && arr->data && arr->count > 0)
            target.assign(arr->data, arr->data + arr->count);
        if (port_name == "line_a") line_a_ = std::move(target);
        else line_b_ = std::move(target);
        return VZ_OK;
    }
    if (port_name == "image") {
        if (type_tag != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH;
        input_image_ = toCvMat((const VzImage*)data);
        has_image_ = true;
        return VZ_OK;
    }
    setLastError("unknown input: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

// 求两直线(各由两点定义)的交点；返回是否相交（平行/重合返回 false）
static bool lineLineIntersect(double x1, double y1, double x2, double y2,
                              double x3, double y3, double x4, double y4,
                              double& ix, double& iy) {
    double denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::abs(denom) < 1e-9) return false;  // 平行
    double t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / denom;
    ix = x1 + t * (x2 - x1);
    iy = y1 + t * (y2 - y1);
    return true;
}

int LineIntersectAlgo::process() {
    if (line_a_.size() < 2 || line_b_.size() < 2) {
        setLastError("need at least 2 points for each line");
        return VZ_ERR_PROCESS;
    }
    double ax1 = line_a_[0].x, ay1 = line_a_[0].y;
    double ax2 = line_a_[1].x, ay2 = line_a_[1].y;
    double bx1 = line_b_[0].x, by1 = line_b_[0].y;
    double bx2 = line_b_[1].x, by2 = line_b_[1].y;

    double ix = 0, iy = 0;
    has_intersection_ = lineLineIntersect(ax1, ay1, ax2, ay2, bx1, by1, bx2, by2, ix, iy);
    if (has_intersection_) {
        out_intersection_.x = ix;
        out_intersection_.y = iy;
    }

    // 两线夹角（0-90度）
    double vax = ax2 - ax1, vay = ay2 - ay1;
    double vbx = bx2 - bx1, vby = by2 - by1;
    double la = std::sqrt(vax * vax + vay * vay);
    double lb = std::sqrt(vbx * vbx + vby * vby);
    if (la > 1e-9 && lb > 1e-9) {
        double cosA = (vax * vbx + vay * vby) / (la * lb);
        cosA = std::max(-1.0, std::min(1.0, cosA));
        out_angle_ = std::acos(std::abs(cosA)) * 180.0 / CV_PI;
    } else {
        out_angle_ = 0.0;
    }

    result_image_ = input_image_.clone();
    return VZ_OK;
}

int LineIntersectAlgo::getOutput(const std::string& port_name,
                                 void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "result_image") {
        VzImage* img = toVzImage(result_image_);
        if (!img) { *out_data = nullptr; *out_count = 0; *out_type_tag = VZ_TYPE_IMAGE; return VZ_OK; }
        *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE;
        return VZ_OK;
    }
    if (port_name == "intersection") {
        if (!has_intersection_) { *out_data = nullptr; *out_count = 0; *out_type_tag = VZ_TYPE_POINT2D_LIST; return VZ_OK; }
        auto* arr = (VzPoint2D*)std::malloc(sizeof(VzPoint2D));
        arr[0] = out_intersection_;
        *out_data = arr; *out_count = 1; *out_type_tag = VZ_TYPE_POINT2D_LIST;
        return VZ_OK;
    }
    if (port_name == "has_intersection") {
        auto* p = (int*)std::malloc(sizeof(int));
        *p = has_intersection_ ? 1 : 0;
        *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_BOOL;
        return VZ_OK;
    }
    if (port_name == "angle") {
        auto* p = (double*)std::malloc(sizeof(double));
        *p = out_angle_; *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_DOUBLE;
        return VZ_OK;
    }
    setLastError("unknown output: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

void LineIntersectAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* LineIntersectAlgo::describe() const {
    return R"JSON({
  "type_id":"LineIntersect","display_name":"线线交点","category":"对位",
  "algo_binding":{"dll":"vision_algo","algo_name":"LineIntersect"},
  "inputs":[
    {"name":"line_a","type":"Point2DList","required":true,"description":"直线A(2个端点)"},
    {"name":"line_b","type":"Point2DList","required":true,"description":"直线B(2个端点)"},
    {"name":"image","type":"Image","required":false,"description":"输入图像(可选,用于绘制叠加)"}
  ],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"结果图(绘制两线及交点)"},
    {"name":"intersection","type":"Point2DList","required":true,"description":"交点坐标(1个点)"},
    {"name":"has_intersection","type":"Bool","required":true,"description":"是否相交(平行时为false)"},
    {"name":"angle","type":"Double","required":true,"description":"两线夹角(度,0-90)"}
  ],
  "parameters":[
    {"name":"draw_color","type":"String","default":"#FF0000","description":"绘制颜色(#RRGGBB 或 B,G,R)"},
    {"name":"draw_thickness","type":"Int","default":2,"min":1,"max":20,"description":"线宽(1-20)"},
    {"name":"point_size","type":"Int","default":8,"min":1,"max":50,"description":"交点标记半径(1-50)"}
  ]
})JSON";
}

std::unique_ptr<AlgoBase> LineIntersectAlgo::create() {
    return std::make_unique<LineIntersectAlgo>();
}

} // namespace vz
