// pixel_stats.cpp — 区域灰度统计
#include "pixel_stats.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>

namespace vz {

using detail::parseJsonDouble;
using detail::parseJsonDouble4;
using detail::parseColor;
using detail::parseThickness;
using detail::toCvMat;
using detail::toVzImage;

int PixelStatsAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "roi") {
        if (value_json.empty() || value_json == "\"\"" || value_json == "null") { has_roi_ = false; return VZ_OK; }
        if (parseJsonDouble4(value_json, roi_x_, roi_y_, roi_w_, roi_h_)) { has_roi_ = true; return VZ_OK; }
        // 解析失败：视为未设置 ROI（兼容空值/默认值），不报错
        has_roi_ = false; return VZ_OK;
    }
    if (key == "draw_color") { draw_color_ = parseColor(value_json, cv::Scalar(255, 0, 255)); return VZ_OK; }
    if (key == "draw_thickness") { draw_thickness_ = parseThickness(value_json, 2); return VZ_OK; }
    setLastError("unknown parameter: " + key); return VZ_ERR_INVALID_ARG;
}

int PixelStatsAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "image") {
        if (type_tag != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH;
        input_ = toCvMat((const VzImage*)data);
        return VZ_OK;
    }
    setLastError("unknown input: " + port_name); return VZ_ERR_INVALID_ARG;
}

int PixelStatsAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Mat gray;
    int ch = input_.channels();
    if (ch == 1) gray = input_;
    else if (ch == 3) cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    else if (ch == 4) cv::cvtColor(input_, gray, cv::COLOR_BGRA2GRAY);
    else return VZ_ERR_PROCESS;

    cv::Mat roi = gray;
    if (has_roi_) {
        cv::Rect r((int)roi_x_, (int)roi_y_, (int)roi_w_, (int)roi_h_);
        r &= cv::Rect(0, 0, gray.cols, gray.rows);
        if (r.width <= 0 || r.height <= 0) { setLastError("invalid roi"); return VZ_ERR_PROCESS; }
        roi = gray(r);
    }
    cv::Scalar mean, stddev;
    cv::meanStdDev(roi, mean, stddev);
    double mn, mx;
    cv::minMaxLoc(roi, &mn, &mx);
    out_mean_ = mean[0];
    out_stddev_ = stddev[0];
    out_min_ = mn;
    out_max_ = mx;
    out_count_ = roi.rows * roi.cols;

    // 结果图：输入图像透传（标注由 UI 层绘制）
    result_image_ = input_.clone();
    return VZ_OK;
}

int PixelStatsAlgo::getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "result_image") { VzImage* img=toVzImage(result_image_); *out_data=img;*out_count=1;*out_type_tag=VZ_TYPE_IMAGE;return VZ_OK; }
    if (port_name == "mean") { auto* p=(double*)std::malloc(sizeof(double)); *p=out_mean_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_DOUBLE;return VZ_OK; }
    if (port_name == "stddev") { auto* p=(double*)std::malloc(sizeof(double)); *p=out_stddev_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_DOUBLE;return VZ_OK; }
    if (port_name == "min_val") { auto* p=(double*)std::malloc(sizeof(double)); *p=out_min_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_DOUBLE;return VZ_OK; }
    if (port_name == "max_val") { auto* p=(double*)std::malloc(sizeof(double)); *p=out_max_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_DOUBLE;return VZ_OK; }
    if (port_name == "count") { auto* p=(int*)std::malloc(sizeof(int)); *p=out_count_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_INT;return VZ_OK; }
    setLastError("unknown output: " + port_name); return VZ_ERR_INVALID_ARG;
}

void PixelStatsAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* PixelStatsAlgo::describe() const {
    return R"JSON({
  "type_id":"PixelStats","display_name":"像素统计","category":"分析",
  "algo_binding":{"dll":"vision_algo","algo_name":"PixelStats"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"结果图"},
    {"name":"mean","type":"Double","required":true,"description":"灰度均值"},
    {"name":"stddev","type":"Double","required":true,"description":"灰度标准差"},
    {"name":"min_val","type":"Double","required":true,"description":"最小值"},
    {"name":"max_val","type":"Double","required":true,"description":"最大值"},
    {"name":"count","type":"Int","required":true,"description":"像素数"}
  ],
  "parameters":[
    {"name":"roi","type":"String","default":"","description":"ROI[x,y,w,h]"},
    {"name":"draw_color","type":"String","default":"#FF00FF","description":"ROI框颜色(#RRGGBB)"},
    {"name":"draw_thickness","type":"Int","default":2,"description":"框线粗细(1-20)"}
  ]
})JSON";
}

std::unique_ptr<AlgoBase> PixelStatsAlgo::create() { return std::make_unique<PixelStatsAlgo>(); }

} // namespace vz
