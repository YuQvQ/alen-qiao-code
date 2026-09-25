// bin_threshold.cpp — 二值化算法实现
#include "bin_threshold.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>

namespace vz {

int BinThresholdAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "threshold") {
        threshold_ = detail::parseJsonInt(value_json, 128);
        return VZ_OK;
    }
    if (key == "max_value") {
        max_value_ = detail::parseJsonInt(value_json, 255);
        return VZ_OK;
    }
    if (key == "mode") {
        mode_ = detail::parseJsonString(value_json);
        return VZ_OK;
    }
    if (key == "adaptive_block_size") {
        adaptive_block_size_ = detail::parseJsonInt(value_json, 11);
        if (adaptive_block_size_ % 2 == 0) adaptive_block_size_ += 1; // 必须奇数
        if (adaptive_block_size_ < 3) adaptive_block_size_ = 3;
        return VZ_OK;
    }
    if (key == "adaptive_c") {
        adaptive_c_ = detail::parseJsonDouble(value_json, 2.0);
        return VZ_OK;
    }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int BinThresholdAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
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

int BinThresholdAlgo::process() {
    if (input_.empty()) {
        setLastError("no input image");
        return VZ_ERR_INVALID_ARG;
    }
    // 转灰度
    cv::Mat gray;
    if (input_.channels() == 1) gray = input_;
    else cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);

    if (mode_ == "binary") {
        cv::threshold(gray, output_, threshold_, max_value_, cv::THRESH_BINARY);
    } else if (mode_ == "binary_inv") {
        cv::threshold(gray, output_, threshold_, max_value_, cv::THRESH_BINARY_INV);
    } else if (mode_ == "trunc") {
        cv::threshold(gray, output_, threshold_, max_value_, cv::THRESH_TRUNC);
    } else if (mode_ == "tozero") {
        cv::threshold(gray, output_, threshold_, max_value_, cv::THRESH_TOZERO);
    } else if (mode_ == "tozero_inv") {
        cv::threshold(gray, output_, threshold_, max_value_, cv::THRESH_TOZERO_INV);
    } else if (mode_ == "otsu") {
        cv::threshold(gray, output_, 0, max_value_, cv::THRESH_BINARY | cv::THRESH_OTSU);
    } else if (mode_ == "adaptive") {
        cv::adaptiveThreshold(gray, output_, max_value_,
                              cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY,
                              adaptive_block_size_, adaptive_c_);
    } else {
        setLastError("unknown threshold mode: " + mode_);
        return VZ_ERR_INVALID_ARG;
    }
    return VZ_OK;
}

int BinThresholdAlgo::getOutput(const std::string& port_name,
                                 void** out_data, int* out_count, int* out_type_tag) {
    if (port_name != "image") {
        setLastError("unknown output port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    VzImage* img = detail::toVzImage(output_);
    if (!img) { *out_data = nullptr; *out_count = 0; *out_type_tag = VZ_TYPE_IMAGE; return VZ_OK; }
    *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE;
    return VZ_OK;
}

void BinThresholdAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* BinThresholdAlgo::describe() const {
    static const char* kDesc = R"JSON({
  "type_id":"BinThreshold",
  "display_name":"二值化",
  "category":"预处理",
  "icon":"icons/bin_threshold.svg",
  "algo_binding":{"dll":"vision_algo","algo_name":"BinThreshold"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"二值图"}],
  "parameters":[
    {"name":"threshold","type":"Int","default":128,"min":0,"max":255,"description":"阈值"},
    {"name":"max_value","type":"Int","default":255,"min":0,"max":255,"description":"二值化最大值"},
    {"name":"mode","type":"String","default":"binary","description":"binary/binary_inv/trunc/tozero/tozero_inv/otsu/adaptive"},
    {"name":"adaptive_block_size","type":"Int","default":11,"description":"自适应阈值块大小(奇数)"},
    {"name":"adaptive_c","type":"Double","default":2.0,"description":"自适应阈值常数C"}
  ]
})JSON";
    return kDesc;
}

std::unique_ptr<AlgoBase> BinThresholdAlgo::create() {
    return std::make_unique<BinThresholdAlgo>();
}

} // namespace vz
