// circle_find.cpp — 圆查找算法（基于边缘检测，通过黑白交接找圆）
// 算法流程：
//   1. 转灰度 + 高斯模糊去噪
//   2. 根据极性预处理：
//      - 任意：直接 Canny 边缘
//      - 亮圆暗底：阈值分割取亮区后边缘检测（圆内亮，圆外暗）
//      - 暗圆亮底：阈值分割取暗区后边缘检测（圆内暗，圆外亮）
//   3. HoughCircles 在指定半径范围内找圆
//   4. 按半径接近目标半径 + 累加器值排序
//   5. 绘制结果（绿色圆 + 红色圆心 + 紫色搜索区）
#include "circle_find.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cstdio>

namespace vz {

using detail::parseJsonDouble;
using detail::parseJsonInt;
using detail::parseColor;
using detail::parseThickness;
using detail::parsePointSize;
using detail::toVzImage;

// 解析 [cx, cy, radius]
static bool parseCircleRoi(const std::string& s, double& cx, double& cy, double& r) {
    auto lb = s.find('[');
    auto rb = s.find(']');
    if (lb == std::string::npos || rb == std::string::npos || rb <= lb) return false;
    auto body = s.substr(lb + 1, rb - lb - 1);
    double out[3] = {0, 0, 0};
    int idx = 0;
    size_t pos = 0;
    while (pos < body.size() && idx < 3) {
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == ',')) pos++;
        if (pos >= body.size()) break;
        size_t end = body.find(',', pos);
        if (end == std::string::npos) end = body.size();
        try { out[idx++] = std::stod(body.substr(pos, end - pos)); }
        catch (...) { return false; }
        pos = end + 1;
    }
    if (idx != 3) return false;
    cx = out[0]; cy = out[1]; r = out[2];
    return true;
}

// 最小二乘圆拟合（Kåsa 代数法）：给定一组点，求最优 (a,b,r)
// 最小化 Σ((x²+y²) + A x + B y + C)²，其中 A=-2a, B=-2b, C=a²+b²-r²
// 返回 false 当点数不足或矩阵奇异。
static bool fitCircleLS(const std::vector<cv::Point2d>& pts,
                        double& a, double& b, double& r) {
    size_t n = pts.size();
    if (n < 3) return false;
    double sx=0, sy=0, sxx=0, syy=0, sxy=0;
    double sxxx=0, syyy=0, sxxy=0, sxyy=0;  // Σ(x²+y²)x 等
    double sz=0;  // Σ(x²+y²)
    for (const auto& p : pts) {
        double x = p.x, y = p.y;
        double xy = x*x + y*y;
        sx += x;  sy += y;
        sxx += x*x;  syy += y*y;  sxy += x*y;
        sz += xy;
        sxxx += xy * x;  // Σ(x²+y²)x
        syyy += xy * y;  // Σ(x²+y²)y
    }
    // 法方程：M * [A B C]^T = rhs
    double M[3][3] = {
        {sxx, sxy, sx},
        {sxy, syy, sy},
        {sx,  sy,  (double)n}
    };
    double rhs[3] = {-sxxx, -syyy, -sz};
    // 高斯消元解 3x3
    for (int i = 0; i < 3; i++) {
        int piv = i;
        for (int k = i + 1; k < 3; k++)
            if (std::abs(M[k][i]) > std::abs(M[piv][i])) piv = k;
        if (std::abs(M[piv][i]) < 1e-12) return false;
        for (int k = 0; k < 3; k++) std::swap(M[i][k], M[piv][k]);
        std::swap(rhs[i], rhs[piv]);
        for (int k = i + 1; k < 3; k++) {
            double f = M[k][i] / M[i][i];
            for (int j = i; j < 3; j++) M[k][j] -= f * M[i][j];
            rhs[k] -= f * rhs[i];
        }
    }
    double sol[3];
    for (int i = 2; i >= 0; i--) {
        double s = rhs[i];
        for (int j = i + 1; j < 3; j++) s -= M[i][j] * sol[j];
        sol[i] = s / M[i][i];
    }
    double A = sol[0], B = sol[1], C = sol[2];
    a = -A / 2.0;
    b = -B / 2.0;
    double r2 = a*a + b*b - C;
    if (r2 <= 0) return false;
    r = std::sqrt(r2);
    return true;
}

int CircleFindAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "target_radius")    { target_radius_    = parseJsonDouble(value_json, 0);    return VZ_OK; }
    if (key == "radius_tolerance"){ radius_tolerance_ = parseJsonDouble(value_json, 0.2);  return VZ_OK; }
    if (key == "polarity")         { polarity_         = parseJsonInt(value_json, 0);       return VZ_OK; }
    if (key == "edge_threshold")  { edge_threshold_   = parseJsonInt(value_json, 128);     return VZ_OK; }
    if (key == "enable_binary")   { enable_binary_    = parseJsonInt(value_json, 1); if (enable_binary_ != 0) enable_binary_ = 1; return VZ_OK; }
    if (key == "max_count")       { max_count_        = parseJsonInt(value_json, 1);       return VZ_OK; }
    if (key == "enable_blur")     { enable_blur_      = parseJsonInt(value_json, 1); if (enable_blur_ != 0) enable_blur_ = 1; return VZ_OK; }
    if (key == "blur_ksize") {
        blur_ksize_ = parseJsonInt(value_json, 5);
        if (blur_ksize_ < 0) blur_ksize_ = 0;
        if (blur_ksize_ > 0 && blur_ksize_ % 2 == 0) blur_ksize_++;
        return VZ_OK;
    }
    if (key == "dp")               { dp_       = parseJsonDouble(value_json, 1.0);   return VZ_OK; }
    if (key == "min_dist")         { min_dist_ = parseJsonDouble(value_json, 20.0);  return VZ_OK; }
    if (key == "param1")           { param1_   = parseJsonDouble(value_json, 100.0); return VZ_OK; }
    if (key == "param2")           { param2_   = parseJsonDouble(value_json, 30.0);  return VZ_OK; }
    if (key == "search_roi") {
        // 空字符串 / null / 无效值：不设搜索区，返回 OK（不报错）
        if (value_json.empty() || value_json == "\"\"" || value_json == "null") {
            has_search_roi_ = false;
            return VZ_OK;
        }
        if (parseCircleRoi(value_json, roi_cx_, roi_cy_, roi_r_)) {
            has_search_roi_ = true;
            return VZ_OK;
        }
        // 解析失败：视为未设置搜索区（兼容空值/默认值），不报错
        has_search_roi_ = false;
        return VZ_OK;
    }
    if (key == "draw_color") { draw_color_ = parseColor(value_json, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (key == "center_color") { center_color_ = parseColor(value_json, cv::Scalar(0, 0, 255)); return VZ_OK; }
    if (key == "roi_color") { roi_color_ = parseColor(value_json, cv::Scalar(255, 0, 255)); return VZ_OK; }
    if (key == "draw_thickness") { draw_thickness_ = parseThickness(value_json, 0); return VZ_OK; }
    if (key == "point_size") { point_size_ = parsePointSize(value_json, 0); return VZ_OK; }
    if (key == "enable_label") { enable_label_ = parseJsonInt(value_json, 1); if (enable_label_ != 0) enable_label_ = 1; return VZ_OK; }
    if (key == "label_color") { label_color_ = parseColor(value_json, cv::Scalar(0, 255, 255)); return VZ_OK; }
    if (key == "enable_refine") { enable_refine_ = parseJsonInt(value_json, 1); if (enable_refine_ != 0) enable_refine_ = 1; return VZ_OK; }
    if (key == "fit_threshold") { fit_threshold_ = parseJsonDouble(value_json, 0.0); if (fit_threshold_ < 0) fit_threshold_ = 0; return VZ_OK; }
    if (key == "min_score") { min_score_ = parseJsonDouble(value_json, 0.2); if (min_score_ < 0) min_score_ = 0; if (min_score_ > 1) min_score_ = 1; return VZ_OK; }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int CircleFindAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name != "image") {
        setLastError("unknown input port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    if (type_tag != VZ_TYPE_IMAGE) {
        setLastError("input 'image' expects Image type");
        return VZ_ERR_TYPE_MISMATCH;
    }
    input_ = detail::toCvMat(static_cast<const VzImage*>(data));
    if (input_.empty()) {
        setLastError("input image is empty");
        return VZ_ERR_PROCESS;
    }
    return VZ_OK;
}

int CircleFindAlgo::process() {
    if (input_.empty()) {
        setLastError("no input image");
        return VZ_ERR_INVALID_ARG;
    }

    // 必须先在图像窗口绘制圆形 ROI：未绘制则不执行霍夫圆（避免在整图上搜索导致卡顿），
    // 且圆查找范围只能是用户绘制的 ROI。
    if (!has_search_roi_ || roi_r_ <= 0) {
        out_count_ = 0;
        setLastError("未绘制圆形ROI：请先在右侧图像窗口用工具栏绘制搜索圆，再运行");
        return VZ_ERR_PROCESS;
    }

    out_point_ = {};
    out_radius_ = 0;
    out_count_ = 0;

    // 1. 转灰度（支持 1/3/4 通道）
    cv::Mat gray;
    int ch = input_.channels();
    if (ch == 1) gray = input_;
    else if (ch == 3) cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    else if (ch == 4) cv::cvtColor(input_, gray, cv::COLOR_BGRA2GRAY);
    else { setLastError("unsupported image channels"); return VZ_ERR_PROCESS; }

    // 2. 高斯模糊去噪（霍夫圆对噪声敏感，可通过 enable_blur 开关控制）
    cv::Mat blurred = gray;
    if (enable_blur_ && blur_ksize_ > 0) {
        cv::GaussianBlur(gray, blurred, cv::Size(blur_ksize_, blur_ksize_), 0);
    }

    // 3. 根据极性预处理边缘图
    //    polarity=0 任意：直接用原图做 Canny（HoughCircles 内部会做 Canny）
    //    polarity!=0 且 enable_binary=1：二值化取亮/暗区，让圆边缘更突出
    //    polarity!=0 且 enable_binary=0：不二值化，直接用灰度图做霍夫圆
    cv::Mat searchImg = blurred;
    if (polarity_ != 0 && enable_binary_) {
        cv::Mat binary;
        if (polarity_ == 1) {
            // 亮圆暗底：阈值以上为白（圆内），以下为黑（圆外）
            cv::threshold(blurred, binary, edge_threshold_, 255, cv::THRESH_BINARY);
        } else {
            // 暗圆亮底：阈值以下为白（圆内），以上为黑（圆外）
            cv::threshold(blurred, binary, edge_threshold_, 255, cv::THRESH_BINARY_INV);
        }
        searchImg = binary;
    }

    // 4. 圆形 ROI：裁剪到 ROI 外接框（只处理搜索区小块，避免整图运算卡顿），
    //    再用圆形掩码限定圆心范围。ox/oy 记录裁剪偏移，最后把坐标还原到原图。
    int ox = 0, oy = 0;
    if (has_search_roi_ && roi_r_ > 0) {
        int rr = (int)roi_r_;
        int x0 = std::max(0, (int)roi_cx_ - rr);
        int y0 = std::max(0, (int)roi_cy_ - rr);
        int x1 = std::min(searchImg.cols, (int)roi_cx_ + rr + 1);
        int y1 = std::min(searchImg.rows, (int)roi_cy_ + rr + 1);
        if (x1 > x0 && y1 > y0) {
            cv::Rect bbox(x0, y0, x1 - x0, y1 - y0);
            searchImg = searchImg(bbox).clone();
            ox = x0; oy = y0;
            cv::Mat mask = cv::Mat::zeros(searchImg.size(), CV_8UC1);
            cv::circle(mask,
                       cv::Point((int)roi_cx_ - ox, (int)roi_cy_ - oy),
                       rr, cv::Scalar(255), -1);
            cv::Mat masked;
            cv::bitwise_and(searchImg, searchImg, masked, mask);
            searchImg = masked;
        }
    }

    // 5. 计算半径搜索范围
    int minR = 0, maxR = 0;
    if (target_radius_ > 0) {
        double tol = std::max(0.0, std::min(1.0, radius_tolerance_));
        minR = std::max(1, (int)(target_radius_ * (1.0 - tol)));
        maxR = (int)(target_radius_ * (1.0 + tol)) + 1;
    } else {
        // 未指定目标半径：把搜索范围限定在 ROI 半径内（圆不可能比搜索区还大），
        // 避免全半径搜索导致卡顿。
        minR = 1;
        maxR = std::max(2, (int)roi_r_);
    }
    // 半径范围不能越出 ROI
    if (maxR > (int)roi_r_) maxR = (int)roi_r_;
    if (minR >= maxR) minR = std::max(1, maxR / 2);

    // 6. HoughCircles 找圆
    std::vector<cv::Vec3f> circles;
    cv::HoughCircles(searchImg, circles, cv::HOUGH_GRADIENT,
                     dp_, min_dist_, param1_, param2_, minR, maxR);

    if (circles.empty()) {
        out_count_ = 0;
        setLastError("no circle found");
        return VZ_ERR_PROCESS;
    }

    // 7. 排序：按半径接近 target_radius 排序（如果有目标半径），否则按累加器值
    if (target_radius_ > 0) {
        std::sort(circles.begin(), circles.end(), [&](const cv::Vec3f& a, const cv::Vec3f& b) {
            double da = std::abs(a[2] - target_radius_);
            double db = std::abs(b[2] - target_radius_);
            return da < db;
        });
    }

    // 取最好的一个（裁剪坐标系下的霍夫候选圆）
    auto& best = circles.front();
    double refCX = best[0], refCY = best[1], refR = best[2];

    // 7.5 工业视觉精修与验证：
    //   Canny 提边缘点 → (可选)最小二乘迭代拟合精修 → 评分 → 得分过低判 NG。
    //   提取边缘点始终执行，用于评分（无论是否精修）。
    cv::Mat edges;
    cv::Canny(searchImg, edges, param1_ * 0.4, param1_);
    std::vector<cv::Point> epts;
    cv::findNonZero(edges, epts);
    std::vector<cv::Point2d> pts;
    pts.reserve(epts.size());
    for (auto& p : epts) pts.emplace_back(p.x, p.y);

    double thr = fit_threshold_ > 0 ? fit_threshold_
                                    : std::max(1.5, refR * 0.02);
    std::vector<cv::Point2d> inliers;

    if (enable_refine_) {
        double curA = refCX, curB = refCY, curR = refR;
        for (int iter = 0; iter < 4; iter++) {
            inliers.clear();
            for (auto& p : pts) {
                double d = std::abs(std::hypot(p.x - curA, p.y - curB) - curR);
                if (d <= thr) inliers.push_back(p);
            }
            if (inliers.size() < 3) break;
            double na, nb, nr;
            if (!fitCircleLS(inliers, na, nb, nr) || nr <= 0) break;
            if (std::abs(nr - curR) > curR * 0.5) break;  // 半径跳变过大不信任
            double dc = std::hypot(na - curA, nb - curB);
            curA = na; curB = nb; curR = nr;
            if (dc < 0.05) break;  // 收敛
        }
        if (curR > 0 && curR <= roi_r_ * 1.1) {
            refCX = curA; refCY = curB; refR = curR;
        }
    }

    // 对最终圆计算内点比例(得分)与内点 RMS 拟合误差
    inliers.clear();
    for (auto& p : pts) {
        double d = std::abs(std::hypot(p.x - refCX, p.y - refCY) - refR);
        if (d <= thr) inliers.push_back(p);
    }
    if (!pts.empty())
        out_score_ = (double)inliers.size() / (double)pts.size();
    {
        double sum2 = 0.0;
        for (auto& p : inliers) {
            double d = std::hypot(p.x - refCX, p.y - refCY) - refR;
            sum2 += d * d;
        }
        out_fit_error_ = inliers.empty() ? 0.0 : std::sqrt(sum2 / (double)inliers.size());
    }

    // 验证：得分(边缘内点比例)低于阈值，说明是把噪声/纹理拟合成圆 → 判未找到，不输出
    if (!pts.empty() && out_score_ < min_score_) {
        out_count_ = 0;
        setLastError("未找到圆：得分过低（可能是把噪声拟合成圆，请调大 ROI 或降低 min_score）");
        return VZ_ERR_PROCESS;
    }

    // 裁剪坐标还原到原图坐标
    float cx = (float)refCX + (float)ox;
    float cy = (float)refCY + (float)oy;
    out_point_.point.x = cx;
    out_point_.point.y = cy;
    out_point_.angle   = 0;
    out_radius_ = refR;
    out_count_ = (int)std::min((size_t)max_count_, circles.size());

    // 8. 结果图：仅原图透传，标注由 C# UI 层依据输出数据绘制
    result_image_ = input_.clone();
    return VZ_OK;
}

int CircleFindAlgo::getOutput(const std::string& port_name,
                               void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "result_image") {
        VzImage* img = toVzImage(result_image_);
        if (!img) { *out_data = nullptr; *out_count = 0; *out_type_tag = VZ_TYPE_IMAGE; return VZ_OK; }
        *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE;
        return VZ_OK;
    }
    if (port_name == "point") {
        auto* p = (VzPose2D*)std::malloc(sizeof(VzPose2D));
        *p = out_point_;
        *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_POSE2D;
        return VZ_OK;
    }
    if (port_name == "radius") {
        auto* p = (double*)std::malloc(sizeof(double));
        *p = out_radius_;
        *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_DOUBLE;
        return VZ_OK;
    }
    if (port_name == "count") {
        auto* p = (int*)std::malloc(sizeof(int));
        *p = out_count_;
        *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_INT;
        return VZ_OK;
    }
    if (port_name == "score") {
        auto* p = (double*)std::malloc(sizeof(double));
        *p = out_score_;
        *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_DOUBLE;
        return VZ_OK;
    }
    if (port_name == "fit_error") {
        auto* p = (double*)std::malloc(sizeof(double));
        *p = out_fit_error_;
        *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_DOUBLE;
        return VZ_OK;
    }
    setLastError("unknown output port: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

void CircleFindAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* CircleFindAlgo::describe() const {
    static const char* kDesc = R"JSON({
  "type_id":"CircleFind",
  "display_name":"圆查找",
  "category":"分析",
  "icon":"icons/circle_find.svg",
  "algo_binding":{"dll":"vision_algo","algo_name":"CircleFind"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"叠加检测圆的结果图"},
    {"name":"point","type":"Pose2D","required":true,"description":"圆心坐标(单个,angle=0)"},
    {"name":"radius","type":"Double","required":true,"description":"圆半径"},
    {"name":"score","type":"Double","required":true,"description":"得分:边缘内点比例0~1,越高越好"},
    {"name":"fit_error","type":"Double","required":true,"description":"拟合误差:内点RMS(像素),越小越圆"},
    {"name":"count","type":"Int","required":true,"description":"找到的圆数量(0则NG)"}
  ],
  "parameters":[
    {"name":"search_roi","type":"Roi","shape":"circle","default":"","description":"圆形搜索区[cx,cy,r],留空搜索整图"},
    {"name":"target_radius","type":"Int","default":0,"description":"目标半径(像素),告诉算法要找多大的圆,0=自动"},
    {"name":"radius_tolerance","type":"Double","default":0.2,"description":"半径容差比例(0.2=±20%)"},
    {"name":"polarity","type":"Int","default":0,"description":"边缘极性:通过黑白交接方向找圆","enum":[[0,"任意"],[1,"亮圆暗底"],[2,"暗圆亮底"]]},
    {"name":"enable_binary","type":"Int","default":1,"description":"二值化开关(polarity非0时生效):0=否,1=是","enum":[[0,"否"],[1,"是"]]},
    {"name":"edge_threshold","type":"Int","default":128,"description":"二值化阈值(0-255,区分亮暗区域)"},
    {"name":"max_count","type":"Int","default":1,"description":"最多找几个圆"},
    {"name":"enable_blur","type":"Int","default":1,"description":"高斯模糊(预处理):0=否,1=是","enum":[[0,"否"],[1,"是"]]},
    {"name":"blur_ksize","type":"Int","default":5,"description":"高斯模糊核大小(奇数)"},
    {"name":"dp","type":"Double","default":1.0,"description":"累加器分辨率(高级)"},
    {"name":"min_dist","type":"Double","default":20.0,"description":"圆心最小距离(高级)"},
    {"name":"param1","type":"Double","default":100.0,"description":"Canny高阈值(高级)"},
    {"name":"param2","type":"Double","default":30.0,"description":"累加器阈值(越小检测越多)"},
    {"name":"draw_color","type":"String","default":"#00FF00","description":"圆颜色(#RRGGBB)"},
    {"name":"center_color","type":"String","default":"#FF0000","description":"圆心颜色(#RRGGBB)"},
    {"name":"roi_color","type":"String","default":"#FF00FF","description":"搜索区颜色(#RRGGBB)"},
    {"name":"draw_thickness","type":"Int","default":0,"description":"线粗细(1-20,0=自动)"},
    {"name":"point_size","type":"Int","default":0,"description":"圆心大小(1-50,0=自动)"},
    {"name":"enable_label","type":"Int","default":1,"description":"在找到的圆上绘制半径标签:0=否,1=是","enum":[[0,"否"],[1,"是"]]},
    {"name":"label_color","type":"String","default":"#FFFF00","description":"标签颜色(#RRGGBB)"},
    {"name":"enable_refine","type":"Int","default":1,"description":"最小二乘精修:用边缘点迭代拟合优化霍夫结果,0=否,1=是","enum":[[0,"否"],[1,"是"]]},
    {"name":"fit_threshold","type":"Double","default":0,"description":"内点距离阈值(像素),0=自动(半径的2%)"},
    {"name":"min_score","type":"Double","default":0.2,"description":"最低得分(边缘内点比例0~1),低于此值判未找到,防止把噪声拟合成圆"}
  ]
})JSON";
    return kDesc;
}

std::unique_ptr<AlgoBase> CircleFindAlgo::create() {
    return std::make_unique<CircleFindAlgo>();
}

} // namespace vz
