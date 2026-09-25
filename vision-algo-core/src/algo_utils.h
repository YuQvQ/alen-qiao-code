// algo_utils.h — 算法层公共工具：cv::Mat <-> VzImage、JSON 解析等
//
// 设计动机：
//   - 多个算法（ImageSource / TemplateMatch / CameraSource ...）都需要相同的
//     互转 + JSON 解析函数。集中放在一处避免代码漂移。
//   - 放在 namespace vz::detail 内避免与全局符号冲突。
//
// 依赖：OpenCV core + imgproc + vision_types.h
//
#pragma once

#include "vision_algo.h"
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>

namespace vz {
namespace detail {

// ============================================================
// JSON 极简解析（避免引入 nlohmann/json 依赖）
//   parseJsonDouble("0.5") -> 0.5
//   parseJsonInt("5")      -> 5
//   parseJsonDoubleRange("[-5, 5]") -> (-5, 5)
//   parseJsonString("\"abc\"") -> abc（去引号）
// ============================================================
inline double parseJsonDouble(const std::string& s, double def) {
    if (s.empty()) return def;
    std::string t = s;
    // C# 端 JsonSerializer.Serialize 可能把数字序列化为带引号的 JSON 字符串
    // 例如 "0.5" 而非 0.5，这里去掉首尾引号再解析
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"')
        t = t.substr(1, t.size() - 2);
    if (t.empty()) return def;
    try { return std::stod(t); } catch (...) { return def; }
}

inline int parseJsonInt(const std::string& s, int def) {
    if (s.empty()) return def;
    std::string t = s;
    // 同上：去掉 JSON 字符串首尾引号
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"')
        t = t.substr(1, t.size() - 2);
    if (t.empty()) return def;
    try { return std::stoi(t); } catch (...) { return def; }
}

inline bool parseJsonDoubleRange(const std::string& s, double& lo, double& hi) {
    auto lb = s.find('[');
    auto rb = s.find(']');
    auto cm = s.find(',');
    if (lb == std::string::npos || rb == std::string::npos || cm == std::string::npos) return false;
    try {
        lo = std::stod(s.substr(lb + 1, cm - lb - 1));
        hi = std::stod(s.substr(cm + 1, rb - cm - 1));
        return true;
    } catch (...) { return false; }
}

// JSON 数组 [x, y, w, h] -> 4 个 double（用于 ROI 参数）
inline bool parseJsonDouble4(const std::string& s,
                              double& a, double& b, double& c, double& d) {
    auto lb = s.find('[');
    auto rb = s.find(']');
    if (lb == std::string::npos || rb == std::string::npos) return false;
    auto body = s.substr(lb + 1, rb - lb - 1);
    int idx = 0;
    double out[4] = {0, 0, 0, 0};
    size_t pos = 0;
    while (pos < body.size() && idx < 4) {
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == ',')) pos++;
        if (pos >= body.size()) break;
        size_t end = body.find(',', pos);
        if (end == std::string::npos) end = body.size();
        try {
            out[idx++] = std::stod(body.substr(pos, end - pos));
        } catch (...) { return false; }
        pos = end + 1;
    }
    if (idx != 4) return false;
    a = out[0]; b = out[1]; c = out[2]; d = out[3];
    return true;
}

// JSON 数组 [a, b, c, d, e] -> 5 个 double（用于带角度的 ROI）
inline bool parseJsonDouble5(const std::string& s,
                              double& a, double& b, double& c, double& d, double& e) {
    auto lb = s.find('[');
    auto rb = s.find(']');
    if (lb == std::string::npos || rb == std::string::npos) return false;
    auto body = s.substr(lb + 1, rb - lb - 1);
    int idx = 0;
    double out[5] = {0, 0, 0, 0, 0};
    size_t pos = 0;
    while (pos < body.size() && idx < 5) {
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == ',')) pos++;
        if (pos >= body.size()) break;
        size_t end = body.find(',', pos);
        if (end == std::string::npos) end = body.size();
        try { out[idx++] = std::stod(body.substr(pos, end - pos)); }
        catch (...) { return false; }
        pos = end + 1;
    }
    if (idx != 5) return false;
    a = out[0]; b = out[1]; c = out[2]; d = out[3]; e = out[4];
    return true;
}

