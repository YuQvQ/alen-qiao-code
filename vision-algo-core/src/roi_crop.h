// roi_crop.h — ROI裁剪输出子图
// 输入：image
// 输出：image(裁剪后的子图)
// 参数：roi[x,y,w,h,angle]
#pragma once
#include "algo_base.h"
#include <opencv2/core.hpp>

namespace vz {

class RoiCropAlgo : public AlgoBase {
public:
    int setParam(const std::string& key, const std::string& value_json) override;
    int setInput(const std::string& port_name, const void* data, int type_tag) override;
    int process() override;
    int getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) override;
    void releaseOutput(void* ptr) override;
    const char* describe() const override;
    static std::unique_ptr<AlgoBase> create();
private:
    cv::Mat input_;
    cv::Mat output_;
    double roi_cx_=0, roi_cy_=0, roi_w_=0, roi_h_=0, roi_angle_=0;
    bool has_roi_=false;
};

} // namespace vz
