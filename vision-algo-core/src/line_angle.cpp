// line_angle.cpp — 线线角度
#include "line_angle.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <cstdio>

namespace vz {

using detail::parseColor;
using detail::parseThickness;
using detail::parsePointSize;
using detail::parseJsonInt;
using detail::toDrawCanvas;
using detail::toCvMat;
using detail::toVzImage;

int LineAngleAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "draw_color") { draw_color_ = parseColor(value_json, cv::Scalar(0, 255, 255)); return VZ_OK; }
    if (key == "draw_thickness") { draw_thickness_ = parseThickness(value_json, 2); return VZ_OK; }
    if (key == "point_size") { point_size_ = parsePointSize(value_json, 6); return VZ_OK; }
    if (key == "angle_mode") {
        angle_mode_ = parseJsonInt(value_json, 0);
        if (angle_mode_ < 0) angle_mode_ = 0;
        if (angle_mode_ > 1) angle_mode_ = 1;
        return VZ_OK;
    }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int LineAngleAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
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

static bool intersectPt(double x1, double y1, double x2, double y2,
                        double x3, double y3, double x4, double y4,
                        double& ix, double& iy) {
    double denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::abs(denom) < 1e-9) return false;
    double t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / denom;
    ix = x1 + t * (x2 - x1);
    iy = y1 + t * (y2 - y1);
    return true;
}

int LineAngleAlgo::process() {
    if (line_a_.size() < 2 || line_b_.size() < 2) {
        setLastError("need at least 2 points for each line");
        return VZ_ERR_PROCESS;
    }
    double ax1 = line_a_[0].x, ay1 = line_a_[0].y;
    double ax2 = line_a_[1].x, ay2 = line_a_[1].y;
    double bx1 = line_b_[0].x, by1 = line_b_[0].y;
    double bx2 = line_b_[1].x, by2 = line_b_[1].y;

    double vax = ax2 - ax1, vay = ay2 - ay1;
    double vbx = bx2 - bx1, vby = by2 - by1;
    double la = std::sqrt(vax * vax + vay * vay);
    double lb = std::sqrt(vbx * vbx + vby * vby);

    double angA = std::atan2(vay, vax) * 180.0 / CV_PI;
    double angB = std::atan2(vby, vbx) * 180.0 / CV_PI;

    if (angle_mode_ == 1) {
        // 有向角：B 相对 A，归一化到 (-180, 180]
        double d = angB - angA;
        while (d > 180) d -= 360;
        while (d <= -180) d += 360;
        out_angle_ = d;
    } else {
        // 绝对角 (0-90)
        if (la > 1e-9 && lb > 1e-9) {
            double cosA = (vax * vbx + vay * vby) / (la * lb);
            cosA = std::max(-1.0, std::min(1.0, cosA));
            out_angle_ = std::acos(std::abs(cosA)) * 180.0 / CV_PI;
        } else {
            out_angle_ = 0.0;
        }
    }

    // 交点作为角度弧顶点
    double ix = 0, iy = 0;
    has_vertex_ = intersectPt(ax1, ay1, ax2, ay2, bx1, by1, bx2, by2, ix, iy);
    if (has_vertex_) {
        out_vertex_.x = ix;
        out_vertex_.y = iy;
    } else {
        // 平行：用 A 的起点作顶点
        out_vertex_.x = ax1;
        out_vertex_.y = ay1;
    }

    result_image_ = input_image_.clone();
    return VZ_OK;
}

int LineAngleAlgo::getOutput(const std::string& port_name,
                             void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "result_image") {
        VzImage* img = toVzImage(result_image_);
        if (!img) { *out_data = nullptr; *out_count = 0; *out_type_tag = VZ_TYPE_IMAGE; return VZ_OK; }
        *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE;
        return VZ_OK;
    }
    if (port_name == "angle") {
        auto* p = (double*)std::malloc(sizeof(double));
        *p = out_angle_; *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_DOUBLE;
        return VZ_OK;
    }
    if (port_name == "intersection") {
        auto* arr = (VzPoint2D*)std::malloc(sizeof(VzPoint2D));
        arr[0] = out_vertex_;
        *out_data = arr; *out_count = 1; *out_type_tag = VZ_TYPE_POINT2D_LIST;
        return VZ_OK;
    }
    setLastError("unknown output: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

void LineAngleAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* LineAngleAlgo::describe() const {
    return R"JSON({
  "type_id":"LineAngle","display_name":"线线角度","category":"对位",
  "algo_binding":{"dll":"vision_algo","algo_name":"LineAngle"},
  "inputs":[
    {"name":"line_a","type":"Point2DList","required":true,"description":"直线A(2个端点,绘制为绿色)"},
    {"name":"line_b","type":"Point2DList","required":true,"description":"直线B(2个端点,绘制为蓝色)"},
    {"name":"image","type":"Image","required":false,"description":"输入图像(可选,用于绘制叠加)"}
  ],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"结果图(绘制两线与角度弧)"},
    {"name":"angle","type":"Double","required":true,"description":"两线夹角(度)"},
    {"name":"intersection","type":"Point2DList","required":true,"description":"角度顶点(两线交点)"}
  ],
  "parameters":[
    {"name":"angle_mode","type":"Int","default":0,"description":"角度模式:0=绝对角(0-90),1=有向角(-180~180,B相对A)", "enum": [[0,"绝对角(0-90)"],[1,"有向角(-180~180)"]]},
    {"name":"draw_color","type":"String","default":"#FFFF00","description":"角度弧/顶点颜色(#RRGGBB 或 B,G,R)"},
    {"name":"draw_thickness","type":"Int","default":2,"min":1,"max":20,"description":"线宽(1-20)"},
    {"name":"point_size","type":"Int","default":6,"min":1,"max":50,"description":"顶点标记半径(1-50)"}
  ]
})JSON";
}

std::unique_ptr<AlgoBase> LineAngleAlgo::create() {
    return std::make_unique<LineAngleAlgo>();
}

} // namespace vz
