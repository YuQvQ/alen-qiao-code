// shape_match.cpp — 形状匹配(Hu矩)
#include "shape_match.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <algorithm>
namespace vz {
using detail::parseJsonDouble; using detail::parseJsonInt; using detail::parseColor;
using detail::parseThickness; using detail::toCvMat; using detail::toVzImage;
int ShapeMatchAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "threshold") { threshold_ = parseJsonDouble(v, 0.5); return VZ_OK; }
    if (k == "min_area") { min_area_ = parseJsonDouble(v, 100.0); return VZ_OK; }
    if (k == "draw_color") { draw_color_ = parseColor(v, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (k == "draw_thickness") { draw_thickness_ = parseThickness(v, 2); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int ShapeMatchAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; image_ = toCvMat((const VzImage*)d); return VZ_OK; }
    if (p == "template") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; template_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
static cv::Mat largestContourBinary(const cv::Mat& img, double minArea) {
    cv::Mat gray; int c = img.channels();
    if (c==1) gray = img; else if (c==3) cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    else if (c==4) cv::cvtColor(img, gray, cv::COLOR_BGRA2GRAY); else gray = img;
    cv::Mat bin; cv::threshold(gray, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    std::vector<std::vector<cv::Point>> cnts; cv::findContours(bin, cnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    cv::Mat out = cv::Mat::zeros(img.size(), CV_8UC1);
    double best = -1; int bi = -1;
    for (size_t i = 0; i < cnts.size(); ++i) { double a = cv::contourArea(cnts[i]); if (a >= minArea && a > best) { best = a; bi = (int)i; } }
    if (bi >= 0) cv::drawContours(out, cnts, bi, 255, cv::FILLED);
    return out;
}
int ShapeMatchAlgo::process() {
    out_score_ = 0.0; out_match_ = 0;
    if (image_.empty() || template_.empty()) { setLastError("image and template required"); return VZ_ERR_PROCESS; }
    cv::Mat b1 = largestContourBinary(image_, min_area_);
    cv::Mat b2 = largestContourBinary(template_, min_area_);
    result_image_ = image_.clone();
    double d = cv::matchShapes(b1, b2, cv::CONTOURS_MATCH_I1, 0);
    // d 越小越相似; 映射到 0-1 分数
    double score = std::exp(-d);
    out_score_ = score;
    out_match_ = (score >= threshold_) ? 1 : 0;
    return VZ_OK;
}
int ShapeMatchAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "result_image") { *o = toVzImage(result_image_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    if (p == "score") { auto* x = (double*)std::malloc(sizeof(double)); *x = out_score_; *o = x; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    if (p == "match") { auto* x = (int*)std::malloc(sizeof(int)); *x = out_match_; *o = x; *n = 1; *tt = VZ_TYPE_BOOL; return VZ_OK; }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void ShapeMatchAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* ShapeMatchAlgo::describe() const {
    return R"JSON({
  "type_id":"ShapeMatch","display_name":"形状匹配","category":"对位",
  "algo_binding":{"dll":"vision_algo","algo_name":"ShapeMatch"},
  "inputs":[
    {"name":"image","type":"Image","required":true,"description":"目标图像"},
    {"name":"template","type":"Image","required":true,"description":"模板形状图像"}
  ],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"结果图(匹配=绿,不匹配=红)"},
    {"name":"score","type":"Double","required":true,"description":"相似度(0-1,越大越相似)"},
    {"name":"match","type":"Bool","required":true,"description":"是否匹配(score>=threshold)"}
  ],
  "parameters":[
    {"name":"threshold","type":"Double","default":0.5,"description":"匹配阈值(0-1)"},
    {"name":"min_area","type":"Double","default":100.0,"description":"最小轮廓面积"},
    {"name":"draw_color","type":"String","default":"#00FF00","description":"匹配时绘制颜色"},
    {"name":"draw_thickness","type":"Int","default":2,"min":1,"max":20,"description":"线宽(1-20)"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> ShapeMatchAlgo::create() { return std::make_unique<ShapeMatchAlgo>(); }
} // namespace vz
