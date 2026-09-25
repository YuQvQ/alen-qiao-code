// dnn_detect.h — 深度学习目标检测(YOLO/SSD等)
// 输入：image (Image)
// 输出：result_image (绘制检测框), count (Int), boxes (RectList), confidences (Double 列表-用Point2DList承载? 简化:输出最大置信度)
//   为兼容现有类型: boxes->RectList, 类别标签->String(逗号分隔), 置信度->Double(最高)
// 参数：model, config, framework, input_w/h, scale, mean, swap_rb, conf_threshold, nms_threshold, labels, draw...
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <string>
#include <vector>
namespace vz {
class DnnDetectAlgo : public AlgoBase {
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
    std::string model_, config_, labels_;
    int framework_ = 0;
    int input_w_ = 416, input_h_ = 416;
    double scale_ = 1.0 / 255.0;
    double mean_r_ = 0, mean_g_ = 0, mean_b_ = 0;
    int swap_rb_ = 1, crop_ = 0;
    double conf_threshold_ = 0.5;
    double nms_threshold_ = 0.4;
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);
    int draw_thickness_ = 2;
    int point_size_ = 0;
    // 输出
    struct Det { int classId; double conf; cv::Rect box; };
    std::vector<Det> dets_;
    std::string out_labels_str_;
    double out_max_conf_ = 0.0;
    cv::Mat result_image_;
    std::vector<std::string> label_names_;
    cv::dnn::Net net_;
    std::string loaded_model_;
    bool net_ready_ = false;
};
} // namespace vz
