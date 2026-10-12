// sample_sobel.c —— 第三方调用 vision_algo.dll 的最小示例（C / C++）
//
// 演示完整生命周期：
//   create -> set_param -> set_input -> process -> get_output -> vz_free -> release
//
// 编译（MSVC，x64 Native Tools 命令行）：
//   cl /EHsc sample_sobel.c /I ..\..\include ..\..\lib\vision_algo.lib
//   运行前确保 ..\..\bin\vision_algo.dll 与 opencv_world4130.dll 在 PATH 或同目录
//
// 也可用 MinGW：
//   gcc sample_sobel.c -I ..\..\include -L ..\..\lib -lvision_algo -o sample.exe

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "vision_algo.h"

// 造一张测试灰度图（带一条竖直亮边），无需任何图像解码库
static VzImage make_test_image(int w, int h) {
    VzImage img;
    img.width      = w;
    img.height     = h;
    img.channels   = 1;                 // 1=灰度
    img.pixel_type = VZ_PIXEL_UINT8;
    img.step       = (uint64_t)w;       // 行字节数
    img.owns_data  = 0;                 // 调用方持有，DLL 不得释放
    img.magic      = VZ_IMAGE_MAGIC;    // 必须设置，用于安全识别
    img.data = malloc((size_t)w * h);
    uint8_t* p = (uint8_t*)img.data;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            p[y * w + x] = (x > w / 2) ? 220 : 30; // 左暗右亮 → 中间一条竖边
    return img;
}

int main(void) {
    printf("vision_algo version: %s\n", vz_algo_version());

    // 1) 创建算法实例
    VzAlgoCtx* ctx = NULL;
    int rc = vz_algo_create("SobelEdge", &ctx);
    if (rc != VZ_OK) { printf("create failed: %s\n", vz_algo_last_error(NULL)); return 1; }

    // 2) 设置参数（值一律传 JSON 字符串）
    vz_algo_set_param(ctx, "ksize", "3");
    vz_algo_set_param(ctx, "dx",    "1");
    vz_algo_set_param(ctx, "dy",    "0");

    // 3) 喂输入图像
    VzImage in = make_test_image(320, 240);
    rc = vz_algo_set_input(ctx, "image", &in, VZ_TYPE_IMAGE);
    if (rc != VZ_OK) { printf("set_input failed: %s\n", vz_algo_last_error(ctx)); return 1; }

    // 4) 执行 —— 这就是“直接调用 process”
    rc = vz_algo_process(ctx);
    if (rc != VZ_OK) { printf("process failed: %s\n", vz_algo_last_error(ctx)); return 1; }

    // 5) 取输出（端口名见各算法 describe 的 outputs）
    void* out = NULL; int count = 0, type = 0;
    rc = vz_algo_get_output(ctx, "image", &out, &count, &type);
    if (rc != VZ_OK || !out) { printf("get_output failed: %s\n", vz_algo_last_error(ctx)); return 1; }

    VzImage* res = (VzImage*)out;
    printf("result image: %dx%d ch=%d type=%d\n", res->width, res->height, res->channels, type);

    // 6) 用完释放（out 由 DLL 分配，必须 vz_free）
    vz_free(out);
    vz_algo_release(ctx);
    free(in.data);
    printf("done.\n");
    return 0;
}
