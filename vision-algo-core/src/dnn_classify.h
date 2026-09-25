// dnn_classify.h — 深度学习图像分类
// 输入：image (Image)
// 输出：result_image (绘制类别/置信度), class_id (Int), confidence (Double), label (String)
// 参数：
//   model   : 模型文件路径(.onnx/.pb/.caffemodel/.weights/.t7)
//   config  : 配置文件路径(.prototxt/.pbtxt, 可选)
//   framework : 0=自动 1=Caffe 2=TensorFlow 3=Darknet 4=Torch 5=ONNX
//   input_w / input_h : 网络输入尺寸
//   scale  : 像素缩放因子(如 1/255)
//   mean_r/g/b : 均值
//   swap_rb : 0/1 是否RGB<->BGR
//   crop   : 0/1 是否中心裁剪
//   labels : 标签文件路径(可选,每行一个类名)
//   draw_color / point_size
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <string>
#include <vector>
namespace vz {
class DnnClassifyAlgo : public AlgoBase {
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
    int input_w_ = 224, input_h_ = 224;
    double scale_ = 1.0;
    double mean_r_ = 0, mean_g_ = 0, mean_b_ = 0;
    int swap_rb_ = 1, crop_ = 0;
    cv::Scalar draw_color_ = cv::Scalar(0, 255, 0);
    int point_size_ = 0;
    // 输出
    int out_class_ = -1;
    double out_conf_ = 0.0;
    std::string out_label_;
    cv::Mat result_image_;
    std::vector<std::string> label_names_;
    bool net_ready_ = false;
    // 缓存网络(避免每次加载)
    cv::dnn::Net net_;
    std::string loaded_model_;
};
} // namespace vz
