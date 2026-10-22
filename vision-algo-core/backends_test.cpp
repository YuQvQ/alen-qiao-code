// backends_test.cpp — 极简版，不 catch 不 throw，测试各 backend 对 YOLO end-to-end 的支持
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <cmath>
int main() {
    printf("OpenCV: %s\n", cv::getVersionString().c_str());

    cv::Mat img = cv::imread(R"(c:\Users\qiaoy\Desktop\frame0.jpg)", cv::IMREAD_COLOR);
    float r = 640.f / std::max(img.cols, img.rows);
    int nw = std::max(1, (int)(img.cols * r)), nh = std::max(1, (int)(img.rows * r));
    cv::Mat resized; cv::resize(img, resized, cv::Size(nw, nh));
    int dh = 640 - nh, dw = 640 - nw;
    int top = dh/2, bottom = dh - top, left = dw/2, right = dw - left;
    cv::Mat lb; cv::copyMakeBorder(resized, lb, top, bottom, left, right, cv::BORDER_CONSTANT, cv::Scalar(114,114,114));
    cv::Mat blob = cv::dnn::blobFromImage(lb, 1.0/255.0, cv::Size(640,640), cv::Scalar(0,0,0), true, false);

    // 先 OPENCV+CPU 基准
    auto net0 = cv::dnn::readNet(R"(c:\Users\qiaoy\Desktop\yolo26n.onnx)");
    net0.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net0.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    net0.setInput(blob, "images");
    std::vector<cv::Mat> outs0;
    net0.forward(outs0, net0.getUnconnectedOutLayersNames());
    cv::Mat o0 = outs0[0].reshape(1, 300);
    printf("\n[OPENCV+CPU] forward outputs=%zu shape=[%d,%d]\n", outs0.size(), o0.cols, o0.rows);
    float c0 = o0.ptr<float>(0)[4], c1 = o0.ptr<float>(1)[4], c2 = o0.ptr<float>(2)[4];
    printf("  conf[0..2]= %.6f %.6f %.6f\n", c0, c1, c2);
    // 计算 conf 方差
    double mean = 0;
    for (int i = 0; i < 300; ++i) mean += o0.ptr<float>(i)[4];
    mean /= 300;
    double var = 0;
    for (int i = 0; i < 300; ++i) { double d = o0.ptr<float>(i)[4] - mean; var += d*d; }
    var /= 300;
    printf("  conf variance=%.10f  (0 = ALL SAME = BUG)\n", var);

    // ONNXRUNTIME backend
    auto net1 = cv::dnn::readNet(R"(c:\Users\qiaoy\Desktop\yolo26n.onnx)");
    try {
        net1.setPreferableBackend(cv::dnn::DNN_BACKEND_ONNXRUNTIME);
        net1.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        net1.setInput(blob, "images");
        std::vector<cv::Mat> outs1;
        net1.forward(outs1, net1.getUnconnectedOutLayersNames());
        cv::Mat o1 = outs1[0].reshape(1, 300);
        printf("\n[ONNXRUNTIME+CPU] shape=[%d,%d]\n", o1.cols, o1.rows);
        printf("  conf[0..2]= %.6f %.6f %.6f\n", o1.ptr<float>(0)[4], o1.ptr<float>(1)[4], o1.ptr<float>(2)[4]);
    } catch (...) {
        printf("\n[ONNXRUNTIME+CPU] NOT AVAILABLE\n");
    }
    return 0;
}
