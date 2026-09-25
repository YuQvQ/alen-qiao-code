// test_template_match.cpp — 算法层单元测试
#include <gtest/gtest.h>
#include "vision_algo.h"

#include <cstring>
#include <cstdlib>

namespace {

// 构造一张 100x100 灰度图，正中央放一块 20x20 模板
struct FakeImage {
    VzImage  header;
    uint8_t* pixels;
    int      total;

    FakeImage(int w, int h, int channels = 1)
        : pixels(nullptr), total(0) {
        total = w * h * channels;
        pixels = (uint8_t*)std::malloc(total);
        std::memset(pixels, 0, total);
        header.width = w;
        header.height = h;
        header.channels = channels;
        header.pixel_type = VZ_PIXEL_UINT8;
        header.data = pixels;
        header.step = w * channels;
        header.owns_data = 1;
        header._reserved = 0;
    }
    ~FakeImage() { if (pixels) std::free(pixels); }

    // 在 (x,y) 处画一个白色的 w x h 矩形
    void drawRect(int x, int y, int rw, int rh, uint8_t v = 255) {
        for (int j = y; j < y + rh; ++j)
            for (int i = x; i < x + rw; ++i) {
                int idx = j * header.step + i;
                pixels[idx] = v;
            }
    }
};

} // namespace

TEST(TemplateMatch, Version) {
    const char* v = vz_algo_version();
    ASSERT_NE(v, nullptr);
    ASSERT_STRNE(v, "");
}

TEST(TemplateMatch, List) {
    const char* list = vz_algo_list();
    ASSERT_NE(list, nullptr);
    ASSERT_TRUE(std::strstr(list, "TemplateMatch") != nullptr)
        << "expected TemplateMatch in list: " << list;
}

TEST(TemplateMatch, Describe) {
    const char* desc = vz_algo_describe("TemplateMatch");
    ASSERT_NE(desc, nullptr);
    ASSERT_TRUE(std::strstr(desc, "type_id") != nullptr);
    ASSERT_TRUE(std::strstr(desc, "TemplateMatch") != nullptr);
    ASSERT_TRUE(std::strstr(desc, "centers") != nullptr)
        << "describe should include 'centers' output port";
}

TEST(TemplateMatch, CreateAndRelease) {
    VzAlgoCtx* ctx = nullptr;
    int rc = vz_algo_create("TemplateMatch", &ctx);
    ASSERT_EQ(rc, VZ_OK) << vz_algo_last_error(nullptr);
    ASSERT_NE(ctx, nullptr);
    vz_algo_release(ctx);
}

TEST(TemplateMatch, CreateUnknownAlgoFails) {
    VzAlgoCtx* ctx = nullptr;
    int rc = vz_algo_create("NonexistentAlgo", &ctx);
    ASSERT_NE(rc, VZ_OK);
    ASSERT_EQ(ctx, nullptr);
}

TEST(TemplateMatch, SetParamValid) {
    VzAlgoCtx* ctx = nullptr;
    ASSERT_EQ(vz_algo_create("TemplateMatch", &ctx), VZ_OK);
    EXPECT_EQ(vz_algo_set_param(ctx, "threshold", "0.7"), VZ_OK);
    EXPECT_EQ(vz_algo_set_param(ctx, "max_count", "5"), VZ_OK);
    EXPECT_EQ(vz_algo_set_param(ctx, "angle_range", "[-10, 10]"), VZ_OK);
    EXPECT_EQ(vz_algo_set_param(ctx, "scale_range", "[0.9, 1.1]"), VZ_OK);
    vz_algo_release(ctx);
}

TEST(TemplateMatch, SetParamInvalid) {
    VzAlgoCtx* ctx = nullptr;
    ASSERT_EQ(vz_algo_create("TemplateMatch", &ctx), VZ_OK);
    EXPECT_NE(vz_algo_set_param(ctx, "bad_param", "1"), VZ_OK);
    EXPECT_NE(vz_algo_set_param(ctx, "threshold", "2.0"), VZ_OK); // 超范围
    vz_algo_release(ctx);
}

TEST(TemplateMatch, ProcessSimpleMatch) {
    // 100x100 黑底图，中央 40,40 处 20x20 白块
    FakeImage src(100, 100, 1);
    src.drawRect(40, 40, 20, 20);

    // 模板：20x20 全白
    FakeImage tmpl(20, 20, 1);
    tmpl.drawRect(0, 0, 20, 20);

    VzAlgoCtx* ctx = nullptr;
    ASSERT_EQ(vz_algo_create("TemplateMatch", &ctx), VZ_OK);
    ASSERT_EQ(vz_algo_set_input(ctx, "image",    &src.header,  VZ_TYPE_IMAGE), VZ_OK);
    ASSERT_EQ(vz_algo_set_input(ctx, "template", &tmpl.header, VZ_TYPE_IMAGE), VZ_OK);
    ASSERT_EQ(vz_algo_set_param(ctx, "threshold", "0.5"), VZ_OK);

    int rc = vz_algo_process(ctx);
    ASSERT_EQ(rc, VZ_OK) << vz_algo_last_error(ctx);

    // 读 count
    void* data = nullptr; int n = 0; int t = 0;
    ASSERT_EQ(vz_algo_get_output(ctx, "count", &data, &n, &t), VZ_OK);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(t, VZ_TYPE_INT);
    ASSERT_EQ(*(int*)data, 1);
    vz_free(data);

    // 读 centers
    ASSERT_EQ(vz_algo_get_output(ctx, "centers", &data, &n, &t), VZ_OK);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(t, VZ_TYPE_POINT2D_LIST);
    VzPoint2D* pts = (VzPoint2D*)data;
    // 中心应该在 (50, 50)
    EXPECT_NEAR(pts[0].x, 50.0, 1.0);
    EXPECT_NEAR(pts[0].y, 50.0, 1.0);
    vz_free(data);

    vz_algo_release(ctx);
}
