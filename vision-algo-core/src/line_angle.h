// line_angle.h — 线线角度
// 输入：line_a (Point2DList, 2个端点)、line_b (Point2DList, 2个端点)、image (可选)
// 输出：
//   angle        : Double  两线夹角(度)
//   intersection : Point2DList  两线交点(用于绘制角度弧的顶点)
//   result_image : Image   叠加绘制的结果图
// 参数：
//   draw_color / draw_thickness / point_size
//   angle_mode : 0=绝对角(0-90) 1=有向角(-180~180, B相对A)
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <vector>

namespace vz {

class LineAngleAlgo : public AlgoBase {
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
    std::vector<VzPoint2D> line_a_;
    std::vector<VzPoint2D> line_b_;
    cv::Mat input_image_;
    bool has_image_ = false;

    cv::Scalar draw_color_ = cv::Scalar(0, 255, 255);  // 默认黄色
    int draw_thickness_ = 2;
    int point_size_ = 6;
    int angle_mode_ = 0;  // 0=绝对角 1=有向角

    double out_angle_ = 0.0;
    VzPoint2D out_vertex_{};
    bool has_vertex_ = false;
    cv::Mat result_image_;
};

} // namespace vz
