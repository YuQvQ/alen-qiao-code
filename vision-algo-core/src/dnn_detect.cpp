// dnn_detect.cpp — 深度学习目标检测
#include "dnn_detect.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <fstream>
#include <cstring>
#include <algorithm>
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::parseJsonString;
using detail::parseColor; using detail::parseThickness; using detail::parsePointSize;
using detail::toCvMat; using detail::toVzImage;
int DnnDetectAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "model") { model_ = parseJsonString(v); net_ready_ = false; return VZ_OK; }
    if (k == "config") { config_ = parseJsonString(v); net_ready_ = false; return VZ_OK; }
    if (k == "framework") { framework_ = parseJsonInt(v, 0); return VZ_OK; }
    if (k == "input_w") { input_w_ = parseJsonInt(v, 416); if (input_w_ < 1) input_w_ = 416; return VZ_OK; }
    if (k == "input_h") { input_h_ = parseJsonInt(v, 416); if (input_h_ < 1) input_h_ = 416; return VZ_OK; }
    if (k == "scale") { scale_ = parseJsonDouble(v, 1.0/255.0); return VZ_OK; }
    if (k == "mean_r") { mean_r_ = parseJsonDouble(v, 0); return VZ_OK; }
    if (k == "mean_g") { mean_g_ = parseJsonDouble(v, 0); return VZ_OK; }
    if (k == "mean_b") { mean_b_ = parseJsonDouble(v, 0); return VZ_OK; }
    if (k == "swap_rb") { swap_rb_ = parseJsonInt(v, 1) ? 1 : 0; return VZ_OK; }
    if (k == "crop") { crop_ = parseJsonInt(v, 0) ? 1 : 0; return VZ_OK; }
    if (k == "conf_threshold") { conf_threshold_ = parseJsonDouble(v, 0.5); return VZ_OK; }
    if (k == "nms_threshold") { nms_threshold_ = parseJsonDouble(v, 0.4); return VZ_OK; }
    if (k == "labels") { labels_ = parseJsonString(v); return VZ_OK; }
    if (k == "draw_color") { draw_color_ = parseColor(v, cv::Scalar(0, 255, 0)); return VZ_OK; }
    if (k == "draw_thickness") { draw_thickness_ = parseThickness(v, 2); return VZ_OK; }
    if (k == "point_size") { point_size_ = parsePointSize(v, 0); return VZ_OK; }
    setLastError("unknown parameter: " + k); return VZ_ERR_INVALID_ARG;
}
int DnnDetectAlgo::setInput(const std::string& p, const void* d, int t) {
    if (p == "image") { if (t != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH; input_ = toCvMat((const VzImage*)d); return VZ_OK; }
    setLastError("unknown input: " + p); return VZ_ERR_INVALID_ARG;
}
static std::vector<std::string> loadLabelsDet(const std::string& path) {
    std::vector<std::string> names;
    if (path.empty()) return names;
    std::ifstream f(path); std::string line;
    while (std::getline(f, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); if (!line.empty()) names.push_back(line); }
    return names;
}
int DnnDetectAlgo::process() {
    dets_.clear(); out_labels_str_.clear(); out_max_conf_ = 0.0;
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    if (model_.empty()) { setLastError("model path empty"); return VZ_ERR_PROCESS; }
    if (!net_ready_ || loaded_model_ != model_) {
        try {
            if (framework_ == 1 || (framework_ == 0 && model_.find(".caffemodel") != std::string::npos))
                net_ = cv::dnn::readNetFromCaffe(config_, model_);
            else if (framework_ == 2 || (framework_ == 0 && model_.find(".pb") != std::string::npos))
                net_ = cv::dnn::readNetFromTensorflow(model_, config_);
            else if (framework_ == 3 || (framework_ == 0 && model_.find(".weights") != std::string::npos))
                net_ = cv::dnn::readNetFromDarknet(config_, model_);
            else
                net_ = cv::dnn::readNet(model_, config_);
        } catch (const cv::Exception& e) { setLastError(std::string("load model failed: ") + e.what()); return VZ_ERR_PROCESS; }
        if (net_.empty()) { setLastError("failed to load model: " + model_); return VZ_ERR_PROCESS; }
        net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        loaded_model_ = model_; net_ready_ = true;
    }
    label_names_ = loadLabelsDet(labels_);
    cv::Mat blob = cv::dnn::blobFromImage(input_, scale_, cv::Size(input_w_, input_h_),
        cv::Scalar(mean_b_, mean_g_, mean_r_), swap_rb_ != 0, crop_ != 0);
    net_.setInput(blob);
    std::vector<cv::Mat> outs;
    try {
        std::vector<cv::String> outNames = net_.getUnconnectedOutLayersNames();
        net_.forward(outs, outNames);
    } catch (const cv::Exception& e) { setLastError(std::string("forward failed: ") + e.what()); return VZ_ERR_PROCESS; }
    // 解析: 支持 YOLO([x,y,w,h,conf,class_scores...]) 与 1x1xNx7(SSD) 格式
    std::vector<int> classIds; std::vector<float> confidences; std::vector<cv::Rect> boxes;
    int W = input_.cols, H = input_.rows;
    for (auto& out : outs) {
        if (out.dims == 4 && out.size[2] > 0 && out.size[3] == 7) {
            // SSD: [1,1,N,7] -> 7 = [batchId, classId, conf, x, y, w, h] (归一化)
            for (int i = 0; i < out.size[2]; ++i) {
                float* d = (float*)out.ptr(0, 0, i);
                float conf = d[2]; if (conf < conf_threshold_) continue;
                int cid = (int)d[1];
                int x = (int)(d[3] * W), y = (int)(d[4] * H);
                int w = (int)((d[5] - d[3]) * W), h = (int)((d[6] - d[4]) * H);
                classIds.push_back(cid); confidences.push_back(conf);
                boxes.push_back(cv::Rect(x, y, w, h));
            }
        } else {
            // YOLO: rows = 检测数, cols = 5+numClasses
            cv::Mat dt = out.reshape(1, (int)out.total() / out.cols);
            for (int i = 0; i < dt.rows; ++i) {
                const float* row = dt.ptr<float>(i);
                float objConf = row[4];
                int nc = dt.cols - 5;
                // 找最大类别分数
                int bestC = 0; float bestS = 0;
                for (int c = 0; c < nc; ++c) { float s = row[5 + c] * objConf; if (s > bestS) { bestS = s; bestC = c; } }
                if (bestS < conf_threshold_) continue;
                float cx = row[0] * W, cy = row[1] * H, w = row[2] * W, h = row[3] * H;
                int x = (int)(cx - w / 2), y = (int)(cy - h / 2);
                classIds.push_back(bestC); confidences.push_back(bestS);
                boxes.push_back(cv::Rect(x, y, (int)w, (int)h));
            }
        }
    }
    // NMS
    std::vector<int> idxs;
    if (!boxes.empty()) cv::dnn::NMSBoxes(boxes, confidences, (float)conf_threshold_, (float)nms_threshold_, idxs);
    for (int i : idxs) {
        Det d; d.classId = classIds[i]; d.conf = confidences[i]; d.box = boxes[i] & cv::Rect(0, 0, W, H);
        if (d.box.width <= 0 || d.box.height <= 0) continue;
        dets_.push_back(d);
        if (d.conf > out_max_conf_) out_max_conf_ = d.conf;
    }
    result_image_ = input_.clone();
    for (auto& d : dets_) {
        std::string name = (d.classId >= 0 && d.classId < (int)label_names_.size()) ? label_names_[d.classId] : ("cls" + std::to_string(d.classId));
        if (!out_labels_str_.empty()) out_labels_str_ += ",";
        out_labels_str_ += name;
    }
    return VZ_OK;
}
int DnnDetectAlgo::getOutput(const std::string& p, void** o, int* n, int* tt) {
    if (p == "result_image") { *o = toVzImage(result_image_); *n = 1; *tt = VZ_TYPE_IMAGE; return VZ_OK; }
    if (p == "count") { auto* x = (int*)std::malloc(sizeof(int)); *x = (int)dets_.size(); *o = x; *n = 1; *tt = VZ_TYPE_INT; return VZ_OK; }
    if (p == "boxes") {
        int sz = (int)dets_.size();
        VzRect* arr = (VzRect*)std::malloc(sizeof(VzRect) * (sz ? sz : 1));
        for (int i = 0; i < sz; ++i) {
            arr[i].x = dets_[i].box.x + dets_[i].box.width / 2.0;
            arr[i].y = dets_[i].box.y + dets_[i].box.height / 2.0;
            arr[i].w = dets_[i].box.width;
            arr[i].h = dets_[i].box.height;
            arr[i].angle = 0;
        }
        *o = arr; *n = sz; *tt = VZ_TYPE_RECT_LIST; return VZ_OK;
    }
    if (p == "max_confidence") { auto* x = (double*)std::malloc(sizeof(double)); *x = out_max_conf_; *o = x; *n = 1; *tt = VZ_TYPE_DOUBLE; return VZ_OK; }
    if (p == "labels") {
        size_t sz = out_labels_str_.size();
        char* buf = (char*)std::malloc(sz + 1);
        std::memcpy(buf, out_labels_str_.c_str(), sz + 1);
        *o = buf; *n = (int)sz; *tt = VZ_TYPE_STRING; return VZ_OK;
    }
    setLastError("unknown output: " + p); return VZ_ERR_INVALID_ARG;
}
void DnnDetectAlgo::releaseOutput(void* p) { if (p) std::free(p); }
const char* DnnDetectAlgo::describe() const {
    return R"JSON({
  "type_id":"DnnDetect","display_name":"深度学习-检测","category":"深度学习",
  "algo_binding":{"dll":"vision_algo","algo_name":"DnnDetect"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"绘制检测框的结果图"},
    {"name":"count","type":"Int","required":true,"description":"检测目标数"},
    {"name":"boxes","type":"RectList","required":true,"description":"检测框列表"},
    {"name":"max_confidence","type":"Double","required":true,"description":"最高置信度"},
    {"name":"labels","type":"String","required":true,"description":"所有标签(逗号分隔)"}
  ],
  "parameters":[
    {"name":"model","type":"String","default":"","description":"模型文件路径(.onnx/.pb/.weights)"},
    {"name":"config","type":"String","default":"","description":"配置文件(.cfg/.pbtxt,可选)"},
    {"name":"framework","type":"Int","default":0,"description":"框架(0=自动)","enum":[[0,"自动"],[1,"Caffe"],[2,"TensorFlow"],[3,"Darknet"],[4,"ONNX"]]},
    {"name":"input_w","type":"Int","default":416,"description":"网络输入宽"},
    {"name":"input_h","type":"Int","default":416,"description":"网络输入高"},
    {"name":"scale","type":"Double","default":0.00392156,"description":"像素缩放(YOLO用1/255)"},
    {"name":"mean_r","type":"Double","default":0,"description":"均值R"},
    {"name":"mean_g","type":"Double","default":0,"description":"均值G"},
    {"name":"mean_b","type":"Double","default":0,"description":"均值B"},
    {"name":"swap_rb","type":"Int","default":1,"description":"RGB<->BGR","enum":[[0,"否"],[1,"是"]]},
    {"name":"crop","type":"Int","default":0,"description":"中心裁剪","enum":[[0,"否"],[1,"是"]]},
    {"name":"conf_threshold","type":"Double","default":0.5,"description":"置信度阈值"},
    {"name":"nms_threshold","type":"Double","default":0.4,"description":"NMS IoU阈值"},
    {"name":"labels","type":"String","default":"","description":"标签文件路径(每行一个类名,可选)"},
    {"name":"draw_color","type":"String","default":"#00FF00","description":"框颜色(#RRGGBB 或 B,G,R)"},
    {"name":"draw_thickness","type":"Int","default":2,"min":1,"max":20,"description":"线宽(1-20)"}
  ]
})JSON";
}
std::unique_ptr<AlgoBase> DnnDetectAlgo::create() { return std::make_unique<DnnDetectAlgo>(); }
} // namespace vz
