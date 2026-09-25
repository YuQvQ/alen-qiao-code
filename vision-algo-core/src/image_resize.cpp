// image_resize.cpp — 图像缩放
#include "image_resize.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>

namespace vz {

using detail::parseJsonDouble;
using detail::parseJsonInt;
using detail::toCvMat;
using detail::toVzImage;

int ImageResizeAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "scale") { scale_ = parseJsonDouble(value_json, 1.0); if (scale_ <= 0) scale_ = 1.0; return VZ_OK; }
    if (key == "width") { width_ = parseJsonInt(value_json, 0); if (width_ < 0) width_ = 0; return VZ_OK; }
    if (key == "height") { height_ = parseJsonInt(value_json, 0); if (height_ < 0) height_ = 0; return VZ_OK; }
    if (key == "interpolation") { interpolation_ = parseJsonInt(value_json, 1); if (interpolation_<0)interpolation_=0; if (interpolation_>4)interpolation_=4; return VZ_OK; }
    setLastError("unknown parameter: " + key); return VZ_ERR_INVALID_ARG;
}

int ImageResizeAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "image") {
        if (type_tag != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH;
        input_ = toCvMat((const VzImage*)data);
        return VZ_OK;
    }
    setLastError("unknown input: " + port_name); return VZ_ERR_INVALID_ARG;
}

int ImageResizeAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Size sz;
    if (width_ > 0 && height_ > 0) sz = cv::Size(width_, height_);
    else if (width_ > 0) sz = cv::Size(width_, (int)(input_.rows * width_ / input_.cols));
    else if (height_ > 0) sz = cv::Size((int)(input_.cols * height_ / input_.rows), height_);
    else sz = cv::Size((int)(input_.cols * scale_), (int)(input_.rows * scale_));
    int interp = cv::INTER_LINEAR;
    switch (interpolation_) {
        case 0: interp = cv::INTER_NEAREST; break;
        case 1: interp = cv::INTER_LINEAR; break;
        case 2: interp = cv::INTER_CUBIC; break;
        case 3: interp = cv::INTER_AREA; break;
        case 4: interp = cv::INTER_LANCZOS4; break;
    }
    cv::resize(input_, output_, sz, 0, 0, interp);
    return VZ_OK;
}

int ImageResizeAlgo::getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "image") {
        VzImage* img = toVzImage(output_);
        *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE; return VZ_OK;
    }
    setLastError("unknown output: " + port_name); return VZ_ERR_INVALID_ARG;
}

void ImageResizeAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* ImageResizeAlgo::describe() const {
    return R"JSON({
  "type_id":"ImageResize","display_name":"图像缩放","category":"预处理",
  "algo_binding":{"dll":"vision_algo","algo_name":"ImageResize"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"缩放后的图像"}],
  "parameters":[
    {"name":"scale","type":"Double","default":1.0,"description":"缩放比例(width/height为0时生效)"},
    {"name":"width","type":"Int","default":0,"description":"目标宽度(0=按比例)"},
    {"name":"height","type":"Int","default":0,"description":"目标高度(0=按比例)"},
    {"name":"interpolation","type":"Int","default":1,"description":"插值方法","enum":[[0,"最近邻"],[1,"线性"],[2,"三次"],[3,"面积"],[4,"Lanczos"]]}
  ]
})JSON";
}

std::unique_ptr<AlgoBase> ImageResizeAlgo::create() { return std::make_unique<ImageResizeAlgo>(); }

} // namespace vz
