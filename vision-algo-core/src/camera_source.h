// camera_source.h — 相机源算法（海康 MVS SDK stub 版本）
//
// 端口契约：
//   输入：无
//   输出：image (Image)
//   参数：
//     device_sn (String)  设备序列号
//     exposure  (Double) 曝光（微秒）
//     gain      (Double) 增益（dB）
//     mode      (String) "stub"=返回测试图 / "real"=尝试连真实 SDK（默认 stub）
//
// 当前实现：MVP stub 版本。
//   - mode="stub"：生成 640x480 渐变测试图
//   - mode="real"：返回 VZ_ERR_UNSUPPORTED + "未安装海康 MVS SDK"
// 真正实现需用户安装海康 MVS SDK（C:\Program Files (x86)\MVS\Development\...），
// 链接 MvCameraControl.lib，包含 MvCameraControl.h，在 process() 中调用 MV_CC_* API。
// 后续真实版本可在此文件直接替换 process() 实现，端口契约不变。
//
#pragma once

#include "algo_base.h"
#include <opencv2/core.hpp>
#include <string>

namespace vz {

class CameraSourceAlgo : public AlgoBase {
public:
    CameraSourceAlgo() = default;
    ~CameraSourceAlgo() override = default;

    int  setParam(const std::string& key, const std::string& value_json) override;
    int  setInput(const std::string& port_name, const void* data, int type_tag) override;
    int  process() override;
    int  getOutput(const std::string& port_name,
                   void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;

    static std::unique_ptr<AlgoBase> create();

private:
    std::string device_sn_;
    double      exposure_ = 10000.0; // 微秒
    double      gain_     = 0.0;     // dB
    std::string mode_     = "stub";  // stub / real
    cv::Mat     image_;              // 采集到的图像
};

} // namespace vz