inline std::string parseJsonString(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

// ============================================================
// 绘制参数辅助：颜色 / 粗细 / 点大小
// 颜色支持 "#RRGGBB" 十六进制 或 "B,G,R" 十进制（OpenCV BGR 顺序）
// ============================================================
inline cv::Scalar parseColor(const std::string& value_json, const cv::Scalar& def) {
    std::string s = parseJsonString(value_json);
    if (s.empty()) return def;
    // 去掉可能的空白
    s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
    if (s.empty()) return def;
    // #RRGGBB
    if (s.front() == '#') {
        std::string hex = s.substr(1);
        if (hex.size() == 6) {
            try {
                unsigned long v = std::stoul(hex, nullptr, 16);
                int r = (int)((v >> 16) & 0xFF);
                int g = (int)((v >> 8) & 0xFF);
                int b = (int)(v & 0xFF);
                return cv::Scalar(b, g, r);  // OpenCV BGR
            } catch (...) { return def; }
        }
        // #RGB 短格式
        if (hex.size() == 3) {
            try {
                unsigned long v = std::stoul(hex, nullptr, 16);
                int r = (int)(((v >> 8) & 0xF) * 17);
                int g = (int)(((v >> 4) & 0xF) * 17);
                int b = (int)((v & 0xF) * 17);
                return cv::Scalar(b, g, r);
            } catch (...) { return def; }
        }
        return def;
    }
    // "B,G,R" 十进制（OpenCV 顺序）
    auto first = s.find(',');
    if (first != std::string::npos) {
        auto second = s.find(',', first + 1);
        if (second != std::string::npos) {
            try {
                int b = std::stoi(s.substr(0, first));
                int g = std::stoi(s.substr(first + 1, second - first - 1));
                int r = std::stoi(s.substr(second + 1));
                return cv::Scalar(b, g, r);
            } catch (...) { return def; }
        }
    }
    return def;
}

inline int parseThickness(const std::string& value_json, int def) {
    int v = parseJsonInt(value_json, def);
    if (v < 1) v = 1;
    if (v > 20) v = 20;
    return v;
}

inline int parsePointSize(const std::string& value_json, int def) {
    int v = parseJsonInt(value_json, def);
    if (v < 1) v = 1;
    if (v > 50) v = 50;
    return v;
}

// 把输入图像转成 BGR 三通道用于绘制（不修改原图）
inline cv::Mat toDrawCanvas(const cv::Mat& input) {
    if (input.empty()) return {};
    cv::Mat out;
    int c = input.channels();
    if (c == 1) cv::cvtColor(input, out, cv::COLOR_GRAY2BGR);
    else if (c == 3) out = input.clone();
    else if (c == 4) cv::cvtColor(input, out, cv::COLOR_BGRA2BGR);
    else out = input.clone();
    return out;
}

// ============================================================
// VzImage <-> cv::Mat 互转
// ============================================================

// VzImage -> cv::Mat（拷贝；调用方持有 VzImage）
inline cv::Mat toCvMat(const VzImage* img) {
    if (!img || !img->data) return {};
    int cv_type = CV_8UC1;
    if (img->pixel_type != VZ_PIXEL_UINT8) return {};
    if (img->channels == 1)      cv_type = CV_8UC1;
    else if (img->channels == 3) cv_type = CV_8UC3;
    else if (img->channels == 4) cv_type = CV_8UC4;
    else return {};
    cv::Mat m(img->height, img->width, cv_type, img->data, img->step);
    return m.clone();
}

// cv::Mat -> VzImage（新分配；caller 需 vz_free 释放 .data + VzImage 本身）
inline VzImage* toVzImage(const cv::Mat& m) {
    if (m.empty()) return nullptr;
    int channels = m.channels();
    int pixel_type = VZ_PIXEL_UINT8;
    if (m.depth() == CV_8U)       pixel_type = VZ_PIXEL_UINT8;
    else if (m.depth() == CV_16U) pixel_type = VZ_PIXEL_UINT16;
    else if (m.depth() == CV_32F) pixel_type = VZ_PIXEL_FLOAT32;

    // 确保数据连续（消除 stride padding），避免 memcpy 越界或错位
    cv::Mat cont = m.isContinuous() ? m : m.clone();
    size_t rowBytes = (size_t)cont.cols * cont.elemSize();
    size_t total = rowBytes * (size_t)cont.rows;
    void* buf = std::malloc(total);
    if (!buf) return nullptr;
    std::memcpy(buf, cont.data, total);

    auto* img = (VzImage*)std::malloc(sizeof(VzImage));
    if (!img) { std::free(buf); return nullptr; }
    img->width = cont.cols;
    img->height = cont.rows;
    img->channels = channels;
    img->pixel_type = pixel_type;
    img->data = buf;
    img->step = rowBytes;   // 连续数据：step = cols * elemSize（无 padding）
    img->owns_data = 1;
    img->magic = VZ_IMAGE_MAGIC;
    return img;
}

/// 安全释放算法输出：若是 VzImage 则先释放像素数据，再释放结构体本身；
/// 其他类型（int*/double*/VzPose2D*/VzPoint2D* 等扁平结构）直接 free。
inline void safeReleaseOutput(void* ptr) {
    if (!ptr) return;
    auto* img = (VzImage*)ptr;
    if (img->magic == VZ_IMAGE_MAGIC) {
        if (img->owns_data && img->data) std::free(img->data);
    }
    std::free(ptr);
}

} // namespace detail
} // namespace vz
