/*
 * MD3 Component Extensions Tests
 *
 * Tests for textfield variants, switch, card, progress, button styled,
 * typography scale, and shape tokens.
 */

#include "common.h"

/* TextField Variants Tests */

static void test_textfield_variants(void)
{
    TEST(textfield_variants);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    static char buffer1[64] = "Test text";
    static size_t cursor1 = 9;

    iui_textfield_options opts1 = {
        .style = IUI_TEXTFIELD_FILLED,
        .placeholder = "Placeholder",
    };
    iui_textfield(ctx, buffer1, sizeof(buffer1), &cursor1, &opts1);

    static char buffer2[64] = "";
    static size_t cursor2 = 0;
    iui_textfield_options opts2 = {
        .style = IUI_TEXTFIELD_OUTLINED,
        .placeholder = "Enter text",
    };
    iui_textfield(ctx, buffer2, sizeof(buffer2), &cursor2, &opts2);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Switch Component Tests */

static void test_switch_component(void)
{
    TEST(switch_component);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    static bool switch1 = false;
    static bool switch2 = true;

    iui_switch(ctx, "Switch with icons", &switch1, "Y", "N");
    iui_switch(ctx, "Simple switch", &switch2, NULL, NULL);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Card Components Tests */

static void test_card_components(void)
{
    TEST(card_components);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    reset_counters();
    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 600, 400, 0);

    iui_card_begin(ctx, 10, 10, 200, 100, IUI_CARD_ELEVATED);
    iui_text(ctx, IUI_ALIGN_LEFT, "Elevated card content");
    iui_card_end(ctx);

    iui_card_begin(ctx, 220, 10, 200, 100, IUI_CARD_FILLED);
    iui_text(ctx, IUI_ALIGN_LEFT, "Filled card content");
    iui_card_end(ctx);

    iui_card_begin(ctx, 10, 120, 200, 100, IUI_CARD_OUTLINED);
    iui_text(ctx, IUI_ALIGN_LEFT, "Outlined card content");
    iui_card_end(ctx);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    ASSERT_TRUE(g_draw_box_calls >= 3);

    free(buffer);
    PASS();
}

/* Progress Indicators Tests */

static void test_progress_indicators(void)
{
    TEST(progress_indicators);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, true);
    ASSERT_NOT_NULL(ctx);

    reset_counters();
    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    iui_progress_linear(ctx, 50, 100, false);
    iui_progress_linear(ctx, 0, 100, true);

    if (iui_has_vector_primitives(ctx)) {
        iui_progress_circular(ctx, 75, 100, 50, false);
        iui_progress_circular(ctx, 0, 100, 50, true);
    }

    iui_end_window(ctx);
    iui_end_frame(ctx);

    ASSERT_TRUE(g_draw_box_calls >= 2);

    if (iui_has_vector_primitives(ctx)) {
        ASSERT_TRUE(g_draw_arc_calls >= 1);
    }

    free(buffer);
    PASS();
}

/* Button Styled Variants Tests */

static void test_button_styled_variants(void)
{
    TEST(button_styled_variants);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    reset_counters();
    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    bool clicked;

    clicked =
        iui_button_styled(ctx, "Tonal", IUI_ALIGN_CENTER, IUI_BUTTON_TONAL);
    (void) clicked;
    iui_newline(ctx);

    clicked =
        iui_button_styled(ctx, "Filled", IUI_ALIGN_CENTER, IUI_BUTTON_FILLED);
    (void) clicked;
    iui_newline(ctx);

    clicked = iui_button_styled(ctx, "Outlined", IUI_ALIGN_CENTER,
                                IUI_BUTTON_OUTLINED);
    (void) clicked;
    iui_newline(ctx);

    clicked = iui_button_styled(ctx, "Text", IUI_ALIGN_CENTER, IUI_BUTTON_TEXT);
    (void) clicked;
    iui_newline(ctx);

    clicked = iui_button_styled(ctx, "Elevated", IUI_ALIGN_CENTER,
                                IUI_BUTTON_ELEVATED);
    (void) clicked;

    iui_newline(ctx);
    clicked = iui_filled_button(ctx, "Macro Filled", IUI_ALIGN_LEFT);
    (void) clicked;

    iui_end_window(ctx);
    iui_end_frame(ctx);

    ASSERT_TRUE(g_draw_box_calls >= 5);

    free(buffer);
    PASS();
}

/* Typography Scale Tests */

static void test_typography_scale(void)
{
    TEST(typography_scale);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    reset_counters();
    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 500, 0);

    iui_text_headline_small(ctx, IUI_ALIGN_LEFT, "Headline Small");
    iui_text_title_large(ctx, IUI_ALIGN_LEFT, "Title Large");
    iui_text_title_medium(ctx, IUI_ALIGN_LEFT, "Title Medium");
    iui_text_title_small(ctx, IUI_ALIGN_LEFT, "Title Small");
    iui_text_body_large(ctx, IUI_ALIGN_LEFT, "Body Large");
    iui_text_body_medium(ctx, IUI_ALIGN_LEFT, "Body Medium");
    iui_text_body_small(ctx, IUI_ALIGN_LEFT, "Body Small");
    iui_text_label_large(ctx, IUI_ALIGN_LEFT, "Label Large");
    iui_text_label_medium(ctx, IUI_ALIGN_LEFT, "Label Medium");
    iui_text_label_small(ctx, IUI_ALIGN_LEFT, "Label Small");

    iui_text_headline_small(ctx, IUI_ALIGN_CENTER, "Value: %d", 42);
    iui_text_body_medium(ctx, IUI_ALIGN_RIGHT, "Float: %.2f", 3.14f);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    ASSERT_TRUE(g_draw_box_calls >= 2);

    free(buffer);
    PASS();
}

