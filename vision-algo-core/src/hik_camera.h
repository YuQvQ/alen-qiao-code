// hik_camera.h — 海康 MVS SDK 封装（枚举 + 单帧采集 + 参数配置 + IP 管理）
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
    // GigE 专属网络信息（点分十进制；非 GigE 为空）
    std::string ip;
    std::string subnet;
    std::string gateway;
};

// 枚举当前在线的海康相机。失败返回 false 并设置 err。
bool enumerateCameras(std::vector<CameraInfo>& out, std::string& err);

// 序列化为 JSON 数组字符串
std::string toJson(const std::vector<CameraInfo>& cams);

// 采一帧参数
struct GrabParams {
    double      exposure_us  = 0.0;   // <=0 表示用相机当前值
    double      gain_db      = 0.0;   // <=0 表示用相机当前值
    std::string trigger_mode = "off"; // off=连续 / software=软触发 / line1|line2|line3=硬触发
    double      frame_rate   = 0.0;   // <=0 表示不改帧率
};

// 打开指定序列号相机，按 params 配置后取一帧，输出 BGR cv::Mat。
bool grabOneFrame(const std::string& sn, const GrabParams& params,
                  cv::Mat& outBgr, std::string& err);

// 强制修改 GigE 相机 IP（无需打开设备，用于相机跨网段救砖）。
// 成功返回 true；失败设置 err。
bool forceIp(const std::string& sn,
             const std::string& ip, const std::string& netmask, const std::string& gateway,
             std::string& err);

} // namespace hik
} // namespace vz
