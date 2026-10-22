// coordsys_convert.h — 坐标系(3x3矩阵) 与 String 互转
//
// 两个算法：
//   StrToCoordSys  — String → CoordSys（解析 "[a,b,c,d,e,f,g,h,i]"，行优先 9 个数）
//   CoordSysToStr  — CoordSys → String（输出 "[a,b,c,d,e,f,g,h,i]"）
//
// 字符串格式（两种节点共用）：
//   "[1,0,0,0,1,0,0,0,1]"        JSON 数组（行优先）
//   也容忍空格/换行分隔："1 0 0 0 1 0 0 0 1"
#pragma once

#include "algo_base.h"
#include <opencv2/core.hpp>

namespace vz {

// 通用解析：从字符串提取行优先 9 个 double。支持 JSON 数组或空白/逗号分隔。
bool parseCoordSysString(const std::string& s, VzCoordSystem3x3& out);

// 序列化为 "[a,b,c,d,e,f,g,h,i]"
std::string coordSysToString(const VzCoordSystem3x3& m, int precision = 6);

// ============================================================
// String → CoordSys
// ============================================================
class StrToCoordSysAlgo : public AlgoBase {
public:
    int  setParam(const std::string& key, const std::string& value_json) override;
    int  setInput(const std::string& port_name, const void* data, int type_tag) override;
    int  process() override;
    int  getOutput(const std::string& port_name,
                   void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;

    static std::unique_ptr<AlgoBase> create();

private:
    std::string       input_str_;
    bool              has_input_ = false;
    VzCoordSystem3x3  result_{};
};

// ============================================================
// CoordSys → String
// ============================================================
class CoordSysToStrAlgo : public AlgoBase {
public:
    int  setParam(const std::string& key, const std::string& value_json) override;
    int  setInput(const std::string& port_name, const void* data, int type_tag) override;
    int  process() override;
    int  getOutput(const std::string& port_name,
                   void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;

    static std::unique_ptr<AlgoBase> create();

private:
    VzCoordSystem3x3 input_{};
    bool             has_input_ = false;
    int              precision_ = 6;   // 小数位数
    std::string      result_;
};

} // namespace vz
