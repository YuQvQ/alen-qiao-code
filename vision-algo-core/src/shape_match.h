// shape_match.h — 形状匹配(基于Hu矩的轮廓形状相似度)
// 输入：image (Image), template (Image)
// 输出：result_image, score (Double, 0-1越大越相似), match (Bool)
// 参数：threshold, min_area
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
namespace vz {
class ShapeMatchAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string&, void**, int*, int*) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    cv::Mat image_, template_;
    double threshold_ = 0.5;
    double min_area_ = 100.0;
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);
    int draw_thickness_ = 2;
    double out_score_ = 0.0;
    int out_match_ = 0;
    cv::Mat result_image_;
};
} // namespace vz
