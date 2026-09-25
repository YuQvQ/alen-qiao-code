// edge_finder.cpp — 沿扫描线亚像素边缘查找
#include "edge_finder.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

namespace vz {

using detail::parseJsonDouble;
using detail::parseJsonDouble5;
using detail::parseJsonDouble4;
using detail::parseColor;
using detail::parseThickness;
using detail::parsePointSize;
using detail::toCvMat;
using detail::toVzImage;

int EdgeFinderAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "polarity") { polarity_ = detail::parseJsonInt(value_json, 0); if (polarity_<0)polarity_=0; if (polarity_>2)polarity_=2; return VZ_OK; }
    if (key == "threshold") { threshold_ = parseJsonDouble(value_json, 30.0); return VZ_OK; }
    if (key == "subpixel") { subpixel_ = detail::parseJsonInt(value_json, 1); if (subpixel_!=0) subpixel_=1; return VZ_OK; }
    if (key == "draw_color") { draw_color_ = parseColor(value_json, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (key == "draw_thickness") { draw_thickness_ = parseThickness(value_json, 2); return VZ_OK; }
    if (key == "point_size") { point_size_ = parsePointSize(value_json, 6); return VZ_OK; }
    if (key == "roi") {
        if (value_json.empty() || value_json == "\"\"" || value_json == "null") { has_roi_ = false; return VZ_OK; }
        if (parseJsonDouble5(value_json, roi_cx_, roi_cy_, roi_w_, roi_h_, roi_angle_)) { has_roi_ = true; return VZ_OK; }
        double x,y,w,h;
        if (parseJsonDouble4(value_json, x, y, w, h)) { roi_cx_=x+w/2; roi_cy_=y+h/2; roi_w_=w; roi_h_=h; roi_angle_=0; has_roi_=true; return VZ_OK; }
        // 解析失败：视为未设置 ROI（兼容空值/默认值），不报错
        has_roi_ = false; return VZ_OK;
    }
    setLastError("unknown parameter: " + key); return VZ_ERR_INVALID_ARG;
}

int EdgeFinderAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "image") {
        if (type_tag != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH;
        input_ = toCvMat((const VzImage*)data);
        return VZ_OK;
    }
    setLastError("unknown input: " + port_name); return VZ_ERR_INVALID_ARG;
}

int EdgeFinderAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Mat gray;
    int ch = input_.channels();
    if (ch == 1) gray = input_;
    else if (ch == 3) cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    else if (ch == 4) cv::cvtColor(input_, gray, cv::COLOR_BGRA2GRAY);
    else return VZ_ERR_PROCESS;

    // ROI 处理：取中心行扫描线
    int scanY, startX, endX;
    if (has_roi_) {
        scanY = (int)roi_cy_;
        startX = (int)(roi_cx_ - roi_w_ / 2);
        endX = (int)(roi_cx_ + roi_w_ / 2);
    } else {
        scanY = gray.rows / 2;
        startX = 0;
        endX = gray.cols - 1;
    }
    scanY = std::max(0, std::min(gray.rows - 1, scanY));
    startX = std::max(0, startX);
    endX = std::min(gray.cols - 1, endX);
    if (endX <= startX) { setLastError("invalid scan range"); return VZ_ERR_PROCESS; }

    // 沿扫描线计算梯度，找最大梯度点
    double bestGrad = 0;
    int bestX = startX;
    int bestDir = 0;
    const uchar* row = gray.ptr<uchar>(scanY);
    for (int x = startX + 1; x < endX; ++x) {
        double grad = (double)row[x + 1] - (double)row[x - 1];
        double absGrad = std::abs(grad);
        bool match = false;
        if (polarity_ == 0) match = (absGrad > threshold_);
        else if (polarity_ == 1) match = (grad < -threshold_);  // 白到黑：负梯度
        else match = (grad > threshold_);                        // 黑到白：正梯度
        if (match && absGrad > bestGrad) {
            bestGrad = absGrad;
            bestX = x;
            bestDir = (grad >= 0) ? 1 : -1;
        }
    }

    if (bestGrad == 0) { setLastError("no edge found"); return VZ_ERR_PROCESS; }

    double ex = bestX;
    double ey = scanY;
    // 亚像素：抛物线拟合
    if (subpixel_ && bestX > startX + 1 && bestX < endX - 1) {
        double g0 = std::abs((double)row[bestX] - (double)row[bestX - 1]);
        double g1 = std::abs((double)row[bestX + 1] - (double)row[bestX - 1]) / 2.0;
        double g2 = std::abs((double)row[bestX + 1] - (double)row[bestX]);
        if (g0 + g2 - 2 * g1 != 0) {
            double delta = (g0 - g2) / (2.0 * (g0 + g2 - 2 * g1));
            ex = bestX + delta;
        }
    }

    out_x_ = ex;
    out_y_ = ey;
    out_angle_ = 0;
    out_score_ = bestGrad;
    out_dir_ = bestDir;

    // 结果图：输入图像透传（标注由 UI 层绘制）
    result_image_ = input_.clone();
    return VZ_OK;
}

int EdgeFinderAlgo::getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "result_image") {
        VzImage* img = toVzImage(result_image_);
        if (!img) { *out_data=nullptr; *out_count=0; *out_type_tag=VZ_TYPE_IMAGE; return VZ_OK; }
        *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE; return VZ_OK;
    }
    if (port_name == "point") {
        auto* p = (VzPose2D*)std::malloc(sizeof(VzPose2D));
        p->point.x = out_x_; p->point.y = out_y_; p->angle = out_angle_;
        *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_POSE2D; return VZ_OK;
    }
    if (port_name == "score") {
        auto* p = (double*)std::malloc(sizeof(double)); *p = out_score_;
        *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_DOUBLE; return VZ_OK;
    }
    if (port_name == "direction") {
        auto* p = (int*)std::malloc(sizeof(int)); *p = out_dir_;
        *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_INT; return VZ_OK;
    }
    setLastError("unknown output: " + port_name); return VZ_ERR_INVALID_ARG;
}

void EdgeFinderAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* EdgeFinderAlgo::describe() const {
    return R"JSON({
  "type_id":"EdgeFinder","display_name":"边缘查找","category":"对位",
  "algo_binding":{"dll":"vision_algo","algo_name":"EdgeFinder"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"结果图"},
    {"name":"point","type":"Pose2D","required":true,"description":"边缘点位置"},
    {"name":"score","type":"Double","required":true,"description":"边缘强度"},
    {"name":"direction","type":"Int","required":true,"description":"方向:1=黑到白,-1=白到黑"}
  ],
  "parameters":[
    {"name":"roi","type":"String","default":"","description":"扫描ROI[cx,cy,w,h,angle]"},
    {"name":"polarity","type":"Int","default":0,"description":"边缘方向","enum":[[0,"任意"],[1,"白到黑"],[2,"黑到白"]]},
    {"name":"threshold","type":"Double","default":30.0,"description":"梯度阈值"},
    {"name":"subpixel","type":"Int","default":1,"description":"亚像素精度","enum":[[0,"否"],[1,"是"]]},
    {"name":"draw_color","type":"String","default":"#00FF00","description":"标注颜色(#RRGGBB)"},
    {"name":"draw_thickness","type":"Int","default":2,"description":"线粗细(1-20)"},
    {"name":"point_size","type":"Int","default":6,"description":"边缘点大小(1-50)"}
  ]
})JSON";
}

std::unique_ptr<AlgoBase> EdgeFinderAlgo::create() { return std::make_unique<EdgeFinderAlgo>(); }

} // namespace vz
