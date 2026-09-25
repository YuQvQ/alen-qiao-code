// feature_match.h — 特征匹配(ORB)
// 输入：image (Image), template (Image)
// 输出：result_image (绘制匹配连线), count (Int), match_points (Point2DList 模板点), target_points (Point2DList 目标点)
// 参数：n_features, ratio_threshold, draw_color, draw_thickness
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <vector>
namespace vz {
class FeatureMatchAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string& port_name, void**, int*, int*) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    cv::Mat image_, template_;
    int n_features_ = 500;
    double ratio_ = 0.75;
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);
    int draw_thickness_ = 1;
    std::vector<VzPoint2D> tgt_pts_, tpl_pts_;
    int count_ = 0;
    cv::Mat result_image_;
};
} // namespace vz
