// dnn_classify.cpp — 深度学习图像分类
#include "dnn_classify.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <fstream>
#include <cstring>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::parseJsonString;
using detail::parseColor; using detail::parsePointSize; using detail::toCvMat; using detail::toVzImage;
int DnnClassifyAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "model") { model_ = parseJsonString(v); net_ready_ = false; return VZ_OK; }
    if (k == "config") { config_ = parseJsonString(v); net_ready_ = false; return VZ_OK; }
    if (k == "framework") { framework_ = parseJsonInt(v, 0); return VZ_OK; }
    if (k == "input_w") { input_w_ = parseJsonInt(v, 224); if (input_w_ < 1) input_w_ = 224; return VZ_OK; }
    if (k == "input_h") { input_h_ = parseJsonInt(v, 224); if (input_h_ < 1) input_h_ = 224; return VZ_OK; }
    if (k == "scale") { scale_ = parseJsonDouble(v, 1.0); return VZ_OK; }
    if (k == "mean_r") { mean_r_ = parseJsonDouble(v, 0); return VZ_OK; }
    if (k == "mean_g") { mean_g_ = parseJsonDouble(v, 0); return VZ_OK; }
    if (k == "mean_b") { mean_b_ = parseJsonDouble(v, 0); return VZ_OK; }
    if (k == "swap_rb") { swap_rb_ = parseJsonInt(v, 1) ? 1 : 0; return VZ_OK; }
    if (k == "crop") { crop_ = parseJsonInt(v, 0) ? 1 : 0; return VZ_OK; }
    if (k == "labels") { labels_ = parseJsonString(v); return VZ_OK; }
    if (k == "draw_color") { draw_color_ = parseColor(v, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (k == "point_size") { point_size_ = parsePointSize(v, 0); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int DnnClassifyAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
static std::vector<std::string> loadLabels(const std::string& path) {
    std::vector<std::string> names;
    if (path.empty()) return names;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) names.push_back(line);
    }
    return names;
}
int DnnClassifyAlgo::process() {
    out_class_ = -1; out_conf_ = 0.0; out_label_.clear();
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    if (model_.empty()) { setLastError("model path empty"); return VZ_ERR_PROCESS; }
    // 加载/缓存网络
    if (!net_ready_ || loaded_model_ != model_) {
        try {
            if (framework_ == 1 || (framework_ == 0 && (model_.find(".caffemodel") != std::string::npos)))
                net_ = cv::dnn::readNetFromCaffe(config_, model_);
            else if (framework_ == 2 || (framework_ == 0 && (model_.find(".pb") != std::string::npos)))
                net_ = cv::dnn::readNetFromTensorflow(model_, config_);
            else if (framework_ == 3 || (framework_ == 0 && (model_.find(".weights") != std::string::npos)))
                net_ = cv::dnn::readNetFromDarknet(config_, model_);
            else if (framework_ == 4 || (framework_ == 0 && (model_.find(".t7") != std::string::npos || model_.find(".net") != std::string::npos)))
                net_ = cv::dnn::readNetFromTorch(model_);
            else
                net_ = cv::dnn::readNet(model_, config_);  // 自动识别(.onnx等)
        } catch (const cv::Exception& e) {
            setLastError(std::string("load model failed: ") + e.what());
            return VZ_ERR_PROCESS;
        }
        if (net_.empty()) { setLastError("failed to load model: " + model_); return VZ_ERR_PROCESS; }
        net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        loaded_model_ = model_;
        net_ready_ = true;
    }
    label_names_ = loadLabels(labels_);
    // 预处理
    cv::Mat blob = cv::dnn::blobFromImage(input_, scale_, cv::Size(input_w_, input_h_),
        cv::Scalar(mean_b_, mean_g_, mean_r_), swap_rb_ != 0, crop_ != 0);
    net_.setInput(blob);
    cv::Mat prob;
    try { prob = net_.forward(); } catch (const cv::Exception& e) { setLastError(std::string("forward failed: ") + e.what()); return VZ_ERR_PROCESS; }
    // 展平找最大值
    cv::Mat flat = prob.reshape(1, 1);
    double minVal, maxVal; cv::Point minLoc, maxLoc;
    cv::minMaxLoc(flat, &minVal, &maxVal, &minLoc, &maxLoc);
    out_class_ = maxLoc.x;
    out_conf_ = maxVal;
    if (out_class_ >= 0 && out_class_ < (int)label_names_.size()) out_label_ = label_names_[out_class_];
    else out_label_ = "class_" + std::to_string(out_class_);
    result_image_ = input_.clone();
    return VZ_OK;
}
int DnnClassifyAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "result_image") { *o = toVzImage(result_image_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    if (p == "class_id") { auto* x = (int*)std::malloc(sizeof(int)); *x = out_class_; *o = x; *n = 1; *tt = VZ_TYPE_INT; return VZ_OK; }
    if (p == "confidence") { auto* x = (double*)std::malloc(sizeof(double)); *x = out_conf_; *o = x; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    if (p == "label") {
        size_t sz = out_label_.size();
        char* buf = (char*)std::malloc(sz + 1);
        std::memcpy(buf, out_label_.c_str(), sz + 1);
        *o = buf; *n = (int)sz; *tt = VZ_TYPE_STRING; return VZ_OK;
    }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void DnnClassifyAlgo::releaseOutput(void* p) { if (p) std::free(p); }
const char* DnnClassifyAlgo::describe() const {
    return R"JSON({
  "type_id":"DnnClassify","display_name":"深度学习-分类","category":"深度学习",
  "algo_binding":{"dll":"vision_algo","algo_name":"DnnClassify"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"叠加类别与置信度的结果图"},
    {"name":"class_id","type":"Int","required":true,"description":"类别ID"},
    {"name":"confidence","type":"Double","required":true,"description":"置信度"},
    {"name":"label","type":"String","required":true,"description":"类别名"}
  ],
  "parameters":[
    {"name":"model","type":"String","default":"","description":"模型文件路径(.onnx/.pb/.caffemodel/.weights)"},
    {"name":"config","type":"String","default":"","description":"配置文件(.prototxt/.pbtxt,可选)"},
    {"name":"framework","type":"Int","default":0,"description":"框架(0=自动)","enum":[[0,"自动"],[1,"Caffe"],[2,"TensorFlow"],[3,"Darknet"],[4,"Torch"],[5,"ONNX"]]},
    {"name":"input_w","type":"Int","default":224,"description":"网络输入宽"},
    {"name":"input_h","type":"Int","default":224,"description":"网络输入高"},
    {"name":"scale","type":"Double","default":1.0,"description":"像素缩放(如0.00392=1/255)"},
    {"name":"mean_r","type":"Double","default":0,"description":"均值R"},
    {"name":"mean_g","type":"Double","default":0,"description":"均值G"},
    {"name":"mean_b","type":"Double","default":0,"description":"均值B"},
    {"name":"swap_rb","type":"Int","default":1,"description":"RGB<->BGR:0=否,1=是","enum":[[0,"否"],[1,"是"]]},
    {"name":"crop","type":"Int","default":0,"description":"中心裁剪:0=否,1=是","enum":[[0,"否"],[1,"是"]]},
    {"name":"labels","type":"String","default":"","description":"标签文件路径(每行一个类名,可选)"},
    {"name":"draw_color","type":"String","default":"#00FF00","description":"文字颜色"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> DnnClassifyAlgo::create() { return std::make_unique<DnnClassifyAlgo>(); }
} // namespace vz
