// vision_types.h — 跨 ABI 边界的 POD 数据结构
// 任何二进制边界（C++ <-> C#）都只通过此处的结构体传输。
// 设计原则：
//   1. 仅 POD（Plain Old Data）
//   2. 显式内存所有权（owns_data / caller-frees）
//   3. 字节对齐由编译器默认，跨位数传输需重打包
//
#pragma once

#include <stdint.h>
#include <stddef.h>

// 符号导出宏
#ifdef VISION_ALGO_EXPORTS
  #define VISION_API extern "C" __declspec(dllexport)
#else
  #define VISION_API extern "C" __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// 像素类型
// ============================================================
#define VZ_PIXEL_UINT8   0
#define VZ_PIXEL_UINT16  1
#define VZ_PIXEL_FLOAT32 2

// ============================================================
// 图像（封装 cv::Mat 的最小数据）
// ============================================================
typedef struct VzImage {
    int32_t  width;
    int32_t  height;
    int32_t  channels;   // 1=灰 3=BGR 4=BGRA
    int32_t  pixel_type; // VZ_PIXEL_*
    void*    data;       // 像素数据
    uint64_t step;       // 行字节数
    int32_t  owns_data;  // 是否持有（释放时是否 free）
    uint32_t magic;      // VZ_IMAGE_MAGIC，用于安全识别 VzImage
} VzImage;

#define VZ_IMAGE_MAGIC 0x565A494D  // "VZIM"

// ============================================================
// 2D 点
// ============================================================
typedef struct VzPoint2D {
    double x;
    double y;
} VzPoint2D;

// ============================================================
// 带角度的矩形（中心点 + 尺寸 + 角度）
// ============================================================
typedef struct VzRect {
    double x;      // 中心 X
    double y;      // 中心 Y
    double w;
    double h;
    double angle;  // 度
} VzRect;

// ============================================================
// 2D 位姿（位置 + 角度）
// ============================================================
typedef struct VzPose2D {
    VzPoint2D point;
    double    angle;
} VzPose2D;

// ============================================================
// 模板匹配结果
// ============================================================
typedef struct VzMatchResult {
    VzRect  rect;
    double  score;
    int32_t template_id;
    int32_t _reserved;
} VzMatchResult;

// ============================================================
// 区间（double）
// ============================================================
typedef struct VzDoubleRange {
    double low;
    double high;
} VzDoubleRange;

// ============================================================
// 数组包装（caller 必须 vz_free 释放 .data）
// ============================================================
typedef struct VzImageArray {
    VzImage* data;
    int32_t  count;
} VzImageArray;

typedef struct VzPoint2DArray {
    VzPoint2D* data;
    int32_t    count;
} VzPoint2DArray;

typedef struct VzRectArray {
    VzRect* data;
    int32_t count;
} VzRectArray;

typedef struct VzMatchResultArray {
    VzMatchResult* data;
    int32_t        count;
} VzMatchResultArray;

typedef struct VzPose2DArray {
    VzPose2D* data;
    int32_t   count;
} VzPose2DArray;

// ============================================================
// 端口类型枚举（与 .NET 端 TypeRegistry 必须严格一致）
// ============================================================
typedef enum VzPortType {
    VZ_TYPE_UNKNOWN       = 0,
    VZ_TYPE_IMAGE         = 1,
    VZ_TYPE_REGION        = 2,
    VZ_TYPE_MATCH_RESULT  = 3,
    VZ_TYPE_POINT2D_LIST  = 4,
    VZ_TYPE_RECT_LIST    = 5,
    VZ_TYPE_INT          = 6,
    VZ_TYPE_DOUBLE        = 7,
    VZ_TYPE_BOOL          = 8,
    VZ_TYPE_STRING        = 9,
    VZ_TYPE_POSE2D       = 10,
    VZ_TYPE_DOUBLE_RANGE  = 11,
} VzPortType;

// ============================================================
// 错误码
// ============================================================
typedef enum VzError {
    VZ_OK                = 0,
    VZ_ERR_INVALID_ARG  = -1,
    VZ_ERR_NOT_FOUND    = -2,
    VZ_ERR_TYPE_MISMATCH= -3,
    VZ_ERR_PROCESS      = -4,
    VZ_ERR_NO_MEMORY    = -5,
    VZ_ERR_UNSUPPORTED  = -6,
    VZ_ERR_INTERNAL     = -7,
} VzError;

#ifdef __cplusplus
} // extern "C"
#endif
