// dnn_detect.cpp — 深度学习目标检测
// 支持: YOLO(end-to-end导出/原始), SSD, Caffe TF Darknet, ONNX
#include "dnn_detect.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <vector>

#ifdef USE_PYTHON_ORT
namespace vz {
// 在 dnn_detect_ort.cpp 里实现
int ortForwardOnnx(const char* model_path,
                   const float* blob, int n, int c, int h, int w,
                   std::vector<float>& out_raw);
}
#endif
namespace vz {
using detail::parseJsonInt; using detail::parseJsonDouble; using detail::parseJsonString;
using detail::parseColor; using detail::parseThickness; using detail::parsePointSize;
using detail::toCvMat; using detail::toVzImage;

static bool isOnnxModel(const std::string& p) {
    auto n = p.size(); if (n < 6) return false;
    return p.compare(n - 5, 5, ".onnx") == 0 || p.compare(n - 5, 5, ".ONNX") == 0;
}

// 探测 ONNX 模型的输入宽高（通过构造假 blob 试探）
static bool probeOnnxInputSize(cv::dnn::Net& net, int& w, int& h) {
    static const int kSizes[] = { 640, 416, 320, 512, 800, 1280 };
    for (int sz : kSizes) {
        try {
            cv::Mat fake = cv::Mat::zeros(sz, sz, CV_8UC3);
            cv::Mat blob = cv::dnn::blobFromImage(fake, 1.0, cv::Size(sz, sz));
            try { net.setInput(blob, "images"); }
            catch (...) { try { net.setInput(blob, "input"); } catch (...) { net.setInput(blob); } }
            std::vector<cv::Mat> dummy;
            net.forward(dummy, net.getUnconnectedOutLayersNames());
            w = h = sz;
            return true;
        } catch (...) { }
    }
    return false;
}

// YOLO 标准 letterbox：保持长宽比缩放 + 灰边填充
static cv::Mat letterbox(const cv::Mat& img, int new_shape, float& out_ratio,
                         int& out_dw, int& out_dh, const cv::Scalar& pad_color = cv::Scalar(114,114,114)) {
    int h = img.rows, w = img.cols;
    float r = (float)new_shape / std::max(h, w);
    int nh = std::max(1, (int)(h * r));
    int nw = std::max(1, (int)(w * r));
    cv::Mat resized;
    cv::resize(img, resized, cv::Size(nw, nh));
    int dh = new_shape - nh;
    int dw = new_shape - nw;
    int top = dh / 2, bottom = dh - top;
    int left = dw / 2, right = dw - left;
    cv::Mat padded;
    cv::copyMakeBorder(resized, padded, top, bottom, left, right, cv::BORDER_CONSTANT, pad_color);
    out_ratio = r;
    out_dw = left;   // 记录左边 padding（右边的 padding 也是同样值）
    out_dh = top;    // 记录上边 padding（下边的 padding 也是同样值）
    return padded;
}

int DnnDetectAlgo::setParam(const std::string& k, const std::string& v) {
    if (k == "model") { model_ = parseJsonString(v); net_ready_ = false; return VZ_OK; }
    if (k == "config") { config_ = parseJsonString(v); net_ready_ = false; return VZ_OK; }
    if (k == "framework") { framework_ = parseJsonInt(v, 0); return VZ_OK; }
    if (k == "input_w") { auto_input_size_ = false; input_w_ = parseJsonInt(v, 640); if (input_w_ < 1) input_w_ = 640; return VZ_OK; }
    if (k == "input_h") { auto_input_size_ = false; input_h_ = parseJsonInt(v, 640); if (input_h_ < 1) input_h_ = 640; return VZ_OK; }
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

        // ONNX 模型自动探测输入尺寸（用户没改默认值时）
        if (isOnnxModel(model_) && auto_input_size_) {
            int pw = input_w_, ph = input_h_;
            if (probeOnnxInputSize(net_, pw, ph)) {
                input_w_ = pw; input_h_ = ph;
            }
        }

        loaded_model_ = model_; net_ready_ = true;
    }
    label_names_ = loadLabelsDet(labels_);

    // ===== 通道数对齐 =====
    cv::Mat net_input = input_;
    if (net_input.channels() == 1) {
        cv::cvtColor(net_input, net_input, cv::COLOR_GRAY2BGR);
    } else if (net_input.channels() == 4) {
        cv::cvtColor(net_input, net_input, cv::COLOR_BGRA2BGR);
    }

