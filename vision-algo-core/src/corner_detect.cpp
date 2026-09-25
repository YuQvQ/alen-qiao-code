// corner_detect.cpp — 角点检测
#include "corner_detect.h"
#include "algo_utils.h"
#include <opencv2/imgproc.hpp>

namespace vz {

using detail::parseJsonDouble;
using detail::parseJsonInt;
using detail::parseColor;
using detail::parseThickness;
using detail::parsePointSize;
using detail::toCvMat;
using detail::toVzImage;

int CornerDetectAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "method") { method_ = parseJsonInt(value_json, 1); if (method_<0)method_=0; if (method_>1)method_=1; return VZ_OK; }
    if (key == "max_corners") { max_corners_ = parseJsonInt(value_json, 100); if (max_corners_<1)max_corners_=1; return VZ_OK; }
    if (key == "quality_level") { quality_level_ = parseJsonDouble(value_json, 0.01); if (quality_level_<=0)quality_level_=0.001; return VZ_OK; }
    if (key == "min_distance") { min_distance_ = parseJsonDouble(value_json, 10.0); if (min_distance_<0)min_distance_=0; return VZ_OK; }
    if (key == "block_size") { block_size_ = parseJsonInt(value_json, 3); if (block_size_<1)block_size_=1; if (block_size_%2==0)block_size_++; return VZ_OK; }
    if (key == "k") { k_ = parseJsonDouble(value_json, 0.04); return VZ_OK; }
    if (key == "draw_color") { draw_color_ = parseColor(value_json, cv::Scalar(0, 0, 255)); return VZ_OK; }
    if (key == "draw_thickness") { draw_thickness_ = parseThickness(value_json, 2); return VZ_OK; }
    if (key == "point_size") { point_size_ = parsePointSize(value_json, 5); return VZ_OK; }
    setLastError("unknown parameter: " + key); return VZ_ERR_INVALID_ARG;
}

int CornerDetectAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "image") {
        if (type_tag != VZ_TYPE_IMAGE) return VZ_ERR_TYPE_MISMATCH;
        input_ = toCvMat((const VzImage*)data);
        return VZ_OK;
    }
    setLastError("unknown input: " + port_name); return VZ_ERR_INVALID_ARG;
}

int CornerDetectAlgo::process() {
    if (input_.empty()) { setLastError("no input image"); return VZ_ERR_PROCESS; }
    cv::Mat gray;
    int ch = input_.channels();
    if (ch == 1) gray = input_;
    else if (ch == 3) cv::cvtColor(input_, gray, cv::COLOR_BGR2GRAY);
    else if (ch == 4) cv::cvtColor(input_, gray, cv::COLOR_BGRA2GRAY);
    else return VZ_ERR_PROCESS;

    std::vector<cv::Point2f> corners;
    if (method_ == 1) {
        cv::goodFeaturesToTrack(gray, corners, max_corners_, quality_level_, min_distance_, cv::noArray(), block_size_);
    } else {
        cv::Mat dst;
        cv::cornerHarris(gray, dst, block_size_, 3, k_);
        cv::Mat dst_norm;
        cv::normalize(dst, dst_norm, 0, 255, cv::NORM_MINMAX, CV_8U);
        double thresh = 150;
        for (int y = 0; y < dst_norm.rows; ++y) {
            for (int x = 0; x < dst_norm.cols; ++x) {
                if ((int)dst_norm.at<uchar>(y, x) > thresh)
                    corners.emplace_back((float)x, (float)y);
            }
        }
    }

    out_points_.clear();
    for (auto& c : corners) out_points_.push_back({c.x, c.y});
    out_count_ = (int)out_points_.size();

    // 结果图：输入图像透传（标注由 UI 层绘制）
    result_image_ = input_.clone();
    return VZ_OK;
}

int CornerDetectAlgo::getOutput(const std::string& port_name, void** out_data, int* out_count, int* out_type_tag) {
    if (port_name == "result_image") { VzImage* img=toVzImage(result_image_); *out_data=img;*out_count=1;*out_type_tag=VZ_TYPE_IMAGE;return VZ_OK; }
    if (port_name == "points") {
        int n = (int)out_points_.size();
        auto* arr = (VzPoint2D*)std::malloc(sizeof(VzPoint2D) * (n ? n : 1));
        for (int i = 0; i < n; ++i) arr[i] = out_points_[i];
        *out_data = arr; *out_count = n; *out_type_tag = VZ_TYPE_POINT2D_LIST; return VZ_OK;
    }
    if (port_name == "count") { auto* p=(int*)std::malloc(sizeof(int)); *p=out_count_; *out_data=p;*out_count=1;*out_type_tag=VZ_TYPE_INT;return VZ_OK; }
    setLastError("unknown output: " + port_name); return VZ_ERR_INVALID_ARG;
}

void CornerDetectAlgo::releaseOutput(void* ptr) { detail::safeReleaseOutput(ptr); }

const char* CornerDetectAlgo::describe() const {
    return R"JSON({
  "type_id":"CornerDetect","display_name":"角点检测","category":"分析",
  "algo_binding":{"dll":"vision_algo","algo_name":"CornerDetect"},
  "inputs":[{"name":"image","type":"Image","required":true,"description":"输入图像"}],
  "outputs":[
    {"name":"result_image","type":"Image","required":true,"description":"结果图"},
    {"name":"points","type":"Point2DList","required":true,"description":"角点列表"},
    {"name":"count","type":"Int","required":true,"description":"角点数量"}
  ],
  "parameters":[
    {"name":"method","type":"Int","default":1,"description":"检测方法","enum":[[0,"Harris"],[1,"Shi-Tomasi"]]},
    {"name":"max_corners","type":"Int","default":100,"description":"最大角点数"},
    {"name":"quality_level","type":"Double","default":0.01,"description":"质量阈值(0-1)"},
    {"name":"min_distance","type":"Double","default":10.0,"description":"角点最小间距"},
    {"name":"block_size","type":"Int","default":3,"description":"邻域块大小(奇数)"},
    {"name":"k","type":"Double","default":0.04,"description":"Harris参数k"},
    {"name":"draw_color","type":"String","default":"#FF0000","description":"角点标注颜色(#RRGGBB)"},
    {"name":"draw_thickness","type":"Int","default":2,"description":"标注线粗细(1-20)"},
    {"name":"point_size","type":"Int","default":5,"description":"角点圆点大小(1-50)"}
  ]
})JSON";
}

std::unique_ptr<AlgoBase> CornerDetectAlgo::create() { return std::make_unique<CornerDetectAlgo>(); }

} // namespace vz
