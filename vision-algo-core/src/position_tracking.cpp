// position_tracking.cpp — 位置追踪算法实现
#include "position_tracking.h"
#include "algo_utils.h"
#include <cmath>
#include <cstring>

namespace vz {

int PositionTrackingAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "ref_x")     { ref_x_     = detail::parseJsonDouble(value_json, 0.0); return VZ_OK; }
    if (key == "ref_y")     { ref_y_     = detail::parseJsonDouble(value_json, 0.0); return VZ_OK; }
    if (key == "ref_angle") { ref_angle_ = detail::parseJsonDouble(value_json, 0.0); return VZ_OK; }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int PositionTrackingAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name != "current") {
        setLastError("unknown input port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    if (type_tag != VZ_TYPE_POSE2D) {
        setLastError("input 'current' expects Pose2D type");
        return VZ_ERR_TYPE_MISMATCH;
    }
    if (data) current_ = *static_cast<const VzPose2D*>(data);
    return VZ_OK;
}

int PositionTrackingAlgo::process() {
    delta_.point.x = current_.point.x - ref_x_;
    delta_.point.y = current_.point.y - ref_y_;
    double a = current_.angle - ref_angle_;
    // 归一化到 [-180, 180]
    while (a > 180.0)  a -= 360.0;
    while (a < -180.0) a += 360.0;
    delta_.angle = a;
    return VZ_OK;
}

int PositionTrackingAlgo::getOutput(const std::string& port_name,
                                      void** out_data, int* out_count, int* out_type_tag) {
    if (port_name != "delta") {
        setLastError("unknown output port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    auto* p = (VzPose2D*)std::malloc(sizeof(VzPose2D));
    *p = delta_;
    *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_POSE2D;
    return VZ_OK;
}

void PositionTrackingAlgo::releaseOutput(void* ptr) {
    if (ptr) std::free(ptr);
}

const char* PositionTrackingAlgo::describe() const {
    static const char* kDesc = R"JSON({
  "type_id":"PositionTracking",
  "display_name":"位置追踪",
  "category":"分析",
  "icon":"icons/position_tracking.svg",
  "algo_binding":{"dll":"vision_algo","algo_name":"PositionTracking"},
  "inputs":[{"name":"current","type":"Pose2D","required":true,"description":"当前位姿"}],
  "outputs":[{"name":"delta","type":"Pose2D","required":true,"description":"相对参考位姿的偏移"}],
  "parameters":[
    {"name":"ref_x","type":"Double","default":0.0,"description":"参考X"},
    {"name":"ref_y","type":"Double","default":0.0,"description":"参考Y"},
    {"name":"ref_angle","type":"Double","default":0.0,"description":"参考角度(度)"}
  ]
})JSON";
    return kDesc;
}

std::unique_ptr<AlgoBase> PositionTrackingAlgo::create() {
    return std::make_unique<PositionTrackingAlgo>();
}

} // namespace vz
