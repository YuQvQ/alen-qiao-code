// hik_camera.cpp — 海康 MVS SDK 封装实现
#include "hik_camera.h"

#ifdef VZ_HAS_MVS_SDK
#include "MvCameraControl.h"
#endif

#include <opencv2/imgproc.hpp>
#include <cstring>
#include <sstream>

namespace vz {
namespace hik {

#ifndef VZ_HAS_MVS_SDK
// 无 SDK 时的空实现：保证可编译，运行时返回明确错误
bool enumerateCameras(std::vector<CameraInfo>& out, std::string& err) {
    out.clear(); err = "未启用海康 MVS SDK（编译时未找到 MvCameraControl）"; return false;
}
std::string toJson(const std::vector<CameraInfo>&) { return "[]"; }
bool grabOneFrame(const std::string&, const GrabParams&, cv::Mat&, std::string& err) {
    err = "未启用海康 MVS SDK"; return false;
}
bool forceIp(const std::string&, const std::string&, const std::string&, const std::string&,
             std::string& err) {
    err = "未启用海康 MVS SDK"; return false;
}
#else

static std::string fromBytes(const unsigned char* p, size_t maxLen) {
    if (!p) return "";
    size_t n = 0;
    while (n < maxLen && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

// 网络字节序 uint32 → 点分十进制
static std::string ipToString(unsigned int addr) {
    std::ostringstream oss;
    oss << ((addr >> 24) & 0xFF) << "." << ((addr >> 16) & 0xFF) << "."
        << ((addr >> 8) & 0xFF) << "." << (addr & 0xFF);
    return oss.str();
}

// 点分十进制 → inet_addr 语义（网络字节序）uint32；失败返回 false
static bool stringToIpInet(const std::string& s, unsigned int& out) {
    int a, b, c, d;
    char extra;
    if (std::sscanf(s.c_str(), "%d.%d.%d.%d%c", &a, &b, &c, &d, &extra) != 4) return false;
    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) return false;
    out = (unsigned int)a | ((unsigned int)b << 8) | ((unsigned int)c << 16) | ((unsigned int)d << 24);
    return true;
}

bool enumerateCameras(std::vector<CameraInfo>& out, std::string& err) {
    out.clear();
    MV_CC_DEVICE_INFO_LIST list;
    std::memset(&list, 0, sizeof(list));
    // 同时枚举 GigE + USB
    int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &list);
    if (ret != MV_OK) {
        std::ostringstream oss; oss << "MV_CC_EnumDevices failed: 0x" << std::hex << ret;
        err = oss.str();
        return false;
    }
    for (unsigned int i = 0; i < list.nDeviceNum && i < MV_MAX_DEVICE_NUM; ++i) {
        MV_CC_DEVICE_INFO* di = list.pDeviceInfo[i];
        if (!di) continue;
        CameraInfo ci;
        if (di->nTLayerType == MV_GIGE_DEVICE) {
            ci.type = "GigE";
            ci.sn = fromBytes(di->SpecialInfo.stGigEInfo.chSerialNumber,
                               sizeof(di->SpecialInfo.stGigEInfo.chSerialNumber));
            ci.model = fromBytes(di->SpecialInfo.stGigEInfo.chModelName,
                                 sizeof(di->SpecialInfo.stGigEInfo.chModelName));
            ci.vendor = fromBytes(di->SpecialInfo.stGigEInfo.chManufacturerName,
                                  sizeof(di->SpecialInfo.stGigEInfo.chManufacturerName));
            ci.user_name = fromBytes(di->SpecialInfo.stGigEInfo.chUserDefinedName,
                                     sizeof(di->SpecialInfo.stGigEInfo.chUserDefinedName));
            ci.ip      = ipToString(di->SpecialInfo.stGigEInfo.nCurrentIp);
            ci.subnet  = ipToString(di->SpecialInfo.stGigEInfo.nCurrentSubNetMask);
            ci.gateway = ipToString(di->SpecialInfo.stGigEInfo.nDefultGateWay);
        } else if (di->nTLayerType == MV_USB_DEVICE) {
            ci.type = "USB";
            ci.sn = fromBytes(di->SpecialInfo.stUsb3VInfo.chSerialNumber,
                               sizeof(di->SpecialInfo.stUsb3VInfo.chSerialNumber));
            ci.model = fromBytes(di->SpecialInfo.stUsb3VInfo.chModelName,
                                 sizeof(di->SpecialInfo.stUsb3VInfo.chModelName));
            ci.vendor = fromBytes(di->SpecialInfo.stUsb3VInfo.chManufacturerName,
                                  sizeof(di->SpecialInfo.stUsb3VInfo.chManufacturerName));
            ci.user_name = fromBytes(di->SpecialInfo.stUsb3VInfo.chUserDefinedName,
                                     sizeof(di->SpecialInfo.stUsb3VInfo.chUserDefinedName));
        } else {
            ci.type = "Other";
        }
        if (ci.sn.empty()) continue;
        out.push_back(ci);
    }
    return true;
}

std::string toJson(const std::vector<CameraInfo>& cams) {
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    for (const auto& c : cams) {
        if (!first) oss << ",";
        first = false;
        oss << "{\"sn\":\"" << c.sn << "\",\"model\":\"" << c.model
            << "\",\"vendor\":\"" << c.vendor << "\",\"type\":\"" << c.type
            << "\",\"user_name\":\"" << c.user_name << "\""
            << ",\"ip\":\"" << c.ip << "\",\"subnet\":\"" << c.subnet
            << "\",\"gateway\":\"" << c.gateway << "\"}";
    }
    oss << "]";
    return oss.str();
}

static bool pixelToBgr(const unsigned char* src, int w, int h, unsigned int pixType, cv::Mat& out) {
    cv::Mat raw(h, w, CV_8UC1, const_cast<unsigned char*>(src));
    switch (pixType) {
        case PixelType_Gvsp_Mono8:
            cv::cvtColor(raw, out, cv::COLOR_GRAY2BGR); return true;
        case PixelType_Gvsp_BayerRG8:
            cv::cvtColor(raw, out, cv::COLOR_BayerRG2BGR); return true;
        case PixelType_Gvsp_BayerBG8:
            cv::cvtColor(raw, out, cv::COLOR_BayerBG2BGR); return true;
        case PixelType_Gvsp_BayerGR8:
            cv::cvtColor(raw, out, cv::COLOR_BayerGR2BGR); return true;
        case PixelType_Gvsp_BayerGB8:
            cv::cvtColor(raw, out, cv::COLOR_BayerGB2BGR); return true;
        case PixelType_Gvsp_RGB8_Packed: {
            cv::Mat rgb(h, w, CV_8UC3, const_cast<unsigned char*>(src));
            cv::cvtColor(rgb, out, cv::COLOR_RGB2BGR); return true;
        }
        default:
            // 未知格式：按 Mono8 兜底
            cv::cvtColor(raw, out, cv::COLOR_GRAY2BGR); return true;
    }
}

// 配置触发模式。返回错误信息（空串=成功）
static std::string applyTrigger(void* handle, const std::string& trigger_mode, unsigned int& timeout_ms) {
    timeout_ms = 1000;
    const std::string& tm = trigger_mode;
    if (tm.empty() || tm == "off" || tm == "Off" || tm == "0") {
        MV_CC_SetEnumValue(handle, "TriggerMode", MV_TRIGGER_MODE_OFF);
        return "";
    }
    MV_CC_SetEnumValue(handle, "TriggerMode", MV_TRIGGER_MODE_ON);
    unsigned int source;
    if (tm == "software" || tm == "Software") {
        source = MV_TRIGGER_SOURCE_SOFTWARE;
        timeout_ms = 3000;   // 软触发指令本身已发出，3s 足够
    } else if (tm == "line0" || tm == "Line0") {
        source = MV_TRIGGER_SOURCE_LINE0; timeout_ms = 5000;   // 硬触发需等待外部信号
    } else if (tm == "line1" || tm == "Line1") {
        source = MV_TRIGGER_SOURCE_LINE1; timeout_ms = 5000;
    } else if (tm == "line2" || tm == "Line2") {
        source = MV_TRIGGER_SOURCE_LINE2; timeout_ms = 5000;
    } else if (tm == "line3" || tm == "Line3") {
        source = MV_TRIGGER_SOURCE_LINE3; timeout_ms = 5000;
    } else {
        return "未知触发模式: " + tm;
    }
    MV_CC_SetEnumValue(handle, "TriggerSource", source);
    // 触发沿：默认上升沿
    MV_CC_SetEnumValue(handle, "TriggerActivation", 0);
    return "";
}

bool grabOneFrame(const std::string& sn, const GrabParams& params,
                  cv::Mat& outBgr, std::string& err) {
    // 1) 枚举，找到匹配 sn 的设备信息
    MV_CC_DEVICE_INFO_LIST list;
    std::memset(&list, 0, sizeof(list));
    int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &list);
    if (ret != MV_OK) { err = "枚举设备失败"; return false; }

