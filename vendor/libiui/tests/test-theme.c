/*
 * Theme System Tests
 *
 * Tests for theme switching and color scheme validation.
 */

#include "common.h"

/* Theme Tests */

static void test_theme_light_dark(void)
{
    TEST(theme_light_dark);

    const iui_theme_t *light = iui_theme_light();
    const iui_theme_t *dark = iui_theme_dark();

    ASSERT_NOT_NULL(light);
    ASSERT_NOT_NULL(dark);

    uint32_t light_surface_r = (light->surface >> 16) & 0xFF;
    uint32_t dark_surface_r = (dark->surface >> 16) & 0xFF;
    ASSERT_TRUE(light_surface_r > dark_surface_r);

    PASS();
}

static void test_theme_switching(void)
{
    TEST(theme_switching);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    const iui_theme_t *light = iui_theme_light();
    const iui_theme_t *dark = iui_theme_dark();

    iui_set_theme(ctx, light);
    const iui_theme_t *current = iui_get_theme(ctx);
    ASSERT_EQ(current->surface, light->surface);

    iui_set_theme(ctx, dark);
    current = iui_get_theme(ctx);
    ASSERT_EQ(current->surface, dark->surface);

    iui_set_theme(ctx, NULL);
    current = iui_get_theme(ctx);
    ASSERT_EQ(current->surface, dark->surface);

    free(buffer);
    PASS();
}

static void test_theme_render_consistency(void)
{
    TEST(theme_render_consistency);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    const iui_theme_t *dark = iui_theme_dark();
    iui_set_theme(ctx, dark);

    reset_counters();
    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);
    iui_button(ctx, "Dark Button", IUI_ALIGN_CENTER);
    iui_end_window(ctx);
    iui_end_frame(ctx);

    ASSERT_TRUE(g_draw_box_calls > 0);

    free(buffer);
    PASS();
}

static void test_extended_color_scheme(void)
{
    TEST(extended_color_scheme);

    const iui_theme_t *light = iui_theme_light();
    const iui_theme_t *dark = iui_theme_dark();

    /* Verify all new color tokens exist and are non-zero (alpha channel set)
     * Secondary container group
     */
    ASSERT_TRUE((light->secondary_container & 0xFF000000) != 0);
    ASSERT_TRUE((light->on_secondary_container & 0xFF000000) != 0);
    ASSERT_TRUE((dark->secondary_container & 0xFF000000) != 0);
    ASSERT_TRUE((dark->on_secondary_container & 0xFF000000) != 0);

    /* Tertiary color group */
    ASSERT_TRUE((light->tertiary & 0xFF000000) != 0);
    ASSERT_TRUE((light->on_tertiary & 0xFF000000) != 0);
    ASSERT_TRUE((light->tertiary_container & 0xFF000000) != 0);
    ASSERT_TRUE((light->on_tertiary_container & 0xFF000000) != 0);
    ASSERT_TRUE((dark->tertiary & 0xFF000000) != 0);
    ASSERT_TRUE((dark->on_tertiary & 0xFF000000) != 0);
    ASSERT_TRUE((dark->tertiary_container & 0xFF000000) != 0);
    ASSERT_TRUE((dark->on_tertiary_container & 0xFF000000) != 0);

    /* Surface variant group */
    ASSERT_TRUE((light->surface_variant & 0xFF000000) != 0);
    ASSERT_TRUE((light->on_surface_variant & 0xFF000000) != 0);
    ASSERT_TRUE((dark->surface_variant & 0xFF000000) != 0);
    ASSERT_TRUE((dark->on_surface_variant & 0xFF000000) != 0);

    /* 5-level surface container hierarchy */
    ASSERT_TRUE((light->surface_container_lowest & 0xFF000000) != 0);
    ASSERT_TRUE((light->surface_container_low & 0xFF000000) != 0);
    ASSERT_TRUE((light->surface_container_highest & 0xFF000000) != 0);
    ASSERT_TRUE((dark->surface_container_lowest & 0xFF000000) != 0);
    ASSERT_TRUE((dark->surface_container_low & 0xFF000000) != 0);
    ASSERT_TRUE((dark->surface_container_highest & 0xFF000000) != 0);

    /* Error container group */
    ASSERT_TRUE((light->error_container & 0xFF000000) != 0);
    ASSERT_TRUE((light->on_error_container & 0xFF000000) != 0);
    ASSERT_TRUE((dark->error_container & 0xFF000000) != 0);
    ASSERT_TRUE((dark->on_error_container & 0xFF000000) != 0);

    /* Utility colors */
    ASSERT_TRUE((light->shadow & 0xFF000000) != 0);
    ASSERT_TRUE((light->scrim & 0xFF000000) != 0);
    ASSERT_TRUE((light->inverse_surface & 0xFF000000) != 0);
    ASSERT_TRUE((light->inverse_on_surface & 0xFF000000) != 0);
    ASSERT_TRUE((light->inverse_primary & 0xFF000000) != 0);
    ASSERT_TRUE((dark->shadow & 0xFF000000) != 0);
    ASSERT_TRUE((dark->scrim & 0xFF000000) != 0);
    ASSERT_TRUE((dark->inverse_surface & 0xFF000000) != 0);
    ASSERT_TRUE((dark->inverse_on_surface & 0xFF000000) != 0);
    ASSERT_TRUE((dark->inverse_primary & 0xFF000000) != 0);

    /* Verify surface container elevation hierarchy (light: decreasing
     * luminance) */
    uint32_t light_lowest_r = (light->surface_container_lowest >> 16) & 0xFF;
    uint32_t light_low_r = (light->surface_container_low >> 16) & 0xFF;
    uint32_t light_mid_r = (light->surface_container >> 16) & 0xFF;
    uint32_t light_high_r = (light->surface_container_high >> 16) & 0xFF;
    uint32_t light_highest_r = (light->surface_container_highest >> 16) & 0xFF;
    ASSERT_TRUE(light_lowest_r >= light_low_r);
    ASSERT_TRUE(light_low_r >= light_mid_r);
    ASSERT_TRUE(light_mid_r >= light_high_r);
    ASSERT_TRUE(light_high_r >= light_highest_r);

    /* Verify surface container elevation hierarchy (dark: increasing luminance)
     */
    uint32_t dark_lowest_r = (dark->surface_container_lowest >> 16) & 0xFF;
    uint32_t dark_low_r = (dark->surface_container_low >> 16) & 0xFF;
    uint32_t dark_mid_r = (dark->surface_container >> 16) & 0xFF;
    uint32_t dark_high_r = (dark->surface_container_high >> 16) & 0xFF;
    uint32_t dark_highest_r = (dark->surface_container_highest >> 16) & 0xFF;
    ASSERT_TRUE(dark_lowest_r <= dark_low_r);
    ASSERT_TRUE(dark_low_r <= dark_mid_r);
    ASSERT_TRUE(dark_mid_r <= dark_high_r);
    ASSERT_TRUE(dark_high_r <= dark_highest_r);

    PASS();
}

/* Test Suite Runner */
void run_theme_tests(void)
{
    SECTION_BEGIN("Theme System");
    test_theme_light_dark();
    test_theme_switching();
    test_theme_render_consistency();
    test_extended_color_scheme();
    SECTION_END();
}