    // ===== YOLO 标准 letterbox（保持长宽比） =====
    float lb_ratio = 1.0f;
    int   lb_pad_dw = 0, lb_pad_dh = 0;
    cv::Mat lb_mat = letterbox(net_input, std::max(input_w_, input_h_),
                               lb_ratio, lb_pad_dw, lb_pad_dh);

    std::vector<cv::Mat> outs;
    bool forwarded = false;

    cv::Mat blob = cv::dnn::blobFromImage(lb_mat, scale_, cv::Size(input_w_, input_h_),
        cv::Scalar(mean_b_, mean_g_, mean_r_), swap_rb_ != 0, false /*crop_*/);

#ifdef USE_PYTHON_ORT
    // ONNX 模型走 Python embed + onnxruntime（绕过 OpenCV 4.13 forward bug）
    if (isOnnxModel(model_)) {
        std::vector<float> ort_raw;
        int rows = ortForwardOnnx(model_.c_str(),
                                  (float*)blob.data, 1, 3, input_h_, input_w_,
                                  ort_raw);
        if (rows > 0 && ort_raw.size() == rows * 6) {
            cv::Mat out_mat(rows, 6, CV_32FC1);
            std::memcpy(out_mat.data, ort_raw.data(), ort_raw.size() * sizeof(float));
            outs.push_back(out_mat);
            forwarded = true;
        } else {
            setLastError("ORT forward failed, falling back to OpenCV");
        }
    }
#endif

    if (!forwarded) {
        try {
            try { net_.setInput(blob, "images"); }
            catch (...) { try { net_.setInput(blob, "input"); } catch (...) { net_.setInput(blob); } }
            std::vector<cv::String> outNames = net_.getUnconnectedOutLayersNames();
            net_.forward(outs, outNames);
            forwarded = true;
        } catch (...) {
            setLastError("forward failed on " + std::to_string(input_w_) + "x" + std::to_string(input_h_));
        }
    }
    // ONNX 模型尺寸自动恢复：只要 forward 失败就探测正确尺寸
    if (!forwarded && isOnnxModel(model_)) {
        static const int kCandidates[] = { 640, 416, 1280, 320, 512, 800 };
        for (int sz : kCandidates) {
            if (sz == input_w_) continue;
            try {
                float r2; int dw2, dh2;
                cv::Mat lb2 = letterbox(net_input, sz, r2, dw2, dh2);
                cv::Mat b = cv::dnn::blobFromImage(lb2, scale_, cv::Size(sz, sz),
                    cv::Scalar(mean_b_, mean_g_, mean_r_), swap_rb_ != 0, false);
                try { net_.setInput(b, "images"); }
                catch (...) { try { net_.setInput(b, "input"); } catch (...) { net_.setInput(b); } }
                std::vector<cv::String> outNames = net_.getUnconnectedOutLayersNames();
                net_.forward(outs, outNames);
                input_w_ = input_h_ = sz;
                lb_ratio = r2; lb_pad_dw = dw2; lb_pad_dh = dh2;
                forwarded = true;
                break;
            } catch (...) { }
        }
    }
    if (!forwarded) {
        return VZ_ERR_PROCESS;
    }

    // ===== 多格式解析 =====
    std::vector<int> classIds; std::vector<float> confidences; std::vector<cv::Rect> boxes;
    const int W = input_.cols, H = input_.rows;

    // letterbox 坐标还原：模型输出坐标 (mx,my) 在 input_w_xinput_h_ 空间
    // 先减 padding，再除以 ratio → 原图坐标
    auto restore_xyxy = [&](float mx1, float my1, float mx2, float my2,
                            int& x, int& y, int& w, int& h) {
        float rx1 = (mx1 - lb_pad_dw) / lb_ratio;
        float ry1 = (my1 - lb_pad_dh) / lb_ratio;
        float rx2 = (mx2 - lb_pad_dw) / lb_ratio;
        float ry2 = (my2 - lb_pad_dh) / lb_ratio;
        x = (int)rx1; y = (int)ry1;
        w = (int)(rx2 - rx1); h = (int)(ry2 - ry1);
    };

