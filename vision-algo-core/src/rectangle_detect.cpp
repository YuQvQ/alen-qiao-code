// rectangle_detect.cpp — 矩形检测（轮廓 + 多边形近似 + 最小外接矩形）
#include "rectangle_detect.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace vz {

using detail::parseJsonDouble;
using detail::parseJsonInt;
using detail::parseJsonDouble4;
using detail::parseJsonDouble5;
using detail::parseColor;
using detail::parseThickness;
using detail::parsePointSize;
using detail::toCvMat;
using detail::toVzImage;

int RectangleDetectAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "draw_color") { draw_color_ = parseColor(value_json, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (key == "draw_thickness") { draw_thickness_ = parseThickness(value_json, 2); return VZ_OK; }
    if (key == "point_size") { point_size_ = parsePointSize(value_json, 5); return VZ_OK; }
    if (key == "min_area") { min_area_ = parseJsonDouble(value_json, 100.0); return VZ_OK; }
    if (key == "max_area_ratio") {
        max_area_ratio_ = parseJsonDouble(value_json, 0.95);
        if (max_area_ratio_ < 0.01) max_area_ratio_ = 0.01;
        if (max_area_ratio_ > 1.0) max_area_ratio_ = 1.0;
        return VZ_OK;
    }
    if (key == "approx_eps") {
        approx_eps_ = parseJsonDouble(value_json, 0.02);
        if (approx_eps_ < 0.001) approx_eps_ = 0.001;
        if (approx_eps_ > 0.2) approx_eps_ = 0.2;
        return VZ_OK;
    }
    if (key == "enable_binary") { enable_binary_ = parseJsonInt(value_json, 1) ? 1 : 0; return VZ_OK; }
    if (key == "bin_threshold") { bin_threshold_ = parseJsonDouble(value_json, 128.0); return VZ_OK; }
    if (key == "polarity") {
        polarity_ = parseJsonInt(value_json, 0);
        if (polarity_ < 0) polarity_ = 0;
        if (polarity_ > 2) polarity_ = 2;
        return VZ_OK;
    }
    if (key == "equalize_hist") { equalize_hist_ = parseJsonInt(value_json, 1) ? 1 : 0; return VZ_OK; }
    if (key == "enable_blur") { enable_blur_ = parseJsonInt(value_json, 1) ? 1 : 0; return VZ_OK; }
    if (key == "gaussian_ksize") {
        gaussian_ksize_ = parseJsonInt(value_json, 3);
        if (gaussian_ksize_ < 0) gaussian_ksize_ = 0;
        if (gaussian_ksize_ > 0 && gaussian_ksize_ % 2 == 0) gaussian_ksize_++;
        return VZ_OK;
    }
    if (key == "search_roi") {
        if (value_json.empty() || value_json == "\"\"" || value_json == "null") {
            has_search_roi_ = false; return VZ_OK;
        }
        double cx, cy, w, h, angle;
        if (parseJsonDouble5(value_json, cx, cy, w, h, angle)) {
            roi_cx_ = cx; roi_cy_ = cy; roi_w_ = w; roi_h_ = h; roi_angle_ = angle;
            has_search_roi_ = true; return VZ_OK;
        }
        double x, y, w2, h2;
        if (parseJsonDouble4(value_json, x, y, w2, h2)) {
            roi_cx_ = x + w2 / 2.0; roi_cy_ = y + h2 / 2.0;
            roi_w_ = w2; roi_h_ = h2; roi_angle_ = 0;
            has_search_roi_ = true; return VZ_OK;
        }
        has_search_roi_ = false; return VZ_OK;
    }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int RectangleDetectAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name != "image") {
        setLastError("unknown input port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    auto* img = const_cast<VzImage*>(static_cast<const VzImage*>(data));
    if (!img || !img->data) { setLastError("null image input"); return VZ_ERR_INVALID_ARG; }
    input_ = toCvMat(img);
    return VZ_OK;
}

int RectangleDetectAlgo::process() {
    out_corners_.clear();
    out_count_ = 0;
    out_width_ = 0; out_height_ = 0;

    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }

    // 转灰度
    cv::Mat gray;
    int ch = input_.channels();
    if (ch == 1) gray = input_;
    else if (ch == 3) cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    else if (ch == 4) cv::cvtColor(input_, gray, cv::COLOR_BGRA2GRAY);
    else { setLastError("unsupported image channels"); return VZ_ERR_PROCESS; }

    double roiOffsetX = 0, roiOffsetY = 0;
    cv::Mat workGray = gray;

    // 搜索区 ROI 裁剪（同 LineFind 的带方向矩形处理）
    if (has_search_roi_ && roi_w_ > 0 && roi_h_ > 0) {
        double rad = roi_angle_ * CV_PI / 180.0;
        double cosA = std::abs(std::cos(rad)), sinA = std::abs(std::sin(rad));
        double bbW = roi_w_ * cosA + roi_h_ * sinA;
        double bbH = roi_w_ * sinA + roi_h_ * cosA;
        cv::Rect bbox(
            (int)std::floor(roi_cx_ - bbW / 2.0),
            (int)std::floor(roi_cy_ - bbH / 2.0),
            (int)std::ceil(bbW), (int)std::ceil(bbH));
        bbox &= cv::Rect(0, 0, gray.cols, gray.rows);
        if (bbox.width <= 0 || bbox.height <= 0) {
            setLastError("search_roi out of image bounds"); return VZ_ERR_PROCESS;
        }
        cv::Mat cropped = gray(bbox);
        cv::Point2f localCenter((float)(roi_cx_ - bbox.x), (float)(roi_cy_ - bbox.y));
        cv::Mat rotLocal = cv::getRotationMatrix2D(localCenter, roi_angle_, 1.0);
        cv::Mat rotated;
        cv::warpAffine(cropped, rotated, rotLocal, bbox.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        int rx = (rotated.cols - (int)roi_w_) / 2, ry = (rotated.rows - (int)roi_h_) / 2;
        rx = std::max(0, rx); ry = std::max(0, ry);
        int rw = std::min((int)roi_w_, rotated.cols - rx);
        int rh = std::min((int)roi_h_, rotated.rows - ry);
        workGray = rotated(cv::Rect(rx, ry, rw, rh)).clone();
        roiOffsetX = bbox.x + rx;
        roiOffsetY = bbox.y + ry;
    }

    // 预处理
    cv::Mat proc = workGray;
    if (equalize_hist_) { cv::Mat eq; cv::equalizeHist(proc, eq); proc = eq; }
    cv::Mat blurred = proc;
    if (enable_blur_ && gaussian_ksize_ > 0)
        cv::GaussianBlur(proc, blurred, cv::Size(gaussian_ksize_, gaussian_ksize_), 0);

    // 二值化
    cv::Mat binary;
    if (polarity_ == 1) {
        // 亮目标
        cv::threshold(blurred, binary, bin_threshold_, 255, cv::THRESH_BINARY);
    } else if (polarity_ == 2) {
        // 暗目标
        cv::threshold(blurred, binary, bin_threshold_, 255, cv::THRESH_BINARY_INV);
    } else {
        // 自动：Otsu
        cv::threshold(blurred, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    }
    if (!enable_binary_) binary = blurred.clone();

    // 形态学闭运算，连接断裂边缘
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);

    // 轮廓检测
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    double imgArea = (double)workGray.rows * workGray.cols;
    double maxArea = imgArea * max_area_ratio_;

    // 局部坐标 -> 原图坐标（逆旋转 + 偏移）
    auto toOrig = [&](double lx, double ly) -> cv::Point2f {
        double x = lx + roiOffsetX, y = ly + roiOffsetY;
        if (has_search_roi_) {
            double rad = -roi_angle_ * CV_PI / 180.0;
            double cosA = std::cos(rad), sinA = std::sin(rad);
            double dx = x - roi_cx_, dy = y - roi_cy_;
            return cv::Point2f((float)(roi_cx_ + dx * cosA - dy * sinA),
                               (float)(roi_cy_ + dx * sinA + dy * cosA));
        }
        return cv::Point2f((float)x, (float)y);
    };

    // 筛选矩形轮廓：按面积降序，取最大的合格矩形
    struct RectCandidate { double area; std::vector<cv::Point2f> corners; cv::Point2f center; double w, h, angle; };
    std::vector<RectCandidate> candidates;

    for (auto& cnt : contours) {
        double area = cv::contourArea(cnt);
        if (area < min_area_ || area > maxArea) continue;
        double peri = cv::arcLength(cnt, true);
        if (peri < 1e-6) continue;
        std::vector<cv::Point> approx;
        cv::approxPolyDP(cnt, approx, approx_eps_ * peri, true);
        // 矩形判定：4个顶点 且 凸 且 面积占比合理
        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            cv::RotatedRect rr = cv::minAreaRect(approx);
            cv::Point2f pts[4]; rr.points(pts);
            std::vector<cv::Point2f> c(pts, pts + 4);
            candidates.push_back({area, c, rr.center, (double)rr.size.width, (double)rr.size.height, (double)rr.angle});
        }
    }

    // 若无四边形近似，回退：对所有轮廓取最小外接矩形中面积最大者
    if (candidates.empty()) {
        for (auto& cnt : contours) {
            double area = cv::contourArea(cnt);
            if (area < min_area_ || area > maxArea) continue;
            cv::RotatedRect rr = cv::minAreaRect(cnt);
            cv::Point2f pts[4]; rr.points(pts);
            std::vector<cv::Point2f> c(pts, pts + 4);
            candidates.push_back({area, c, rr.center, (double)rr.size.width, (double)rr.size.height, (double)rr.angle});
        }
    }

    if (candidates.empty()) {
        setLastError("no rectangle found");
        // 结果图：输入图像透传（标注由 UI 层绘制）
        result_image_ = input_.clone();
        return VZ_OK;
    }

    // 取面积最大的矩形
    std::sort(candidates.begin(), candidates.end(),
              [](const RectCandidate& a, const RectCandidate& b) { return a.area > b.area; });
    auto& best = candidates.front();

    // 角点排序：左上、右上、右下、左下
    auto& c = best.corners;
    // 计算中心
    double cx = 0, cy = 0;
    for (auto& p : c) { cx += p.x; cy += p.y; }
    cx /= 4; cy /= 4;
    // 按相对中心的角度排序（从左上开始顺时针）
    std::sort(c.begin(), c.end(), [&](const cv::Point2f& a, const cv::Point2f& b) {
        double angA = std::atan2(a.y - cy, a.x - cx);
        double angB = std::atan2(b.y - cy, b.x - cx);
        return angA < angB;
    });
    // 找到最左上（x+y 最小）作为起点，重排为顺时针
    int startIdx = 0;
    double minSum = 1e18;
    for (int i = 0; i < 4; ++i) {
        double s = c[i].x + c[i].y;
        if (s < minSum) { minSum = s; startIdx = i; }
    }
    std::vector<cv::Point2f> ordered;
    for (int i = 0; i < 4; ++i) ordered.push_back(c[(startIdx + i) % 4]);

    // 转换到原图坐标并输出
    for (auto& p : ordered) {
        cv::Point2f op = toOrig(p.x, p.y);
        VzPoint2D vp{}; vp.x = op.x; vp.y = op.y;
        out_corners_.push_back(vp);
    }
    cv::Point2f oc = toOrig(best.center.x, best.center.y);
    out_center_.point.x = oc.x;
    out_center_.point.y = oc.y;
    out_center_.angle = best.angle;
    out_width_ = std::max(best.w, best.h);
    out_height_ = std::min(best.w, best.h);
    out_count_ = 1;

    // 结果图：输入图像透传（标注由 UI 层绘制）
    result_image_ = input_.clone();
    return VZ_OK;
}

int RectangleDetectAlgo::getOutput(const std::string& port_name,
                                   void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "result_image") {
        VzImage* img = toVzImage(result_image_);
        if (!img) { *out_data = nullptr; *out_count = 0; *out_type_tag = VZ_TYPE_IMAGE; return VZ_OK; }
        *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE;
        return VZ_OK;
    }
    if (port_name == "corners") {
        int n = (int)out_corners_.size();
        auto* arr = (VzPoint2D*)std::malloc(sizeof(VzPoint2D) * (n ? n : 1));
        for (int i = 0; i < n; ++i) arr[i] = out_corners_[i];
        *out_data = arr; *out_count = n; *out_type_tag = VZ_TYPE_POINT2D_LIST;
        return VZ_OK;
    }
    if (port_name == "center") {
        auto* p = (VzPose2D*)std::malloc(sizeof(VzPose2D));
        *p = out_center_; *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_POSE2D;
        return VZ_OK;
    }
    if (port_name == "width") {
        auto* p = (double*)std::malloc(sizeof(double));
        *p = out_width_; *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_DOUBLE;
        return VZ_OK;
    }
    if (port_name == "height") {
        auto* p = (double*)std::malloc(sizeof(double));
        *p = out_height_; *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_DOUBLE;
        return VZ_OK;
    }
    if (port_name == "count") {
        auto* p = (int*)std::malloc(sizeof(int));
        *p = out_count_; *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_INT;
        return VZ_OK;
    }
    setLastError("unknown output port: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

void RectangleDetectAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* RectangleDetectAlgo::describe() const {
    return R"JSON({
  "type_id":"RectangleDetect","display_name":"矩形检测","category":"分析",
  "algo_binding":{"dll":"vision_algo","algo_name":"RectangleDetect"},
  "inputs":[
    {"name":"image","type":"Image","required":true,"description":"输入图像"}
  ],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"叠加检测矩形的结果图"},
    {"name":"corners","type":"Point2DList","required":true,"description":"矩形4个角点(顺时针:左上,右上,右下,左下)"},
    {"name":"center","type":"Pose2D","required":true,"description":"矩形中心+角度"},
    {"name":"width","type":"Double","required":true,"description":"矩形宽(像素)"},
    {"name":"height","type":"Double","required":true,"description":"矩形高(像素)"},
    {"name":"count","type":"Int","required":true,"description":"检测到的矩形数量"}
  ],
  "parameters":[
    {"name":"search_roi","type":"String","default":"","description":"搜索区[cx,cy,w,h,angle]"},
    {"name":"polarity","type":"Int","default":0,"description":"目标极性:0=自动(Otsu),1=亮目标,2=暗目标", "enum": [[0,"自动"],[1,"亮目标"],[2,"暗目标"]]},
    {"name":"min_area","type":"Double","default":100.0,"description":"最小面积(像素,小于此值的轮廓被忽略)"},
    {"name":"max_area_ratio","type":"Double","default":0.95,"description":"最大面积占比(相对图像0-1)"},
    {"name":"approx_eps","type":"Double","default":0.02,"description":"多边形近似精度(占周长比例,越小越精细)"},
    {"name":"equalize_hist","type":"Int","default":1,"description":"直方图均衡化(预处理):0=否,1=是", "enum": [[0,"否"],[1,"是"]]},
    {"name":"enable_blur","type":"Int","default":1,"description":"高斯模糊(预处理):0=否,1=是", "enum": [[0,"否"],[1,"是"]]},
    {"name":"gaussian_ksize","type":"Int","default":3,"description":"高斯模糊核大小(奇数)"},
    {"name":"enable_binary","type":"Int","default":1,"description":"二值化开关:0=否,1=是", "enum": [[0,"否"],[1,"是"]]},
    {"name":"bin_threshold","type":"Double","default":128.0,"description":"二值化阈值(极性非自动时生效,0-255)"},
    {"name":"draw_color","type":"String","default":"#00FF00","description":"矩形绘制颜色(#RRGGBB 或 B,G,R)"},
    {"name":"draw_thickness","type":"Int","default":2,"min":1,"max":20,"description":"线宽(1-20)"},
    {"name":"point_size","type":"Int","default":5,"min":1,"max":50,"description":"角点标记半径(1-50)"}
  ]
})JSON";
}

std::unique_ptr<AlgoBase> RectangleDetectAlgo::create() {
    return std::make_unique<RectangleDetectAlgo>();
}

} // namespace vz
