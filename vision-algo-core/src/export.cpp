// export.cpp — C ABI 导出层
//
// 把 C++ 内部的算法注册表 + AlgoBase 抽象包装为 vision_algo.h 中声明的
// extern "C" 函数。所有跨 DLL 调用（来自 C# P/Invoke）只经过此文件。
//
#include "vision_algo.h"
#include "algo_base.h"
#include "template_match.h"
#include "image_source.h"
#include "camera_source.h"
#include "bin_threshold.h"
#include "blob_analysis.h"
#include "circle_find.h"
#include "line_find.h"
#include "text_output.h"
#include "calibration_transform.h"
#include "position_tracking.h"
#include "index_selector.h"
#include "if_condition.h"
#include "edge_finder.h"
#include "line_fit.h"
#include "circle_fit.h"
#include "distance_measure.h"
#include "angle_measure.h"
#include "line_intersect.h"
#include "line_angle.h"
#include "rectangle_detect.h"
#include "roi_crop.h"
#include "morphology.h"
#include "pixel_stats.h"
#include "image_resize.h"
#include "corner_detect.h"
#include "image_display.h"
#include "grayscale.h"
#include "gaussian_blur.h"
#include "median_blur.h"
#include "hist_equalize.h"
#include "invert.h"
#include "canny_edge.h"
#include "sobel_edge.h"
#include "adaptive_threshold.h"
#include "convert_color.h"
#include "image_flip.h"
#include "image_rotate.h"
#include "find_contours.h"
#include "convex_hull.h"
#include "area_measure.h"
#include "math_op.h"
#include "math_func.h"
#include "counter.h"
#include "accumulator.h"
#include "feature_match.h"
#include "shape_match.h"
#include "qrcode_detect.h"
#include "color_detect.h"
#include "dnn_classify.h"
#include "dnn_detect.h"
#include "hik_camera.h"
#include "bilateral_filter.h"
#include "threshold.h"
#include "min_max_loc.h"
#include "distance_transform.h"
#include "connected_components.h"
#include "image_math.h"
#include "for_loop.h"

#include <string>
#include <sstream>
#include <unordered_map>
#include <mutex>

namespace {

// 算法上下文实现：C 头中只声明 typedef struct VzAlgoCtx VzAlgoCtx;
// 此处用 VzAlgoCtxImpl 避免与全局 typedef 同名冲突。
struct VzAlgoCtxImpl {
    std::unique_ptr<vz::AlgoBase> algo;
};

// 全局错误（线程局部，避免跨线程串扰）
thread_local std::string g_global_error;

void setGlobalError(const std::string& msg) {
    g_global_error = msg;
}

// describe 返回的字符串必须是静态生命周期的，避免悬垂
// 因此每次调用同一 algo_name 都返回同一个静态 buffer
const char* cacheDescribe(const std::string& name) {
    static std::unordered_map<std::string, std::string> cache;
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    auto it = cache.find(name);
    if (it != cache.end()) return it->second.c_str();
    const vz::AlgoRegistryEntry* e = vz::findAlgo(name);
    if (!e) return nullptr;
    cache[name] = e->describe();
    return cache[name].c_str();
}

} // namespace

// ============================================================
// 生命周期
// ============================================================
VISION_API int vz_algo_create(const char* algo_name, VzAlgoCtx** out) {
    if (!algo_name || !out) {
        setGlobalError("invalid argument: null algo_name or out");
        return VZ_ERR_INVALID_ARG;
    }
    const vz::AlgoRegistryEntry* e = vz::findAlgo(algo_name);
    if (!e) {
        setGlobalError("algorithm not found: " + std::string(algo_name));
        return VZ_ERR_NOT_FOUND;
    }
    auto* impl = new (std::nothrow) VzAlgoCtxImpl();
    if (!impl) {
        setGlobalError("alloc context failed");
        return VZ_ERR_NO_MEMORY;
    }
    impl->algo = e->factory();
    if (!impl->algo) {
        delete impl;
        setGlobalError("factory returned null");
        return VZ_ERR_INTERNAL;
    }
    *out = reinterpret_cast<VzAlgoCtx*>(impl);
    return VZ_OK;
}

