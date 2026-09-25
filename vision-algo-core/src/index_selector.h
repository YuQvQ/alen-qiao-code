// index_selector.h — 索引选择器节点
//
// 输入：points (Point2DList) — 点组
// 输入：index  (Int)         — 索引（从 0 开始）
// 输出：point  (Pose2D)      — Points[index]（angle=0）
// 参数：default_index (Int)  — 端口未连线时兜底索引
//
// 用途：从一组点中按索引取一个，便于下游节点接收单个点输入。
//
#pragma once
#include "algo_base.h"
#include <vector>

namespace vz {

class IndexSelectorAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string& port_name,
                  void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;

    static std::unique_ptr<AlgoBase> create();

private:
    std::vector<VzPoint2D> points_;
    bool has_points_ = false;
    int  port_index_ = -1;   // 端口传入的 index（-1 表示端口未传）
    int  param_index_ = 0;   // 默认 index 参数兜底
    VzPose2D output_pose_{}; // 输出
};

} // namespace vz
