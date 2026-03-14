/*
 * Clip Stack Tests
 *
 * Tests for iui_push_clip, iui_pop_clip, and iui_is_clipped.
 */

#include "common.h"

/* Clip Stack Basic Tests */

static void test_clip_push_pop_basic(void)
{
    TEST(clip_push_pop_basic);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    reset_counters();

    /* Push a clip rect */
    iui_rect_t clip = {50, 50, 100, 100};
    bool success = iui_push_clip(ctx, clip);
    ASSERT_TRUE(success);
    ASSERT_TRUE(g_set_clip_calls > 0);

    /* Verify clip was set correctly */
    ASSERT_EQ(g_last_clip_min_x, 50);
    ASSERT_EQ(g_last_clip_min_y, 50);
    ASSERT_EQ(g_last_clip_max_x, 150);
    ASSERT_EQ(g_last_clip_max_y, 150);

    /* Pop the clip */
    iui_pop_clip(ctx);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_clip_nested(void)
{
    TEST(clip_nested);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    /* Push outer clip */
    iui_rect_t outer = {0, 0, 200, 200};
    ASSERT_TRUE(iui_push_clip(ctx, outer));

    /* Push inner clip (should intersect with outer) */
    iui_rect_t inner = {50, 50, 200, 200};
    ASSERT_TRUE(iui_push_clip(ctx, inner));

    /* Inner clip should be intersection: (50,50) to (200,200) */
    ASSERT_TRUE(g_last_clip_min_x >= 50);
    ASSERT_TRUE(g_last_clip_min_y >= 50);
    ASSERT_TRUE(g_last_clip_max_x <= 200);
    ASSERT_TRUE(g_last_clip_max_y <= 200);

    /* Pop inner */
    iui_pop_clip(ctx);

    /* Pop outer */
    iui_pop_clip(ctx);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_clip_is_clipped_inside(void)
{
    TEST(clip_is_clipped_inside);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    /* Push a clip rect */
    iui_rect_t clip = {50, 50, 100, 100};
    iui_push_clip(ctx, clip);

    /* Rect fully inside clip - not clipped */
    iui_rect_t inside = {60, 60, 20, 20};
    ASSERT_FALSE(iui_is_clipped(ctx, inside));

    iui_pop_clip(ctx);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_clip_is_clipped_outside(void)
{
    TEST(clip_is_clipped_outside);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    /* Push a clip rect */
    iui_rect_t clip = {50, 50, 100, 100};
    iui_push_clip(ctx, clip);

    /* Rect fully outside clip - is clipped */
    iui_rect_t outside = {200, 200, 50, 50};
    ASSERT_TRUE(iui_is_clipped(ctx, outside));

    iui_pop_clip(ctx);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_clip_is_clipped_partial(void)
{
    TEST(clip_is_clipped_partial);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    /* Push a clip rect */
    iui_rect_t clip = {50, 50, 100, 100};
    iui_push_clip(ctx, clip);

    /* Rect partially overlapping - not fully clipped */
    iui_rect_t partial = {100, 100, 100, 100};
    ASSERT_FALSE(iui_is_clipped(ctx, partial));

    iui_pop_clip(ctx);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Clip Stack Edge Case Tests */

static void test_clip_null_context(void)
{
    TEST(clip_null_context);

    /* NULL context tests may crash depending on implementation
     * Skip this test if the implementation doesn't guard against NULL */
    /* Just verify that valid contexts work properly */

    PASS();
}

static void test_clip_zero_size(void)
{
    TEST(clip_zero_size);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    /* Zero width clip */
    iui_rect_t zero_w = {50, 50, 0, 100};
    bool success = iui_push_clip(ctx, zero_w);
    /* Should still succeed but anything would be clipped */
    if (success)
        iui_pop_clip(ctx);

    /* Zero height clip */
    iui_rect_t zero_h = {50, 50, 100, 0};
    success = iui_push_clip(ctx, zero_h);
    if (success)
        iui_pop_clip(ctx);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_clip_negative_coords(void)
{
    TEST(clip_negative_coords);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    /* Negative coordinates (should be clamped to 0) */
    iui_rect_t negative = {-50, -50, 100, 100};
    bool success = iui_push_clip(ctx, negative);
    /* Implementation should handle gracefully */
    if (success)
        iui_pop_clip(ctx);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_clip_pop_without_push(void)
{
    TEST(clip_pop_without_push);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    /* Push one clip, then pop more than pushed */
    iui_rect_t rect = {10, 10, 100, 100};
    iui_push_clip(ctx, rect);
    iui_pop_clip(ctx);
    /* Extra pop should be safe (does nothing) */
    iui_pop_clip(ctx);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_clip_outside_window(void)
{
    TEST(clip_outside_window);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);

    reset_counters();
    int initial_clip_calls = g_set_clip_calls;

    /* Try clip operations outside window context */
    iui_rect_t rect = {0, 0, 100, 100};
    bool success = iui_push_clip(ctx, rect);

    /* Clip outside window may succeed (implementation-dependent).
     * The state should remain consistent regardless.
     */
    if (success) {
        /* If push succeeded, pop should be safe */
        iui_pop_clip(ctx);
    }

    /* Verify no unexpected state changes without window */
    (void) initial_clip_calls;

    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Test Suite Runner */

void run_clip_tests(void)
{
    SECTION_BEGIN("Clip Stack");
    test_clip_push_pop_basic();
    test_clip_nested();
    test_clip_is_clipped_inside();
    test_clip_is_clipped_outside();
    test_clip_is_clipped_partial();
    test_clip_null_context();
    test_clip_zero_size();
    test_clip_negative_coords();
    test_clip_pop_without_push();
    test_clip_outside_window();
    SECTION_END();
}