VISION_API void vz_algo_release(VzAlgoCtx* ctx) {
    if (ctx) delete reinterpret_cast<VzAlgoCtxImpl*>(ctx);
}

// ============================================================
// 参数
// ============================================================
VISION_API int vz_algo_set_param(VzAlgoCtx* ctx, const char* key, const char* value_json) {
    if (!ctx || !key || !value_json) {
        setGlobalError("invalid argument: null ctx/key/value");
        return VZ_ERR_INVALID_ARG;
    }
    auto* impl = reinterpret_cast<VzAlgoCtxImpl*>(ctx);
    return impl->algo->setParam(key, value_json);
}

// ============================================================
// 输入
// ============================================================
VISION_API int vz_algo_set_input(VzAlgoCtx* ctx, const char* port_name,
                                 const void* data, int type_tag) {
    if (!ctx || !port_name) {
        setGlobalError("invalid argument: null ctx/port_name");
        return VZ_ERR_INVALID_ARG;
    }
    auto* impl = reinterpret_cast<VzAlgoCtxImpl*>(ctx);
    return impl->algo->setInput(port_name, data, type_tag);
}

// ============================================================
// 执行
// ============================================================
VISION_API int vz_algo_process(VzAlgoCtx* ctx) {
    if (!ctx) {
        setGlobalError("invalid argument: null ctx");
        return VZ_ERR_INVALID_ARG;
    }
    auto* impl = reinterpret_cast<VzAlgoCtxImpl*>(ctx);
    return impl->algo->process();
}

// ============================================================
// 输出
// ============================================================
VISION_API int vz_algo_get_output(VzAlgoCtx* ctx, const char* port_name,
                                   void** out_data, int* out_count, int* out_type_tag) {
    if (!ctx || !port_name || !out_data) {
        setGlobalError("invalid argument: null ctx/port_name/out_data");
        return VZ_ERR_INVALID_ARG;
    }
    auto* impl = reinterpret_cast<VzAlgoCtxImpl*>(ctx);
    return impl->algo->getOutput(port_name, out_data, out_count, out_type_tag);
}

// ============================================================
// 内存
// ============================================================
VISION_API void vz_free(void* ptr) {
    if (ptr) std::free(ptr);
}

// ============================================================
// 自描述
// ============================================================
VISION_API const char* vz_algo_list() {
    static thread_local std::string buffer;
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    for (const auto& e : vz::allAlgos()) {
        if (!first) oss << ",";
        oss << "\"" << e.name << "\"";
        first = false;
    }
    oss << "]";
    buffer = oss.str();
    return buffer.c_str();
}

VISION_API const char* vz_algo_describe(const char* algo_name) {
    if (!algo_name) return nullptr;
    return cacheDescribe(algo_name);
}

VISION_API const char* vz_algo_version() {
    return "0.1.0";
}

VISION_API const char* vz_enum_cameras(void) {
    static thread_local std::string buffer;
    std::vector<vz::hik::CameraInfo> cams;
    std::string err;
    if (!vz::hik::enumerateCameras(cams, err)) {
        setGlobalError("枚举相机失败: " + err);
        buffer = "[]";
        return buffer.c_str();
    }
    setGlobalError("");
    buffer = vz::hik::toJson(cams);
    return buffer.c_str();
}

VISION_API const char* vz_algo_last_error(VzAlgoCtx* ctx) {
    if (!ctx) return g_global_error.c_str();
    auto* impl = reinterpret_cast<VzAlgoCtxImpl*>(ctx);
    return impl->algo->lastError().c_str();
}

