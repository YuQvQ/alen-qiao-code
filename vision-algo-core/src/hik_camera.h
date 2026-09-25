// hik_camera.h — 海康 MVS SDK 封装（枚举 + 单帧采集）
// 依赖：MvCameraControl.h / MvCameraControl.lib（来自海康 MVS SDK）
#pragma once
#include <string>
#include <vector>
#include <opencv2/core.hpp>

namespace vz {
namespace hik {

// 单个相机的枚举信息
struct CameraInfo {
    std::string sn;          // 序列号（唯一标识，用于连接）
    std::string model;       // 型号
    std::string vendor;      // 厂商
    std::string type;        // "GigE" / "USB" / "CameraLink" 等
    std::string user_name;   // 用户自定义名
};

// 枚举当前在线的海康相机。失败返回 false 并设置 err。
bool enumerateCameras(std::vector<CameraInfo>& out, std::string& err);

// 序列化为 JSON 数组字符串
std::string toJson(const std::vector<CameraInfo>& cams);

// 打开指定序列号相机，取一帧，输出 BGR cv::Mat。
// exposure_us/gain_db: 设置参数（<=0 表示不设置，使用相机当前值）
// err: 失败原因
bool grabOneFrame(const std::string& sn, double exposure_us, double gain_db,
                  cv::Mat& outBgr, std::string& err);

} // namespace hik
} // namespace vz