    MV_CC_DEVICE_INFO* target = nullptr;
    for (unsigned int i = 0; i < list.nDeviceNum; ++i) {
        MV_CC_DEVICE_INFO* di = list.pDeviceInfo[i];
        if (!di) continue;
        std::string s;
        if (di->nTLayerType == MV_GIGE_DEVICE)
            s = fromBytes(di->SpecialInfo.stGigEInfo.chSerialNumber, sizeof(di->SpecialInfo.stGigEInfo.chSerialNumber));
        else if (di->nTLayerType == MV_USB_DEVICE)
            s = fromBytes(di->SpecialInfo.stUsb3VInfo.chSerialNumber, sizeof(di->SpecialInfo.stUsb3VInfo.chSerialNumber));
        if (s == sn) { target = di; break; }
    }
    if (!target) { err = "未找到序列号为 " + sn + " 的相机"; return false; }

    void* handle = nullptr;
    ret = MV_CC_CreateHandle(&handle, target);
    if (ret != MV_OK) { err = "CreateHandle 失败"; return false; }

    bool ok = false;
    ret = MV_CC_OpenDevice(handle, MV_ACCESS_Exclusive, 0);
    if (ret != MV_OK) { err = "OpenDevice 失败（相机可能被占用）"; goto cleanup; }

