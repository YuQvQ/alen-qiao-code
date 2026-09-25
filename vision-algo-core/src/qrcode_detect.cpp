// qrcode_detect.cpp — 二维码检测与解码
#include "qrcode_detect.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <cstring>
namespace vz {
using detail::parseColor; using detail::parseThickness; using detail::parsePointSize;
using detail::toCvMat; using detail::toVzImage;
int QrCodeDetectAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "draw_color") { draw_color_ = parseColor(v, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (k == "draw_thickness") { draw_thickness_ = parseThickness(v, 2); return VZ_OK; }
    if (k == "point_size") { point_size_ = parsePointSize(v, 5); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int QrCodeDetectAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
int QrCodeDetectAlgo::process() {
    out_content_.clear(); out_points_.clear(); out_count_ = 0;
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Mat gray; int c = input_.channels();
    if (c==1) gray = input_; else if (c==3) cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    else if (c==4) cv::cvtColor(input_, gray, cv::COLOR_BGRA2GRAY); else gray = input_;
    result_image_ = input_.clone();
    cv::QRCodeDetector det;
    cv::Mat points;
    std::string info = det.detectAndDecode(gray, points);
    if (!info.empty() && !points.empty()) {
        out_content_ = info;
        out_count_ = 1;
        for (int i = 0; i < points.rows; ++i) {
            VzPoint2D vp{}; vp.x = points.at<float>(i, 0); vp.y = points.at<float>(i, 1);
            out_points_.push_back(vp);
        }
    }
    return VZ_OK;
}
int QrCodeDetectAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "result_image") { *o = toVzImage(result_image_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    if (p == "content") {
        size_t sz = out_content_.size();
        char* buf = (char*)std::malloc(sz + 1);
        std::memcpy(buf, out_content_.c_str(), sz + 1);
        *o = buf; *n = (int)sz; *tt = VZ_TYPE_STRING; return VZ_OK;
    }
    if (p == "count") { auto* x = (int*)std::malloc(sizeof(int)); *x = out_count_; *o = x; *n = 1; *tt = VZ_TYPE_INT; return VZ_OK; }
    if (p == "points") {
        int sz = (int)out_points_.size();
        auto* arr = (VzPoint2D*)std::malloc(sizeof(VzPoint2D) * (sz ? sz : 1));
        for (int i = 0; i < sz; ++i) arr[i] = out_points_[i];
        *o = arr; *n = sz; *tt = VZ_TYPE_POINT2D_LIST; return VZ_OK;
    }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void QrCodeDetectAlgo::releaseOutput(void* p) { if (p) std::free(p); }
const char* QrCodeDetectAlgo::describe() const {
    return R"JSON({
  "type_id":"QrCodeDetect","display_name":"二维码检测","category":"识别",
  "algo_binding":{"dll":"vision_algo","algo_name":"QrCodeDetect"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"绘制二维码框的结果图"},
    {"name":"content","type":"String","required":true,"description":"解码内容(无则空)"},
    {"name":"count","type":"Int","required":true,"description":"检测数量"},
    {"name":"points","type":"Point2DList","required":true,"description":"二维码4个角点"}
  ],
  "parameters":[
    {"name":"draw_color","type":"String","default":"#00FF00","description":"框颜色(#RRGGBB 或 B,G,R)"},
    {"name":"draw_thickness","type":"Int","default":2,"min":1,"max":20,"description":"线宽(1-20)"},
    {"name":"point_size","type":"Int","default":5,"min":1,"max":50,"description":"角点半径(1-50)"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> QrCodeDetectAlgo::create() { return std::make_unique<QrCodeDetectAlgo>(); }
} // namespace vz