/* Shape Tokens Tests */

static void test_shape_tokens(void)
{
    TEST(shape_tokens);

    ASSERT_EQ(iui_shape_tokens_default.none, 0.f);
    ASSERT_EQ(iui_shape_tokens_default.extra_small, 2.f);
    ASSERT_EQ(iui_shape_tokens_default.small, 4.f);
    ASSERT_EQ(iui_shape_tokens_default.medium, 8.f);
    ASSERT_EQ(iui_shape_tokens_default.large, 12.f);
    ASSERT_EQ(iui_shape_tokens_default.extra_large, 16.f);

    ASSERT_EQ(iui_shape_tokens_compact.none, 0.f);
    ASSERT_EQ(iui_shape_tokens_compact.small, 2.f);
    ASSERT_EQ(iui_shape_tokens_compact.medium, 4.f);

    PASS();
}

static void test_typography_scale_values(void)
{
    TEST(typography_scale_values);

    ASSERT_EQ(iui_typography_scale_default.headline_small, 24.f);
    ASSERT_EQ(iui_typography_scale_default.title_large, 22.f);
    ASSERT_EQ(iui_typography_scale_default.title_medium, 16.f);
    ASSERT_EQ(iui_typography_scale_default.title_small, 14.f);
    ASSERT_EQ(iui_typography_scale_default.body_large, 16.f);
    ASSERT_EQ(iui_typography_scale_default.body_medium, 14.f);
    ASSERT_EQ(iui_typography_scale_default.body_small, 12.f);
    ASSERT_EQ(iui_typography_scale_default.label_large, 14.f);
    ASSERT_EQ(iui_typography_scale_default.label_medium, 12.f);
    ASSERT_EQ(iui_typography_scale_default.label_small, 11.f);

    ASSERT_TRUE(iui_typography_scale_dense.headline_small <
                iui_typography_scale_default.headline_small);
    ASSERT_TRUE(iui_typography_scale_dense.body_medium <
                iui_typography_scale_default.body_medium);

    PASS();
}

static void test_fab_extended_functions(void)
{
    TEST(fab_extended_functions);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    /* Test different FAB sizes */
    bool clicked = iui_fab(ctx, 10, 10, "add");
    ASSERT_FALSE(clicked);

    clicked = iui_fab_large(ctx, 10, 100, "star");
    ASSERT_FALSE(clicked);

    clicked = iui_fab_extended(ctx, 10, 200, "add", "Create");
    ASSERT_FALSE(clicked);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_tab_functions(void)
{
    TEST(tab_functions);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    const char *labels[] = {"Tab 1", "Tab 2", "Tab 3"};
    const char *icons[] = {"home", "search", "settings"};

    /* Test primary tabs */
    int selected = iui_tabs(ctx, 0, 3, labels);
    ASSERT_EQ(selected, 0);

    /* Test tabs with icons */
    selected = iui_tabs_with_icons(ctx, 0, 3, labels, icons);
    ASSERT_EQ(selected, 0);

    /* Test secondary tabs */
    selected = iui_tabs_secondary(ctx, 0, 3, labels);
    ASSERT_EQ(selected, 0);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_search_bar_functions(void)
{
    TEST(search_bar_functions);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    char search_buffer[64] = "";
    size_t cursor = 0;

    /* Test basic search bar */
    bool submitted = iui_search_bar(ctx, search_buffer, sizeof(search_buffer),
                                    &cursor, "Search...");
    ASSERT_FALSE(submitted);

    /* Test extended search bar */
    iui_search_bar_result result =
        iui_search_bar_ex(ctx, search_buffer, sizeof(search_buffer), &cursor,
                          "Search", "search", "clear");
    ASSERT_FALSE(result.value_changed);
    ASSERT_FALSE(result.submitted);
    ASSERT_FALSE(result.cleared);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_date_time_picker_functions(void)
{
    TEST(date_time_picker_functions);

    /* Test date picker state functions */
    iui_date_picker_state date_picker = {0};
    ASSERT_FALSE(iui_date_picker_is_open(&date_picker));

    iui_date_picker_show(&date_picker, 2023, 12, 25);
    ASSERT_TRUE(date_picker.is_open);
    ASSERT_EQ(date_picker.year, 2023);
    ASSERT_EQ(date_picker.month, 12);
    ASSERT_EQ(date_picker.day, 25);

    iui_date_picker_close(&date_picker);
    ASSERT_FALSE(date_picker.is_open);

    /* Test time picker state functions */
    iui_time_picker_state time_picker = {0};
    ASSERT_FALSE(iui_time_picker_is_open(&time_picker));

    /* 14:30 in 12H format -> 2:30 PM */
    iui_time_picker_show(&time_picker, 14, 30, false);
    ASSERT_TRUE(time_picker.is_open);
    ASSERT_EQ(time_picker.hour, 2); /* 14 % 12 = 2 (2 PM in 12H format) */
    ASSERT_EQ(time_picker.minute, 30);
    ASSERT_FALSE(time_picker.use_24h);

    iui_time_picker_close(&time_picker);
    ASSERT_FALSE(time_picker.is_open);

    PASS();
}

/* Test Suite Runner */

void run_new_component_tests(void)
{
    SECTION_BEGIN("New Component Extensions");
    test_textfield_variants();
    test_switch_component();
    test_card_components();
    test_progress_indicators();
    test_button_styled_variants();
    test_typography_scale();
    test_shape_tokens();
    test_typography_scale_values();
    test_fab_extended_functions();
    test_tab_functions();
    test_search_bar_functions();
    test_date_time_picker_functions();
    SECTION_END();
}
