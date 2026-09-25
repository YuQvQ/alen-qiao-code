// if_condition.cpp — 条件判断节点实现
#include "if_condition.h"
#include "algo_utils.h"

namespace vz {

int IfConditionAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "op") {
        op_ = detail::parseJsonInt(value_json, 0);
        if (op_ < 0) op_ = 0;
        if (op_ > 5) op_ = 5;
        return VZ_OK;
    }
    if (key == "a") { a_ = detail::parseJsonDouble(value_json, 0.0); has_a_ = true; return VZ_OK; }
    if (key == "b") { b_ = detail::parseJsonDouble(value_json, 0.0); has_b_ = true; return VZ_OK; }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int IfConditionAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "a") {
        if (type_tag == VZ_TYPE_INT) { a_ = data ? *(const int*)data : 0; has_a_ = true; }
        else if (type_tag == VZ_TYPE_DOUBLE) { a_ = data ? *(const double*)data : 0; has_a_ = true; }
        else { setLastError("input 'a' expects Int or Double"); return VZ_ERR_TYPE_MISMATCH; }
        return VZ_OK;
    }
    if (port_name == "b") {
        if (type_tag == VZ_TYPE_INT) { b_ = data ? *(const int*)data : 0; has_b_ = true; }
        else if (type_tag == VZ_TYPE_DOUBLE) { b_ = data ? *(const double*)data : 0; has_b_ = true; }
        else { setLastError("input 'b' expects Int or Double"); return VZ_ERR_TYPE_MISMATCH; }
        return VZ_OK;
    }
    setLastError("unknown input port: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

int IfConditionAlgo::process() {
    if (!has_a_ || !has_b_) {
        setLastError("inputs a and b are required");
        return VZ_ERR_PROCESS;
    }
    bool r = false;
    switch (op_) {
        case 0: r = (a_ > b_); break;
        case 1: r = (a_ >= b_); break;
        case 2: r = (a_ < b_); break;
        case 3: r = (a_ <= b_); break;
        case 4: r = (a_ == b_); break;
        case 5: r = (a_ != b_); break;
        default: r = false; break;
    }
    result_ = r ? 1 : 0;
    return VZ_OK;
}

int IfConditionAlgo::getOutput(const std::string& port_name,
                                void** out_data, int* out_count, int* out_type_tag) {
    if (port_name != "result") {
        setLastError("unknown output port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    int* p = (int*)std::malloc(sizeof(int));
    *p = result_;
    *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_INT;
    return VZ_OK;
}

void IfConditionAlgo::releaseOutput(void* ptr) {
    if (ptr) std::free(ptr);
}

const char* IfConditionAlgo::describe() const {
    static const char* kDesc = R"JSON({
  "type_id":"IfCondition",
  "display_name":"条件判断",
  "category":"工具",
  "icon":"icons/if_condition.svg",
  "algo_binding":{"dll":"vision_algo","algo_name":"IfCondition"},
  "inputs":[
    {"name":"a","type":"Double","required":false,"description":"操作数A(可由参数a提供)"},
    {"name":"b","type":"Double","required":false,"description":"操作数B(可由参数b提供)"}
  ],
  "outputs":[
    {"name":"result","type":"Int","required":true,"description":"比较结果:1=成立,0=不成立"}
  ],
  "parameters":[
    {"name":"op","type":"Int","default":0,"description":"比较运算符", "enum": [[0,">"], [1,">="], [2,"<"], [3,"<="], [4,"=="], [5,"!="]]},
    {"name":"a","type":"Double","default":0.0,"description":"操作数A(若输入端口a未连接则用此值)"},
    {"name":"b","type":"Double","default":0.0,"description":"操作数B(若输入端口b未连接则用此值)"}
  ]
})JSON";
    return kDesc;
}

std::unique_ptr<AlgoBase> IfConditionAlgo::create() {
    return std::make_unique<IfConditionAlgo>();
}

} // namespace vz
