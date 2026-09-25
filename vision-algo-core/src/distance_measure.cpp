// distance_measure.cpp — 距离测量
#include "distance_measure.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

namespace vz {

using detail::parseColor;
using detail::parseThickness;
using detail::parsePointSize;
using detail::toCvMat;
using detail::toVzImage;

int DistanceMeasureAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "draw_color") { draw_color_ = parseColor(value_json, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (key == "draw_thickness") { draw_thickness_ = parseThickness(value_json, 2); return VZ_OK; }
    if (key == "point_size") { point_size_ = parsePointSize(value_json, 6); return VZ_OK; }
    setLastError("unknown parameter: " + key); return VZ_ERR_INVALID_ARG;
}

int DistanceMeasureAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    auto readPt = [&](double& x, double& y) {
        if (type_tag == VZ_TYPE_POSE2D) {
            auto* p = (const VzPose2D*)data; x = p->point.x; y = p->point.y; return true;
        }
        if (type_tag == VZ_TYPE_POINT2D_LIST) {
            auto* a = (const VzPoint2DArray*)data;
            if (a && a->count > 0) { x = a->data[0].x; y = a->data[0].y; return true; }
        }
        return false;
    };
    if (port_name == "a") { if (!readPt(ax_, ay_)) return VZ_ERR_TYPE_MISMATCH; has_a_=true; return VZ_OK; }
    if (port_name == "b") { if (!readPt(bx_, by_)) return VZ_ERR_TYPE_MISMATCH; has_b_=true; return VZ_OK; }
    if (port_name == "image") {
        if (type_tag != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH;
        input_image_ = toCvMat((const VzImage*)data);
        has_image_ = true;
        return VZ_OK;
    }
    setLastError("unknown input: " + port_name); return VZ_ERR_INVALID_ARG;
}

int DistanceMeasureAlgo::process() {
    if (!has_a_ || !has_b_) { setLastError("need inputs a and b"); return VZ_ERR_PROCESS; }
    double dx = bx_ - ax_, dy = by_ - ay_;
    out_distance_ = std::sqrt(dx*dx + dy*dy);

    result_image_ = input_image_.clone();
    return VZ_OK;
}

int DistanceMeasureAlgo::getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "result_image") { VzImage* img=toVzImage(result_image_); *out_data=img;*out_count=1;*out_type_tag=VZ_TYPE_IMAGE;return VZ_OK; }
    if (port_name == "distance") { auto* p=(double*)std::malloc(sizeof(double)); *p=out_distance_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_DOUBLE;return VZ_OK; }
    setLastError("unknown output: " + port_name); return VZ_ERR_INVALID_ARG;
}

void DistanceMeasureAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* DistanceMeasureAlgo::describe() const {
    return R"JSON({
  "type_id":"DistanceMeasure","display_name":"距离测量","category":"对位",
  "algo_binding":{"dll":"vision_algo","algo_name":"DistanceMeasure"},
  "inputs":[
    {"name":"a","type":"Pose2D","required":true,"description":"点A"},
    {"name":"b","type":"Pose2D","required":true,"description":"点B"},
    {"name":"image","type":"Image","required":false,"description":"输入图像(可选,用于绘制)"}
  ],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"结果图(绿色=连线,红=A,蓝=B)"},
    {"name":"distance","type":"Double","required":true,"description":"两点距离(像素)"}
  ],
  "parameters":[
    {"name":"draw_color","type":"String","default":"#00FF00","description":"连线颜色(#RRGGBB 或 B,G,R)"},
    {"name":"draw_thickness","type":"Int","default":2,"min":1,"max":20,"description":"线宽(1-20)"},
    {"name":"point_size","type":"Int","default":6,"min":1,"max":50,"description":"端点标记半径(1-50)"}
  ]
})JSON";
}

std::unique_ptr<AlgoBase> DistanceMeasureAlgo::create() { return std::make_unique<DistanceMeasureAlgo>(); }

} // namespace vz
