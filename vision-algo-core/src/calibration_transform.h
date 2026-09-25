// calibration_transform.h — 标定矩阵转换算法
//
// 端口设计（按用户要求：矩阵通过端口传入 + 坐标通过端口传入/输出）：
//   输入端口：
//     matrix (String)  — 3x3 单应矩阵，JSON 格式 "[a,b,c,d,e,f,g,h,i]"（行优先）
//     point  (Pose2D)  — 待转换的坐标（像素坐标）
//   输出端口：
//     point  (Pose2D)  — 转换后的坐标（世界坐标，角度原样保留）
//   参数：
//     matrix (String)  — 默认矩阵，当 matrix 端口未连线时使用
//
// 变换公式（单应矩阵透视除法）：
//   x' = (m00*x + m01*y + m02) / (m20*x + m21*y + m22)
//   y' = (m10*x + m11*y + m12) / (m20*x + m21*y + m22)
//
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>

namespace vz {

class CalibrationTransformAlgo : public AlgoBase {
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
    // 矩阵：优先用端口传入的，其次用参数设置的默认值
    cv::Mat matrix_ = cv::Mat::eye(3, 3, CV_64F);
    bool    has_port_matrix_ = false;   // 端口是否传入了矩阵

    // 输入坐标
    VzPose2D input_pose_{};             // 待转换坐标
    bool     has_input_pose_ = false;

    // 输出坐标
    VzPose2D output_pose_{};            // 转换后坐标
};

} // namespace vz