// ============================================================
// 算法自注册（启动即注册）
// ============================================================
namespace {
struct AutoRegistrar {
    AutoRegistrar() {
        vz::registerAlgo({
            "TemplateMatch",
            &vz::TemplateMatchAlgo::create,
            []() -> const char* { return vz::TemplateMatchAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "ImageSource",
            &vz::ImageSourceAlgo::create,
            []() -> const char* { return vz::ImageSourceAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "CameraSource",
            &vz::CameraSourceAlgo::create,
            []() -> const char* { return vz::CameraSourceAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "BinThreshold",
            &vz::BinThresholdAlgo::create,
            []() -> const char* { return vz::BinThresholdAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "BlobAnalysis",
            &vz::BlobAnalysisAlgo::create,
            []() -> const char* { return vz::BlobAnalysisAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "CircleFind",
            &vz::CircleFindAlgo::create,
            []() -> const char* { return vz::CircleFindAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "LineFind",
            &vz::LineFindAlgo::create,
            []() -> const char* { return vz::LineFindAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "TextOutput",
            &vz::TextOutputAlgo::create,
            []() -> const char* { return vz::TextOutputAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "CalibrationTransform",
            &vz::CalibrationTransformAlgo::create,
            []() -> const char* { return vz::CalibrationTransformAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "PositionTracking",
            &vz::PositionTrackingAlgo::create,
            []() -> const char* { return vz::PositionTrackingAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "IndexSelector",
            &vz::IndexSelectorAlgo::create,
            []() -> const char* { return vz::IndexSelectorAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "IfCondition",
            &vz::IfConditionAlgo::create,
            []() -> const char* { return vz::IfConditionAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "EdgeFinder",
            &vz::EdgeFinderAlgo::create,
            []() -> const char* { return vz::EdgeFinderAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "LineFit",
            &vz::LineFitAlgo::create,
            []() -> const char* { return vz::LineFitAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "CircleFit",
            &vz::CircleFitAlgo::create,
            []() -> const char* { return vz::CircleFitAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "DistanceMeasure",
            &vz::DistanceMeasureAlgo::create,
            []() -> const char* { return vz::DistanceMeasureAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "AngleMeasure",
            &vz::AngleMeasureAlgo::create,
            []() -> const char* { return vz::AngleMeasureAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "LineIntersect",
            &vz::LineIntersectAlgo::create,
            []() -> const char* { return vz::LineIntersectAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "LineAngle",
            &vz::LineAngleAlgo::create,
            []() -> const char* { return vz::LineAngleAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "RectangleDetect",
            &vz::RectangleDetectAlgo::create,
            []() -> const char* { return vz::RectangleDetectAlgo{}.describe(); }
        });
        vz::registerAlgo({"Grayscale", &vz::GrayscaleAlgo::create, []() -> const char* { return vz::GrayscaleAlgo{}.describe(); }});
        vz::registerAlgo({"GaussianBlur", &vz::GaussianBlurAlgo::create, []() -> const char* { return vz::GaussianBlurAlgo{}.describe(); }});
        vz::registerAlgo({"MedianBlur", &vz::MedianBlurAlgo::create, []() -> const char* { return vz::MedianBlurAlgo{}.describe(); }});
        vz::registerAlgo({"HistEqualize", &vz::HistEqualizeAlgo::create, []() -> const char* { return vz::HistEqualizeAlgo{}.describe(); }});
        vz::registerAlgo({"Invert", &vz::InvertAlgo::create, []() -> const char* { return vz::InvertAlgo{}.describe(); }});
        vz::registerAlgo({"CannyEdge", &vz::CannyEdgeAlgo::create, []() -> const char* { return vz::CannyEdgeAlgo{}.describe(); }});
        vz::registerAlgo({"SobelEdge", &vz::SobelEdgeAlgo::create, []() -> const char* { return vz::SobelEdgeAlgo{}.describe(); }});
        vz::registerAlgo({"AdaptiveThreshold", &vz::AdaptiveThresholdAlgo::create, []() -> const char* { return vz::AdaptiveThresholdAlgo{}.describe(); }});
        vz::registerAlgo({"ConvertColor", &vz::ConvertColorAlgo::create, []() -> const char* { return vz::ConvertColorAlgo{}.describe(); }});
        vz::registerAlgo({"ImageFlip", &vz::ImageFlipAlgo::create, []() -> const char* { return vz::ImageFlipAlgo{}.describe(); }});
        vz::registerAlgo({"ImageRotate", &vz::ImageRotateAlgo::create, []() -> const char* { return vz::ImageRotateAlgo{}.describe(); }});
        vz::registerAlgo({"FindContours", &vz::FindContoursAlgo::create, []() -> const char* { return vz::FindContoursAlgo{}.describe(); }});
        vz::registerAlgo({"ConvexHull", &vz::ConvexHullAlgo::create, []() -> const char* { return vz::ConvexHullAlgo{}.describe(); }});
        vz::registerAlgo({"AreaMeasure", &vz::AreaMeasureAlgo::create, []() -> const char* { return vz::AreaMeasureAlgo{}.describe(); }});
        vz::registerAlgo({"MathOp", &vz::MathOpAlgo::create, []() -> const char* { return vz::MathOpAlgo{}.describe(); }});
        vz::registerAlgo({"MathFunc", &vz::MathFuncAlgo::create, []() -> const char* { return vz::MathFuncAlgo{}.describe(); }});
        vz::registerAlgo({"Counter", &vz::CounterAlgo::create, []() -> const char* { return vz::CounterAlgo{}.describe(); }});
        vz::registerAlgo({"Accumulator", &vz::AccumulatorAlgo::create, []() -> const char* { return vz::AccumulatorAlgo{}.describe(); }});
        vz::registerAlgo({"FeatureMatch", &vz::FeatureMatchAlgo::create, []() -> const char* { return vz::FeatureMatchAlgo{}.describe(); }});
        vz::registerAlgo({"ShapeMatch", &vz::ShapeMatchAlgo::create, []() -> const char* { return vz::ShapeMatchAlgo{}.describe(); }});
        vz::registerAlgo({"QrCodeDetect", &vz::QrCodeDetectAlgo::create, []() -> const char* { return vz::QrCodeDetectAlgo{}.describe(); }});
        vz::registerAlgo({"ColorDetect", &vz::ColorDetectAlgo::create, []() -> const char* { return vz::ColorDetectAlgo{}.describe(); }});
        vz::registerAlgo({"DnnClassify", &vz::DnnClassifyAlgo::create, []() -> const char* { return vz::DnnClassifyAlgo{}.describe(); }});
        vz::registerAlgo({"DnnDetect", &vz::DnnDetectAlgo::create, []() -> const char* { return vz::DnnDetectAlgo{}.describe(); }});
        vz::registerAlgo({"BilateralFilter", &vz::BilateralFilterAlgo::create, []() -> const char* { return vz::BilateralFilterAlgo{}.describe(); }});
        vz::registerAlgo({"Threshold", &vz::ThresholdAlgo::create, []() -> const char* { return vz::ThresholdAlgo{}.describe(); }});
        vz::registerAlgo({"MinMaxLoc", &vz::MinMaxLocAlgo::create, []() -> const char* { return vz::MinMaxLocAlgo{}.describe(); }});
        vz::registerAlgo({"DistanceTransform", &vz::DistanceTransformAlgo::create, []() -> const char* { return vz::DistanceTransformAlgo{}.describe(); }});
        vz::registerAlgo({"ConnectedComponents", &vz::ConnectedComponentsAlgo::create, []() -> const char* { return vz::ConnectedComponentsAlgo{}.describe(); }});
        vz::registerAlgo({"ImageMath", &vz::ImageMathAlgo::create, []() -> const char* { return vz::ImageMathAlgo{}.describe(); }});
        vz::registerAlgo({"ForLoop", &vz::ForLoopAlgo::create, []() -> const char* { return vz::ForLoopAlgo{}.describe(); }});
        vz::registerAlgo({
            "RoiCrop",
            &vz::RoiCropAlgo::create,
            []() -> const char* { return vz::RoiCropAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "Morphology",
            &vz::MorphologyAlgo::create,
            []() -> const char* { return vz::MorphologyAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "PixelStats",
            &vz::PixelStatsAlgo::create,
            []() -> const char* { return vz::PixelStatsAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "ImageResize",
            &vz::ImageResizeAlgo::create,
            []() -> const char* { return vz::ImageResizeAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "CornerDetect",
            &vz::CornerDetectAlgo::create,
            []() -> const char* { return vz::CornerDetectAlgo{}.describe(); }
        });
        vz::registerAlgo({
            "ImageDisplay",
            &vz::ImageDisplayAlgo::create,
            []() -> const char* { return vz::ImageDisplayAlgo{}.describe(); }
        });
    }
};
static AutoRegistrar g_auto_registrar;
} // namespace
