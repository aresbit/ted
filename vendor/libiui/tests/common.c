/*
 * Test Infrastructure Implementation
 *
 * Shared test counters, mock renderers, and context factory.
 */

#include "common.h"

/* Test Counters */

int g_tests_run = 0, g_tests_passed = 0, g_tests_failed = 0;
int g_verbose = 0;

/* Renderer callback counters */
int g_draw_box_calls = 0, g_draw_text_calls = 0, g_set_clip_calls = 0;
int g_draw_line_calls = 0, g_draw_circle_calls = 0, g_draw_arc_calls = 0;

/* Last call parameters for verification */
float g_last_box_x, g_last_box_y, g_last_box_w, g_last_box_h, g_last_box_radius;
uint32_t g_last_box_color;

/* Extended last call parameters for text */
float g_last_text_x, g_last_text_y;
char g_last_text_content[256];
uint32_t g_last_text_color;

/* Extended last call parameters for clip */
uint16_t g_last_clip_min_x, g_last_clip_min_y;
uint16_t g_last_clip_max_x, g_last_clip_max_y;

/* Extended last call parameters for primitives */
float g_last_line_x0, g_last_line_y0, g_last_line_x1, g_last_line_y1;
float g_last_line_width;
uint32_t g_last_line_color;

float g_last_circle_cx, g_last_circle_cy, g_last_circle_radius;
uint32_t g_last_circle_fill, g_last_circle_stroke;
float g_last_circle_stroke_w;

float g_last_arc_cx, g_last_arc_cy, g_last_arc_radius;
float g_last_arc_start, g_last_arc_end, g_last_arc_width;
uint32_t g_last_arc_color;

/* Section tracking */
int g_section_failed;
const char *g_section_name;

/* Mock Renderer Callbacks */

void mock_draw_box(iui_rect_t rect, float r, uint32_t color, void *user)
{
    (void) user;
    g_draw_box_calls++;
    g_last_box_x = rect.x;
    g_last_box_y = rect.y;
    g_last_box_w = rect.width;
    g_last_box_h = rect.height;
    g_last_box_radius = r;
    g_last_box_color = color;
    if (g_verbose > 1) {
        printf("draw_box(%.1f, %.1f, %.1f, %.1f, %.1f, 0x%08X)\n", rect.x,
               rect.y, rect.width, rect.height, r, color);
    }
}

void mock_draw_text(float x,
                    float y,
                    const char *text,
                    uint32_t color,
                    void *user)
{
    (void) user;
    g_draw_text_calls++;
    g_last_text_x = x, g_last_text_y = y;
    g_last_text_color = color;

    if (text) {
        strncpy(g_last_text_content, text, sizeof(g_last_text_content) - 1);
        g_last_text_content[sizeof(g_last_text_content) - 1] = '\0';
    } else {
        g_last_text_content[0] = '\0';
    }

    if (g_verbose > 1) {
        printf("draw_text(%.1f, %.1f, \"%s\", 0x%08X)\n", x, y,
               text ? text : "(null)", color);
    }
}

void mock_set_clip(uint16_t min_x,
                   uint16_t min_y,
                   uint16_t max_x,
                   uint16_t max_y,
                   void *user)
{
    (void) user;
    g_set_clip_calls++;
    g_last_clip_min_x = min_x, g_last_clip_min_y = min_y;
    g_last_clip_max_x = max_x, g_last_clip_max_y = max_y;
    if (g_verbose > 1)
        printf("set_clip_rect(%u, %u, %u, %u)\n", min_x, min_y, max_x, max_y);
}

float mock_text_width(const char *text, void *user)
{
    (void) user;
    if (!text)
        return 0.0f;
    return 8.0f * (float) strlen(text);
}

void mock_draw_line(float x0,
                    float y0,
                    float x1,
                    float y1,
                    float width,
                    uint32_t color,
                    void *user)
{
    (void) user;
    g_draw_line_calls++;
    g_last_line_x0 = x0, g_last_line_y0 = y0;
    g_last_line_x1 = x1, g_last_line_y1 = y1;
    g_last_line_width = width;
    g_last_line_color = color;
}

void mock_draw_circle(float cx,
                      float cy,
                      float radius,
                      uint32_t fill,
                      uint32_t stroke,
                      float stroke_w,
                      void *user)
{
    (void) user;
    g_draw_circle_calls++;
    g_last_circle_cx = cx;
    g_last_circle_cy = cy;
    g_last_circle_radius = radius;
    g_last_circle_fill = fill;
    g_last_circle_stroke = stroke;
    g_last_circle_stroke_w = stroke_w;
}

void mock_draw_arc(float cx,
                   float cy,
                   float radius,
                   float start,
                   float end,
                   float width,
                   uint32_t color,
                   void *user)
{
    (void) user;
    g_draw_arc_calls++;
    g_last_arc_cx = cx;
    g_last_arc_cy = cy;
    g_last_arc_radius = radius;
    g_last_arc_start = start;
    g_last_arc_end = end;
    g_last_arc_width = width;
    g_last_arc_color = color;
}

void reset_counters(void)
{
    g_draw_box_calls = 0;
    g_draw_text_calls = 0;
    g_set_clip_calls = 0;
    g_draw_line_calls = 0;
    g_draw_circle_calls = 0;
    g_draw_arc_calls = 0;
}

/* Test Context Factory */

iui_context *create_test_context(void *buffer, bool with_vector_prims)
{
    iui_config_t config = {
        .buffer = buffer,
        .font_height = 16.0f,
        .renderer =
            {
                .draw_box = mock_draw_box,
                .draw_text = mock_draw_text,
                .set_clip_rect = mock_set_clip,
                .text_width = mock_text_width,
                .draw_line = with_vector_prims ? mock_draw_line : NULL,
                .draw_circle = with_vector_prims ? mock_draw_circle : NULL,
                .draw_arc = with_vector_prims ? mock_draw_arc : NULL,
                .user = NULL,
            },
        .vector = NULL,
    };
    return iui_init(&config);
}

/* Interaction Simulation Helpers */

void test_simulate_click(iui_context *ctx, float x, float y)
{
    iui_update_mouse_pos(ctx, x, y);
    iui_update_mouse_buttons(ctx, IUI_MOUSE_LEFT, 0);
    iui_update_mouse_buttons(ctx, 0, IUI_MOUSE_LEFT);
}

void test_simulate_click_frames(iui_context *ctx,
                                float x,
                                float y,
                                float delta_time)
{
    /* Frame 1: Move and press */
    iui_update_mouse_pos(ctx, x, y);
    iui_update_mouse_buttons(ctx, IUI_MOUSE_LEFT, 0);
    iui_begin_frame(ctx, delta_time);
    iui_end_frame(ctx);

    /* Frame 2: Release */
    iui_update_mouse_buttons(ctx, 0, IUI_MOUSE_LEFT);
}

void test_simulate_drag(iui_context *ctx,
                        float x0,
                        float y0,
                        float x1,
                        float y1,
                        float delta_time)
{
    /* Start position and press */
    iui_update_mouse_pos(ctx, x0, y0);
    iui_update_mouse_buttons(ctx, IUI_MOUSE_LEFT, 0);
    iui_begin_frame(ctx, delta_time);
    iui_end_frame(ctx);

    /* Move to end position */
    iui_update_mouse_pos(ctx, x1, y1);
    iui_begin_frame(ctx, delta_time);
    iui_end_frame(ctx);

    /* Release */
    iui_update_mouse_buttons(ctx, 0, IUI_MOUSE_LEFT);
}
