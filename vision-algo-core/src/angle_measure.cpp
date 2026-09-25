// angle_measure.cpp — 角度测量（两角度夹角）
#include "angle_measure.h"
#include "algo_utils.h"
#include <cmath>

namespace vz {

int AngleMeasureAlgo::setParam(const std::string& key, const std::string& value_json) {
    setLastError("unknown parameter: " + key); return VZ_ERR_INVALID_ARG;
}

int AngleMeasureAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    double val = 0;
    if (type_tag == VZ_TYPE_DOUBLE) val = *(const double*)data;
    else if (type_tag == VZ_TYPE_INT) val = *(const int*)data;
    else return VZ_ERR_TYPE_MISMATCH;
    if (port_name == "a") { a_ = val; has_a_ = true; return VZ_OK; }
    if (port_name == "b") { b_ = val; has_b_ = true; return VZ_OK; }
    setLastError("unknown input: " + port_name); return VZ_ERR_INVALID_ARG;
}

int AngleMeasureAlgo::process() {
    if (!has_a_ || !has_b_) { setLastError("need inputs a and b"); return VZ_ERR_PROCESS; }
    double diff = b_ - a_;
    // 归一化到 [-180, 180]
    while (diff > 180) diff -= 360;
    while (diff < -180) diff += 360;
    out_angle_ = diff;
    return VZ_OK;
}

int AngleMeasureAlgo::getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "angle") { auto* p=(double*)std::malloc(sizeof(double)); *p=out_angle_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_DOUBLE;return VZ_OK; }
    setLastError("unknown output: " + port_name); return VZ_ERR_INVALID_ARG;
}

void AngleMeasureAlgo::releaseOutput(void* ptr) { if (ptr) std::free(ptr); }

const char* AngleMeasureAlgo::describe() const {
    return R"JSON({
  "type_id":"AngleMeasure","display_name":"角度测量","category":"对位",
  "algo_binding":{"dll":"vision_algo","algo_name":"AngleMeasure"},
  "inputs":[
    {"name":"a","type":"Double","required":true,"description":"角度A(度)"},
    {"name":"b","type":"Double","required":true,"description":"角度B(度)"}
  ],
  "outputs":[{"name":"angle","type":"Double","required":true,"description":"夹角(B-A,归一化到[-180,180])"}],
  "parameters":[]
})JSON";
}

std::unique_ptr<AlgoBase> AngleMeasureAlgo::create() { return std::make_unique<AngleMeasureAlgo>(); }

} // namespace vz
