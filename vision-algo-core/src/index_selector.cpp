// index_selector.cpp — 索引选择器节点实现
#include "index_selector.h"
#include "algo_utils.h"
#include <cstring>
#include <cstdlib>

namespace vz {

int IndexSelectorAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "default_index") {
        param_index_ = detail::parseJsonInt(value_json, 0);
        return VZ_OK;
    }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int IndexSelectorAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "points") {
        // 点组（Point2DList -> VzPoint2DArray）
        if (type_tag != VZ_TYPE_POINT2D_LIST) {
            setLastError("input 'points' expects Point2DList type");
            return VZ_ERR_TYPE_MISMATCH;
        }
        if (!data) {
            setLastError("input 'points' data is null");
            return VZ_ERR_INVALID_ARG;
        }
        auto* arr = static_cast<const VzPoint2DArray*>(data);
        points_.clear();
        if (arr->count > 0 && arr->data) {
            points_.assign(arr->data, arr->data + arr->count);
        }
        has_points_ = true;
        return VZ_OK;
    }
    if (port_name == "index") {
        if (type_tag != VZ_TYPE_INT) {
            setLastError("input 'index' expects Int type");
            return VZ_ERR_TYPE_MISMATCH;
        }
        if (!data) {
            setLastError("input 'index' data is null");
            return VZ_ERR_INVALID_ARG;
        }
        port_index_ = *static_cast<const int*>(data);
        return VZ_OK;
    }
    setLastError("unknown input port: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

int IndexSelectorAlgo::process() {
    if (!has_points_) {
        setLastError("input 'points' not set; connect a Point2DList source");
        return VZ_ERR_INVALID_ARG;
    }
    // index 来源：端口优先，端口未传则用参数兜底
    int idx = (port_index_ >= 0) ? port_index_ : param_index_;

    if (points_.empty()) {
        setLastError("points list is empty; cannot select any index");
        return VZ_ERR_PROCESS;
    }
    if (idx < 0 || idx >= (int)points_.size()) {
        setLastError("index out of range: idx=" + std::to_string(idx) +
                     " size=" + std::to_string(points_.size()));
        return VZ_ERR_PROCESS;
    }

    output_pose_.point = points_[idx];
    output_pose_.angle = 0;  // 索引取点不携带角度
    return VZ_OK;
}

int IndexSelectorAlgo::getOutput(const std::string& port_name,
                                  void** out_data, int* out_count, int* out_type_tag) {
    if (port_name != "point") {
        setLastError("unknown output port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    // 输出单个 Pose2D（caller 必须 vz_free）
    auto* p = (VzPose2D*)std::malloc(sizeof(VzPose2D));
    *p = output_pose_;
    *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_POSE2D;
    return VZ_OK;
}

void IndexSelectorAlgo::releaseOutput(void* ptr) {
    if (ptr) std::free(ptr);
}

const char* IndexSelectorAlgo::describe() const {
    static const char* kDesc = R"JSON({
  "type_id":"IndexSelector",
  "display_name":"索引选择器",
  "category":"工具",
  "icon":"icons/index_selector.svg",
  "algo_binding":{"dll":"vision_algo","algo_name":"IndexSelector"},
  "inputs":[
    {"name":"points","type":"Point2DList","required":true,"description":"点组"},
    {"name":"index","type":"Int","required":false,"description":"索引(0基,未连线时用参数default_index)"}
  ],
  "outputs":[
    {"name":"point","type":"Pose2D","required":true,"description":"Points[index]单个点(angle=0)"}
  ],
  "parameters":[
    {"name":"default_index","type":"Int","default":0,"description":"默认索引(端口未连线时使用)"}
  ]
})JSON";
    return kDesc;
}

std::unique_ptr<AlgoBase> IndexSelectorAlgo::create() {
    return std::make_unique<IndexSelectorAlgo>();
}

} // namespace vz
