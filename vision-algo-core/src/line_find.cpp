// line_find.cpp — 直线查找算法实现（HoughLinesP 概率霍夫直线检测）
#include "line_find.h"
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
using detail::toVzImage;

int LineFindAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "draw_color") { draw_color_ = parseColor(value_json, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (key == "draw_thickness") { draw_thickness_ = parseThickness(value_json, 2); return VZ_OK; }
    if (key == "point_size") { point_size_ = parsePointSize(value_json, 3); return VZ_OK; }
    if (key == "bin_threshold" || key == "threshold") {  // threshold 为旧名兼容
        bin_threshold_ = parseJsonDouble(value_json, 128.0);
        return VZ_OK;
    }
    if (key == "enable_binary") {
        enable_binary_ = parseJsonInt(value_json, 1);
        if (enable_binary_ != 0) enable_binary_ = 1;
        return VZ_OK;
    }
    if (key == "polarity") {
        polarity_ = parseJsonInt(value_json, 0);
        if (polarity_ < 0) polarity_ = 0;
        if (polarity_ > 2) polarity_ = 2;
        return VZ_OK;
    }
    if (key == "min_line_length") {
        min_line_length_ = parseJsonDouble(value_json, 30.0);
        return VZ_OK;
    }
    if (key == "max_line_gap") {
        max_line_gap_ = parseJsonDouble(value_json, 10.0);
        return VZ_OK;
    }
    if (key == "canny_low") {
        canny_low_ = parseJsonDouble(value_json, 50.0);
        return VZ_OK;
    }
    if (key == "canny_high") {
        canny_high_ = parseJsonDouble(value_json, 150.0);
        return VZ_OK;
    }
    if (key == "hough_threshold") {
        hough_threshold_ = parseJsonInt(value_json, 15);
        if (hough_threshold_ < 1) hough_threshold_ = 1;
        return VZ_OK;
    }
    if (key == "enable_blur") {
        enable_blur_ = parseJsonInt(value_json, 1);
        if (enable_blur_ != 0) enable_blur_ = 1;
        return VZ_OK;
    }
    if (key == "gaussian_ksize") {
        gaussian_ksize_ = parseJsonInt(value_json, 3);
        if (gaussian_ksize_ < 0) gaussian_ksize_ = 0;
        if (gaussian_ksize_ > 0 && gaussian_ksize_ % 2 == 0) gaussian_ksize_++; // 必须奇数
        return VZ_OK;
    }
    if (key == "equalize_hist") {
        equalize_hist_ = parseJsonInt(value_json, 1);
        if (equalize_hist_ != 0) equalize_hist_ = 1;
        return VZ_OK;
    }
    if (key == "edge_method") {
        edge_method_ = parseJsonInt(value_json, 0);
        if (edge_method_ < 0) edge_method_ = 0;
        if (edge_method_ > 1) edge_method_ = 1;
        return VZ_OK;
    }
    if (key == "segment_count") {
        segment_count_ = parseJsonInt(value_json, 10);
        if (segment_count_ < 3) segment_count_ = 3;
        if (segment_count_ > 100) segment_count_ = 100;
        return VZ_OK;
    }
    if (key == "search_roi") {
        // 搜索区：[cx, cy, w, h, angle]（带方向）或 [x, y, w, h]（轴对齐）
        if (value_json.empty() || value_json == "\"\"" || value_json == "null") {
            has_search_roi_ = false;
            return VZ_OK;
        }
        double cx, cy, w, h, angle;
        if (parseJsonDouble5(value_json, cx, cy, w, h, angle)) {
            roi_cx_ = cx; roi_cy_ = cy; roi_w_ = w; roi_h_ = h; roi_angle_ = angle;
            has_search_roi_ = true;
            return VZ_OK;
        }
        double x, y, w2, h2;
        if (parseJsonDouble4(value_json, x, y, w2, h2)) {
            roi_cx_ = x + w2 / 2.0; roi_cy_ = y + h2 / 2.0;
            roi_w_ = w2; roi_h_ = h2; roi_angle_ = 0;
            has_search_roi_ = true;
            return VZ_OK;
        }
        // 解析失败：视为未设置搜索区（兼容空值/默认值），不报错
        has_search_roi_ = false;
        return VZ_OK;
    }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int LineFindAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name != "image") {
        setLastError("unknown input port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    auto* img = const_cast<VzImage*>(static_cast<const VzImage*>(data));
    if (!img || !img->data) {
        setLastError("null image input");
        return VZ_ERR_INVALID_ARG;
    }
    input_ = detail::toCvMat(img);
    return VZ_OK;
}

int LineFindAlgo::process() {
    out_lines_.clear();
    out_count_ = 0;

    if (input_.empty()) {
        setLastError("no input image");
        return VZ_ERR_PROCESS;
    }

    // 转灰度（兼容 1/3/4 通道）
    cv::Mat gray;
    int ch = input_.channels();
    if (ch == 1) gray = input_;
    else if (ch == 3) cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    else if (ch == 4) cv::cvtColor(input_, gray, cv::COLOR_BGRA2GRAY);
    else { setLastError("unsupported image channels"); return VZ_ERR_PROCESS; }

    // 搜索区 ROI 偏移量（结果坐标需要加回）
    double roiOffsetX = 0, roiOffsetY = 0;

    cv::Mat workGray = gray;

    if (has_search_roi_ && roi_w_ > 0 && roi_h_ > 0) {
        // 带方向矩形 ROI：先裁剪外接矩形，再在局部坐标内旋转
        double rad = roi_angle_ * CV_PI / 180.0;
        double cosA = std::abs(std::cos(rad));
        double sinA = std::abs(std::sin(rad));
        double bbW = roi_w_ * cosA + roi_h_ * sinA;
        double bbH = roi_w_ * sinA + roi_h_ * cosA;
        cv::Rect bbox(
            (int)std::floor(roi_cx_ - bbW / 2.0),
            (int)std::floor(roi_cy_ - bbH / 2.0),
            (int)std::ceil(bbW),
            (int)std::ceil(bbH));
        bbox &= cv::Rect(0, 0, gray.cols, gray.rows);
        if (bbox.width <= 0 || bbox.height <= 0) {
            setLastError("search_roi out of image bounds");
            return VZ_ERR_PROCESS;
        }
        cv::Mat cropped = gray(bbox);
        // 旋转中心使用裁剪后的局部坐标（关键修复：原代码用原图坐标导致平移错误）
        cv::Point2f localCenter((float)(roi_cx_ - bbox.x), (float)(roi_cy_ - bbox.y));
        cv::Mat rotLocal = cv::getRotationMatrix2D(localCenter, roi_angle_, 1.0);
        cv::Mat rotated;
        cv::warpAffine(cropped, rotated, rotLocal, bbox.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        // 只取 ROI 中心区域（roi_w x roi_h）
        int rx = (rotated.cols - (int)roi_w_) / 2;
        int ry = (rotated.rows - (int)roi_h_) / 2;
        rx = std::max(0, rx); ry = std::max(0, ry);
        int rw = std::min((int)roi_w_, rotated.cols - rx);
        int rh = std::min((int)roi_h_, rotated.rows - ry);
        workGray = rotated(cv::Rect(rx, ry, rw, rh)).clone();
        roiOffsetX = bbox.x + rx;
        roiOffsetY = bbox.y + ry;
    }

    // 预处理：可选直方图均衡化 + 可选高斯模糊
    cv::Mat proc = workGray;
    if (equalize_hist_) {
        cv::Mat eq;
        cv::equalizeHist(proc, eq);
        proc = eq;
    }
    cv::Mat blurred = proc;
    if (enable_blur_ && gaussian_ksize_ > 0) {
        cv::GaussianBlur(proc, blurred, cv::Size(gaussian_ksize_, gaussian_ksize_), 0);
    }

    // === 黑白交界直线检测：Canny + HoughLinesP 取最长线段 ===
    cv::Mat edges;
    double med = cv::mean(blurred)[0];
    double cannyLow = std::max(20.0, med * 0.4);
    double cannyHigh = std::min(200.0, med * 1.2);
    cv::Canny(blurred, edges, cannyLow, cannyHigh, 3);

    // 形态学闭运算，连接断裂的边缘
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(edges, edges, cv::MORPH_CLOSE, kernel);

    // 概率霍夫变换找所有线段
    std::vector<cv::Vec4i> segs;
    int hthresh = std::max(10, std::min(blurred.rows, blurred.cols) / 8);
    double minLen = std::max(15.0, std::min(blurred.rows, blurred.cols) * 0.2);
    cv::HoughLinesP(edges, segs, 1, CV_PI / 180, hthresh, minLen, std::max(5.0, minLen * 0.2));

    if (segs.empty()) {
        setLastError("no line segments found");
        return VZ_ERR_PROCESS;
    }

    // ROI 长宽比偏好：高>宽时偏向竖直边界，宽>高时偏向水平边界
    bool preferVertical = blurred.rows > blurred.cols;
    // 选取对比度最高的线段作为主直线（黑白交界处两侧亮度差最大）
    int bestIdx = 0;
    double bestScore = -1.0;
    for (size_t i = 0; i < segs.size(); ++i) {
        float x1 = (float)segs[i][0], y1 = (float)segs[i][1];
        float x2 = (float)segs[i][2], y2 = (float)segs[i][3];
        float dx = x2 - x1, dy = y2 - y1;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 30.0f) continue;
        // 垂直方向（用于采样两侧像素）
        float px = -dy / len, py = dx / len;
        // 沿线采样，测量两侧亮度
        float sumL = 0, sumR = 0; int cnt = 0;
        int nsamp = std::min(50, (int)len);
        for (int s = 0; s < nsamp; ++s) {
            float t = (s + 0.5f) / nsamp;
            float cx = x1 + dx * t, cy = y1 + dy * t;
            for (int d = 1; d <= 5; ++d) {
                int lx = (int)(cx - px * d), ly = (int)(cy - py * d);
                int rx = (int)(cx + px * d), ry = (int)(cy + py * d);
                if (lx >= 0 && lx < blurred.cols && ly >= 0 && ly < blurred.rows)
                    sumL += blurred.at<uchar>(ly, lx);
                if (rx >= 0 && rx < blurred.cols && ry >= 0 && ry < blurred.rows)
                    sumR += blurred.at<uchar>(ry, rx);
                cnt++;
            }
        }
        if (cnt == 0) continue;
        float meanL = sumL / cnt, meanR = sumR / cnt;
        float contrast = std::abs(meanL - meanR);
        // polarity 过滤：1=白到黑(左亮右暗), 2=黑到白(左暗右亮)
        if (polarity_ == 1 && meanL <= meanR) continue;
        if (polarity_ == 2 && meanL >= meanR) continue;
        // 方向偏好系数（与ROI长宽比一致的方向得2倍权重）
        bool isVertical = std::abs(dy) > std::abs(dx);
        float dirBias = (isVertical == preferVertical) ? 2.0f : 1.0f;
        // 评分 = 对比度 * sqrt(长度) * 方向偏好
        double score = contrast * std::sqrt(len) * dirBias;
        if (score > bestScore) { bestScore = score; bestIdx = (int)i; }
    }
    if (bestScore < 0) {
        setLastError("no valid line with sufficient contrast");
        return VZ_ERR_PROCESS;
    }
    cv::Vec4i best = segs[bestIdx];
    float sx1 = (float)best[0], sy1 = (float)best[1];
    float sx2 = (float)best[2], sy2 = (float)best[3];

    // 直接使用最优线段的方向和中点（fitLine 易被附近垂直边缘干扰）
    float vx = sx2 - sx1, vy = sy2 - sy1;
    float vlen = std::sqrt(vx * vx + vy * vy);
    if (vlen < 1e-6f) { setLastError("degenerate line"); return VZ_ERR_PROCESS; }
    vx /= vlen; vy /= vlen;
    // 直线上一点：线段中点
    float x0 = (sx1 + sx2) * 0.5f;
    float y0 = (sy1 + sy2) * 0.5f;

    // 确定边界方向（用于可视化子框）
    bool verticalBoundary = (std::abs(vy) > std::abs(vx));

    // 用于可视化的点：在直线上均匀采样
    std::vector<cv::Point2f> pts;
    int segCount = std::max(3, segment_count_);
    cv::Rect bounds(0, 0, blurred.cols, blurred.rows);
    // 求直线与 ROI 边界的交点，得到 t 的范围
    std::vector<float> ts;
    if (std::abs(vx) > 1e-6f) {
        ts.push_back((0 - x0) / vx);
        ts.push_back((bounds.width - 1 - x0) / vx);
    }
    if (std::abs(vy) > 1e-6f) {
        ts.push_back((0 - y0) / vy);
        ts.push_back((bounds.height - 1 - y0) / vy);
    }
    std::sort(ts.begin(), ts.end());
    // 取中间两个 t 作为有效段（直线进入/离开矩形的点）
    float t0 = (ts.size() >= 4) ? ts[1] : ts.front();
    float t1 = (ts.size() >= 4) ? ts[2] : ts.back();
    for (int i = 0; i <= segCount; ++i) {
        float t = t0 + (t1 - t0) * i / segCount;
        pts.push_back(cv::Point2f(x0 + vx * t, y0 + vy * t));
    }
    // 将直线延伸到 ROI/图像边界，得到两个端点
    auto clipToRect = [&](float vx, float vy, float x0, float y0, const cv::Rect& r) -> std::pair<cv::Point2f, cv::Point2f> {
        std::vector<float> ts;
        if (std::abs(vx) > 1e-6f) {
            ts.push_back((r.x - x0) / vx);
            ts.push_back((r.x + r.width - 1 - x0) / vx);
        }
        if (std::abs(vy) > 1e-6f) {
            ts.push_back((r.y - y0) / vy);
            ts.push_back((r.y + r.height - 1 - y0) / vy);
        }
        std::sort(ts.begin(), ts.end());
        // 直线穿过矩形时，有效段为排序后中间两个 t 之间（前一个进入，后一个离开）
        if (ts.size() >= 4) {
            cv::Point2f p1(x0 + vx * ts[1], y0 + vy * ts[1]);
            cv::Point2f p2(x0 + vx * ts[2], y0 + vy * ts[2]);
            return {p1, p2};
        }
        cv::Point2f p1(x0 + vx * ts.front(), y0 + vy * ts.front());
        cv::Point2f p2(x0 + vx * ts.back(),  y0 + vy * ts.back());
        return {p1, p2};
    };
    auto [p1, p2] = clipToRect(vx, vy, x0, y0, bounds);

    out_lines_.clear();
    VzPoint2D pp1{}, pp2{};
    if (has_search_roi_) {
        double lx1 = p1.x + roiOffsetX, ly1 = p1.y + roiOffsetY;
        double lx2 = p2.x + roiOffsetX, ly2 = p2.y + roiOffsetY;
        double rad = -roi_angle_ * CV_PI / 180.0;
        double cosA = std::cos(rad), sinA = std::sin(rad);
        double dx1 = lx1 - roi_cx_, dy1 = ly1 - roi_cy_;
        pp1.x = roi_cx_ + dx1 * cosA - dy1 * sinA;
        pp1.y = roi_cy_ + dx1 * sinA + dy1 * cosA;
        double dx2 = lx2 - roi_cx_, dy2 = ly2 - roi_cy_;
        pp2.x = roi_cx_ + dx2 * cosA - dy2 * sinA;
        pp2.y = roi_cy_ + dx2 * sinA + dy2 * cosA;
    } else {
        pp1.x = p1.x; pp1.y = p1.y;
        pp2.x = p2.x; pp2.y = p2.y;
    }
    out_lines_.push_back(pp1);
    out_lines_.push_back(pp2);
    out_count_ = 1;
    result_image_ = input_.clone();

    return VZ_OK;
}

int LineFindAlgo::getOutput(const std::string& port_name,
                            void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "result_image") {
        VzImage* img = toVzImage(result_image_);
        if (!img) { *out_data = nullptr; *out_count = 0; *out_type_tag = VZ_TYPE_IMAGE; return VZ_OK; }
        *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE;
        return VZ_OK;
    }
    if (port_name == "lines") {
        int n = (int)out_lines_.size();
        auto* arr = (VzPoint2D*)std::malloc(sizeof(VzPoint2D) * (n ? n : 1));
        for (int i = 0; i < n; ++i) arr[i] = out_lines_[i];
        *out_data = arr;
        *out_count = n;
        *out_type_tag = VZ_TYPE_POINT2D_LIST;
        return VZ_OK;
    }
    if (port_name == "count") {
        auto* p = (int*)std::malloc(sizeof(int));
        *p = out_count_;
        *out_data = p;
        *out_count = 1;
        *out_type_tag = VZ_TYPE_INT;
        return VZ_OK;
    }
    setLastError("unknown output port: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

void LineFindAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* LineFindAlgo::describe() const {
    return R"JSON({
  "type_id":"LineFind",
  "display_name":"直线查找(黑白交界)",
  "category":"分析",
  "algo_binding":{"dll":"vision_algo","algo_name":"LineFind"},
  "inputs":[
    {"name":"image","type":"Image","required":true,"description":"输入图像"}
  ],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"叠加检测直线的结果图"},
    {"name":"lines","type":"Point2DList","required":true,"description":"黑白交界直线的两个端点(固定输出1条直线)"},
    {"name":"count","type":"Int","required":true,"description":"直线数量(找到=1,未找到=0)"}
  ],
  "parameters":[
    {"name":"search_roi","type":"String","default":"","description":"搜索区[cx,cy,w,h,angle]"},
    {"name":"polarity","type":"Int","default":0,"description":"搜索方向:0=任意,1=白到黑,2=黑到白", "enum": [[0,"任意"],[1,"白到黑"],[2,"黑到白"]]},
    {"name":"edge_method","type":"Int","default":0,"description":"边缘检测方法:0=Canny,1=Sobel", "enum": [[0,"Canny"],[1,"Sobel"]]},
    {"name":"segment_count","type":"Int","default":10,"description":"子框数量(将ROI切成N个子框,每个框找一个交界点)"},
    {"name":"equalize_hist","type":"Int","default":1,"description":"直方图均衡化(预处理):0=否,1=是", "enum": [[0,"否"],[1,"是"]]},
    {"name":"enable_blur","type":"Int","default":1,"description":"高斯模糊(预处理):0=否,1=是", "enum": [[0,"否"],[1,"是"]]},
    {"name":"gaussian_ksize","type":"Int","default":3,"description":"高斯模糊核大小(奇数)"},
    {"name":"canny_low","type":"Double","default":50.0,"description":"梯度峰值阈值(低于此值的交界点被忽略,0-255)"},
    {"name":"canny_high","type":"Double","default":150.0,"description":"Canny高阈值(0-255)"},
    {"name":"enable_binary","type":"Int","default":1,"description":"二值化开关(polarity非0时生效):0=否,1=是", "enum": [[0,"否"],[1,"是"]]},
    {"name":"bin_threshold","type":"Double","default":128.0,"description":"二值化阈值(用于方向搜索,0-255)"},
    {"name":"hough_threshold","type":"Int","default":15,"description":"霍夫累加器阈值(越小越易检出)"},
    {"name":"min_line_length","type":"Double","default":30.0,"description":"最小直线长度(像素)"},
    {"name":"max_line_gap","type":"Double","default":10.0,"description":"最大间隙(像素)"},
    {"name":"draw_color","type":"String","default":"#00FF00","description":"拟合直线颜色(#RRGGBB 或 B,G,R)"},
    {"name":"draw_thickness","type":"Int","default":2,"min":1,"max":20,"description":"拟合直线线宽(1-20)"},
    {"name":"point_size","type":"Int","default":3,"min":1,"max":50,"description":"交界点标记半径(1-50)"}
  ]
})JSON";
}

std::unique_ptr<AlgoBase> LineFindAlgo::create() {
    return std::make_unique<LineFindAlgo>();
}

} // namespace vz