    for (auto& out : outs) {
        if (out.empty()) continue;
        // [A] end-to-end YOLO: shape [1, N, 6] 或 [N, 6]，值为 x1,y1,x2,y2,conf,class_id
        int lastDim = out.size[out.dims - 1];
        if ((out.dims == 3 && lastDim == 6) || (out.dims == 2 && out.cols == 6)) {
            int total = out.total() / 6;
            float* d = (float*)out.ptr();
            for (int i = 0; i < total; ++i) {
                float x1 = d[i*6 + 0], y1 = d[i*6 + 1];
                float x2 = d[i*6 + 2], y2 = d[i*6 + 3];
                float conf = d[i*6 + 4];
                int   cid  = (int)d[i*6 + 5];
                if (conf < conf_threshold_) continue;
                int x, y, w, h;
                restore_xyxy(x1, y1, x2, y2, x, y, w, h);
                if (w <= 0 || h <= 0) continue;
                classIds.push_back(cid); confidences.push_back(conf);
                boxes.push_back(cv::Rect(x, y, w, h));
            }
            continue;
        }
        // [B] YOLO 原始: rows 行检测, cols = 5+numClasses, 值为 cx,cy,w,h(归一化),objScore,classScores
        if (out.dims == 2 && out.cols >= 5) {
            cv::Mat dt = out.reshape(1, (int)out.total() / out.cols);
            for (int i = 0; i < dt.rows; ++i) {
                const float* row = dt.ptr<float>(i);
                float objConf = row[4];
                int nc = dt.cols - 5;
                int bestC = 0; float bestS = 0;
                for (int c = 0; c < nc; ++c) { float s = row[5 + c] * objConf; if (s > bestS) { bestS = s; bestC = c; } }
                if (bestS < conf_threshold_) continue;
                float cx = row[0] * W, cy = row[1] * H, w = row[2] * W, h = row[3] * H;
                int x = (int)(cx - w / 2), y = (int)(cy - h / 2);
                classIds.push_back(bestC); confidences.push_back(bestS);
                boxes.push_back(cv::Rect(x, y, (int)w, (int)h));
            }
            continue;
        }
        // [C] SSD: [1,1,N,7] -> [batchId, classId, conf, x, y, w, h] (归一化坐标 0-1)
        if (out.dims == 4 && out.size[3] == 7) {
            int n = out.size[2];
            for (int i = 0; i < n; ++i) {
                float* d = (float*)out.ptr(0, 0, i);
                float conf = d[2]; if (conf < conf_threshold_) continue;
                int cid = (int)d[1];
                float nx = d[3], ny = d[4], nw = d[5], nh = d[6];
                if (nx > 1.0f || ny > 1.0f || nw > 1.0f || nh > 1.0f) {
                    int x, y, w, h;
                    restore_xyxy(nx, ny, nw, nh, x, y, w, h);
                    classIds.push_back(cid); confidences.push_back(conf);
                    boxes.push_back(cv::Rect(x, y, w, h));
                } else {
                    int x = (int)(nx * W), y = (int)(ny * H);
                    int w = (int)((nw - nx) * W), h = (int)((nh - ny) * H);
                    classIds.push_back(cid); confidences.push_back(conf);
                    boxes.push_back(cv::Rect(x, y, w, h));
                }
            }
            continue;
        }
        // [D] 默认 fallback: 尝试 reshape 成 2D 再按 YOLO 原始解析
        cv::Mat flat = out.reshape(1, out.dims > 1 ? out.size[out.dims - 2] : 1);
        if (flat.cols >= 5) {
            for (int i = 0; i < flat.rows; ++i) {
                const float* row = flat.ptr<float>(i);
                float objConf = row[4];
                int nc = flat.cols - 5;
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

    // NMS（end-to-end 模型已内置 NMS，但保险起见也对其余格式跑一次）
    std::vector<int> idxs;
    if (!boxes.empty()) cv::dnn::NMSBoxes(boxes, confidences, (float)conf_threshold_, (float)nms_threshold_, idxs);
    for (int i : idxs) {
        Det d; d.classId = classIds[i]; d.conf = confidences[i]; d.box = boxes[i] & cv::Rect(0, 0, W, H);
        if (d.box.width <= 0 || d.box.height <= 0) continue;
        dets_.push_back(d);
        if (d.conf > out_max_conf_) out_max_conf_ = d.conf;
    }
    result_image_ = input_.clone();
    // 构建输出数据
    out_labels_str_.clear(); out_confs_str_.clear(); out_detections_json_.clear();
    out_detections_json_ += "[";
    for (size_t i = 0; i < dets_.size(); ++i) {
        auto& d = dets_[i];
        std::string name = (d.classId >= 0 && d.classId < (int)label_names_.size()) ? label_names_[d.classId] : ("cls" + std::to_string(d.classId));
        // 标签字符串 (分号分隔)
        if (!out_labels_str_.empty()) out_labels_str_ += ";";
        out_labels_str_ += name;
        // 置信度字符串
        if (!out_confs_str_.empty()) out_confs_str_ += ";";
        char cf[64]; std::snprintf(cf, sizeof(cf), "%.6f", d.conf);
        out_confs_str_ += cf;
        // JSON 条目
        if (i > 0) out_detections_json_ += ",";
        char j[512]; std::snprintf(j, sizeof(j),
            "{\"index\":%zu,\"class_id\":%d,\"label\":\"%s\",\"confidence\":%.6f,\"cx\":%.2f,\"cy\":%.2f,\"w\":%.2f,\"h\":%.2f}",
            i, d.classId, name.c_str(), d.conf,
            d.box.x + d.box.width/2.0, d.box.y + d.box.height/2.0,
            (double)d.box.width, (double)d.box.height);
        out_detections_json_ += j;
        // ===== 绘制检测框 =====
        cv::rectangle(result_image_, d.box, draw_color_, draw_thickness_);
        // 中心十字
        {
            int cx = d.box.x + d.box.width / 2;
            int cy = d.box.y + d.box.height / 2;
            cv::line(result_image_, cv::Point(cx - 6, cy), cv::Point(cx + 6, cy), draw_color_, 1);
            cv::line(result_image_, cv::Point(cx, cy - 6), cv::Point(cx, cy + 6), draw_color_, 1);
        }
        // 多行文本
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "#%zu idx:%d label:%s conf:%.2f\ncx,cy=(%.1f,%.1f)\nw,h=(%.1f,%.1f)",
            i, d.classId, name.c_str(), d.conf,
            d.box.x + d.box.width/2.0, d.box.y + d.box.height/2.0,
            (double)d.box.width, (double)d.box.height);
        int baseLine = 0;
        double font_scale = 0.45;
        int thickness = 1;
        std::vector<std::string> lines;
        std::stringstream ss(buf);
        std::string line;
        int line_h = 0;
        while (std::getline(ss, line)) {
            lines.push_back(line);
            cv::Size sz = cv::getTextSize(line, cv::FONT_HERSHEY_SIMPLEX, font_scale, thickness, &baseLine);
            if (sz.height > line_h) line_h = sz.height;
        }
        // 文字放在框上方；放不下则放框内顶部
        int ty = d.box.y - 6;
        if (ty - (int)lines.size() * (line_h + 4) < 0) ty = d.box.y + line_h + 4;
        // 无背景：黑描边 + 彩色文字
        for (size_t li = 0; li < lines.size(); ++li) {
            int lty = ty + (int)li * (line_h + 4);
            cv::putText(result_image_, lines[li], cv::Point(d.box.x, lty),
                cv::FONT_HERSHEY_SIMPLEX, font_scale, cv::Scalar(0, 0, 0), thickness + 2, cv::LINE_AA);
            cv::putText(result_image_, lines[li], cv::Point(d.box.x, lty),
                cv::FONT_HERSHEY_SIMPLEX, font_scale, draw_color_, thickness, cv::LINE_AA);
        }
    }
    out_detections_json_ += "]";
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
    if (p == "confidences") {
        size_t sz = out_confs_str_.size();
        char* buf = (char*)std::malloc(sz + 1);
        std::memcpy(buf, out_confs_str_.c_str(), sz + 1);
        *o = buf; *n = (int)sz; *tt = VZ_TYPE_STRING; return VZ_OK;
    }
    if (p == "detections_json") {
        size_t sz = out_detections_json_.size();
        char* buf = (char*)std::malloc(sz + 1);
        std::memcpy(buf, out_detections_json_.c_str(), sz + 1);
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
    {"name":"boxes","type":"RectList","required":true,"description":"检测框列表(中心XY+宽高)"},
    {"name":"max_confidence","type":"Double","required":true,"description":"最高置信度"},
    {"name":"labels","type":"String","required":true,"description":"所有标签(分号分隔)"},
    {"name":"confidences","type":"String","required":false,"description":"每框置信度(分号分隔)"},
    {"name":"detections_json","type":"String","required":false,"description":"完整检测结果JSON数组"}
  ],
  "parameters":[
    {"name":"model","type":"String","default":"","description":"模型文件路径(.onnx/.pb/.weights)"},
    {"name":"config","type":"String","default":"","description":"配置文件(.cfg/.pbtxt,可选)"},
    {"name":"framework","type":"Int","default":0,"description":"框架(0=自动)","enum":[[0,"自动"],[1,"Caffe"],[2,"TensorFlow"],[3,"Darknet"],[4,"ONNX"]]},
    {"name":"input_w","type":"Int","default":640,"description":"网络输入宽(ONNX自动探测)"},
    {"name":"input_h","type":"Int","default":640,"description":"网络输入高(ONNX自动探测)"},
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
