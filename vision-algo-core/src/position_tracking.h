// position_tracking.h — 位置追踪算法
//
// 输入：current (Pose2D) — 当前检测到的位姿
// 输出：delta (Pose2D) — 当前位姿相对于参考位姿的偏移
// 参数：
//   ref_x, ref_y, ref_angle: 参考位姿（标定/基准）
// 计算：
//   delta.point.x = current.x - ref_x
//   delta.point.y = current.y - ref_y
//   delta.angle   = current.angle - ref_angle  (归一化到 [-180, 180])
//
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>

namespace vz {

class PositionTrackingAlgo : public AlgoBase {
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
    VzPose2D current_{};
    double ref_x_ = 0, ref_y_ = 0, ref_angle_ = 0;
    VzPose2D delta_{};
};

} // namespace vz
