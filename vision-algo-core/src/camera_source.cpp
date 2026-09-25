// camera_source.cpp — 海康相机源算法实现（stub 版本）
//
// MVP 说明：
//   - 真实 MVS SDK 未链接，stub 模式生成测试图；real 模式返回错误。
//   - 后续接入真实 SDK 时只需：
//       1) CMakeLists 链接 MvCameraControl.lib
//       2) 此处 #include "MvCameraControl.h"
//       3) process() 中实现 MV_CC_EnumDevices + CreateHandle + StartGrabbing + GetOneFrameTimeout
//
#include "camera_source.h"
#include "algo_utils.h"
#include "hik_camera.h"
#include <opencv2/imgproc.hpp>
#include <cstdio>

namespace vz {

// ============================================================
// 参数
// ============================================================
int CameraSourceAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "device_sn") {
        device_sn_ = detail::parseJsonString(value_json);
        return VZ_OK;
    }
    if (key == "exposure") {
        exposure_ = detail::parseJsonDouble(value_json, 10000.0);
        return VZ_OK;
    }
    if (key == "gain") {
        gain_ = detail::parseJsonDouble(value_json, 0.0);
        return VZ_OK;
    }
    if (key == "mode") {
        mode_ = detail::parseJsonString(value_json);
        return VZ_OK;
    }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

// ============================================================
// 输入
// ============================================================
int CameraSourceAlgo::setInput(const std::string& port_name,
                                 const void* /*data*/, int /*type_tag*/) {
    setLastError("camera source has no input ports, got: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

// ============================================================
// 执行
// ============================================================
int CameraSourceAlgo::process() {
    image_ = cv::Mat();

    if (mode_ == "real") {
        cv::Mat frame;
        std::string err;
        if (device_sn_.empty()) {
            setLastError("real 模式需要先在参数中选择设备序列号（点击参数对话框的「枚举相机」）");
            return VZ_ERR_INVALID_ARG;
        }
        if (!hik::grabOneFrame(device_sn_, exposure_, gain_, frame, err)) {
            setLastError("相机采集失败: " + err);
            return VZ_ERR_PROCESS;
        }
        image_ = frame;
        setLastError("");
        return VZ_OK;
    }

    // stub 模式：生成 640x480 BGR 渐变测试图（便于流程跑通）
    const int W = 640, H = 480;
    cv::Mat m(H, W, CV_8UC3);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            // 简单渐变 + 对角条纹
            uint8_t b = (uint8_t)((x * 255) / W);
            uint8_t g = (uint8_t)((y * 255) / H);
            uint8_t r = (uint8_t)(((x + y) * 255) / (W + H));
            m.at<cv::Vec3b>(y, x) = cv::Vec3b(b, g, r);
        }
    }

    // 叠加文字"STUB"，便于用户一眼看出是测试图
    cv::putText(m, "CAMERA STUB", cv::Point(20, 40),
                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2);
    if (!device_sn_.empty()) {
        cv::putText(m, "sn: " + device_sn_, cv::Point(20, 70),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
    }

    image_ = m;
    // stub 模式留 warning，但不返回错误码（节点不应变红）
    setLastError("");
    return VZ_OK;
}

// ============================================================
// 输出
// ============================================================
int CameraSourceAlgo::getOutput(const std::string& port_name,
                                  void** out_data, int* out_count, int* out_type_tag) {
    if (port_name != "image") {
        setLastError("unknown output port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    VzImage* img = detail::toVzImage(image_);
    if (!img) {
        *out_data = nullptr; *out_count = 0; *out_type_tag = VZ_TYPE_IMAGE;
        return VZ_OK;
    }
    *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE;
    return VZ_OK;
}

void CameraSourceAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

// ============================================================
// 描述
// ============================================================
const char* CameraSourceAlgo::describe() const {
    static const char* kDesc = R"({
  "type_id":"CameraSource",
  "display_name":"海康相机",
  "category":"源",
  "icon":"icons/camera_source.svg",
  "algo_binding":{"dll":"vision_algo","algo_name":"CameraSource"},
  "inputs":[],
  "outputs":[
    {"name":"image","type":"Image","required":true,"description":"采集到的图像"}
  ],
  "parameters":[
    {"name":"device_sn","type":"String","default":"","description":"设备序列号"},
    {"name":"exposure","type":"Double","default":10000.0,"min":0,"description":"曝光（微秒）"},
    {"name":"gain","type":"Double","default":0.0,"min":0,"description":"增益（dB）"},
    {"name":"mode","type":"String","default":"stub","description":"stub=测试图 / real=真实采集（需 MVS SDK）"}
  ]
})";
    return kDesc;
}

std::unique_ptr<AlgoBase> CameraSourceAlgo::create() {
    return std::make_unique<CameraSourceAlgo>();
}

} // namespace vz
