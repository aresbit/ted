/*
 * Vector Primitives Tests
 *
 * Tests for line, circle, and arc drawing functions.
 */

#include "common.h"

/* Vector Primitives Tests */

static void test_vector_primitives_available(void)
{
    TEST(vector_primitives_available);
    void *buffer = malloc(iui_min_memory_size());

    iui_context *ctx = create_test_context(buffer, true);
    ASSERT_NOT_NULL(ctx);
    ASSERT_TRUE(iui_has_vector_primitives(ctx));

    free(buffer);
    buffer = malloc(iui_min_memory_size());

    ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);
    ASSERT_FALSE(iui_has_vector_primitives(ctx));

    free(buffer);
    PASS();
}

static void test_draw_line_with_primitives(void)
{
    TEST(draw_line_with_primitives);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, true);
    ASSERT_NOT_NULL(ctx);

    reset_counters();
    iui_begin_frame(ctx, 1.0f / 60.0f);

    bool result = iui_draw_line(ctx, 0, 0, 100, 100, 2.0f, 0xFFFFFFFF);
    ASSERT_TRUE(result);
    ASSERT_EQ(g_draw_line_calls, 1);

    iui_end_frame(ctx);
    free(buffer);
    PASS();
}

static void test_draw_line_without_primitives(void)
{
    TEST(draw_line_without_primitives);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    reset_counters();
    iui_begin_frame(ctx, 1.0f / 60.0f);

    bool result = iui_draw_line(ctx, 0, 0, 100, 100, 2.0f, 0xFFFFFFFF);
    ASSERT_FALSE(result);
    ASSERT_EQ(g_draw_line_calls, 0);

    iui_end_frame(ctx);
    free(buffer);
    PASS();
}

static void test_draw_circle(void)
{
    TEST(draw_circle);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, true);
    ASSERT_NOT_NULL(ctx);

    reset_counters();
    iui_begin_frame(ctx, 1.0f / 60.0f);

    bool result = iui_draw_circle(ctx, 50, 50, 25, 0xFF0000FF, 0, 0);
    ASSERT_TRUE(result);

    result = iui_draw_circle(ctx, 100, 50, 25, 0, 0xFF00FF00, 2.0f);
    ASSERT_TRUE(result);

    result = iui_draw_circle(ctx, 150, 50, 25, 0xFF0000FF, 0xFF00FF00, 2.0f);
    ASSERT_TRUE(result);

    ASSERT_EQ(g_draw_circle_calls, 3);

    iui_end_frame(ctx);
    free(buffer);
    PASS();
}

static void test_draw_arc(void)
{
    TEST(draw_arc);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, true);
    ASSERT_NOT_NULL(ctx);

    reset_counters();
    iui_begin_frame(ctx, 1.0f / 60.0f);

    bool result = iui_draw_arc(ctx, 50, 50, 25, 0, 1.5708f, 2.0f, 0xFFFFFFFF);
    ASSERT_TRUE(result);

    result = iui_draw_arc(ctx, 100, 50, 25, 0, 6.28318f, 2.0f, 0xFFFFFFFF);
    ASSERT_TRUE(result);

    ASSERT_EQ(g_draw_arc_calls, 2);

    iui_end_frame(ctx);
    free(buffer);
    PASS();
}

static void test_vector_primitives_edge_values(void)
{
    TEST(vector_primitives_edge_values);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, true);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);

    iui_draw_circle(ctx, 50, 50, 0, 0xFF0000FF, 0, 0);
    iui_draw_line(ctx, 0, 0, 100, 100, 0.0f, 0xFFFFFFFF);
    iui_draw_circle(ctx, 50, 50, -10.0f, 0xFF0000FF, 0, 0);
    iui_draw_arc(ctx, 50, 50, 25, 3.14159f, 0.0f, 2.0f, 0xFFFFFFFF);

    iui_end_frame(ctx);
    free(buffer);
    PASS();
}

/* Test Suite Runner */
void run_vector_tests(void)
{
    SECTION_BEGIN("Vector Primitives");
    test_vector_primitives_available();
    test_draw_line_with_primitives();
    test_draw_line_without_primitives();
    test_draw_circle();
    test_draw_arc();
    test_vector_primitives_edge_values();
    SECTION_END();
}
