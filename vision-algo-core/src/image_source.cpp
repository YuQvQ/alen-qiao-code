// image_source.cpp — 图像源算法实现
//
// 行为：process() 时从 path_ 加载图像到 image_。
// getOutput("image") 时把 cv::Mat 转 VzImage 返回（caller 释放）。
//
// 路径编码处理（关键）：
//   .NET 端 JSON 序列化用 UnsafeRelaxedJsonEscaping，中文字符以 UTF-8 字节直出。
//   C++ 端 std::string 拿到 UTF-8 字节序列，但 Windows 默认 fopen 按 ANSI
//   代码页解释，UTF-8 中文路径会失败。所以不能直接用 cv::imread(path)。
//   方案：UTF-8 -> UTF-16 -> _wfopen 读字节流 -> cv::imdecode 解码。
//
#include "image_source.h"
#include "algo_utils.h"
#include <opencv2/imgcodecs.hpp>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace vz {

// ============================================================
// 参数
// ============================================================
int ImageSourceAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "path") {
        // JSON 字符串可能带引号，剥离
        path_ = detail::parseJsonString(value_json);
        return VZ_OK;
    }
    if (key == "index") {
        index_ = detail::parseJsonInt(value_json, 0);
        return VZ_OK;
    }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

// ============================================================
// 输入（无输入端口）
// ============================================================
int ImageSourceAlgo::setInput(const std::string& port_name,
                                const void* /*data*/, int /*type_tag*/) {
    setLastError("image source has no input ports, got: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

// ============================================================
// UTF-8 -> UTF-16（Windows）
// ============================================================
#ifdef _WIN32
static std::wstring utf8ToUtf16(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), ws.data(), len);
    return ws;
}
#endif

// ============================================================
// 执行
// ============================================================
int ImageSourceAlgo::process() {
    image_ = cv::Mat();  // 清空上一帧

    if (path_.empty()) {
        setLastError("path is empty");
        return VZ_ERR_INVALID_ARG;
    }

#ifdef _WIN32
    // Windows：UTF-8 -> UTF-16 -> _wfopen + imdecode
    std::wstring wpath = utf8ToUtf16(path_);
    FILE* fp = _wfopen(wpath.c_str(), L"rb");
    if (!fp) {
        setLastError("open file failed for path: " + path_);
        return VZ_ERR_PROCESS;
    }
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz <= 0) {
        std::fclose(fp);
        setLastError("file is empty or seek failed: " + path_);
        return VZ_ERR_PROCESS;
    }
    std::vector<unsigned char> buf(static_cast<size_t>(sz));
    size_t nread = std::fread(buf.data(), 1, buf.size(), fp);
    std::fclose(fp);
    if (nread != buf.size()) {
        setLastError("read file incomplete: " + path_);
        return VZ_ERR_PROCESS;
    }
    // imdecode 从内存缓冲解码图像，不受 fopen 路径编码影响
    cv::Mat m = cv::imdecode(buf, cv::IMREAD_COLOR);
    if (m.empty()) {
        setLastError("imdecode failed (not a valid image?): " + path_);
        return VZ_ERR_PROCESS;
    }
    image_ = m;
    return VZ_OK;
#else
    // 非 Windows 平台路径直接当 UTF-8 用
    cv::Mat m = cv::imread(path_, cv::IMREAD_COLOR);
    if (m.empty()) {
        setLastError("imread failed for path: " + path_);
        return VZ_ERR_PROCESS;
    }
    image_ = m;
    return VZ_OK;
#endif
}

// ============================================================
// 输出
// ============================================================
int ImageSourceAlgo::getOutput(const std::string& port_name,
                                void** out_data, int* out_count, int* out_type_tag) {
    if (port_name != "image") {
        setLastError("unknown output port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    VzImage* img = detail::toVzImage(image_);
    if (!img) {
        *out_data = nullptr; *out_count = 0; *out_type_tag = VZ_TYPE_IMAGE;
        // 留空不算失败（caller 可能查空）
        return VZ_OK;
    }
    *out_data = img; *out_count = 1; *out_type_tag = VZ_TYPE_IMAGE;
    return VZ_OK;
}

void ImageSourceAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

// ============================================================
// 描述
// ============================================================
const char* ImageSourceAlgo::describe() const {
    static const char* kDesc = R"({
  "type_id":"ImageSource",
  "display_name":"图像源",
  "category":"源",
  "icon":"icons/image_source.svg",
  "algo_binding":{"dll":"vision_algo","algo_name":"ImageSource"},
  "inputs":[],
  "outputs":[
    {"name":"image","type":"Image","required":true,"description":"加载的图像"}
  ],
  "parameters":[
    {"name":"path","type":"String","default":"","description":"图片文件路径"},
    {"name":"index","type":"Int","default":0,"min":0,"description":"摄像头索引（保留扩展）"}
  ]
})";
    return kDesc;
}

std::unique_ptr<AlgoBase> ImageSourceAlgo::create() {
    return std::make_unique<ImageSourceAlgo>();
}

} // namespace vz
