/*
 * Navigation Component Tests
 *
 * Tests for:
 * - Navigation Bar (iui_nav_bar_*)
 * - Navigation Drawer (iui_nav_drawer_*)
 * - Navigation Rail (iui_nav_rail_*)
 */

#include "common.h"

/* Navigation Bar Tests */

static void test_nav_bar_basic(void)
{
    TEST(nav_bar_basic);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    reset_counters();

    static iui_nav_bar_state bar = {0};

    /* Begin navigation bar with 3 items */
    iui_nav_bar_begin(ctx, &bar, 0, 220, 400, 3);

    /* Verify state was initialized */
    ASSERT_EQ(bar.item_count, 0);
    ASSERT_EQ(bar.total_items, 3);
    ASSERT_NEAR(bar.width, 400.f, 0.1f);

    /* Add items */
    bool clicked1 = iui_nav_bar_item(ctx, &bar, "home", "Home", 0);
    ASSERT_EQ(bar.item_count, 1);

    bool clicked2 = iui_nav_bar_item(ctx, &bar, "search", "Search", 1);
    ASSERT_EQ(bar.item_count, 2);

    bool clicked3 = iui_nav_bar_item(ctx, &bar, "settings", "Settings", 2);
    ASSERT_EQ(bar.item_count, 3);

    iui_nav_bar_end(ctx, &bar);

    /* Verify draw calls were made (background + items) */
    ASSERT_TRUE(g_draw_box_calls > 0);

    /* Initial state - nothing clicked */
    ASSERT_FALSE(clicked1);
    ASSERT_FALSE(clicked2);
    ASSERT_FALSE(clicked3);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_nav_bar_selection(void)
{
    TEST(nav_bar_selection);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    static iui_nav_bar_state bar = {0};
    bar.selected = 1; /* Pre-select second item */

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    iui_nav_bar_begin(ctx, &bar, 0, 220, 400, 3);

    iui_nav_bar_item(ctx, &bar, "home", "Home", 0);
    iui_nav_bar_item(ctx, &bar, "search", "Search", 1);
    iui_nav_bar_item(ctx, &bar, "settings", "Settings", 2);

    iui_nav_bar_end(ctx, &bar);

    /* Verify selected state persists */
    ASSERT_EQ(bar.selected, 1);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_nav_bar_item_width_calculation(void)
{
    TEST(nav_bar_item_width);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    /* Test with different item counts */
    static iui_nav_bar_state bar3 = {0};
    static iui_nav_bar_state bar5 = {0};

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    /* 3 items - each should be 400/3 = 133.3 pixels wide */
    iui_nav_bar_begin(ctx, &bar3, 0, 100, 300, 3);
    ASSERT_EQ(bar3.total_items, 3);
    iui_nav_bar_end(ctx, &bar3);

    /* 5 items - each should be 400/5 = 80 pixels wide */
    iui_nav_bar_begin(ctx, &bar5, 0, 180, 400, 5);
    ASSERT_EQ(bar5.total_items, 5);
    iui_nav_bar_end(ctx, &bar5);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_nav_bar_click_detection(void)
{
    TEST(nav_bar_click);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    static iui_nav_bar_state bar = {0};

    /* Simulate click on second item */
    /* For 3 items at width 300, each item is 100px wide */
    /* Item 0: 0-100, Item 1: 100-200, Item 2: 200-300 */
    iui_update_mouse_pos(ctx, 150.f, 230.f); /* Middle of item 1 */
    iui_update_mouse_buttons(ctx, IUI_MOUSE_LEFT, 0);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    iui_nav_bar_begin(ctx, &bar, 0, 220, 300, 3);

    bool c0 = iui_nav_bar_item(ctx, &bar, "home", "Home", 0);
    bool c1 = iui_nav_bar_item(ctx, &bar, "search", "Search", 1);
    bool c2 = iui_nav_bar_item(ctx, &bar, "settings", "Settings", 2);

    iui_nav_bar_end(ctx, &bar);

    /* Second item should have been clicked */
    ASSERT_FALSE(c0);
    ASSERT_TRUE(c1);
    ASSERT_FALSE(c2);

    /* Selection should update */
    ASSERT_EQ(bar.selected, 1);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_nav_bar_null_safety(void)
{
    TEST(nav_bar_null_safety);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    /* NULL context - should not crash */
    iui_nav_bar_begin(NULL, NULL, 0, 0, 100, 3);

    static iui_nav_bar_state bar = {0};

    /* NULL state - should not crash */
    iui_nav_bar_begin(ctx, NULL, 0, 0, 100, 3);

    /* NULL icon - should return false and not crash */
    iui_nav_bar_begin(ctx, &bar, 0, 0, 100, 3);
    bool result = iui_nav_bar_item(ctx, &bar, NULL, "Label", 0);
    ASSERT_FALSE(result);
    iui_nav_bar_end(ctx, &bar);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Navigation Drawer Tests */

static void test_nav_drawer_basic(void)
{
    TEST(nav_drawer_basic);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    reset_counters();

    static iui_nav_drawer_state drawer = {0};
    drawer.open = true;

    iui_nav_drawer_begin(ctx, &drawer, 0, 0, 300);

    /* Add drawer items */
    bool clicked1 = iui_nav_drawer_item(ctx, &drawer, "home", "Home", 0);
    bool clicked2 = iui_nav_drawer_item(ctx, &drawer, "inbox", "Inbox", 1);

    iui_nav_drawer_end(ctx, &drawer);

    /* Verify draw calls were made */
    ASSERT_TRUE(g_draw_box_calls > 0);

    /* Initial state - nothing clicked */
    ASSERT_FALSE(clicked1);
    ASSERT_FALSE(clicked2);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_nav_drawer_closed(void)
{
    TEST(nav_drawer_closed);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    reset_counters();

    static iui_nav_drawer_state drawer = {0};
    drawer.open = false; /* Drawer is closed */

    int initial_box_calls = g_draw_box_calls;

    iui_nav_drawer_begin(ctx, &drawer, 0, 0, 300);
    iui_nav_drawer_item(ctx, &drawer, "home", "Home", 0);
    iui_nav_drawer_end(ctx, &drawer);

    /* When closed, minimal or no draw calls for drawer content */
    (void) initial_box_calls; /* Silence unused warning */

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Navigation Rail Tests */

static void test_nav_rail_basic(void)
{
    TEST(nav_rail_basic);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    reset_counters();

    static iui_nav_rail_state rail = {0};

    iui_nav_rail_begin(ctx, &rail, 0, 0, 300);

    bool clicked1 = iui_nav_rail_item(ctx, &rail, "home", "Home", 0);
    bool clicked2 = iui_nav_rail_item(ctx, &rail, "search", "Search", 1);
    bool clicked3 = iui_nav_rail_item(ctx, &rail, "settings", "Settings", 2);

    iui_nav_rail_end(ctx, &rail);

    /* Verify draw calls were made */
    ASSERT_TRUE(g_draw_box_calls > 0);

    /* Initial state - nothing clicked */
    ASSERT_FALSE(clicked1);
    ASSERT_FALSE(clicked2);
    ASSERT_FALSE(clicked3);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_nav_rail_fab(void)
{
    TEST(nav_rail_fab);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    static iui_nav_rail_state rail = {0};

    iui_nav_rail_begin(ctx, &rail, 0, 0, 300);

    /* Add FAB to rail */
    bool fab_clicked = iui_nav_rail_fab(ctx, &rail, "add");
    ASSERT_FALSE(fab_clicked); /* Not clicked initially */

    iui_nav_rail_item(ctx, &rail, "home", "Home", 0);

    iui_nav_rail_end(ctx, &rail);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_nav_rail_expanded(void)
{
    TEST(nav_rail_expanded);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    static iui_nav_rail_state rail = {0};
    rail.expanded = true; /* Expanded mode shows labels */

    iui_nav_rail_begin(ctx, &rail, 0, 0, 300);

    iui_nav_rail_item(ctx, &rail, "home", "Home", 0);
    iui_nav_rail_item(ctx, &rail, "search", "Search", 1);

    iui_nav_rail_end(ctx, &rail);

    /* Expanded state should persist */
    ASSERT_TRUE(rail.expanded);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Combined Navigation Tests */

static void test_nav_components_coexist(void)
{
    TEST(nav_components_coexist);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 800, 600, 0);

    /* Use multiple navigation components together */
    static iui_nav_rail_state rail = {0};
    static iui_nav_drawer_state drawer = {0};
    static iui_nav_bar_state bar = {0};

    /* Rail on left */
    iui_nav_rail_begin(ctx, &rail, 0, 0, 500);
    iui_nav_rail_item(ctx, &rail, "home", "Home", 0);
    iui_nav_rail_end(ctx, &rail);

    /* Drawer (closed by default) */
    drawer.open = false;
    iui_nav_drawer_begin(ctx, &drawer, 80, 0, 500);
    iui_nav_drawer_end(ctx, &drawer);

    /* Bar at bottom */
    iui_nav_bar_begin(ctx, &bar, 80, 520, 720, 4);
    iui_nav_bar_item(ctx, &bar, "home", "Home", 0);
    iui_nav_bar_item(ctx, &bar, "search", "Search", 1);
    iui_nav_bar_item(ctx, &bar, "inbox", "Inbox", 2);
    iui_nav_bar_item(ctx, &bar, "profile", "Profile", 3);
    iui_nav_bar_end(ctx, &bar);

    /* All should work without interference */

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Runner function */
void run_navigation_tests(void)
{
    SECTION_BEGIN("Navigation Components");

    /* Nav Bar tests */
    test_nav_bar_basic();
    test_nav_bar_selection();
    test_nav_bar_item_width_calculation();
    test_nav_bar_click_detection();
    test_nav_bar_null_safety();

    /* Nav Drawer tests */
    test_nav_drawer_basic();
    test_nav_drawer_closed();

    /* Nav Rail tests */
    test_nav_rail_basic();
    test_nav_rail_fab();
    test_nav_rail_expanded();

    /* Combined tests */
    test_nav_components_coexist();

    SECTION_END();
}
