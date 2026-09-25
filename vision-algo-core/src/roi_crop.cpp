// roi_crop.cpp — ROI裁剪输出子图
#include "roi_crop.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>

namespace vz {

using detail::parseJsonDouble;
using detail::parseJsonDouble5;
using detail::parseJsonDouble4;
using detail::toCvMat;
using detail::toVzImage;

int RoiCropAlgo::setParam(const std::string& key, const std::string& value_json) {
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

int RoiCropAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "image") {
        if (type_tag != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH;
        input_ = toCvMat((const VzImage*)data);
        return VZ_OK;
    }
    setLastError("unknown input: " + port_name); return VZ_ERR_INVALID_ARG;
}

int RoiCropAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    if (!has_roi_) { output_ = input_.clone(); return VZ_OK; }
    cv::RotatedRect rr(cv::Point2f((float)roi_cx_, (float)roi_cy_),
                       cv::Size2f((float)roi_w_, (float)roi_h_), (float)roi_angle_);
    cv::Mat M = cv::getRotationMatrix2D(rr.center, rr.angle, 1.0);
    cv::Mat rotated;
    cv::warpAffine(input_, rotated, M, input_.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    cv::Rect bbox = rr.boundingRect() & cv::Rect(0, 0, input_.cols, input_.rows);
    if (bbox.width <= 0 || bbox.height <= 0) { setLastError("invalid roi"); return VZ_ERR_PROCESS; }
    output_ = rotated(bbox).clone();
    return VZ_OK;
}

int RoiCropAlgo::getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "image") {
        VzImage* img = toVzImage(output_);
        *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE; return VZ_OK;
    }
    setLastError("unknown output: " + port_name); return VZ_ERR_INVALID_ARG;
}

void RoiCropAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* RoiCropAlgo::describe() const {
    return R"JSON({
  "type_id":"RoiCrop","display_name":"ROI裁剪","category":"预处理",
  "algo_binding":{"dll":"vision_algo","algo_name":"RoiCrop"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"裁剪后的子图"}],
  "parameters":[
    {"name":"roi","type":"String","default":"","description":"ROI[cx,cy,w,h,angle]"}
  ]
})JSON";
}

std::unique_ptr<AlgoBase> RoiCropAlgo::create() { return std::make_unique<RoiCropAlgo>(); }

} // namespace vz
