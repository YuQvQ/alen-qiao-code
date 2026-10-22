// standalone_dnn_test.cpp — 独立测试 vision_algo.dll 的 DnnDetect
// 直接用 C API 调 DLL，输入 frame0.jpg (cv::imread 读原图 → VzImage)，
// 打印所有检测框的原始坐标、标签、置信度。
// 用法：放在 vision-algo-core/build 目录下 cmake build 即可

#include "vision_algo.h"
#include "opencv2/opencv.hpp"
#include <cstdio>
#include <vector>

int main() {
    printf("vision_algo version: %s\n", vz_algo_version());

    // 1. 读图（原图，不做灰度化！和 ImageSource 的 Mono8 区分开）
    cv::Mat img_bgr = cv::imread(R"(c:\Users\qiaoy\Desktop\frame0.jpg)", cv::IMREAD_COLOR);
    cv::Mat img_mono = cv::imread(R"(c:\Users\qiaoy\Desktop\frame0.jpg)", cv::IMREAD_GRAYSCALE);
    if (img_bgr.empty() || img_mono.empty()) { fprintf(stderr, "读文件失败\n"); return 1; }
    printf("原图 BGR: %dx%d  (%d 通道)\n", img_bgr.cols, img_bgr.rows, img_bgr.channels());
    printf("原图 MONO: %dx%d  (%d 通道)\n", img_mono.cols, img_mono.rows, img_mono.channels());

    const char* model = R"(c:\Users\qiaoy\Desktop\yolo26n.onnx)";

    auto run_once = [&](cv::Mat& mat, const char* tag) {
        printf("\n========== %s ==========\n", tag);
        VzAlgoCtx* ctx = nullptr;
        int rc = vz_algo_create("DnnDetect", &ctx);
        printf("create rc=%d  err=%s\n", rc, vz_algo_last_error(ctx));
        if (rc != VZ_OK) return;

        vz_algo_set_param(ctx, "model", model);
        vz_algo_set_param(ctx, "input_w", "640");
        vz_algo_set_param(ctx, "input_h", "640");
        vz_algo_set_param(ctx, "scale", "0.00392156");
        vz_algo_set_param(ctx, "swap_rb", "1");
        vz_algo_set_param(ctx, "conf_threshold", "0.5");
        vz_algo_set_param(ctx, "nms_threshold", "0.4");

        // 构造 VzImage
        VzImage vimg{};
        vimg.width = mat.cols;
        vimg.height = mat.rows;
        vimg.channels = mat.channels();
        vimg.pixel_type = mat.depth() == 0 ? VZ_PIXEL_UINT8 : (mat.depth() == 2 ? VZ_PIXEL_UINT16 : VZ_PIXEL_FLOAT32);
        vimg.step = (uint64_t)mat.step;
        vimg.data = mat.data;
        vimg.owns_data = 0;
        vimg.magic = VZ_IMAGE_MAGIC;

        rc = vz_algo_set_input(ctx, "image", &vimg, VZ_TYPE_IMAGE);
        printf("set_input rc=%d\n", rc);

        rc = vz_algo_process(ctx);
        printf("process rc=%d  err=%s\n", rc, vz_algo_last_error(ctx));
        if (rc != VZ_OK) { vz_algo_release(ctx); return; }

        // count
        void* o = nullptr; int n = 0, t = 0;
        vz_algo_get_output(ctx, "count", &o, &n, &t);
        int count = *(int*)o;
        printf("count = %d\n", count);
        vz_free(o);

        // boxes
        vz_algo_get_output(ctx, "boxes", &o, &n, &t);
        VzRect* boxes = (VzRect*)o;
        for (int i = 0; i < count; ++i)
            printf("  box#%d  cx=%.2f cy=%.2f  w=%.2f h=%.2f\n",
                   i, boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h);
        vz_free(o);

        // labels
        vz_algo_get_output(ctx, "labels", &o, &n, &t);
        printf("  labels: %s\n", (char*)o);
        vz_free(o);

        // confidences
        vz_algo_get_output(ctx, "confidences", &o, &n, &t);
        printf("  confs: %s\n", (char*)o);
        vz_free(o);

        // detections_json（最详细）
        vz_algo_get_output(ctx, "detections_json", &o, &n, &t);
        printf("  json: %s\n", (char*)o);
        vz_free(o);

        // 保存 result_image
        vz_algo_get_output(ctx, "result_image", &o, &n, &t);
        VzImage* res = (VzImage*)o;
        cv::Mat res_mat(res->height, res->width, res->channels == 3 ? CV_8UC3 : CV_8UC1, res->data, res->step);
        std::string out_path = std::string(R"(d:\unisic_vision\_dnn_standalone_)") + tag + ".png";
        cv::imwrite(out_path, res_mat);
        printf("  result_image 已写: %s\n", out_path.c_str());
        vz_free(o);

        vz_algo_release(ctx);
    };

    run_once(img_bgr,  "BGR");
    run_once(img_mono, "MONO");

    printf("\nDone.\n");
    return 0;
}
