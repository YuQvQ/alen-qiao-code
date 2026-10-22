// coordsys_convert.cpp — 坐标系(3x3矩阵) 与 String 互转实现
#include "coordsys_convert.h"
#include "algo_utils.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace vz {

// ============================================================
// 公共解析/序列化
// ============================================================
bool parseCoordSysString(const std::string& s, VzCoordSystem3x3& out) {
    auto lb = s.find('[');
    auto rb = s.find(']');
    std::string body = s;
    if (lb != std::string::npos && rb != std::string::npos && rb > lb)
        body = s.substr(lb + 1, rb - lb - 1);   // 去掉 JSON 中括号
    double v[9] = {1,0,0, 0,1,0, 0,0,1};
    int idx = 0;
    size_t pos = 0;
    while (pos < body.size() && idx < 9) {
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == ',' ||
                                     body[pos] == '\t' || body[pos] == '\n' || body[pos] == '\r')) pos++;
        if (pos >= body.size()) break;
        size_t end = body.find_first_of(", \t\n\r", pos);
        if (end == std::string::npos) end = body.size();
        try { v[idx++] = std::stod(body.substr(pos, end - pos)); }
        catch (...) { return false; }
        pos = end + 1;
    }
    if (idx != 9) return false;
    out = VzCoordSystem3x3{};
    std::memcpy(out.m, v, sizeof(v));
    return true;
}

std::string coordSysToString(const VzCoordSystem3x3& m, int precision) {
    if (precision < 0) precision = 6;
    char buf[32];
    std::string out = "[";
    for (int i = 0; i < 9; ++i) {
        if (i > 0) out += ",";
        std::snprintf(buf, sizeof(buf), "%.*f", precision, m.m[i]);
        out += buf;
    }
    out += "]";
    return out;
}

// ============================================================
// StrToCoordSys
// ============================================================
int StrToCoordSysAlgo::setParam(const std::string& key, const std::string& value_json) {
    // value 参数（当输入端口未连线时使用）
    if (key == "value") {
        if (value_json.empty() || value_json == "\"\"" || value_json == "null") return VZ_OK;
        input_str_ = detail::parseJsonString(value_json);
        has_input_ = true;
        return VZ_OK;
    }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int StrToCoordSysAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "value") {
        if (type_tag != VZ_TYPE_STRING) {
            setLastError("input 'value' expects String type");
            return VZ_ERR_TYPE_MISMATCH;
        }
        if (!data) {
            setLastError("input 'value' data is null");
            return VZ_ERR_INVALID_ARG;
        }
        input_str_ = std::string(static_cast<const char*>(data));
        has_input_ = true;
        return VZ_OK;
    }
    setLastError("unknown input port: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

int StrToCoordSysAlgo::process() {
    if (!has_input_) {
        setLastError("input 'value' not set; connect a String source");
        return VZ_ERR_INVALID_ARG;
    }
    if (!parseCoordSysString(input_str_, result_)) {
        setLastError("解析失败: 需要 9 个数字（行优先），如 [1,0,0,0,1,0,0,0,1]，实际: " + input_str_);
        return VZ_ERR_INVALID_ARG;
    }
    setLastError("");
    return VZ_OK;
}

int StrToCoordSysAlgo::getOutput(const std::string& port_name,
                                  void** out_data, int* out_count, int* out_type_tag) {
    if (port_name != "coord") {
        setLastError("unknown output port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    auto* p = (VzCoordSystem3x3*)std::malloc(sizeof(VzCoordSystem3x3));
    if (!p) return VZ_ERR_NO_MEMORY;
    *p = result_;
    *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_COORD_SYS;
    return VZ_OK;
}

void StrToCoordSysAlgo::releaseOutput(void* ptr) { if (ptr) std::free(ptr); }

const char* StrToCoordSysAlgo::describe() const {
    static const char* kDesc = R"JSON({
  "type_id":"StrToCoordSys",
  "display_name":"String转坐标系",
  "category":"工具",
  "icon":"icons/convert.svg",
  "algo_binding":{"dll":"vision_algo","algo_name":"StrToCoordSys"},
  "inputs":[
    {"name":"value","type":"String","required":false,"description":"矩阵字符串 [a,b,c,d,e,f,g,h,i] 行优先"}
  ],
  "outputs":[
    {"name":"coord","type":"CoordSys","required":true,"description":"3x3坐标系矩阵"}
  ],
  "parameters":[
    {"name":"value","type":"String","default":"[1,0,0,0,1,0,0,0,1]","description":"默认字符串(端口未连线时使用)"}
  ]
})JSON";
    return kDesc;
}

std::unique_ptr<AlgoBase> StrToCoordSysAlgo::create() {
    return std::make_unique<StrToCoordSysAlgo>();
}

// ============================================================
// CoordSysToStr
// ============================================================
int CoordSysToStrAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "precision") {
        precision_ = (int)detail::parseJsonDouble(value_json, 6.0);
        return VZ_OK;
    }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int CoordSysToStrAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "coord") {
        if (type_tag != VZ_TYPE_COORD_SYS) {
            setLastError("input 'coord' expects CoordSys type");
            return VZ_ERR_TYPE_MISMATCH;
        }
        if (!data) {
            setLastError("input 'coord' data is null");
            return VZ_ERR_INVALID_ARG;
        }
        input_ = *static_cast<const VzCoordSystem3x3*>(data);
        has_input_ = true;
        return VZ_OK;
    }
    setLastError("unknown input port: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

int CoordSysToStrAlgo::process() {
    if (!has_input_) {
        setLastError("input 'coord' not set; connect a CoordSys source");
        return VZ_ERR_INVALID_ARG;
    }
    result_ = coordSysToString(input_, precision_);
    setLastError("");
    return VZ_OK;
}

int CoordSysToStrAlgo::getOutput(const std::string& port_name,
                                  void** out_data, int* out_count, int* out_type_tag) {
    if (port_name != "value") {
        setLastError("unknown output port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    size_t n = result_.size() + 1;
    auto* p = (char*)std::malloc(n);
    if (!p) return VZ_ERR_NO_MEMORY;
    std::memcpy(p, result_.c_str(), n);
    *out_data = p; *out_count = (int)result_.size(); *out_type_tag = VZ_TYPE_STRING;
    return VZ_OK;
}

void CoordSysToStrAlgo::releaseOutput(void* ptr) { if (ptr) std::free(ptr); }

const char* CoordSysToStrAlgo::describe() const {
    static const char* kDesc = R"JSON({
  "type_id":"CoordSysToStr",
  "display_name":"坐标系转String",
  "category":"工具",
  "icon":"icons/convert.svg",
  "algo_binding":{"dll":"vision_algo","algo_name":"CoordSysToStr"},
  "inputs":[
    {"name":"coord","type":"CoordSys","required":true,"description":"3x3坐标系矩阵"}
  ],
  "outputs":[
    {"name":"value","type":"String","required":true,"description":"矩阵字符串 [a,b,c,d,e,f,g,h,i]"}
  ],
  "parameters":[
    {"name":"precision","type":"Int","default":6,"min":0,"description":"小数位数"}
  ]
})JSON";
    return kDesc;
}

std::unique_ptr<AlgoBase> CoordSysToStrAlgo::create() {
    return std::make_unique<CoordSysToStrAlgo>();
}

} // namespace vz
