// line_fit.h — 点集最小二乘直线拟合
// 输入：points(Point2DList), image(可选,用于绘制)
// 输出：result_image(Image), angle(Double), intercept(Double), point(Pose2D), residuals(Double)
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <vector>

namespace vz {

class LineFitAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    std::vector<VzPoint2D> points_;
    cv::Mat input_image_;
    bool has_image_ = false;
    cv::Mat result_image_;
    double out_angle_ = 0;
    double out_intercept_ = 0;
    double out_residuals_ = 0;
    VzPose2D out_point_{};
    // 绘制参数
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);
    int draw_thickness_ = 2;
    int point_size_ = 4;
};

} // namespace vz
