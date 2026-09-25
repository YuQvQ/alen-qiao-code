// algo_base.h — C++ 算法内部基类与注册表
//
// 这是算法层 *内部* 抽象，不出现在 vision_algo.h 公开接口里。
// extern "C" 导出层（export.cpp）通过此抽象统一调度算法实例。
//
#pragma once

#include "vision_algo.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

namespace vz {

// ============================================================
// 内部数据载体（与外部 POD 等价的 C++ 视图）
// ============================================================
struct InputData {
    const void* data = nullptr;
    int         type_tag = VZ_TYPE_UNKNOWN;
};

struct OutputData {
    void* data = nullptr;
    int   count = 0;
    int   type_tag = VZ_TYPE_UNKNOWN;
};

// ============================================================
// 算法基类
// 子类实现 5 个纯虚函数即可：
//   setParam / setInput / process / getOutput / describe
// 子类可重写 releaseOutput 以释放复杂数据（如嵌套 cv::Mat）
// ============================================================
class AlgoBase {
public:
    virtual ~AlgoBase() = default;

    // 参数（key -> JSON 字符串值）
    virtual int setParam(const std::string& key, const std::string& value_json) = 0;

    // 输入端口写入
    virtual int setInput(const std::string& port_name, const void* data, int type_tag) = 0;

    // 执行
    virtual int process() = 0;

    // 输出端口读取（实现需将内部数据转 POD 后 malloc 拷贝）
    virtual int getOutput(const std::string& port_name,
                          void** out_data, int* out_count, int* out_type_tag) = 0;

    // 释放输出（默认走 free）
    virtual void releaseOutput(void* ptr) {
        if (ptr) std::free(ptr);
    }

    // 算法描述 JSON（静态）
    virtual const char* describe() const = 0;

    // 错误
    const std::string& lastError() const { return last_error_; }

protected:
    void setLastError(std::string msg) { last_error_ = std::move(msg); }
    std::string last_error_;
};

// ============================================================
// 注册表
// ============================================================
struct AlgoRegistryEntry {
    std::string name;
    std::function<std::unique_ptr<AlgoBase>()> factory;
    std::function<const char*()> describe;
};

// 注册（线程安全，重复注册返回 false）
bool registerAlgo(AlgoRegistryEntry entry);

// 查找
const AlgoRegistryEntry* findAlgo(const std::string& name);

// 列出全部
const std::vector<AlgoRegistryEntry>& allAlgos();

} // namespace vz
