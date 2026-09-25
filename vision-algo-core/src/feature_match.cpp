// feature_match.cpp — 特征匹配(ORB + 汉明距离 + 比率测试)
#include "feature_match.h"
#include "algo_utils.h"
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <algorithm>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::parseColor;
using detail::parseThickness; using detail::toCvMat; using detail::toVzImage;
int FeatureMatchAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "n_features") { n_features_ = parseJsonInt(v, 500); if (n_features_ < 50) n_features_ = 50; if (n_features_ > 10000) n_features_ = 10000; return VZ_OK; }
    if (k == "ratio") { ratio_ = parseJsonDouble(v, 0.75); if (ratio_ < 0.1) ratio_ = 0.1; if (ratio_ > 1.0) ratio_ = 1.0; return VZ_OK; }
    if (k == "draw_color") { draw_color_ = parseColor(v, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (k == "draw_thickness") { draw_thickness_ = parseThickness(v, 1); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int FeatureMatchAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; image_ = toCvMat((const VzImage*)d); return VZ_OK; }
    if (p == "template") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; template_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int FeatureMatchAlgo::process() {
    tgt_pts_.clear(); tpl_pts_.clear(); count_ = 0;
    if (image_.empty() || template_.empty()) { setLastError("image and template required"); return VZ_ERR_PROCESS; }
    cv::Mat g1, g2;
    auto toG = [](const cv::Mat& m)->cv::Mat{ int c=m.channels(); if(c==1)return m; if(c==3){cv::Mat r;cv::cvtColor(m,r,cv::COLOR_BGR2GRAY);return r;} if(c==4){cv::Mat r;cv::cvtColor(m,r,cv::COLOR_BGRA2GRAY);return r;} return m; };
    g1 = toG(image_); g2 = toG(template_);
    auto orb = cv::ORB::create(n_features_);
    std::vector<cv::KeyPoint> kp1, kp2; cv::Mat d1, d2;
    orb->detectAndCompute(g1, cv::noArray(), kp1, d1);
    orb->detectAndCompute(g2, cv::noArray(), kp2, d2);
    if (kp1.empty() || kp2.empty()) {
        result_image_ = image_.clone();
        setLastError("no features detected"); return VZ_OK;
    }
    cv::BFMatcher matcher(cv::NORM_HAMMING);
    std::vector<std::vector<cv::DMatch>> knn;
    matcher.knnMatch(d2, d1, knn, 2);  // 模板->目标
    std::vector<cv::Point2f> src, dst;
    for (auto& m : knn) {
        if (m.size() < 2) continue;
        if (m[0].distance < ratio_ * m[1].distance) {
            src.push_back(kp2[m[0].queryIdx].pt);   // 模板点
            dst.push_back(kp1[m[0].trainIdx].pt);   // 目标点
        }
    }
    // RANSAC 提纯
    if (src.size() >= 4) {
        std::vector<uchar> inlierMask;
        cv::findHomography(src, dst, cv::RANSAC, 3.0, inlierMask);
        std::vector<cv::Point2f> fs, fd;
        for (size_t i = 0; i < inlierMask.size(); ++i) if (inlierMask[i]) { fs.push_back(src[i]); fd.push_back(dst[i]); }
        if (!fs.empty()) { src = fs; dst = fd; }
    }
    count_ = (int)src.size();
    for (auto& p : src) { VzPoint2D v{}; v.x = p.x; v.y = p.y; tpl_pts_.push_back(v); }
    for (auto& p : dst) { VzPoint2D v{}; v.x = p.x; v.y = p.y; tgt_pts_.push_back(v); }
    result_image_ = image_.clone();
    return VZ_OK;
}
int FeatureMatchAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "result_image") { *o = toVzImage(result_image_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    if (p == "count") { auto* x = (int*)std::malloc(sizeof(int)); *x = count_; *o = x; *n = 1; *tt = VZ_TYPE_INT; return VZ_OK; }
    if (p == "target_points" || p == "template_points") {
        auto& ref = (p == "target_points") ? tgt_pts_ : tpl_pts_;
        int sz = (int)ref.size();
        auto* arr = (VzPoint2D*)std::malloc(sizeof(VzPoint2D) * (sz ? sz : 1));
        for (int i = 0; i < sz; ++i) arr[i] = ref[i];
        *o = arr; *n = sz; *tt = VZ_TYPE_POINT2D_LIST; return VZ_OK;
    }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void FeatureMatchAlgo::releaseOutput(void* p) { detail::safeReleaseOutput(p); }
const char* FeatureMatchAlgo::describe() const {
    return R"JSON({
  "type_id":"FeatureMatch","display_name":"特征匹配","category":"对位",
  "algo_binding":{"dll":"vision_algo","algo_name":"FeatureMatch"},
  "inputs":[
    {"name":"image","type":"Image","required":true,"description":"目标图像"},
    {"name":"template","type":"Image","required":true,"description":"模板图像"}
  ],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"匹配连线结果图"},
    {"name":"count","type":"Int","required":true,"description":"匹配点对数量(经RANSAC提纯)"},
    {"name":"target_points","type":"Point2DList","required":true,"description":"目标图像上的匹配点"},
    {"name":"template_points","type":"Point2DList","required":true,"description":"模板图像上的匹配点"}
  ],
  "parameters":[
    {"name":"n_features","type":"Int","default":500,"description":"ORB特征点上限"},
    {"name":"ratio","type":"Double","default":0.75,"description":"Lowe比率测试阈值(越小越严格)"},
    {"name":"draw_color","type":"String","default":"#00FF00","description":"匹配线颜色(#RRGGBB 或 B,G,R)"},
    {"name":"draw_thickness","type":"Int","default":1,"min":1,"max":20,"description":"线宽(1-20)"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> FeatureMatchAlgo::create() { return std::make_unique<FeatureMatchAlgo>(); }
} // namespace vz
