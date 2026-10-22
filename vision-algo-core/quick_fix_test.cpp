// quick_fix_test.cpp — 试试 OpenCV 4.13 的各种 DNN 开关能不能修 YOLO end-to-end
#include <opencv2/opencv.hpp>
#include <cstdio>
void check(const char* tag, cv::dnn::Net& net, cv::Mat& blob) {
    net.setInput(blob, "images");
    std::vector<cv::Mat> outs;
    net.forward(outs, net.getUnconnectedOutLayersNames());
    if (outs.empty()) { printf("[%s] empty\n", tag); return; }
    cv::Mat o = outs[0].reshape(1, 300);
    float c0 = o.ptr<float>(0)[4], c1 = o.ptr<float>(1)[4], c2 = o.ptr<float>(2)[4];
    printf("[%s] conf[0..2]= %.6f %.6f %.6f  allSame=%d\n", tag, c0, c1, c2, (c0==c1 && c1==c2));
}
int main() {
    printf("OpenCV: %s\n", cv::getVersionString().c_str());
    cv::Mat img = cv::imread(R"(c:\Users\qiaoy\Desktop\frame0.jpg)", cv::IMREAD_COLOR);
    float r = 640.f / std::max(img.cols, img.rows);
    int nw = std::max(1,(int)(img.cols*r)), nh = std::max(1,(int)(img.rows*r));
    cv::Mat resized; cv::resize(img, resized, cv::Size(nw,nh));
    int dh = 640-nh, dw = 640-nw; int top=dh/2,left=dw/2;
    cv::Mat lb; cv::copyMakeBorder(resized, lb, top, dh-top, left, dw-left, cv::BORDER_CONSTANT, cv::Scalar(114,114,114));
    cv::Mat blob = cv::dnn::blobFromImage(lb, 1.0/255.0, cv::Size(640,640), cv::Scalar(0,0,0), true, false);

    // 原始
    auto n0 = cv::dnn::readNet(R"(c:\Users\qiaoy\Desktop\yolo26n.onnx)");
    n0.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    n0.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    check("baseline", n0, blob);

    // 试各种开关
    // 1. disable Winograd
    auto n1 = cv::dnn::readNet(R"(c:\Users\qiaoy\Desktop\yolo26n.onnx)");
    n1.enableWinograd(false);
    n1.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    n1.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    check("noWinograd", n1, blob);

    // 2. warmup off
    auto n2 = cv::dnn::readNet(R"(c:\Users\qiaoy\Desktop\yolo26n.onnx)");
    n2.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    n2.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    n2.disableWarmUp();
    check("noWarmup", n2, blob);

    // 3. 单线程
    auto n3 = cv::dnn::readNet(R"(c:\Users\qiaoy\Desktop\yolo26n.onnx)");
    n3.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    n3.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    n3.setNumThreads(1);
    check("1thread", n3, blob);

    // 4. blob 不用 swapRB
    cv::Mat blob2 = cv::dnn::blobFromImage(lb, 1.0/255.0, cv::Size(640,640), cv::Scalar(0,0,0), false, false);
    auto n4 = cv::dnn::readNet(R"(c:\Users\qiaoy\Desktop\yolo26n.onnx)");
    n4.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    n4.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    check("noSwapRB", n4, blob2);

    // 5. 试 cv::dnn::DNN_TARGET_OPENCL if available
    auto tgts = cv::dnn::getAvailableTargets(cv::dnn::DNN_BACKEND_OPENCV);
    printf("Available OPENCV targets: %s\n", "some");
    for (int t : tgts) {
        try {
            auto n = cv::dnn::readNet(R"(c:\Users\qiaoy\Desktop\yolo26n.onnx)");
            n.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            n.setPreferableTarget(t);
            char tag[64]; snprintf(tag, sizeof(tag), "target_%d", t);
            check(tag, n, blob);
        } catch (cv::Exception& e) { printf("target %d FAIL: %s\n", t, e.what()); }
    }
    return 0;
}
