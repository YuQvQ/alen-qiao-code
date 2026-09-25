// morphology.cpp — 形态学操作
#include "morphology.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>

namespace vz {

using detail::parseJsonDouble;
using detail::parseJsonInt;
using detail::toCvMat;
using detail::toVzImage;

int MorphologyAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "operation") { operation_ = parseJsonInt(value_json, 0); if (operation_<0)operation_=0; if (operation_>6)operation_=6; return VZ_OK; }
    if (key == "kernel_size") { kernel_size_ = parseJsonInt(value_json, 3); if (kernel_size_<1)kernel_size_=1; if (kernel_size_%2==0)kernel_size_++; return VZ_OK; }
    if (key == "iterations") { iterations_ = parseJsonInt(value_json, 1); if (iterations_<1)iterations_=1; return VZ_OK; }
    if (key == "kernel_shape") { kernel_shape_ = parseJsonInt(value_json, 0); if (kernel_shape_<0)kernel_shape_=0; if (kernel_shape_>2)kernel_shape_=2; return VZ_OK; }
    setLastError("unknown parameter: " + key); return VZ_ERR_INVALID_ARG;
}

int MorphologyAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "image") {
        if (type_tag != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH;
        input_ = toCvMat((const VzImage*)data);
        return VZ_OK;
    }
    setLastError("unknown input: " + port_name); return VZ_ERR_INVALID_ARG;
}

int MorphologyAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    int shape = kernel_shape_ == 0 ? cv::MORPH_RECT : kernel_shape_ == 1 ? cv::MORPH_CROSS : cv::MORPH_ELLIPSE;
    cv::Mat kernel = cv::getStructuringElement(shape, cv::Size(kernel_size_, kernel_size_));
    cv::Mat gray;
    int ch = input_.channels();
    if (ch == 1) gray = input_;
    else if (ch == 3) cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    else if (ch == 4) cv::cvtColor(input_, gray, cv::COLOR_BGRA2GRAY);
    else return VZ_ERR_PROCESS;

    switch (operation_) {
        case 0: cv::erode(gray, output_, kernel, cv::Point(-1,-1), iterations_); break;
        case 1: cv::dilate(gray, output_, kernel, cv::Point(-1,-1), iterations_); break;
        case 2: cv::morphologyEx(gray, output_, cv::MORPH_OPEN, kernel, cv::Point(-1,-1), iterations_); break;
        case 3: cv::morphologyEx(gray, output_, cv::MORPH_CLOSE, kernel, cv::Point(-1,-1), iterations_); break;
        case 4: cv::morphologyEx(gray, output_, cv::MORPH_GRADIENT, kernel, cv::Point(-1,-1), iterations_); break;
        case 5: cv::morphologyEx(gray, output_, cv::MORPH_TOPHAT, kernel, cv::Point(-1,-1), iterations_); break;
        case 6: cv::morphologyEx(gray, output_, cv::MORPH_BLACKHAT, kernel, cv::Point(-1,-1), iterations_); break;
    }
    return VZ_OK;
}

int MorphologyAlgo::getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "image") {
        VzImage* img = toVzImage(output_);
        *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE; return VZ_OK;
    }
    setLastError("unknown output: " + port_name); return VZ_ERR_INVALID_ARG;
}

void MorphologyAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* MorphologyAlgo::describe() const {
    return R"JSON({
  "type_id":"Morphology","display_name":"形态学","category":"预处理",
  "algo_binding":{"dll":"vision_algo","algo_name":"Morphology"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"形态学处理结果"}],
  "parameters":[
    {"name":"operation","type":"Int","default":0,"description":"操作类型","enum":[[0,"腐蚀"],[1,"膨胀"],[2,"开运算"],[3,"闭运算"],[4,"梯度"],[5,"顶帽"],[6,"黑帽"]]},
    {"name":"kernel_shape","type":"Int","default":0,"description":"核形状","enum":[[0,"矩形"],[1,"十字"],[2,"椭圆"]]},
    {"name":"kernel_size","type":"Int","default":3,"description":"核大小(奇数)"},
    {"name":"iterations","type":"Int","default":1,"description":"迭代次数"}
  ]
})JSON";
}

std::unique_ptr<AlgoBase> MorphologyAlgo::create() { return std::make_unique<MorphologyAlgo>(); }

} // namespace vz
