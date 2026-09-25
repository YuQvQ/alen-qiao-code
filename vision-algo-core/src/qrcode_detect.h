// qrcode_detect.h — 二维码检测与解码
// 输入：image (Image)
// 输出：result_image, content (String), count (Int), points (Point2DList 4个角点)
// 参数：draw_color, draw_thickness, point_size
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <string>
#include <vector>
namespace vz {
class QrCodeDetectAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string&, void**, int*, int*) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    cv::Mat input_;
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);
    int draw_thickness_ = 2;
    int point_size_ = 5;
    std::string out_content_;
    std::vector<VzPoint2D> out_points_;
    int out_count_ = 0;
    cv::Mat result_image_;
};
} // namespace vz