    // 触发模式
    {
        unsigned int timeout_ms = 1000;
        std::string terr = applyTrigger(handle, params.trigger_mode, timeout_ms);
        if (!terr.empty()) { err = terr; goto close; }

        // 曝光/增益
        if (params.exposure_us > 0) MV_CC_SetFloatValue(handle, "ExposureTime", (float)params.exposure_us);
        if (params.gain_db > 0)     MV_CC_SetFloatValue(handle, "Gain", (float)params.gain_db);

        // 帧率（仅连续采集模式生效；触发模式下海康要求先关使能）
        if (params.frame_rate > 0) {
            if (params.trigger_mode.empty() || params.trigger_mode == "off")
                MV_CC_SetBoolValue(handle, "AcquisitionFrameRateEnable", true);
            else
                MV_CC_SetBoolValue(handle, "AcquisitionFrameRateEnable", false);
            MV_CC_SetFloatValue(handle, "AcquisitionFrameRate", (float)params.frame_rate);
        }

        ret = MV_CC_StartGrabbing(handle);
        if (ret != MV_OK) { err = "StartGrabbing 失败"; goto close; }

        // 软触发：发一次触发指令
        if (params.trigger_mode == "software" || params.trigger_mode == "Software") {
            ret = MV_CC_SetCommandValue(handle, "TriggerSoftware");
            if (ret != MV_OK) {
                std::ostringstream oss; oss << "软触发指令失败 0x" << std::hex << ret;
                err = oss.str(); goto stop;
            }
        }

        {
            MV_FRAME_OUT_INFO_EX info;
            std::memset(&info, 0, sizeof(info));
            // 预分配足够大的缓冲（按最大可能帧）
            const unsigned int bufSize = 4096u * 4096u * 3u;
            unsigned char* buf = new (std::nothrow) unsigned char[bufSize];
            if (!buf) { err = "分配取流缓冲失败"; goto stop; }
            ret = MV_CC_GetOneFrameTimeout(handle, buf, bufSize, &info, timeout_ms);
            if (ret == MV_OK) {
                int w = info.nWidth ? info.nWidth : (int)info.nExtendWidth;
                int h = info.nHeight ? info.nHeight : (int)info.nExtendHeight;
                if (w > 0 && h > 0) {
                    ok = pixelToBgr(buf, w, h, (unsigned int)info.enPixelType, outBgr);
                    if (!ok) err = "像素格式转换失败";
                } else {
                    err = "图像尺寸无效";
                }
            } else {
                std::ostringstream oss;
                if (params.trigger_mode == "software" || params.trigger_mode == "Software")
                    oss << "软触发取流超时/失败 0x" << std::hex << ret;
                else if (params.trigger_mode != "off" && !params.trigger_mode.empty())
                    oss << "等待硬触发信号超时 0x" << std::hex << ret << "（请检查触发源接线）";
                else
                    oss << "取流超时/失败 0x" << std::hex << ret;
                err = oss.str();
            }
            delete[] buf;
        }
    }
stop:
    MV_CC_StopGrabbing(handle);
close:
    MV_CC_CloseDevice(handle);
cleanup:
    MV_CC_DestroyHandle(handle);
    return ok;
}

bool forceIp(const std::string& sn,
             const std::string& ip, const std::string& netmask, const std::string& gateway,
             std::string& err) {
    MV_CC_DEVICE_INFO_LIST list;
    std::memset(&list, 0, sizeof(list));
    int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE, &list);
    if (ret != MV_OK) { err = "枚举设备失败"; return false; }

