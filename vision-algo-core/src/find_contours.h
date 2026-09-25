// find_contours.h — 轮廓查找
// 输入：image(二值/灰度图)
// 输出：result_image(绘制轮廓)、points(最大轮廓点集 Point2DList)、count(轮廓数)
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <vector>
namespace vz {
class FindContoursAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string& port_name, void**, int*, int*) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    cv::Mat input_;
    int mode_ = 0;     // 0=外部,1=列表,2=树
    int method_ = 1;   // 近似方法
    double min_area_ = 0;
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);
    int draw_thickness_ = 2;
    std::vector<VzPoint2D> out_points_;
    int out_count_ = 0;
    cv::Mat result_image_;
};
} // namespace vz
