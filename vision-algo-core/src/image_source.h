// image_source.h — 图像源算法
//
// 端口契约：
//   输入：无
//   输出：image (Image)
//   参数：
//     path  (String)   图片文件绝对/相对路径（支持 cv::imread 通用格式）
//     index (Int)      摄像头索引（保留扩展，当前未使用）
//
// 用途：作为流程图的起点，从本地磁盘加载一张静态图作为后续算法的输入。
// 不依赖相机 SDK，跨平台。
//
#pragma once

#include "algo_base.h"
#include <opencv2/core.hpp>
#include <string>

namespace vz {

class ImageSourceAlgo : public AlgoBase {
public:
    ImageSourceAlgo() = default;
    ~ImageSourceAlgo() override = default;

    int  setParam(const std::string& key, const std::string& value_json) override;
    int  setInput(const std::string& port_name, const void* data, int type_tag) override;
    int  process() override;
    int  getOutput(const std::string& port_name,
                   void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;

    static std::unique_ptr<AlgoBase> create();

private:
    std::string path_;      // 文件路径
    int         index_ = 0; // 摄像头索引（保留）
    cv::Mat     image_;     // 加载后的图像
};

} // namespace vz
