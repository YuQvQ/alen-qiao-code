// calibration_transform.cpp — 标定矩阵转换算法实现
//
// 端口：
//   输入 matrix(String)  — JSON 3x3 矩阵 "[a,b,c,d,e,f,g,h,i]"（行优先）
//   输入 point(Pose2D)   — 待转换坐标
//   输出 point(Pose2D)   — 转换后坐标
//   参数 matrix(String)  — 默认矩阵（端口未连线时兜底）
//
#include "calibration_transform.h"
#include "algo_utils.h"
#include <cstring>
#include <cstdlib>

namespace vz {

// 解析 JSON 数组的 9 个 double（行优先 3x3 矩阵）
static bool parseMatrix9(const std::string& s, double out[9]) {
    auto lb = s.find('[');
    auto rb = s.find(']');
    if (lb == std::string::npos || rb == std::string::npos || rb <= lb) return false;
    auto body = s.substr(lb + 1, rb - lb - 1);
    int idx = 0;
    size_t pos = 0;
    while (pos < body.size() && idx < 9) {
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == ',' || body[pos] == '\t' || body[pos] == '\n')) pos++;
        if (pos >= body.size()) break;
        size_t end = body.find(',', pos);
        if (end == std::string::npos) end = body.size();
        try { out[idx++] = std::stod(body.substr(pos, end - pos)); }
        catch (...) { return false; }
        pos = end + 1;
    }
    return idx == 9;
}

// 把 9 个 double 写入 cv::Mat(3,3)
static void fillMatrix(cv::Mat& m, const double v[9]) {
    m = (cv::Mat_<double>(3, 3) <<
        v[0], v[1], v[2],
        v[3], v[4], v[5],
        v[6], v[7], v[8]);
}

int CalibrationTransformAlgo::setParam(const std::string& key, const std::string& value_json) {
    if (key == "matrix") {
        // 空字符串 / null：保持默认单位矩阵，不报错
        if (value_json.empty() || value_json == "\"\"" || value_json == "null") {
            return VZ_OK;
        }
        // 默认矩阵参数（端口未传 matrix 时使用）
        double m[9] = {1,0,0, 0,1,0, 0,0,1};
        if (parseMatrix9(value_json, m)) {
            fillMatrix(matrix_, m);
        } else {
            setLastError("matrix parse failed; expect [a,b,c,d,e,f,g,h,i]");
            return VZ_ERR_INVALID_ARG;
        }
        return VZ_OK;
    }
    setLastError("unknown parameter: " + key);
    return VZ_ERR_INVALID_ARG;
}

int CalibrationTransformAlgo::setInput(const std::string& port_name, const void* data, int type_tag) {
    if (port_name == "matrix") {
        // 矩阵通过端口传入（String 类型，UTF-8 JSON）
        if (type_tag != VZ_TYPE_STRING) {
            setLastError("input 'matrix' expects String type");
            return VZ_ERR_TYPE_MISMATCH;
        }
        if (!data) {
            setLastError("input 'matrix' data is null");
            return VZ_ERR_INVALID_ARG;
        }
        const char* s = static_cast<const char*>(data);
        double m[9] = {1,0,0, 0,1,0, 0,0,1};
        if (parseMatrix9(s, m)) {
            fillMatrix(matrix_, m);
            has_port_matrix_ = true;
            return VZ_OK;
        }
        setLastError("input 'matrix' parse failed; expect [a,b,c,d,e,f,g,h,i]");
        return VZ_ERR_INVALID_ARG;
    }
    if (port_name == "point") {
        // 待转换坐标（Pose2D 类型）
        if (type_tag != VZ_TYPE_POSE2D) {
            setLastError("input 'point' expects Pose2D type");
            return VZ_ERR_TYPE_MISMATCH;
        }
        if (!data) {
            setLastError("input 'point' data is null");
            return VZ_ERR_INVALID_ARG;
        }
        input_pose_ = *static_cast<const VzPose2D*>(data);
        has_input_pose_ = true;
        return VZ_OK;
    }
    setLastError("unknown input port: " + port_name);
    return VZ_ERR_INVALID_ARG;
}

int CalibrationTransformAlgo::process() {
    if (!has_input_pose_) {
        setLastError("input 'point' not set; connect a Pose2D source");
        return VZ_ERR_INVALID_ARG;
    }
    // 矩阵来源：端口优先，端口没传则用参数默认值（matrix_ 已在 setParam/setInput 中赋好）

    double m00 = matrix_.at<double>(0,0), m01 = matrix_.at<double>(0,1), m02 = matrix_.at<double>(0,2);
    double m10 = matrix_.at<double>(1,0), m11 = matrix_.at<double>(1,1), m12 = matrix_.at<double>(1,2);
    double m20 = matrix_.at<double>(2,0), m21 = matrix_.at<double>(2,1), m22 = matrix_.at<double>(2,2);

    double x = input_pose_.point.x;
    double y = input_pose_.point.y;
    double w = m20 * x + m21 * y + m22;
    if (std::abs(w) < 1e-12) w = 1e-12;  // 透视除法保护

    output_pose_.point.x = (m00 * x + m01 * y + m02) / w;
    output_pose_.point.y = (m10 * x + m11 * y + m12) / w;
    output_pose_.angle   = input_pose_.angle;  // 角度原样保留

    return VZ_OK;
}

int CalibrationTransformAlgo::getOutput(const std::string& port_name,
                                          void** out_data, int* out_count, int* out_type_tag) {
    if (port_name != "point") {
        setLastError("unknown output port: " + port_name);
        return VZ_ERR_INVALID_ARG;
    }
    // 输出单个 Pose2D（caller 必须 vz_free）
    auto* p = (VzPose2D*)std::malloc(sizeof(VzPose2D));
    *p = output_pose_;
    *out_data = p; *out_count = 1; *out_type_tag = VZ_TYPE_POSE2D;
    return VZ_OK;
}

void CalibrationTransformAlgo::releaseOutput(void* ptr) {
    if (ptr) std::free(ptr);
}

const char* CalibrationTransformAlgo::describe() const {
    static const char* kDesc = R"JSON({
  "type_id":"CalibrationTransform",
  "display_name":"标定矩阵转换",
  "category":"工具",
  "icon":"icons/calibration.svg",
  "algo_binding":{"dll":"vision_algo","algo_name":"CalibrationTransform"},
  "inputs":[
    {"name":"matrix","type":"String","required":false,"description":"3x3单应矩阵(JSON数组,行优先9个数)"},
    {"name":"point","type":"Pose2D","required":true,"description":"待转换坐标(像素坐标)"}
  ],
  "outputs":[
    {"name":"point","type":"Pose2D","required":true,"description":"转换后坐标(世界坐标,角度原样保留)"}
  ],
  "parameters":[
    {"name":"matrix","type":"String","default":"[1,0,0,0,1,0,0,0,1]","description":"默认3x3矩阵(端口未连线时使用)"}
  ]
})JSON";
    return kDesc;
}

std::unique_ptr<AlgoBase> CalibrationTransformAlgo::create() {
    return std::make_unique<CalibrationTransformAlgo>();
}

} // namespace vz
