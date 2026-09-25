// vision_algo.h — C ABI 公开接口
//
// 这是算法 DLL 唯一对外公开的接口。所有算法以"工厂 + 句柄"形式使用：
//
//   VzAlgoCtx* ctx;
//   vz_algo_create("TemplateMatch", &ctx);
//   vz_algo_set_param(ctx, "threshold", "0.7");
//   vz_algo_set_input(ctx, "image", &img, VZ_TYPE_IMAGE);
//   vz_algo_process(ctx);
//   void* out; int n, t;
//   vz_algo_get_output(ctx, "centers", &out, &n, &t);
//   // ... 使用 out ...
//   vz_free(out);
//   vz_algo_release(ctx);
//
// 内存规则：
//   - 任何 *out_data 指针由算法层分配，调用方用 vz_free 释放
//   - const char* 返回值指向算法层静态缓冲，下次同线程调用同函数即失效
//
#pragma once
#include "vision_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// 不透明算法上下文
typedef struct VzAlgoCtx VzAlgoCtx;

// === 生命周期 ===
VISION_API int  vz_algo_create(const char* algo_name, VzAlgoCtx** out);
VISION_API void vz_algo_release(VzAlgoCtx* ctx);

// === 参数 ===
// value_json: JSON 字符串。算法自行解析（数字、字符串、数组）
VISION_API int  vz_algo_set_param(VzAlgoCtx* ctx, const char* key, const char* value_json);

// === 输入端口 ===
// data: 指向对应 type_tag 的 POD 结构（如 VzImage）
//       若为数组端口，data 指向数组首元素
VISION_API int  vz_algo_set_input(VzAlgoCtx* ctx, const char* port_name,
                                  const void* data, int type_tag);

// === 执行 ===
VISION_API int  vz_algo_process(VzAlgoCtx* ctx);

// === 输出端口 ===
// out_data:    返回数据指针（caller 必须 vz_free）
// out_count:  数组元素数（标量为 1，空数组为 0）
// out_type_tag: 数据类型
VISION_API int  vz_algo_get_output(VzAlgoCtx* ctx, const char* port_name,
                                   void** out_data, int* out_count, int* out_type_tag);

// === 内存 ===
VISION_API void vz_free(void* ptr);

// === 自描述 ===
// 返回所有注册算法名 JSON 数组：["AlgoA","AlgoB"]
VISION_API const char* vz_algo_list(void);

// 返回某算法的完整描述 JSON（端口、参数、绑定、图标、显示名）
VISION_API const char* vz_algo_describe(const char* algo_name);

// 版本号 "major.minor.patch"
VISION_API const char* vz_algo_version(void);

// 枚举当前在线的海康相机，返回 JSON 数组：
// [{"sn":"...","model":"...","vendor":"...","type":"GigE/USB","user_name":"..."}, ...]
// 枚举失败时返回 "[]"，错误信息可通过 vz_algo_last_error(nullptr) 获取。
VISION_API const char* vz_enum_cameras(void);

// 最后一次错误（ctx 为 null 时返回全局错误）
VISION_API const char* vz_algo_last_error(VzAlgoCtx* ctx);

// === 连线路由（正交、可拐弯、绕开节点）===
// 规划一条从 (sx,sy) 到 (tx,ty) 的轴对齐折线，不穿过 obstacles 中的矩形。
//   obstacles      : 每个矩形 4 个 double [x, y, w, h]，共 obstacle_count 个
//   padding        : 障碍物外扩像素（走线与节点的间距）
//   grid_step      : 搜索网格步长（像素）
//   out_points     : 调用方提供的缓冲，每点写 2 个 double [x, y]
//   max_points     : out_points 可容纳的点数（不是 double 数）
// 返回实际写入的点数；失败返回 <= 0。
VISION_API int vz_route_orthogonal(
    double sx, double sy, double tx, double ty,
    const double* obstacles, int obstacle_count,
    double padding, double grid_step,
    double* out_points, int max_points);

#ifdef __cplusplus
} // extern "C"
#endif
