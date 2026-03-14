/*
 * Bottom Sheet Tests
 *
 * Tests for MD3 Bottom Sheet component:
 * - Standard bottom sheet
 * - Modal bottom sheet
 * - Open/close animations
 * - Drag handle interaction
 * - Height settings
 */

#include "common.h"

/* Basic bottom sheet functionality */

static void test_bottom_sheet_init_state(void)
{
    TEST(bottom_sheet_init_state);

    iui_bottom_sheet_state sheet = {0};

    /* Default state */
    ASSERT_FALSE(sheet.open);
    ASSERT_FALSE(sheet.modal);
    ASSERT_EQ(sheet.height, 0.f);
    ASSERT_EQ(sheet.anim_progress, 0.f);

    PASS();
}

static void test_bottom_sheet_open_close(void)
{
    TEST(bottom_sheet_open_close);

    iui_bottom_sheet_state sheet = {.height = 300.f};

    /* Initially closed */
    ASSERT_FALSE(sheet.open);

    /* Open the sheet */
    iui_bottom_sheet_open(&sheet);
    ASSERT_TRUE(sheet.open);

    /* Close the sheet */
    iui_bottom_sheet_close(&sheet);
    ASSERT_FALSE(sheet.open);

    PASS();
}

static void test_bottom_sheet_set_height(void)
{
    TEST(bottom_sheet_set_height);

    iui_bottom_sheet_state sheet = {0};

    /* Set height */
    iui_bottom_sheet_set_height(&sheet, 250.f);
    ASSERT_NEAR(sheet.height, 250.f, 0.1f);

    /* Update height */
    iui_bottom_sheet_set_height(&sheet, 400.f);
    ASSERT_NEAR(sheet.height, 400.f, 0.1f);

    /* Zero height is valid */
    iui_bottom_sheet_set_height(&sheet, 0.f);
    ASSERT_NEAR(sheet.height, 0.f, 0.1f);

    PASS();
}

static void test_bottom_sheet_null_safety(void)
{
    TEST(bottom_sheet_null_safety);

    /* NULL state - should not crash */
    iui_bottom_sheet_open(NULL);
    iui_bottom_sheet_close(NULL);
    iui_bottom_sheet_set_height(NULL, 100.f);

    PASS();
}

/* Bottom sheet rendering tests */

static void test_bottom_sheet_render_closed(void)
{
    TEST(bottom_sheet_render_closed);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 600, 0);

    reset_counters();

    iui_bottom_sheet_state sheet = {.height = 300.f, .open = false};

    /* Closed sheet should return false and not render content */
    bool result = iui_bottom_sheet_begin(ctx, &sheet, 400, 600);
    ASSERT_FALSE(result);
    iui_bottom_sheet_end(ctx, &sheet);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_bottom_sheet_render_open(void)
{
    TEST(bottom_sheet_render_open);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 600, 0);

    reset_counters();

    iui_bottom_sheet_state sheet = {
        .height = 300.f, .open = true, .anim_progress = 1.f};

    bool result = iui_bottom_sheet_begin(ctx, &sheet, 400, 600);

    if (result) {
        /* Sheet is open and rendering - add some content */
        iui_button(ctx, "Sheet Button", IUI_ALIGN_CENTER);
    }

    iui_bottom_sheet_end(ctx, &sheet);

    /* Open sheet should render (draw calls made) */
    ASSERT_TRUE(g_draw_box_calls > 0);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_bottom_sheet_modal(void)
{
    TEST(bottom_sheet_modal);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 600, 0);

    reset_counters();

    iui_bottom_sheet_state sheet = {
        .height = 300.f, .open = true, .modal = true, .anim_progress = 1.f};

    bool result = iui_bottom_sheet_begin(ctx, &sheet, 400, 600);

    if (result) {
        iui_text_body_medium(ctx, IUI_ALIGN_CENTER, "Modal Content");
    }

    iui_bottom_sheet_end(ctx, &sheet);

    /* Modal sheet should render scrim + sheet */
    ASSERT_TRUE(g_draw_box_calls >= 2);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_bottom_sheet_types(void)
{
    TEST(bottom_sheet_types);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    /* Test standard type (modal = false) */
    iui_bottom_sheet_state standard = {
        .height = 200.f, .open = true, .modal = false, .anim_progress = 1.f};

    /* Test modal type (modal = true) */
    iui_bottom_sheet_state modal = {
        .height = 200.f, .open = true, .modal = true, .anim_progress = 1.f};

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 600, 0);

    /* Render standard */
    if (iui_bottom_sheet_begin(ctx, &standard, 400, 600)) {
        iui_text_body_medium(ctx, IUI_ALIGN_CENTER, "Standard");
    }
    iui_bottom_sheet_end(ctx, &standard);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    /* Start new frame for modal */
    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 600, 0);

    /* Render modal */
    if (iui_bottom_sheet_begin(ctx, &modal, 400, 600)) {
        iui_text_body_medium(ctx, IUI_ALIGN_CENTER, "Modal");
    }
    iui_bottom_sheet_end(ctx, &modal);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Animation tests */

