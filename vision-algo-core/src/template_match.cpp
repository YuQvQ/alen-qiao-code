// template_match.cpp — 模板匹配算法实现（多角度、多尺度、金字塔、NMS、子像素）
//
// 算法流程：
//   1. 准备模板（端口优先 → template_roi 裁剪）
//   2. 准备搜索区（search_roi / search_circle_roi）
//   3. 转灰度（兼容 1/3/4 通道）
//   4. 可选二值化预处理（enable_binary）
//   5. 构造图像金字塔（image + template 同步下采样）
//   6. 在最粗金字塔层做 (angle, scale) 网格搜索 + 多峰值检测
//   7. 在金字塔逐层精细化（在上一层候选位置 ±2 像素范围内重做 matchTemplate）
//   8. NMS（按 IoU 阈值去除重叠匹配）
//   9. 子像素精细化（抛物线拟合位置 + 角度）
//  10. 输出 result_image（绿色框 + 红色中心 + 分数）
//
#include "template_match.h"
#include "algo_utils.h"
#include <algorithm>
#include <cmath>

namespace vz {

using detail::toCvMat;
using detail::toVzImage;
using detail::parseJsonDouble;
using detail::parseJsonInt;
using detail::parseJsonDoubleRange;
using detail::parseJsonDouble4;
using detail::parseJsonDouble5;
using detail::parseColor;
using detail::parseThickness;
using detail::parsePointSize;

// ============================================================
// 参数
// ============================================================
int TemplateMatchAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "threshold") {
        threshold_ = parseJsonDouble(value_json, threshold_);
        if (threshold_ < 0 || threshold_ > 1) {
            setLastError("threshold out of range [0,1]");
            return VZ_ERR_INVALID_ARG;
        }
        return VZ_OK;
    }
    if (key == "max_count") {
        max_count_ = parseJsonInt(value_json, max_count_);
        if (max_count_ < 1) {
            setLastError("max_count must be >= 1");
            return VZ_ERR_INVALID_ARG;
        }
        return VZ_OK;
    }
    if (key == "angle_range") {
        // 空字符串 / null：保持默认值 [-5, 5]，不报错
        if (value_json.empty() || value_json == "\"\"" || value_json == "null") {
            return VZ_OK;
        }
        if (!parseJsonDoubleRange(value_json, angle_min_, angle_max_)) {
            setLastError("angle_range parse failed");
            return VZ_ERR_INVALID_ARG;
        }
        return VZ_OK;
    }
    if (key == "angle_step") {
        angle_step_ = parseJsonDouble(value_json, 5.0);
        if (angle_step_ <= 0) angle_step_ = 5.0;
        return VZ_OK;
    }
    if (key == "scale_range") {
        // 空字符串 / null：保持默认值 [0.95, 1.05]，不报错
        if (value_json.empty() || value_json == "\"\"" || value_json == "null") {
            return VZ_OK;
        }
        if (!parseJsonDoubleRange(value_json, scale_min_, scale_max_)) {
            setLastError("scale_range parse failed");
            return VZ_ERR_INVALID_ARG;
        }
        return VZ_OK;
    }
    if (key == "scale_step") {
        scale_step_ = parseJsonDouble(value_json, 0.05);
        if (scale_step_ <= 0) scale_step_ = 0.05;
        return VZ_OK;
    }
    if (key == "pyramid_levels") {
        pyramid_levels_ = parseJsonInt(value_json, 3);
        if (pyramid_levels_ < 0) pyramid_levels_ = 0;
        if (pyramid_levels_ > 4) pyramid_levels_ = 4;
        return VZ_OK;
    }
    if (key == "nms_overlap") {
        nms_overlap_ = parseJsonDouble(value_json, 0.5);
        if (nms_overlap_ < 0) nms_overlap_ = 0;
        if (nms_overlap_ > 1) nms_overlap_ = 1;
        return VZ_OK;
    }
    if (key == "subpixel") {
        subpixel_ = parseJsonInt(value_json, 1);
        if (subpixel_ != 0) subpixel_ = 1;
        return VZ_OK;
    }
    if (key == "enable_binary") {
        enable_binary_ = parseJsonInt(value_json, 0);
        if (enable_binary_ != 0) enable_binary_ = 1;
        return VZ_OK;
    }
    if (key == "binary_threshold") {
        binary_threshold_ = parseJsonInt(value_json, 128);
        if (binary_threshold_ < 0) binary_threshold_ = 0;
        if (binary_threshold_ > 255) binary_threshold_ = 255;
        return VZ_OK;
    }
    if (key == "refine_boundary") {
        refine_boundary_ = parseJsonInt(value_json, 1);
        if (refine_boundary_ != 0) refine_boundary_ = 1;
        return VZ_OK;
    }
    if (key == "display_mode") {
        display_mode_ = parseJsonInt(value_json, 0);
        if (display_mode_ < 0) display_mode_ = 0;
        if (display_mode_ > 2) display_mode_ = 2;
        return VZ_OK;
    }
    if (key == "template_roi") {
        // 空字符串 / null：不设模板 ROI（用户可能通过 template 端口输入模板）
        if (value_json.empty() || value_json == "\"\"" || value_json == "null") {
            has_template_roi_ = false;
            return VZ_OK;
        }
        // [cx, cy, w, h, angle] 或 [x, y, w, h]
        double cx, cy, w, h, angle;
        if (parseJsonDouble5(value_json, cx, cy, w, h, angle)) {
            roi_x_ = cx; roi_y_ = cy; roi_w_ = w; roi_h_ = h; roi_angle_ = angle;
            has_template_roi_ = true;
            return VZ_OK;
        }
        double x, y, w2, h2;
        if (parseJsonDouble4(value_json, x, y, w2, h2)) {
            roi_x_ = x + w2 / 2.0; roi_y_ = y + h2 / 2.0;
            roi_w_ = w2; roi_h_ = h2; roi_angle_ = 0;
            has_template_roi_ = true;
            return VZ_OK;
        }
        // 解析失败：视为未设置模板 ROI（用户通过 template 端口输入），不报错
        has_template_roi_ = false;
        return VZ_OK;
    }
    if (key == "search_roi") {
        double cx, cy, w, h, angle;
        if (parseJsonDouble5(value_json, cx, cy, w, h, angle)) {
            // 旋转矩形取外接轴对齐矩形
            double a = angle * CV_PI / 180.0;
            double hw = w / 2, hh = h / 2;
            double corners_x[4] = { cx - hw, cx + hw, cx + hw, cx - hw };
            double corners_y[4] = { cy - hh, cy - hh, cy + hh, cy + hh };
            double minx = corners_x[0], maxx = corners_x[0], miny = corners_y[0], maxy = corners_y[0];
            for (int i = 1; i < 4; i++) {
                double rx = corners_x[i] * std::cos(a) - corners_y[i] * std::sin(a);
                double ry = corners_x[i] * std::sin(a) + corners_y[i] * std::cos(a);
                minx = std::min(minx, rx); maxx = std::max(maxx, rx);
                miny = std::min(miny, ry); maxy = std::max(maxy, ry);
            }
            search_x_ = minx; search_y_ = miny;
            search_w_ = maxx - minx; search_h_ = maxy - miny;
            has_search_roi_ = true;
            return VZ_OK;
        }
        double x, y, w2, h2;
        if (parseJsonDouble4(value_json, x, y, w2, h2)) {
            search_x_ = x; search_y_ = y; search_w_ = w2; search_h_ = h2;
            has_search_roi_ = true;
            return VZ_OK;
        }
        has_search_roi_ = false;
        return VZ_OK;
    }
    if (key == "search_circle_roi") {
        // 空字符串 / null / 无效值：不设搜索区，返回 OK（不报错）
        if (value_json.empty() || value_json == "\"\"" || value_json == "null") {
            has_search_circle_roi_ = false;
            return VZ_OK;
        }
        auto lb = value_json.find('[');
        auto rb = value_json.find(']');
        if (lb == std::string::npos || rb == std::string::npos || rb <= lb) {
            // 没有方括号：视为未设置搜索区（兼容空值/默认值），不报错
            has_search_circle_roi_ = false;
            return VZ_OK;
        }
        auto body = value_json.substr(lb + 1, rb - lb - 1);
        double out[3] = {0, 0, 0};
        int idx = 0;
        size_t pos = 0;
        while (pos < body.size() && idx < 3) {
            while (pos < body.size() && (body[pos] == ' ' || body[pos] == ',')) pos++;
            if (pos >= body.size()) break;
            size_t end = body.find(',', pos);
            if (end == std::string::npos) end = body.size();
            try { out[idx++] = std::stod(body.substr(pos, end - pos)); }
            catch (...) {
                setLastError("search_circle_roi parse failed; expect [cx,cy,radius]");
                return VZ_ERR_INVALID_ARG;
            }
            pos = end + 1;
        }
        if (idx != 3) {
            setLastError("search_circle_roi needs 3 numbers [cx,cy,radius]");
            return VZ_ERR_INVALID_ARG;
        }
        scr_cx_ = out[0]; scr_cy_ = out[1]; scr_r_ = out[2];
        has_search_circle_roi_ = true;
        return VZ_OK;
    }
    if (key == "draw_color") { draw_color_ = parseColor(value_json, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (key == "draw_thickness") { draw_thickness_ = parseThickness(value_json, 2); return VZ_OK; }
    if (key == "point_size") { point_size_ = parsePointSize(value_json, 5); return VZ_OK; }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

// ============================================================
// 输入
// ============================================================
int TemplateMatchAlgo::setInput(const std::string& port_name,
                                 const void* data, int type_tag) {
    if (port_name == "image") {
        if (type_tag != VZ_TYPE_IMAGE) {
            setLastError("port 'image' requires Image type");
            return VZ_ERR_TYPE_MISMATCH;
        }
        image_ = toCvMat(static_cast<const VzImage*>(data));
        return VZ_OK;
    }
    if (port_name == "template") {
        if (type_tag != VZ_TYPE_IMAGE) {
            setLastError("port 'template' requires Image type");
            return VZ_ERR_TYPE_MISMATCH;
        }
        template_ = toCvMat(static_cast<const VzImage*>(data));
        return VZ_OK;
    }
    if (port_name == "region") {
        if (type_tag != VZ_TYPE_REGION) {
            setLastError("port 'region' requires Region type");
            return VZ_ERR_TYPE_MISMATCH;
        }
        region_mask_ = toCvMat(static_cast<const VzImage*>(data));
        return VZ_OK;
    }
    if (port_name == "template_mask") {
        if (type_tag != VZ_TYPE_IMAGE) {
            setLastError("port 'template_mask' requires Image type");
            return VZ_ERR_TYPE_MISMATCH;
        }
        cv::Mat m = toCvMat(static_cast<const VzImage*>(data));
        // 确保掩膜为单通道 8 位
        if (m.channels() == 3) cv::cvtColor(m, m, cv::COLOR_BGR2GRAY);
        else if (m.channels() == 4) cv::cvtColor(m, m, cv::COLOR_BGRA2GRAY);
        if (m.depth() != CV_8U) m.convertTo(m, CV_8U);
        template_mask_ = m;
        return VZ_OK;
    }
    setLastError("unknown input port: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

// ============================================================
// 工具函数：旋转+缩放模板
// ============================================================
static cv::Mat transformTemplate(const cv::Mat& tmpl, double angle, double scale) {
    if (std::abs(angle) < 0.01 && std::abs(scale - 1.0) < 0.001) return tmpl;
    cv::Point2f center(tmpl.cols / 2.0f, tmpl.rows / 2.0f);
    cv::Mat rot = cv::getRotationMatrix2D(center, angle, scale);
    cv::Mat out;
    cv::warpAffine(tmpl, out, rot, tmpl.size(), cv::INTER_LINEAR,
                   cv::BORDER_CONSTANT, cv::Scalar(0));
    return out;
}

// 统一使用 TM_CCOEFF_NORMED，亮度不变性好，误匹配少。
// 掩膜已在模板预处理阶段通过"排除像素设均值"处理，无需传 mask。
static void matchTemplateEx(const cv::Mat& search, const cv::Mat& tmpl, cv::Mat& result) {
    cv::matchTemplate(search, tmpl, result, cv::TM_CCOEFF_NORMED);
}

// 在 result 图中找所有 >= threshold 的峰值，并抑制已找到的区域
struct PeakInfo {
    cv::Point loc;
    double score;
};

static std::vector<PeakInfo> findPeaks(cv::Mat result, double threshold, int max_n,
                                       int tmpl_w, int tmpl_h) {
    std::vector<PeakInfo> peaks;
    cv::Mat masked = result.clone();
    for (int i = 0; i < max_n; ++i) {
        double min_val, max_val;
        cv::Point min_loc, max_loc;
        cv::minMaxLoc(masked, &min_val, &max_val, &min_loc, &max_loc);
        if (max_val < threshold) break;
        peaks.push_back({max_loc, max_val});
        // 抑制周围一个模板大小的区域
        int sx = std::max(0, max_loc.x - tmpl_w / 2);
        int sy = std::max(0, max_loc.y - tmpl_h / 2);
        int ex = std::min(masked.cols, max_loc.x + tmpl_w / 2 + 1);
        int ey = std::min(masked.rows, max_loc.y + tmpl_h / 2 + 1);
        if (ex > sx && ey > sy) {
            masked(cv::Rect(sx, sy, ex - sx, ey - sy)).setTo(0.0);
        }
    }
    return peaks;
}

// 子像素精细化：在 result 的 3x3 邻域做抛物线拟合
static void refineSubpixel(const cv::Mat& result, cv::Point peak, double& x, double& y) {
    int w = result.cols, h = result.rows;
    int px = peak.x, py = peak.y;
    // 边界保护
    if (px < 1 || px >= w - 1 || py < 1 || py >= h - 1) {
        x = px + 0.5; y = py + 0.5;
        return;
    }
    // 中心点取 matchTemplate 结果，其值表示 (loc.x, loc.y) 是模板左上角位置
    // 中心点 = loc + tmpl_size/2
    // 取 3x3 邻域做抛物线拟合
    auto get = [&](int dx, int dy) -> double {
        return result.at<float>(py + dy, px + dx);
    };
    double center = get(0, 0);
    double left = get(-1, 0), right = get(1, 0);
    double up = get(0, -1), down = get(0, 1);
    // 抛物线拟合：delta = 0.5 * (left - right) / (left - 2*center + right)
    double dx = 0.0, dy = 0.0;
    double denomX = left - 2 * center + right;
    double denomY = up - 2 * center + down;
    if (std::abs(denomX) > 1e-12) dx = 0.5 * (left - right) / denomX;
    if (std::abs(denomY) > 1e-12) dy = 0.5 * (up - down) / denomY;
    // 限制在 ±0.5 范围内
    if (dx > 0.5) dx = 0.5; if (dx < -0.5) dx = -0.5;
    if (dy > 0.5) dy = 0.5; if (dy < -0.5) dy = -0.5;
    x = px + dx + 0.5;  // +0.5 转换为模板中心坐标（左上角 + 模板尺寸/2 在调用处补）
    y = py + dy + 0.5;
}

// 候选匹配
struct Candidate {
    double x;       // 原图坐标（中心 x）
    double y;       // 原图坐标（中心 y）
    double score;
    double angle;
    double scale;
};

// ============================================================
// 执行
// ============================================================
int TemplateMatchAlgo::process() {
    result_image_ = cv::Mat();
    matches_.clear();
    centers_.clear();
    rects_.clear();
    count_ = 0;

    if (image_.empty()) {
        setLastError("input image is empty");
        return VZ_ERR_INVALID_ARG;
    }

    // 拟合圆搜索区：将其外接矩形作为 search_roi（覆盖矩形 search_roi）
    if (has_search_circle_roi_ && scr_r_ > 0) {
        search_x_ = scr_cx_ - scr_r_;
        search_y_ = scr_cy_ - scr_r_;
        search_w_ = scr_r_ * 2.0;
        search_h_ = scr_r_ * 2.0;
        has_search_roi_ = true;
    }

    // 模板来源：端口优先 → template_roi 裁剪
    cv::Mat tmpl = template_;
    if (tmpl.empty() && has_template_roi_) {
        int w = static_cast<int>(roi_w_);
        int h = static_cast<int>(roi_h_);
        if (w <= 0 || h <= 0) {
            setLastError("template_roi w/h must be > 0");
            return VZ_ERR_INVALID_ARG;
        }
        double cx = roi_x_, cy = roi_y_;
        cx = std::max(0.0, std::min(cx, (double)(image_.cols - 1)));
        cy = std::max(0.0, std::min(cy, (double)(image_.rows - 1)));
        if (std::abs(roi_angle_) < 0.5) {
            int x = static_cast<int>(std::floor(cx - w / 2.0));
            int y = static_cast<int>(std::floor(cy - h / 2.0));
            x = std::max(0, std::min(x, image_.cols - 1));
            y = std::max(0, std::min(y, image_.rows - 1));
            int max_w = image_.cols - x;
            int max_h = image_.rows - y;
            w = std::min(w, max_w);
            h = std::min(h, max_h);
            if (w <= 0 || h <= 0) {
                setLastError("template_roi clamped to zero size");
                return VZ_ERR_INVALID_ARG;
            }
            tmpl = image_(cv::Rect(x, y, w, h)).clone();
        } else {
            cv::Point2f center(static_cast<float>(cx), static_cast<float>(cy));
            cv::Mat rot = cv::getRotationMatrix2D(center, roi_angle_, 1.0);
            cv::Mat rotated;
            cv::warpAffine(image_, rotated, rot, image_.size(), cv::INTER_LINEAR,
                           cv::BORDER_CONSTANT, cv::Scalar(0));
            int x = static_cast<int>(cx - w / 2.0);
            int y = static_cast<int>(cy - h / 2.0);
            x = std::max(0, std::min(x, image_.cols - w));
            y = std::max(0, std::min(y, image_.rows - h));
            w = std::min(w, image_.cols - x);
            h = std::min(h, image_.rows - y);
            if (w <= 0 || h <= 0) {
                setLastError("template_roi rotated clamped to zero size");
                return VZ_ERR_INVALID_ARG;
            }
            tmpl = rotated(cv::Rect(x, y, w, h)).clone();
        }
    }
    if (tmpl.empty()) {
        setLastError("template is empty; either connect 'template' port or set 'template_roi'");
        return VZ_ERR_INVALID_ARG;
    }

    // 搜索区：若设置了 search_roi，裁剪图像后再匹配，结果坐标加回偏移
    cv::Mat search_img = image_;
    double off_x = 0, off_y = 0;
    if (has_search_roi_) {
        int sx = static_cast<int>(std::floor(search_x_));
        int sy = static_cast<int>(std::floor(search_y_));
        int sw = static_cast<int>(std::ceil(search_w_));
        int sh = static_cast<int>(std::ceil(search_h_));
        if (sw <= 0 || sh <= 0) {
            setLastError("search_roi w/h must be > 0");
            return VZ_ERR_INVALID_ARG;
        }
        sx = std::max(0, std::min(sx, image_.cols - 1));
        sy = std::max(0, std::min(sy, image_.rows - 1));
        sw = std::min(sw, image_.cols - sx);
        sh = std::min(sh, image_.rows - sy);
        if (sw <= 0 || sh <= 0) {
            setLastError("search_roi fully outside image bounds");
            return VZ_ERR_INVALID_ARG;
        }
        search_img = image_(cv::Rect(sx, sy, sw, sh));
        off_x = sx;
        off_y = sy;
    }

    if (tmpl.rows > search_img.rows || tmpl.cols > search_img.cols) {
        setLastError("template larger than search region");
        return VZ_ERR_INVALID_ARG;
    }

    // 转灰度
    auto toGray = [](const cv::Mat& src) -> cv::Mat {
        int ch = src.channels();
        if (ch == 1) return src.clone();
        if (ch == 3) { cv::Mat g; cv::cvtColor(src, g, cv::COLOR_BGR2GRAY); return g; }
        if (ch == 4) { cv::Mat g; cv::cvtColor(src, g, cv::COLOR_BGRA2GRAY); return g; }
        return src.clone();
    };
    cv::Mat searchGray = toGray(search_img);
    cv::Mat tmplGray = toGray(tmpl);

    // 可选二值化预处理
    if (enable_binary_) {
        cv::threshold(searchGray, searchGray, binary_threshold_, 255, cv::THRESH_BINARY);
        cv::threshold(tmplGray, tmplGray, binary_threshold_, 255, cv::THRESH_BINARY);
    }

    // === 模板掩膜处理：排除像素设为有效像素均值，使其对 TM_CCOEFF_NORMED 贡献为 0 ===
    // 这样既支持掩膜又保持 CCOEFF 的亮度不变性，避免 CCORR 的误匹配。
    bool hasMask = false;
    if (!template_mask_.empty()) {
        cv::Mat tmplMask;
        if (template_mask_.size() == tmplGray.size()) tmplMask = template_mask_.clone();
        else cv::resize(template_mask_, tmplMask, tmplGray.size());
        // 检查是否有排除像素（值 < 128）
        int excludeCount = cv::countNonZero(tmplMask < 128);
        if (excludeCount > 0) {
            hasMask = true;
            // 计算有效像素均值
            cv::Scalar meanVal = cv::mean(tmplGray, tmplMask >= 128);
            uchar meanU = static_cast<uchar>(std::min(255.0, std::max(0.0, meanVal[0])));
            // 排除像素设为均值
            tmplGray.setTo(meanU, tmplMask < 128);
        }
    }

    // 保存预处理后的图像用于调试显示（灰度或二值）
    debug_image_ = searchGray.clone();

    // === 构造图像金字塔 ===
    // searchPyr[0] = 原图，searchPyr[i] = 第 i 层下采样
    std::vector<cv::Mat> searchPyr, tmplPyr;
    searchPyr.push_back(searchGray);
    tmplPyr.push_back(tmplGray);
    for (int i = 0; i < pyramid_levels_; ++i) {
        cv::Mat s, t;
        cv::pyrDown(searchPyr.back(), s);
        cv::pyrDown(tmplPyr.back(), t);
        if (s.cols < t.cols || s.rows < t.rows) break;
        if (s.cols < 8 || s.rows < 8) break;
        searchPyr.push_back(s);
        tmplPyr.push_back(t);
    }
    int coarseLevel = static_cast<int>(searchPyr.size()) - 1;
    // 粗层缩放系数（粗层 1 像素 = 原图 2^coarseLevel 像素）
    double coarseScale = std::pow(2.0, coarseLevel);

    // === 构造角度/尺度网格 ===
    std::vector<double> angles;
    if (std::abs(angle_min_ - angle_max_) < 1e-6) {
        angles.push_back(angle_min_);
    } else {
        for (double a = angle_min_; a <= angle_max_ + 1e-6; a += angle_step_)
            angles.push_back(a);
        if (angles.empty()) angles.push_back(angle_min_);
    }
    std::vector<double> scales;
    if (std::abs(scale_min_ - scale_max_) < 1e-6) {
        scales.push_back(scale_min_);
    } else {
        for (double s = scale_min_; s <= scale_max_ + 1e-6; s += scale_step_)
            scales.push_back(s);
        if (scales.empty()) scales.push_back(scale_min_);
    }

    // === 第 1 阶段：在最粗金字塔层做全图 (angle, scale) 网格搜索 ===
    std::vector<Candidate> candidates;
    const cv::Mat& coarseSearch = searchPyr[coarseLevel];
    const cv::Mat& coarseTmpl = tmplPyr[coarseLevel];
    // 粗层多取候选（max_count * 4，至少 8 个）
    int coarseMax = std::max(8, max_count_ * 4);

    for (double angle : angles) {
        for (double scale : scales) {
            // 在粗层，模板不需要再做 scale（已经下采样过）
            // 但角度仍需旋转
            cv::Mat transformedTmpl = transformTemplate(coarseTmpl, angle, 1.0);
            if (transformedTmpl.rows > coarseSearch.rows ||
                transformedTmpl.cols > coarseSearch.cols) continue;
            cv::Mat result;
            matchTemplateEx(coarseSearch, transformedTmpl, result);

            auto peaks = findPeaks(result, threshold_, coarseMax,
                                   transformedTmpl.cols, transformedTmpl.rows);
            for (const auto& p : peaks) {
                Candidate c;
                // 粗层 loc -> 原图坐标（中心）
                c.x = (p.loc.x + transformedTmpl.cols / 2.0) * coarseScale;
                c.y = (p.loc.y + transformedTmpl.rows / 2.0) * coarseScale;
                c.score = p.score;
                c.angle = angle;
                c.scale = scale;  // 保留用户指定的 scale（用于结果尺寸）
                candidates.push_back(c);
            }
        }
    }

    // === 第 2 阶段：金字塔逐层精细化 ===
    // 在 coarseLevel-1 → 0 的每一层，对每个候选位置 ±2 像素范围内重做 matchTemplate
    for (int lvl = coarseLevel - 1; lvl >= 0; --lvl) {
        if (candidates.empty()) break;
        const cv::Mat& lvlSearch = searchPyr[lvl];
        const cv::Mat& lvlTmpl = tmplPyr[lvl];
        // 当前层 1 像素 = 原图 2^lvl 像素
        double lvlScale = std::pow(2.0, lvl);
        int searchRadius = 2;  // 在上一层位置 ±2 像素范围内搜索
        std::vector<Candidate> refined;

        for (const auto& c : candidates) {
            // 将候选位置（原图坐标）转换到当前层坐标
            double lx = c.x / lvlScale;
            double ly = c.y / lvlScale;
            int cx = static_cast<int>(lx);
            int cy = static_cast<int>(ly);
            // 限定搜索窗口：以 (cx, cy) 为中心，±searchRadius 像素范围
            int sx = std::max(0, cx - searchRadius - lvlTmpl.cols / 2);
            int sy = std::max(0, cy - searchRadius - lvlTmpl.rows / 2);
            int ex = std::min(lvlSearch.cols, cx + searchRadius + lvlTmpl.cols / 2);
            int ey = std::min(lvlSearch.rows, cy + searchRadius + lvlTmpl.rows / 2);
            int sw = ex - sx;
            int sh = ey - sy;
            if (sw < lvlTmpl.cols || sh < lvlTmpl.rows) continue;

            cv::Mat subSearch = lvlSearch(cv::Rect(sx, sy, sw, sh));
            cv::Mat transformedTmpl = transformTemplate(lvlTmpl, c.angle, 1.0);
            if (transformedTmpl.rows > subSearch.rows ||
                transformedTmpl.cols > subSearch.cols) continue;
            cv::Mat result;
            matchTemplateEx(subSearch, transformedTmpl, result);
            double min_val, max_val;
            cv::Point min_loc, max_loc;
            cv::minMaxLoc(result, &min_val, &max_val, &min_loc, &max_loc);

            if (max_val < threshold_) continue;
            // 当前层局部坐标 -> 原图坐标
            Candidate rc = c;
            rc.x = (sx + max_loc.x + transformedTmpl.cols / 2.0) * lvlScale;
            rc.y = (sy + max_loc.y + transformedTmpl.rows / 2.0) * lvlScale;
            rc.score = max_val;
            refined.push_back(rc);
        }
        if (!refined.empty()) {
            candidates = std::move(refined);
        }
    }

    // === 第 3 阶段：子像素精细化（在最细层 = lvl 0 做抛物线拟合） ===
    if (subpixel_ && !candidates.empty()) {
        const cv::Mat& fineSearch = searchPyr[0];
        const cv::Mat& fineTmpl = tmplPyr[0];
        for (auto& c : candidates) {
            // 将 c.x, c.y 视为模板中心；模板左上角 = (c.x - w/2, c.y - h/2)
            cv::Mat transformedTmpl = transformTemplate(fineTmpl, c.angle, 1.0);
            int tw = transformedTmpl.cols;
            int th = transformedTmpl.rows;
            int px = static_cast<int>(c.x - tw / 2.0);
            int py = static_cast<int>(c.y - th / 2.0);
            // 在 ±2 像素范围内重新做 matchTemplate，找精确峰值
            int sx = std::max(0, px - 2);
            int sy = std::max(0, py - 2);
            int ex = std::min(fineSearch.cols, px + 2 + tw);
            int ey = std::min(fineSearch.rows, py + 2 + th);
            int sw = ex - sx;
            int sh = ey - sy;
            if (sw < tw || sh < th) continue;
            cv::Mat subSearch = fineSearch(cv::Rect(sx, sy, sw, sh));
            cv::Mat result;
            matchTemplateEx(subSearch, transformedTmpl, result);
            double min_val, max_val;
            cv::Point min_loc, max_loc;
            cv::minMaxLoc(result, &min_val, &max_val, &min_loc, &max_loc);
            if (max_val < threshold_) continue;
            c.score = max_val;
            // 抛物线拟合位置
            double rx, ry;
            refineSubpixel(result, max_loc, rx, ry);
            // 转换回原图坐标：
            // rx = max_loc.x + dx + 0.5（result 中亚像素位置，max_loc 是模板左上角）
            // 模板中心 = subSearch 左上角(sx) + 模板左上角(max_loc) + 模板宽高一半(tw/2,th/2) + 亚像素偏移(dx,dy)
            c.x = sx + rx + tw / 2.0 - 0.5;
            c.y = sy + ry + th / 2.0 - 0.5;
        }
    }

    // === 第 4 阶段：NMS 去除重叠 ===
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    std::vector<Candidate> survived;
    for (const auto& c : candidates) {
        bool overlap = false;
        double w = tmpl.cols * c.scale;
        double h = tmpl.rows * c.scale;
        for (const auto& s : survived) {
            double sw = tmpl.cols * s.scale;
            double sh = tmpl.rows * s.scale;
            // 简单 AABB IoU（不考虑旋转）
            double ix1 = std::max(c.x - w / 2, s.x - sw / 2);
            double iy1 = std::max(c.y - h / 2, s.y - sh / 2);
            double ix2 = std::min(c.x + w / 2, s.x + sw / 2);
            double iy2 = std::min(c.y + h / 2, s.y + sh / 2);
            double iw = std::max(0.0, ix2 - ix1);
            double ih = std::max(0.0, iy2 - iy1);
            double inter = iw * ih;
            double uni = w * h + sw * sh - inter;
            double iou = inter / std::max(1e-6, uni);
            if (iou > nms_overlap_) { overlap = true; break; }
        }
        if (!overlap) survived.push_back(c);
        if (static_cast<int>(survived.size()) >= max_count_) break;
    }

    // === 边界精化：从匹配中心点做 flood fill（区域生长）找到目标实际边界 ===
    auto refineBoundary = [&](VzRect& r) -> bool {
        if (image_.empty()) return false;
        cv::Mat gray;
        int ch = image_.channels();
        if (ch == 1) gray = image_;
        else if (ch == 3) cv::cvtColor(image_, gray, cv::COLOR_BGR2GRAY);
        else if (ch == 4) cv::cvtColor(image_, gray, cv::COLOR_BGRA2GRAY);
        else gray = image_;
        if (gray.depth() != CV_8U) gray.convertTo(gray, CV_8U);

        double cx = r.x, cy = r.y;
        double tw = r.w, th = r.h;
        // 裁剪区域：比模板大一圈，确保目标完整在内
        int pad = static_cast<int>(std::max(tw, th) * 0.5);
        int rx = static_cast<int>(std::floor(cx - tw / 2 - pad));
        int ry = static_cast<int>(std::floor(cy - th / 2 - pad));
        int rw = static_cast<int>(tw + pad * 2);
        int rh = static_cast<int>(th + pad * 2);
        rx = std::max(0, rx); ry = std::max(0, ry);
        rw = std::min(rw, gray.cols - rx);
        rh = std::min(rh, gray.rows - ry);
        if (rw < 4 || rh < 4) return false;

        cv::Mat roi = gray(cv::Rect(rx, ry, rw, rh)).clone();

        // 中心坐标（ROI 内）
        int ccx = static_cast<int>(cx - rx);
        int ccy = static_cast<int>(cy - ry);
        ccx = std::max(1, std::min(rw - 2, ccx));
        ccy = std::max(1, std::min(rh - 2, ccy));

        // 中心 3x3 平均灰度作为种子参考值
        double seedVal = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                seedVal += roi.at<uchar>(ccy + dy, ccx + dx);
        seedVal /= 9.0;

        // 轻微模糊去噪
        cv::Mat blurred;
        cv::GaussianBlur(roi, blurred, cv::Size(3, 3), 0.8);

        // flood fill：从中心点生长灰度相近的连通区域
        // loDiff/upDiff = 灰度容差，目标与背景灰度差大时容差取 20~30
        cv::Mat mask = cv::Mat::zeros(rh + 2, rw + 2, CV_8UC1);
        int loDiff = 25, upDiff = 25;
        int flags = 4 | (255 << 8) | cv::FLOODFILL_FIXED_RANGE;
        cv::Rect floodRect;
        cv::floodFill(blurred, mask, cv::Point(ccx, ccy),
                      cv::Scalar(255), &floodRect,
                      cv::Scalar(loDiff), cv::Scalar(upDiff), flags);

        // 从 mask 提取填充区域的轮廓
        cv::Mat filled = mask(cv::Rect(1, 1, rw, rh)).clone();
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(filled, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        if (contours.empty()) return false;

        // 找面积最大的轮廓（即填充的目标区域）
        double maxArea = 0;
        cv::RotatedRect bestRR;
        for (const auto& cnt : contours) {
            double area = cv::contourArea(cnt);
            if (area > maxArea) {
                maxArea = area;
                bestRR = cv::minAreaRect(cnt);
            }
        }
        if (maxArea < 50) return false;

        // 安全校验：精化后的尺寸不应与模板差太多（0.3x ~ 3x）
        double refW = bestRR.size.width;
        double refH = bestRR.size.height;
        if (refW < tw * 0.3 || refW > tw * 3.0 || refH < th * 0.3 || refH > th * 3.0) {
            return false;
        }

        // 转回原图坐标
        r.x = bestRR.center.x + rx;
        r.y = bestRR.center.y + ry;
        r.w = refW;
        r.h = refH;
        r.angle = bestRR.angle;
        return true;
    };

    // === 输出匹配结果 ===
    for (const auto& c : survived) {
        VzMatchResult m{};
        m.rect.x = c.x + off_x;
        m.rect.y = c.y + off_y;
        m.rect.w = static_cast<double>(tmpl.cols) * c.scale;
        m.rect.h = static_cast<double>(tmpl.rows) * c.scale;
        m.rect.angle = c.angle;
        m.score = c.score;
        m.template_id = 0;
        m._reserved = 0;
        // 边缘检测精化边界（异常保护，防止崩溃）
        if (refine_boundary_) {
            try {
                refineBoundary(m.rect);
            } catch (...) {
                // 精化失败时保留模板匹配的原始矩形，不崩溃
            }
        }
        matches_.push_back(m);
        centers_.push_back({m.rect.x, m.rect.y});
        rects_.push_back(m.rect);
    }
    count_ = static_cast<int>(matches_.size());
    if (count_ == 0) {
        setLastError("no template matches found");
        return VZ_ERR_PROCESS;
    }

    result_image_ = image_.clone();

    return VZ_OK;
}

// ============================================================
// 输出
// ============================================================
int TemplateMatchAlgo::getOutput(const std::string& port_name,
                                  void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "result_image") {
        VzImage* img = toVzImage(result_image_);
        if (!img) {
            *out_data = nullptr; *out_count = 0; *out_type_tag = VZ_TYPE_IMAGE;
            return VZ_OK;
        }
        *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE;
        return VZ_OK;
    }
    if (port_name == "debug_image") {
        VzImage* img = toVzImage(debug_image_);
        if (!img) {
            *out_data = nullptr; *out_count = 0; *out_type_tag = VZ_TYPE_IMAGE;
            return VZ_OK;
        }
        *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE;
        return VZ_OK;
    }
    if (port_name == "matches") {
        int n = static_cast<int>(matches_.size());
        VzMatchResult* arr = (VzMatchResult*)std::malloc(sizeof(VzMatchResult) * (n ? n : 1));
        if (!arr) { setLastError("malloc failed"); return VZ_ERR_NO_MEMORY; }
        for (int i = 0; i < n; ++i) arr[i] = matches_[i];
        *out_data = arr; *out_count = n; *out_type_tag = VZ_TYPE_MATCH_RESULT;
        return VZ_OK;
    }
    if (port_name == "centers") {
        int n = static_cast<int>(centers_.size());
        VzPoint2D* arr = (VzPoint2D*)std::malloc(sizeof(VzPoint2D) * (n ? n : 1));
        if (!arr) { setLastError("malloc failed"); return VZ_ERR_NO_MEMORY; }
        for (int i = 0; i < n; ++i) arr[i] = centers_[i];
        *out_data = arr; *out_count = n; *out_type_tag = VZ_TYPE_POINT2D_LIST;
        return VZ_OK;
    }
    if (port_name == "rects") {
        int n = static_cast<int>(rects_.size());
        VzRect* arr = (VzRect*)std::malloc(sizeof(VzRect) * (n ? n : 1));
        if (!arr) { setLastError("malloc failed"); return VZ_ERR_NO_MEMORY; }
        for (int i = 0; i < n; ++i) arr[i] = rects_[i];
        *out_data = arr; *out_count = n; *out_type_tag = VZ_TYPE_RECT_LIST;
        return VZ_OK;
    }
    if (port_name == "count") {
        int* p = (int*)std::malloc(sizeof(int));
        if (!p) { setLastError("malloc failed"); return VZ_ERR_NO_MEMORY; }
        *p = count_;
        *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_INT;
        return VZ_OK;
    }
    setLastError("unknown output port: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

void TemplateMatchAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* TemplateMatchAlgo::describe() const {
    static const char* kDesc = R"JSON({
  "type_id":"TemplateMatch",
  "display_name":"模板匹配",
  "category":"定位",
  "icon":"icons/template_match.svg",
  "algo_binding":{"dll":"vision_algo","algo_name":"TemplateMatch"},
  "inputs":[
    {"name":"image","type":"Image","required":true},
    {"name":"template","type":"Image","required":false},
    {"name":"region","type":"Region","required":false}
  ],
  "outputs":[
    {"name":"result_image","type":"Image"},
    {"name":"debug_image","type":"Image"},
    {"name":"matches","type":"MatchResultList"},
    {"name":"centers","type":"Point2DList"},
    {"name":"rects","type":"RectList"},
    {"name":"count","type":"Int"}
  ],
  "parameters":[
    {"name":"threshold","type":"Double","default":0.5,"min":0,"max":1,"description":"匹配置信度阈值"},
    {"name":"max_count","type":"Int","default":1,"min":1,"description":"最大匹配数量"},
    {"name":"angle_range","type":"DoubleRange","default":[-5,5],"description":"角度搜索范围(度)"},
    {"name":"angle_step","type":"Double","default":5.0,"description":"角度搜索步长(度)"},
    {"name":"scale_range","type":"DoubleRange","default":[0.95,1.05],"description":"缩放搜索范围"},
    {"name":"scale_step","type":"Double","default":0.05,"description":"缩放搜索步长"},
    {"name":"pyramid_levels","type":"Int","default":3,"min":0,"max":4,"description":"金字塔层数(0=不用金字塔)"},
    {"name":"nms_overlap","type":"Double","default":0.5,"min":0,"max":1,"description":"NMS IoU阈值"},
    {"name":"subpixel","type":"Int","default":1,"description":"子像素精度","enum":[[0,"否"],[1,"是"]]},
    {"name":"enable_binary","type":"Int","default":0,"description":"二值化预处理","enum":[[0,"否"],[1,"是"]]},
    {"name":"binary_threshold","type":"Int","default":128,"min":0,"max":255,"description":"二值化阈值(仅enable_binary=1时)"},
    {"name":"refine_boundary","type":"Int","default":1,"description":"边缘检测精化边界","enum":[[0,"否"],[1,"是"]]},
    {"name":"display_mode","type":"Int","default":0,"description":"结果图显示模式","enum":[[0,"检测结果"],[1,"预处理灰度图"],[2,"预处理二值图"]]},
    {"name":"template_roi","type":"String","default":"","description":"模板ROI[cx,cy,w,h,angle]"},
    {"name":"search_roi","type":"String","default":"","description":"搜索区ROI"},
    {"name":"search_circle_roi","type":"String","default":"","description":"拟合圆搜索区[cx,cy,radius]"},
    {"name":"draw_color","type":"String","default":"#00FF00","description":"匹配框/标注颜色(#RRGGBB)"},
    {"name":"draw_thickness","type":"Int","default":2,"description":"框线粗细(1-20)"},
    {"name":"point_size","type":"Int","default":5,"description":"中心点大小(1-50)"}
  ]
})JSON";
    return kDesc;
}

std::unique_ptr<AlgoBase> TemplateMatchAlgo::create() {
    return std::make_unique<TemplateMatchAlgo>();
}

} // namespace vz
