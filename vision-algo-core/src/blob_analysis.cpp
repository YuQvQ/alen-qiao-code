// blob_analysis.cpp — Blob 分析算法实现
#include "blob_analysis.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

namespace vz {

int BlobAnalysisAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "min_area")  { min_area_  = detail::parseJsonInt(value_json, 50); return VZ_OK; }
    if (key == "max_area")  { max_area_  = detail::parseJsonInt(value_json, 10000000); return VZ_OK; }
    if (key == "sort_by")   { sort_by_   = detail::parseJsonString(value_json); return VZ_OK; }
    if (key == "max_count") { max_count_ = detail::parseJsonInt(value_json, 0); return VZ_OK; }
    if (key == "enable_binary") { enable_binary_ = detail::parseJsonInt(value_json, 1); return VZ_OK; }
    if (key == "binary_threshold") { binary_threshold_ = detail::parseJsonInt(value_json, 128); return VZ_OK; }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int BlobAnalysisAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
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

int BlobAnalysisAlgo::process() {
    if (input_.empty()) {
        setLastError("no input image");
        return VZ_ERR_INVALID_ARG;
    }
    // 转灰度 + 二值化（若已二值则无影响）
    cv::Mat gray;
    if (input_.channels() == 1) gray = input_;
    else cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    cv::Mat bin;
    if (enable_binary_ == 1) {
        // Otsu 自动阈值二值化
        cv::threshold(gray, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    } else if (enable_binary_ == 2) {
        // 手动阈值二值化
        cv::threshold(gray, bin, binary_threshold_, 255, cv::THRESH_BINARY);
    } else {
        // 关闭二值化：直接用灰度图（假设输入已经是二值图，或用户想用 findContours 自带的处理）
        bin = gray;
    }

    // 连通域分析
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(bin, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    struct BlobInfo {
        VzRect rect;
        VzPoint2D center;
        double area;
    };
    std::vector<BlobInfo> blobs;
    for (auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < min_area_ || area > max_area_) continue;
        cv::Moments m = cv::moments(c);
        VzPoint2D center;
        if (m.m00 > 0) {
            center.x = m.m10 / m.m00;
            center.y = m.m01 / m.m00;
        } else {
            center.x = 0; center.y = 0;
        }
        cv::RotatedRect rr = cv::minAreaRect(c);
        VzRect r;
        r.x = rr.center.x; r.y = rr.center.y;
        r.w = rr.size.width; r.h = rr.size.height;
        r.angle = rr.angle;
        blobs.push_back({r, center, area});
    }

    // 排序
    if (sort_by_ == "area") {
        std::sort(blobs.begin(), blobs.end(),
                  [](const BlobInfo& a, const BlobInfo& b) { return a.area > b.area; });
    } else if (sort_by_ == "x") {
        std::sort(blobs.begin(), blobs.end(),
                  [](const BlobInfo& a, const BlobInfo& b) { return a.center.x < b.center.x; });
    } else if (sort_by_ == "y") {
        std::sort(blobs.begin(), blobs.end(),
                  [](const BlobInfo& a, const BlobInfo& b) { return a.center.y < b.center.y; });
    }

    // 限制数量
    if (max_count_ > 0 && (int)blobs.size() > max_count_)
        blobs.resize(max_count_);

    out_rects_.clear();
    out_centers_.clear();
    for (auto& b : blobs) {
        out_rects_.push_back(b.rect);
        out_centers_.push_back(b.center);
    }
    out_count_ = (int)blobs.size();
    return VZ_OK;
}

int BlobAnalysisAlgo::getOutput(const std::string& port_name,
                                 void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "blobs") {
        int n = (int)out_rects_.size();
        auto* arr = (VzRect*)std::malloc(n * sizeof(VzRect));
        if (n > 0) std::memcpy(arr, out_rects_.data(), n * sizeof(VzRect));
        *out_data = arr; *out_count = n; *out_type_tag = VZ_TYPE_RECT_LIST;
        return VZ_OK;
    }
    if (port_name == "centers") {
        int n = (int)out_centers_.size();
        auto* arr = (VzPoint2D*)std::malloc(n * sizeof(VzPoint2D));
        if (n > 0) std::memcpy(arr, out_centers_.data(), n * sizeof(VzPoint2D));
        *out_data = arr; *out_count = n; *out_type_tag = VZ_TYPE_POINT2D_LIST;
        return VZ_OK;
    }
    if (port_name == "count") {
        auto* p = (int*)std::malloc(sizeof(int));
        *p = out_count_;
        *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_INT;
        return VZ_OK;
    }
    setLastError("unknown output port: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

void BlobAnalysisAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* BlobAnalysisAlgo::describe() const {
    static const char* kDesc = R"JSON({
  "type_id":"BlobAnalysis",
  "display_name":"Blob分析",
  "category":"分析",
  "icon":"icons/blob_analysis.svg",
  "algo_binding":{"dll":"vision_algo","algo_name":"BlobAnalysis"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像（建议二值图）"}],
  "outputs":[
    {"name":"blobs","type":"RectList","required":true,"description":"Blob旋转外接矩形列表"},
    {"name":"centers","type":"Point2DList","required":true,"description":"Blob中心列表"},
    {"name":"count","type":"Int","required":true,"description":"Blob数量"}
  ],
  "parameters":[
    {"name":"enable_binary","type":"Int","default":1,"description":"二值化模式:0=关闭(输入已是二值图),1=Otsu自动阈值,2=手动阈值","enum":[[0,"关闭"],[1,"Otsu自动"],[2,"手动阈值"]]},
    {"name":"binary_threshold","type":"Int","default":128,"min":0,"max":255,"description":"手动二值化阈值(仅enable_binary=2时生效)"},
    {"name":"min_area","type":"Int","default":50,"min":0,"description":"最小面积"},
    {"name":"max_area","type":"Int","default":10000000,"description":"最大面积"},
    {"name":"sort_by","type":"String","default":"area","description":"排序方式 area/x/y"},
    {"name":"max_count","type":"Int","default":0,"description":"最大输出数(0=不限)"}
  ]
})JSON";
    return kDesc;
}

std::unique_ptr<AlgoBase> BlobAnalysisAlgo::create() {
    return std::make_unique<BlobAnalysisAlgo>();
}

} // namespace vz