    MV_CC_DEVICE_INFO* target = nullptr;
    for (unsigned int i = 0; i < list.nDeviceNum; ++i) {
        MV_CC_DEVICE_INFO* di = list.pDeviceInfo[i];
        if (!di || di->nTLayerType != MV_GIGE_DEVICE) continue;
        std::string s = fromBytes(di->SpecialInfo.stGigEInfo.chSerialNumber,
                                  sizeof(di->SpecialInfo.stGigEInfo.chSerialNumber));
        if (s == sn) { target = di; break; }
    }
    if (!target) { err = "未找到序列号为 " + sn + " 的 GigE 相机"; return false; }

    unsigned int nIp = 0, nMask = 0, nGw = 0;
    if (!stringToIpInet(ip, nIp))        { err = "IP 格式无效: " + ip; return false; }
    if (!stringToIpInet(netmask, nMask)) { err = "子网掩码格式无效: " + netmask; return false; }
    if (!stringToIpInet(gateway, nGw))   { err = "网关格式无效: " + gateway; return false; }

    void* handle = nullptr;
    ret = MV_CC_CreateHandle(&handle, target);
    if (ret != MV_OK) { err = "CreateHandle 失败"; return false; }

    ret = MV_GIGE_ForceIpEx(handle, nIp, nMask, nGw);
    MV_CC_DestroyHandle(handle);
    if (ret != MV_OK) {
        std::ostringstream oss; oss << "MV_GIGE_ForceIpEx 失败 0x" << std::hex << ret
                                    << "（请确认本机网卡与相机在同一物理网段）";
        err = oss.str();
        return false;
    }
    return true;
}

} // namespace hik
} // namespace vz
#endif // VZ_HAS_MVS_SDK
