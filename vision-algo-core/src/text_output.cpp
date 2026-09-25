// text_output.cpp — 文本输出节点实现
#include "text_output.h"
#include "algo_utils.h"
#include <cstring>

namespace vz {

int TextOutputAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "prefix") { prefix_ = detail::parseJsonString(value_json); return VZ_OK; }
    if (key == "suffix") { suffix_ = detail::parseJsonString(value_json); return VZ_OK; }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int TextOutputAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name != "value") {
        setLastError("unknown input port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    if (type_tag != VZ_TYPE_STRING) {
        setLastError("input 'value' expects String type");
        return VZ_ERR_TYPE_MISMATCH;
    }
    input_value_ = data ? (const char*)data : "";
    return VZ_OK;
}

int TextOutputAlgo::process() {
    output_text_ = prefix_ + input_value_ + suffix_;
    return VZ_OK;
}

int TextOutputAlgo::getOutput(const std::string& port_name,
                               void** out_data, int* out_count, int* out_type_tag) {
    if (port_name != "text") {
        setLastError("unknown output port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    // 输出字符串：malloc 一块 char*，包含结尾 \0
    size_t len = output_text_.size() + 1;
    char* buf = (char*)std::malloc(len);
    std::memcpy(buf, output_text_.c_str(), len);
    *out_data = buf; *out_count = (int)output_text_.size(); *out_type_tag = VZ_TYPE_STRING;
    return VZ_OK;
}

void TextOutputAlgo::releaseOutput(void* ptr) {
    if (ptr) std::free(ptr);
}

const char* TextOutputAlgo::describe() const {
    static const char* kDesc = R"JSON({
  "type_id":"TextOutput",
  "display_name":"文本输出",
  "category":"工具",
  "icon":"icons/text_output.svg",
  "algo_binding":{"dll":"vision_algo","algo_name":"TextOutput"},
  "inputs":[{"name":"value","type":"String","required":false,"description":"输入文本"}],
  "outputs":[{"name":"text","type":"String","required":true,"description":"输出文本"}],
  "parameters":[
    {"name":"prefix","type":"String","default":"","description":"前缀"},
    {"name":"suffix","type":"String","default":"","description":"后缀"}
  ]
})JSON";
    return kDesc;
}

std::unique_ptr<AlgoBase> TextOutputAlgo::create() {
    return std::make_unique<TextOutputAlgo>();
}

} // namespace vz
