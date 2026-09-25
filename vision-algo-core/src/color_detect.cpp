// color_detect.cpp — 颜色检测
#include "color_detect.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::parseColor;
using detail::parseThickness; using detail::parsePointSize; using detail::toCvMat; using detail::toVzImage;
int ColorDetectAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "h_min") { h_min_ = parseJsonInt(v, 0); return VZ_OK; }
    if (k == "h_max") { h_max_ = parseJsonInt(v, 10); return VZ_OK; }
    if (k == "s_min") { s_min_ = parseJsonInt(v, 100); return VZ_OK; }
    if (k == "s_max") { s_max_ = parseJsonInt(v, 255); return VZ_OK; }
    if (k == "v_min") { v_min_ = parseJsonInt(v, 100); return VZ_OK; }
    if (k == "v_max") { v_max_ = parseJsonInt(v, 255); return VZ_OK; }
    if (k == "min_area") { min_area_ = parseJsonDouble(v, 100.0); return VZ_OK; }
    if (k == "draw_color") { draw_color_ = parseColor(v, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (k == "draw_thickness") { draw_thickness_ = parseThickness(v, 2); return VZ_OK; }
    if (k == "point_size") { point_size_ = parsePointSize(v, 5); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int ColorDetectAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int ColorDetectAlgo::process() {
    out_area_ = 0; out_count_ = 0;
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Mat hsv; int c = input_.channels();
    if (c==3) cv::cvtColor(input_, hsv, cv::COLOR_BGR2HSV);
    else if (c==4) { cv::Mat bgr; cv::cvtColor(input_, bgr, cv::COLOR_BGRA2BGR); cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV); }
    else if (c==1) cv::cvtColor(input_, hsv, cv::COLOR_GRAY2BGR), cv::cvtColor(hsv, hsv, cv::COLOR_BGR2HSV);
    else hsv = input_;
    // 处理 H 跨越 0 度(如红色)
    cv::Mat mask;
    if (h_min_ <= h_max_) {
        cv::inRange(hsv, cv::Scalar(h_min_, s_min_, v_min_), cv::Scalar(h_max_, s_max_, v_max_), mask);
    } else {
        cv::Mat m1, m2;
        cv::inRange(hsv, cv::Scalar(h_min_, s_min_, v_min_), cv::Scalar(180, s_max_, v_max_), m1);
        cv::inRange(hsv, cv::Scalar(0, s_min_, v_min_), cv::Scalar(h_max_, s_max_, v_max_), m2);
        cv::bitwise_or(m1, m2, mask);
    }
    // 形态学去噪
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
    result_image_ = input_.clone();
    std::vector<std::vector<cv::Point>> cnts;
    cv::findContours(mask, cnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    out_count_ = 0; double best = -1; int bi = -1;
    for (size_t i = 0; i < cnts.size(); ++i) {
        double a = cv::contourArea(cnts[i]);
        if (a >= min_area_) { out_count_++; if (a > best) { best = a; bi = (int)i; } }
    }
    if (bi >= 0) {
        out_area_ = best;
        cv::Moments m = cv::moments(cnts[bi]);
        double cx = m.m10 / std::max(1e-6, m.m00), cy = m.m01 / std::max(1e-6, m.m00);
        out_center_.point.x = cx; out_center_.point.y = cy;
    }
    return VZ_OK;
}
int ColorDetectAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "result_image") { *o = toVzImage(result_image_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    if (p == "center") { auto* x = (VzPose2D*)std::malloc(sizeof(VzPose2D)); *x = out_center_; *o = x; *n = 1; *tt = VZ_TYPE_POSE2D; return VZ_OK; }
    if (p == "area") { auto* x = (double*)std::malloc(sizeof(double)); *x = out_area_; *o = x; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    if (p == "count") { auto* x = (int*)std::malloc(sizeof(int)); *x = out_count_; *o = x; *n = 1; *tt = VZ_TYPE_INT; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void ColorDetectAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* ColorDetectAlgo::describe() const {
    return R"JSON({
  "type_id":"ColorDetect","display_name":"颜色检测","category":"分析",
  "algo_binding":{"dll":"vision_algo","algo_name":"ColorDetect"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"绘制最大色块的结果图"},
    {"name":"center","type":"Pose2D","required":true,"description":"最大色块中心"},
    {"name":"area","type":"Double","required":true,"description":"最大色块面积(像素²)"},
    {"name":"count","type":"Int","required":true,"description":"色块数量"}
  ],
  "parameters":[
    {"name":"h_min","type":"Int","default":0,"description":"H下限(0-180)"},
    {"name":"h_max","type":"Int","default":10,"description":"H上限(0-180,可小于下限以跨越0度)"},
    {"name":"s_min","type":"Int","default":100,"description":"S下限(0-255)"},
    {"name":"s_max","type":"Int","default":255,"description":"S上限"},
    {"name":"v_min","type":"Int","default":100,"description":"V下限"},
    {"name":"v_max","type":"Int","default":255,"description":"V上限"},
    {"name":"min_area","type":"Double","default":100.0,"description":"最小面积过滤"},
    {"name":"draw_color","type":"String","default":"#00FF00","description":"绘制颜色(#RRGGBB 或 B,G,R)"},
    {"name":"draw_thickness","type":"Int","default":2,"min":1,"max":20,"description":"线宽(1-20)"},
    {"name":"point_size","type":"Int","default":5,"min":1,"max":50,"description":"中心标记半径(1-50)"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> ColorDetectAlgo::create() { return std::make_unique<ColorDetectAlgo>(); }
} // namespace vz
