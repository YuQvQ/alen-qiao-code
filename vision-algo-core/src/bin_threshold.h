// bin_threshold.h — 二值化算法
//
// 输入：image (Image)
// 输出：image (Image，二值图 0/255)
// 参数：
//   threshold: 阈值 0-255
//   max_value: 二值化后最大值（默认 255）
//   mode: 二值化模式
//     "binary"         : dst = (src > th) ? max : 0
//     "binary_inv"     : dst = (src > th) ? 0   : max
//     "trunc"          : dst = min(src, th)
//     "tozero"         : dst = (src > th) ? src : 0
//     "tozero_inv"     : dst = (src > th) ? 0   : src
//     "otsu"           : Otsu 自动阈值
//     "adaptive"       : 自适应阈值（高斯加权邻域）
//   adaptive_block_size: 自适应阈值块大小（奇数）
//   adaptive_c        : 自适应阈值常数 C
//
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>

namespace vz {

class BinThresholdAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string& port_name,
                  void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;

    static std::unique_ptr<AlgoBase> create();

private:
    cv::Mat input_;
    cv::Mat output_;
    int    threshold_ = 128;
    int    max_value_ = 255;
    std::string mode_ = "binary";
    int    adaptive_block_size_ = 11;
    double adaptive_c_ = 2.0;
};

} // namespace vz
