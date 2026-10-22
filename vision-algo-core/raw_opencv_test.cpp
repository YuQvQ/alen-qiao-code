// raw_opencv_test.cpp — 直接用 OpenCV DNN（不走 DLL）
// 确认是不是 OpenCV 4.13.0 本身 forward 就有问题
#include <opencv2/opencv.hpp>
#include <cstdio>
int main() {
    printf("OpenCV version: %s\n", cv::getVersionString().c_str());
    cv::Mat img = cv::imread(R"(c:\Users\qiaoy\Desktop\frame0.jpg)", cv::IMREAD_COLOR);
    printf("input: %dx%d ch=%d\n", img.cols, img.rows, img.channels());
    // letterbox
    float r = 640.f / std::max(img.cols, img.rows);
    int nw = std::max(1, (int)(img.cols * r)), nh = std::max(1, (int)(img.rows * r));
    cv::Mat resized; cv::resize(img, resized, cv::Size(nw, nh));
    int dh = 640 - nh, dw = 640 - nw;
    int top = dh/2, bottom = dh - top, left = dw/2, right = dw - left;
    cv::Mat lb; cv::copyMakeBorder(resized, lb, top, bottom, left, right, cv::BORDER_CONSTANT, cv::Scalar(114,114,114));
    cv::Mat blob = cv::dnn::blobFromImage(lb, 1.0/255.0, cv::Size(640,640), cv::Scalar(0,0,0), true, false);
    auto net = cv::dnn::readNet(R"(c:\Users\qiaoy\Desktop\yolo26n.onnx)");
    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    net.setInput(blob, "images");
    auto names = net.getUnconnectedOutLayersNames();
    std::vector<cv::Mat> outs;
    net.forward(outs, names);
    printf("forward: %zu outputs\n", outs.size());
    for (size_t i = 0; i < outs.size(); ++i) {
        cv::Mat o = outs[i];
        int d0 = o.dims >= 1 ? o.size[0] : 0;
        int d1 = o.dims >= 2 ? o.size[1] : 0;
        int d2 = o.dims >= 3 ? o.size[2] : 0;
        printf("  out[%zu] name='%s' dims=%d shape=[%d,%d,%d]\n",
               i, names[i].c_str(), o.dims, d0, d1, d2);
        cv::Mat flat = o.reshape(1, o.dims >= 2 ? o.size[o.dims-2] : 1);
        printf("    dims=%d cols=%d rows=%d\n", o.dims, flat.cols, flat.rows);
        for (int j = 0; j < std::min(15, flat.rows); ++j) {
            const float* row = flat.ptr<float>(j);
            printf("    #%d x1=%.3f y1=%.3f x2=%.3f y2=%.3f conf=%.6f cls=%.1f\n",
                   j, row[0], row[1], row[2], row[3], row[4], row[5]);
        }
    }
    return 0;
}