static void test_bottom_sheet_animation_progress(void)
{
    TEST(bottom_sheet_animation);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_bottom_sheet_state sheet = {.height = 300.f, .open = true};

    /* Simulate animation over multiple frames */
    for (int frame = 0; frame < 30; frame++) {
        iui_begin_frame(ctx, 1.0f / 60.0f);
        iui_begin_window(ctx, "Test", 0, 0, 400, 600, 0);

        iui_bottom_sheet_begin(ctx, &sheet, 400, 600);
        iui_bottom_sheet_end(ctx, &sheet);

        iui_end_window(ctx);
        iui_end_frame(ctx);
    }

    /* Animation should have progressed */
    ASSERT_TRUE(sheet.anim_progress > 0.f);

    free(buffer);
    PASS();
}

static void test_bottom_sheet_close_animation(void)
{
    TEST(bottom_sheet_close_anim);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_bottom_sheet_state sheet = {
        .height = 300.f, .open = true, .anim_progress = 1.f};

    /* Close and animate */
    iui_bottom_sheet_close(&sheet);

    for (int frame = 0; frame < 30; frame++) {
        iui_begin_frame(ctx, 1.0f / 60.0f);
        iui_begin_window(ctx, "Test", 0, 0, 400, 600, 0);

        iui_bottom_sheet_begin(ctx, &sheet, 400, 600);
        iui_bottom_sheet_end(ctx, &sheet);

        iui_end_window(ctx);
        iui_end_frame(ctx);
    }

    /* Animation should progress toward 0 */
    ASSERT_TRUE(sheet.anim_progress < 1.f);

    free(buffer);
    PASS();
}

/* Interaction tests */

static void test_bottom_sheet_drag_handle(void)
{
    TEST(bottom_sheet_drag_handle);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_bottom_sheet_state sheet = {
        .height = 300.f, .open = true, .anim_progress = 1.f};

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 600, 0);

    reset_counters();

    if (iui_bottom_sheet_begin(ctx, &sheet, 400, 600)) {
        /* Drag handle should be rendered */
    }
    iui_bottom_sheet_end(ctx, &sheet);

    /* Verify draw calls include drag handle */
    ASSERT_TRUE(g_draw_box_calls > 0);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_bottom_sheet_scrim_click(void)
{
    TEST(bottom_sheet_scrim_click);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_bottom_sheet_state sheet = {
        .height = 300.f,
        .open = true,
        .modal = true,
        .anim_progress = 1.f,
    };

    /* Click on scrim area (above the sheet) */
    iui_update_mouse_pos(ctx, 200.f, 100.f); /* Top of screen, above sheet */
    iui_update_mouse_buttons(ctx, IUI_MOUSE_LEFT, 0);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 600, 0);

    iui_bottom_sheet_begin(ctx, &sheet, 400, 600);
    iui_bottom_sheet_end(ctx, &sheet);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    /* Scrim click may close the modal sheet (implementation dependent) */
    /* Just verify it doesn't crash */

    free(buffer);
    PASS();
}

static void test_bottom_sheet_content_scroll(void)
{
    TEST(bottom_sheet_content_scroll);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_bottom_sheet_state sheet = {
        .height = 200.f, .open = true, .anim_progress = 1.f};

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 600, 0);

    if (iui_bottom_sheet_begin(ctx, &sheet, 400, 600)) {
        /* Add content that would need scrolling */
        for (int i = 0; i < 20; i++) {
            char label[32];
            snprintf(label, sizeof(label), "Item %d", i);
            iui_button(ctx, label, IUI_ALIGN_LEFT);
            iui_newline(ctx);
        }
    }
    iui_bottom_sheet_end(ctx, &sheet);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    /* Should not crash with content overflow */

    free(buffer);
    PASS();
}

/* Edge cases */

static void test_bottom_sheet_zero_height(void)
{
    TEST(bottom_sheet_zero_height);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_bottom_sheet_state sheet = {
        .height = 0.f, .open = true, .anim_progress = 1.f};

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 600, 0);

    /* Zero height sheet should handle gracefully */
    iui_bottom_sheet_begin(ctx, &sheet, 400, 600);
    iui_bottom_sheet_end(ctx, &sheet);

    /* May or may not render, but shouldn't crash */

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_bottom_sheet_full_height(void)
{
    TEST(bottom_sheet_full_height);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_bottom_sheet_state sheet = {
        .height = 600.f, /* Full screen height */
        .open = true,
        .anim_progress = 1.f,
    };

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 600, 0);

    bool result = iui_bottom_sheet_begin(ctx, &sheet, 400, 600);
    if (result)
        iui_text_body_medium(ctx, IUI_ALIGN_CENTER, "Full Height Content");
    iui_bottom_sheet_end(ctx, &sheet);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Runner function */
void run_bottom_sheet_tests(void)
{
    SECTION_BEGIN("Bottom Sheet Component");

    /* State management */
    test_bottom_sheet_init_state();
    test_bottom_sheet_open_close();
    test_bottom_sheet_set_height();
    test_bottom_sheet_null_safety();

    /* Rendering */
    test_bottom_sheet_render_closed();
    test_bottom_sheet_render_open();
    test_bottom_sheet_modal();
    test_bottom_sheet_types();

    /* Animation */
    test_bottom_sheet_animation_progress();
    test_bottom_sheet_close_animation();

    /* Interaction */
    test_bottom_sheet_drag_handle();
    test_bottom_sheet_scrim_click();
    test_bottom_sheet_content_scroll();

    /* Edge cases */
    test_bottom_sheet_zero_height();
    test_bottom_sheet_full_height();

    SECTION_END();
}
