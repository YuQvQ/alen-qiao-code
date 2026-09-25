// line_intersect.h — 线线交点
// 输入：line_a (Point2DList, 2个端点)、line_b (Point2DList, 2个端点)、image (可选，用于绘制)
// 输出：
//   intersection : Point2DList  交点(1个点)
//   has_intersection : Bool     是否相交(平行时为false)
//   angle        : Double       两线夹角(度, 0-90)
//   result_image : Image        叠加绘制的结果图
// 参数：
//   draw_color      : 交点/直线绘制颜色 "#RRGGBB" 或 "B,G,R"
//   draw_thickness  : 线宽(1-20)
//   point_size      : 交点标记半径(1-50)
//
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <vector>

namespace vz {

class LineIntersectAlgo : public AlgoBase {
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

    // 绘制参数
    cv::Scalar draw_color_ = cv::Scalar(0, 0, 255);  // 默认红色
    int draw_thickness_ = 2;
    int point_size_ = 8;

    // 输出
    VzPoint2D out_intersection_{};
    bool has_intersection_ = false;
    double out_angle_ = 0.0;
    cv::Mat result_image_;
};

} // namespace vz
