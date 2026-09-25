// blob_analysis.h — Blob 分析算法
//
// 输入：image (Image，建议二值图)
// 输出：
//   blobs   : VzRect[]  每个 Blob 的旋转外接矩形（中心+宽高+角度）
//   centers : VzPoint2D[] 每个 Blob 的中心
//   count   : Int       Blob 数量
// 参数：
//   min_area: 最小面积（像素）
//   max_area: 最大面积（像素）
//   sort_by : "area" | "x" | "y"  排序方式
//   max_count: 最大输出 Blob 数（0=不限）
//
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>
#include <vector>

namespace vz {

class BlobAnalysisAlgo : public AlgoBase {
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
    int min_area_ = 50;
    int max_area_ = 10000000;
    std::string sort_by_ = "area";
    int max_count_ = 0;
    int enable_binary_ = 1;     // 二值化开关（0=否,1=Otsu自动二值化）
    int binary_threshold_ = 128; // 二值化阈值（enable_binary=2 时手动阈值）

    std::vector<VzRect>    out_rects_;
    std::vector<VzPoint2D> out_centers_;
    int                    out_count_ = 0;
};

} // namespace vz
