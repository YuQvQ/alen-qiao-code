// image_display.cpp — 图像显示节点（透传）
#include "image_display.h"
#include "algo_utils.h"

namespace vz {

using detail::toCvMat;
using detail::toVzImage;

int ImageDisplayAlgo::setParam(const std::string& key, const std::string& value_json) {
    setLastError("unknown parameter: " + key); return VZ_ERR_INVALID_ARG;
}

int ImageDisplayAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "image") {
        if (type_tag != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH;
        input_ = toCvMat((const VzImage*)data);
        return VZ_OK;
    }
    setLastError("unknown input: " + port_name); return VZ_ERR_INVALID_ARG;
}

int ImageDisplayAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    return VZ_OK;
}

int ImageDisplayAlgo::getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "image") {
        VzImage* img = toVzImage(input_);
        *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE; return VZ_OK;
    }
    setLastError("unknown output: " + port_name); return VZ_ERR_INVALID_ARG;
}

void ImageDisplayAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* ImageDisplayAlgo::describe() const {
    return R"JSON({
  "type_id":"ImageDisplay","display_name":"图像显示","category":"显示",
  "algo_binding":{"dll":"vision_algo","algo_name":"ImageDisplay"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[{"name":"image","type":"Image","required":true,"description":"透传输出图像"}],
  "parameters":[]
})JSON";
}

std::unique_ptr<AlgoBase> ImageDisplayAlgo::create() { return std::make_unique<ImageDisplayAlgo>(); }

} // namespace vz