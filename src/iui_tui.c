/**
 * iui_tui.c - libiui terminal backend bridge
 */

#include "iui_tui.h"
#include "iui.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#define IUI_TUI_CELL_W 8.0f
#define IUI_TUI_CELL_H 16.0f

typedef struct {
    c8 ch;
    u8 fg;
    u8 bg;
} tui_cell_t;

typedef struct {
    const c8 *name;
    iui_theme_t theme;
} tui_theme_preset_t;

typedef struct {
    bool ready;
    bool focused;
    u8 active_tab;
    u32 active_theme;
    u32 cols;
    u32 rows;
    u32 pixel_w;
    u32 pixel_h;
    u16 clip_min_col;
    u16 clip_min_row;
    u16 clip_max_col;
    u16 clip_max_row;
    tui_cell_t *cells;
    void *iui_mem;
    size_t iui_mem_size;
    iui_context *ctx;
    iui_renderer_t renderer;
    tui_theme_preset_t presets[3];
    u8 default_fg;
    u8 default_bg;
    float mouse_x;
    float mouse_y;
    u8 mouse_pressed;
    u8 mouse_released;
    float frame_dt;
} iui_tui_state_t;

static iui_tui_state_t S = {0};

static u8 clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (u8)v;
}

static u8 rgb_to_ansi256(u8 r, u8 g, u8 b) {
    if (r == g && g == b) {
        if (r < 8) return 16;
        if (r > 248) return 231;
        return (u8)(232 + (r - 8) / 10);
    }
    u8 rc = (u8)((r * 5) / 255);
    u8 gc = (u8)((g * 5) / 255);
    u8 bc = (u8)((b * 5) / 255);
    return (u8)(16 + (36 * rc) + (6 * gc) + bc);
}

static u8 srgb_to_ansi256(u32 color) {
    u8 r = (u8)((color >> 16) & 0xFF);
    u8 g = (u8)((color >> 8) & 0xFF);
    u8 b = (u8)(color & 0xFF);
    return rgb_to_ansi256(r, g, b);
}

static u8 color_alpha(u32 color) {
    return (u8)((color >> 24) & 0xFF);
}

static bool color_is_pure_black(u32 color) {
    return (color & 0x00FFFFFFu) == 0;
}

static u8 bg_srgb_to_ansi256(u32 color) {
    u8 bg = srgb_to_ansi256(color);
    // Avoid accidental "always-black panel" caused by 256-color quantization:
    // if source isn't true black but collapsed to ANSI 16, lift to deep gray.
    if (bg == 16 && !color_is_pure_black(color)) {
        return 235;
    }
    return bg;
}

static int px_to_col(float x) {
    return (int)floorf(x / IUI_TUI_CELL_W);
}

static int px_to_row(float y) {
    return (int)floorf(y / IUI_TUI_CELL_H);
}

static void clip_rect_apply(int *min_col, int *min_row, int *max_col, int *max_row) {
    if (*min_col < (int)S.clip_min_col) *min_col = (int)S.clip_min_col;
    if (*min_row < (int)S.clip_min_row) *min_row = (int)S.clip_min_row;
    if (*max_col > (int)S.clip_max_col) *max_col = (int)S.clip_max_col;
    if (*max_row > (int)S.clip_max_row) *max_row = (int)S.clip_max_row;
}

static float clampf_local(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static tui_cell_t *cell_at(int col, int row) {
    if (col < 0 || row < 0) return SP_NULLPTR;
    if ((u32)col >= S.cols || (u32)row >= S.rows) return SP_NULLPTR;
    return &S.cells[row * S.cols + col];
}

static void tui_clear_cells(void) {
    if (!S.cells) return;
    for (u32 i = 0; i < S.cols * S.rows; i++) {
        S.cells[i].ch = ' ';
        S.cells[i].fg = S.default_fg;
        S.cells[i].bg = S.default_bg;
    }
}

static void tui_plot_cell_fg(int col, int row, c8 ch, u32 color) {
    tui_cell_t *cell = cell_at(col, row);
    if (!cell) return;
    if (col < (int)S.clip_min_col || col >= (int)S.clip_max_col) return;
    if (row < (int)S.clip_min_row || row >= (int)S.clip_max_row) return;
    if (color != 0 && color_alpha(color) < 0x40) return;
    cell->ch = ch;
    cell->fg = (color == 0) ? S.default_fg : srgb_to_ansi256(color);
}

static float tui_point_to_segment_distance(float px, float py, float x0, float y0, float x1, float y1) {
    float vx = x1 - x0;
    float vy = y1 - y0;
    float denom = vx * vx + vy * vy;
    float t = 0.0f;
    if (denom > 1e-6f) {
        t = ((px - x0) * vx + (py - y0) * vy) / denom;
        t = clampf_local(t, 0.0f, 1.0f);
    }
    float qx = x0 + t * vx;
    float qy = y0 + t * vy;
    float dx = px - qx;
    float dy = py - qy;
    return sqrtf(dx * dx + dy * dy);
}

static c8 tui_vector_glyph(float d) {
    if (d < 1.75f) return '.';
    return ' ';
}

static void tui_draw_line(float x0, float y0, float x1, float y1, float width, u32 color, void *user) {
    (void)user;
    float pad = fmaxf(1.0f, width * 0.5f + 1.0f);
    int min_col = px_to_col(fminf(x0, x1) - pad);
    int min_row = px_to_row(fminf(y0, y1) - pad);
    int max_col = (int)ceilf((fmaxf(x0, x1) + pad) / IUI_TUI_CELL_W);
    int max_row = (int)ceilf((fmaxf(y0, y1) + pad) / IUI_TUI_CELL_H);
    clip_rect_apply(&min_col, &min_row, &max_col, &max_row);

    for (int row = min_row; row < max_row; row++) {
        for (int col = min_col; col < max_col; col++) {
            float cx = ((float)col + 0.5f) * IUI_TUI_CELL_W;
            float cy = ((float)row + 0.5f) * IUI_TUI_CELL_H;
            float d = tui_point_to_segment_distance(cx, cy, x0, y0, x1, y1);
            c8 glyph = tui_vector_glyph(d - width * 0.5f);
            if (glyph != ' ') {
                tui_plot_cell_fg(col, row, glyph, color);
            }
        }
    }
}

static void tui_draw_circle(float cx, float cy, float radius, u32 fill_color, u32 stroke_color, float stroke_width, void *user) {
    (void)user;
    float outer = radius + fmaxf(stroke_width, 1.0f);
    int min_col = px_to_col(cx - outer);
    int min_row = px_to_row(cy - outer);
    int max_col = (int)ceilf((cx + outer) / IUI_TUI_CELL_W);
    int max_row = (int)ceilf((cy + outer) / IUI_TUI_CELL_H);
    clip_rect_apply(&min_col, &min_row, &max_col, &max_row);

    for (int row = min_row; row < max_row; row++) {
        for (int col = min_col; col < max_col; col++) {
            float px = ((float)col + 0.5f) * IUI_TUI_CELL_W;
            float py = ((float)row + 0.5f) * IUI_TUI_CELL_H;
            float dx = px - cx;
            float dy = py - cy;
            float dist = sqrtf(dx * dx + dy * dy);

            if (fill_color != 0 && dist <= radius) {
                tui_plot_cell_fg(col, row, '.', fill_color);
            }
            if (stroke_color != 0) {
                float ring = fabsf(dist - radius);
                c8 glyph = tui_vector_glyph(ring - stroke_width * 0.5f);
                if (glyph != ' ') {
                    tui_plot_cell_fg(col, row, glyph, stroke_color);
                }
            }
        }
    }
}

static bool tui_angle_in_arc(float angle, float start, float end) {
    const float tau = 6.28318530718f;
    while (angle < 0.0f) angle += tau;
    while (start < 0.0f) start += tau;
    while (end < 0.0f) end += tau;
    while (angle >= tau) angle -= tau;
    while (start >= tau) start -= tau;
    while (end >= tau) end -= tau;
    if (start <= end) return angle >= start && angle <= end;
    return angle >= start || angle <= end;
}

static void tui_draw_arc(float cx, float cy, float radius, float start_angle, float end_angle, float width, u32 color, void *user) {
    (void)user;
    float outer = radius + fmaxf(width, 1.0f);
    int min_col = px_to_col(cx - outer);
    int min_row = px_to_row(cy - outer);
    int max_col = (int)ceilf((cx + outer) / IUI_TUI_CELL_W);
    int max_row = (int)ceilf((cy + outer) / IUI_TUI_CELL_H);
    clip_rect_apply(&min_col, &min_row, &max_col, &max_row);

    for (int row = min_row; row < max_row; row++) {
        for (int col = min_col; col < max_col; col++) {
            float px = ((float)col + 0.5f) * IUI_TUI_CELL_W;
            float py = ((float)row + 0.5f) * IUI_TUI_CELL_H;
            float dx = px - cx;
            float dy = py - cy;
            float angle = atan2f(dy, dx);
            if (!tui_angle_in_arc(angle, start_angle, end_angle)) continue;
            float dist = sqrtf(dx * dx + dy * dy);
            c8 glyph = tui_vector_glyph(fabsf(dist - radius) - width * 0.5f);
            if (glyph != ' ') {
                tui_plot_cell_fg(col, row, glyph, color);
            }
        }
    }
}

static void tui_draw_box(iui_rect_t rect, float radius, u32 color, void *user) {
    (void)radius;
    (void)user;
    // libiui uses ARGB. This terminal backend currently has no alpha blending,
    // so skip translucent overlays to avoid black artifacts.
    if (color == 0) return;
    if (color_alpha(color) < 0xF0) return;
    int min_col = px_to_col(rect.x);
    int min_row = px_to_row(rect.y);
    int max_col = (int)ceilf((rect.x + rect.width) / IUI_TUI_CELL_W);
    int max_row = (int)ceilf((rect.y + rect.height) / IUI_TUI_CELL_H);
    clip_rect_apply(&min_col, &min_row, &max_col, &max_row);
    u8 bg = bg_srgb_to_ansi256(color);
    for (int r = min_row; r < max_row; r++) {
        for (int c = min_col; c < max_col; c++) {
            tui_cell_t *cell = cell_at(c, r);
            if (!cell) continue;
            cell->bg = bg;
        }
    }
}

static void tui_draw_text(float x, float y, const char *text, u32 color, void *user) {
    (void)user;
    if (!text) return;
    int col = px_to_col(x);
    int row = px_to_row(y);
    if (row < (int)S.clip_min_row || row >= (int)S.clip_max_row) return;
    if (color != 0 && color_alpha(color) < 0x80) return;
    u8 fg = (color == 0) ? S.default_fg : srgb_to_ansi256(color);
    u32 len = (u32)strlen(text);
    for (u32 i = 0; i < len; i++) {
        int c = col + (int)i;
        if (c < (int)S.clip_min_col || c >= (int)S.clip_max_col) continue;
        tui_cell_t *cell = cell_at(c, row);
        if (!cell) continue;
        c8 ch = text[i];
        cell->ch = (ch >= 32 && ch < 127) ? ch : ' ';
        cell->fg = fg;
    }
}

static void tui_set_clip_rect(u16 min_x, u16 min_y, u16 max_x, u16 max_y, void *user) {
    (void)user;
    if (max_x == UINT16_MAX && max_y == UINT16_MAX) {
        S.clip_min_col = 0;
        S.clip_min_row = 0;
        S.clip_max_col = (u16)S.cols;
        S.clip_max_row = (u16)S.rows;
        return;
    }
    int min_col = px_to_col((float)min_x);
    int min_row = px_to_row((float)min_y);
    int max_col = (int)ceilf((float)max_x / IUI_TUI_CELL_W);
    int max_row = (int)ceilf((float)max_y / IUI_TUI_CELL_H);
    S.clip_min_col = clamp_u8(min_col);
    S.clip_min_row = clamp_u8(min_row);
    S.clip_max_col = clamp_u8(max_col);
    S.clip_max_row = clamp_u8(max_row);
}

static float tui_text_width(const char *text, void *user) {
    (void)user;
    return (float)strlen(text) * IUI_TUI_CELL_W;
}

static const iui_theme_t *tui_active_theme(void) {
    return &S.presets[S.active_theme].theme;
}

static u32 tui_theme_count(void) {
    return (u32)(sizeof(S.presets) / sizeof(S.presets[0]));
}

static s32 tui_theme_index(sp_str_t name) {
    if (name.len == 0) return -1;
    for (u32 i = 0; i < tui_theme_count(); i++) {
        if (sp_str_equal(name, sp_str_from_cstr(S.presets[i].name))) return (s32)i;
    }
    return -1;
}

static void tui_recompute_defaults(void) {
    const iui_theme_t *t = tui_active_theme();
    S.default_fg = srgb_to_ansi256(t->on_surface);
    S.default_bg = srgb_to_ansi256(t->surface);
}

static void tui_presets_init(void) {
    const iui_theme_t *dark = iui_theme_dark();

    // Terminal-native preset: lean on contrast and one cyan accent instead of
    // large MD3 color fields, which quantize poorly in ANSI 256-color mode.
    S.presets[0].name = "cyber";
    S.presets[0].theme = *dark;
    S.presets[0].theme.primary = 0xFF4FE3FF;
    S.presets[0].theme.on_primary = 0xFF061217;
    S.presets[0].theme.primary_container = 0xFF123847;
    S.presets[0].theme.on_primary_container = 0xFFD9F7FF;
    S.presets[0].theme.secondary = 0xFFFFC857;
    S.presets[0].theme.on_secondary = 0xFF0B1116;
    S.presets[0].theme.secondary_container = 0xFF4A3510;
    S.presets[0].theme.on_secondary_container = 0xFFFFF0C7;
    S.presets[0].theme.tertiary = 0xFF7DFFA1;
    S.presets[0].theme.on_tertiary = 0xFF0C120D;
    S.presets[0].theme.tertiary_container = 0xFF193820;
    S.presets[0].theme.on_tertiary_container = 0xFFD8F6DE;
    S.presets[0].theme.surface = 0xFF101114;
    S.presets[0].theme.on_surface = 0xFFE7EDF2;
    S.presets[0].theme.surface_variant = 0xFF1B1E24;
    S.presets[0].theme.on_surface_variant = 0xFFC6D1DA;
    S.presets[0].theme.surface_container_lowest = 0xFF0C0D10;
    S.presets[0].theme.surface_container_low = 0xFF161920;
    S.presets[0].theme.surface_container = 0xFF1D2129;
    S.presets[0].theme.surface_container_high = 0xFF262C36;
    S.presets[0].theme.surface_container_highest = 0xFF303845;
    S.presets[0].theme.outline = 0xFF5DC6E4;
    S.presets[0].theme.outline_variant = 0xFF4A5B69;
    S.presets[0].theme.shadow = 0xFF000000;
    S.presets[0].theme.scrim = 0xAA02040A;
    S.presets[0].theme.inverse_surface = 0xFFE6F8FF;
    S.presets[0].theme.inverse_on_surface = 0xFF0B1118;
    S.presets[0].theme.inverse_primary = 0xFF006B7A;

    S.presets[1] = S.presets[0];
    S.presets[1].name = "amber";
    S.presets[1].theme.primary = 0xFFFFD166;
    S.presets[1].theme.on_primary = 0xFF151008;
    S.presets[1].theme.primary_container = 0xFF4F3A11;
    S.presets[1].theme.on_primary_container = 0xFFFFF0CB;
    S.presets[1].theme.secondary = 0xFFFF9E7A;
    S.presets[1].theme.secondary_container = 0xFF533122;
    S.presets[1].theme.tertiary = 0xFFFFEFC1;
    S.presets[1].theme.tertiary_container = 0xFF3B3421;
    S.presets[1].theme.surface = 0xFF15120E;
    S.presets[1].theme.on_surface = 0xFFF5E8D3;
    S.presets[1].theme.surface_variant = 0xFF221C14;
    S.presets[1].theme.on_surface_variant = 0xFFE6D7BE;
    S.presets[1].theme.surface_container_lowest = 0xFF0E0B08;
    S.presets[1].theme.surface_container_low = 0xFF1B1711;
    S.presets[1].theme.surface_container = 0xFF231E16;
    S.presets[1].theme.surface_container_high = 0xFF2D261D;
    S.presets[1].theme.surface_container_highest = 0xFF382F24;
    S.presets[1].theme.outline = 0xFFE3A64A;
    S.presets[1].theme.outline_variant = 0xFF7F6640;

    S.presets[2] = S.presets[0];
    S.presets[2].name = "mono";
    S.presets[2].theme.primary = 0xFFD1D4D9;
    S.presets[2].theme.on_primary = 0xFF101215;
    S.presets[2].theme.primary_container = 0xFF343A43;
    S.presets[2].theme.on_primary_container = 0xFFE8EDF2;
    S.presets[2].theme.secondary = 0xFFAEB5BF;
    S.presets[2].theme.secondary_container = 0xFF30353C;
    S.presets[2].theme.tertiary = 0xFFCCD0D6;
    S.presets[2].theme.tertiary_container = 0xFF3A3E45;
    S.presets[2].theme.surface = 0xFF0F1114;
    S.presets[2].theme.on_surface = 0xFFE2E5EA;
    S.presets[2].theme.surface_variant = 0xFF191C21;
    S.presets[2].theme.on_surface_variant = 0xFFC4CAD3;
    S.presets[2].theme.surface_container_lowest = 0xFF090B0E;
    S.presets[2].theme.surface_container_low = 0xFF12161A;
    S.presets[2].theme.surface_container = 0xFF1A1F25;
    S.presets[2].theme.surface_container_high = 0xFF232A33;
    S.presets[2].theme.surface_container_highest = 0xFF2E3744;
    S.presets[2].theme.outline = 0xFF9BA5B4;
    S.presets[2].theme.outline_variant = 0xFF596273;
    S.active_theme = 0;
    tui_recompute_defaults();
}

u32 iui_tui_panel_rows(void) { return 3; }

void iui_tui_init(u32 cols, u32 rows) {
    if (S.ready) return;
    S.cols = cols;
    S.rows = rows;
    S.pixel_w = (u32)(cols * IUI_TUI_CELL_W);
    S.pixel_h = (u32)(rows * IUI_TUI_CELL_H);
    S.cells = (tui_cell_t *)calloc(cols * rows, sizeof(tui_cell_t));
    if (!S.cells) return;

    S.iui_mem_size = iui_min_memory_size();
    S.iui_mem = malloc(S.iui_mem_size);
    if (!S.iui_mem) return;

    S.renderer = (iui_renderer_t){
        .draw_box = tui_draw_box,
        .draw_text = tui_draw_text,
        .set_clip_rect = tui_set_clip_rect,
        .text_width = tui_text_width,
        .draw_line = tui_draw_line,
        .draw_circle = tui_draw_circle,
        .draw_arc = tui_draw_arc,
        .user = SP_NULLPTR,
    };

    iui_config_t cfg = iui_make_config(S.iui_mem, S.renderer, 14.0f, SP_NULLPTR);
    S.ctx = iui_init(&cfg);
    if (!S.ctx) return;

    tui_presets_init();
    iui_set_theme(S.ctx, tui_active_theme());
    S.clip_min_col = 0;
    S.clip_min_row = 0;
    S.clip_max_col = (u16)S.cols;
    S.clip_max_row = (u16)S.rows;
    S.ready = true;
    tui_clear_cells();
}

void iui_tui_shutdown(void) {
    if (S.cells) free(S.cells);
    if (S.iui_mem) free(S.iui_mem);
    sp_memset(&S, 0, sizeof(S));
}

void iui_tui_resize(u32 cols, u32 rows) {
    if (!S.ready) {
        iui_tui_init(cols, rows);
        return;
    }
    if (cols == S.cols && rows == S.rows) return;
    tui_cell_t *new_cells = (tui_cell_t *)calloc(cols * rows, sizeof(tui_cell_t));
    if (!new_cells) return;
    free(S.cells);
    S.cells = new_cells;
    S.cols = cols;
    S.rows = rows;
    S.pixel_w = (u32)(cols * IUI_TUI_CELL_W);
    S.pixel_h = (u32)(rows * IUI_TUI_CELL_H);
    S.clip_min_col = 0;
    S.clip_min_row = 0;
    S.clip_max_col = (u16)cols;
    S.clip_max_row = (u16)rows;
    tui_clear_cells();
}

static void iui_feed_key(int key) {
    if (!S.ready || !S.ctx) return;
    iui_update_key(S.ctx, key);
}

bool iui_tui_is_focused(void) {
    return S.focused;
}

bool iui_tui_handle_key(int key) {
    if (!S.ready || !S.ctx) return false;

    if (key == 20) {
        S.focused = !S.focused;
        editor_set_message(S.focused ? "UI focus ON (Tab/Enter/Esc)" : "UI focus OFF");
        return true;
    }

    if (!S.focused) return false;

    switch (key) {
    case '\033':
        S.focused = false;
        editor_set_message("UI focus OFF");
        return true;
    case '\t':
        iui_update_modifiers(S.ctx, IUI_MOD_NONE);
        iui_feed_key(IUI_KEY_TAB);
        return true;
    case KEY_SHIFT_TAB:
        iui_update_modifiers(S.ctx, IUI_MOD_SHIFT);
        iui_feed_key(IUI_KEY_TAB);
        iui_update_modifiers(S.ctx, IUI_MOD_NONE);
        return true;
    case '\r':
    case '\n':
        iui_feed_key(IUI_KEY_ENTER);
        return true;
    case KEY_LEFT:
        iui_feed_key(IUI_KEY_LEFT);
        return true;
    case KEY_RIGHT:
        iui_feed_key(IUI_KEY_RIGHT);
        return true;
    case KEY_UP:
        iui_feed_key(IUI_KEY_UP);
        return true;
    case KEY_DOWN:
        iui_feed_key(IUI_KEY_DOWN);
        return true;
    default:
        if (key >= 32 && key < 127) {
            iui_update_char(S.ctx, key);
        } else {
            iui_feed_key(key);
        }
        return true;
    }
}

bool iui_tui_handle_mouse(u32 term_col_1b, u32 term_row_1b, bool pressed) {
    if (!S.ready) return false;
    u32 start_row_1b = 1;
    u32 end_row_1b = start_row_1b + S.rows - 1;
    if (term_row_1b < start_row_1b || term_row_1b > end_row_1b) return false;
    if (term_col_1b == 0 || term_col_1b > E.screen_cols) return false;

    u32 local_row = term_row_1b - start_row_1b;
    u32 local_col = term_col_1b - 1;
    S.mouse_x = ((float)local_col + 0.5f) * IUI_TUI_CELL_W;
    S.mouse_y = ((float)local_row + 0.5f) * IUI_TUI_CELL_H;
    if (pressed) {
        S.mouse_pressed |= IUI_MOUSE_LEFT;
    } else {
        S.mouse_released |= IUI_MOUSE_LEFT;
    }
    return true;
}

static void copy_sp_str_to_cstr(sp_str_t src, c8 *dst, u32 cap, const c8 *fallback) {
    if (!dst || cap == 0) return;
    if (src.len == 0 || !src.data) {
        snprintf(dst, cap, "%s", fallback ? fallback : "");
        return;
    }
    u32 n = src.len < cap - 1 ? src.len : cap - 1;
    memcpy(dst, src.data, n);
    dst[n] = '\0';
}

static const c8 *editor_mode_label(void) {
    switch (E.mode) {
    case MODE_NORMAL: return "normal";
    case MODE_OPERATOR_PENDING: return "operator";
    case MODE_INSERT: return "insert";
    case MODE_COMMAND: return "command";
    case MODE_SEARCH: return "search";
    case MODE_REPLACE: return "replace";
    default: return "unknown";
    }
}

static const c8 *sketch_kind_label(sketch_shape_kind_t kind) {
    switch (kind) {
    case SKETCH_SHAPE_AUTO: return "auto";
    case SKETCH_SHAPE_LINE: return "line";
    case SKETCH_SHAPE_RECT: return "rect";
    case SKETCH_SHAPE_SQUARE: return "square";
    case SKETCH_SHAPE_ELLIPSE: return "ellipse";
    case SKETCH_SHAPE_CIRCLE: return "circle";
    default: return "none";
    }
}

static void make_plugin_summary(c8 *buf, u32 cap) {
    if (!buf || cap == 0) return;

    sp_str_t plugins = ext_list_loaded_plugins();
    sp_str_t recognizers = ext_list_recognizers();
    u32 plugin_count = ext_loaded_plugin_count();
    u32 recognizer_count = ext_recognizer_count();
    u32 target_count = input_operator_target_count();

    if (plugin_count > 0 && plugins.len > 0) {
        u32 keep = 0;
        while (keep < plugins.len && plugins.data[keep] != ',') keep++;
        c8 first[24];
        copy_sp_str_to_cstr(sp_str_sub(plugins, 0, (s32)keep), first, sizeof(first), "plugin");
        snprintf(buf, cap, "%s +%u", first, plugin_count - 1);
        return;
    }

    if (recognizer_count > 0 && recognizers.len > 0) {
        u32 keep = 0;
        while (keep < recognizers.len && recognizers.data[keep] != ',') keep++;
        c8 first[20];
        copy_sp_str_to_cstr(sp_str_sub(recognizers, 0, (s32)keep), first, sizeof(first), "rec");
        snprintf(buf, cap, "rec %s +%u", first, recognizer_count - 1);
        return;
    }

    snprintf(buf, cap, "p%u r%u t%u", plugin_count, recognizer_count, target_count);
}

static void make_runtime_list_summary(sp_str_t list, u32 count, const c8 *empty_label,
                                      c8 *buf, u32 cap) {
    if (!buf || cap == 0) return;
    if (count == 0 || list.len == 0) {
        snprintf(buf, cap, "%s", empty_label);
        return;
    }

    u32 first_end = 0;
    while (first_end < list.len && list.data[first_end] != ',') first_end++;

    c8 first[24];
    copy_sp_str_to_cstr(sp_str_sub(list, 0, (s32)first_end), first, sizeof(first), empty_label);
    if (count == 1) {
        snprintf(buf, cap, "%s", first);
    } else {
        snprintf(buf, cap, "%s +%u", first, count - 1);
    }
}

static bool sp_str_contains_cstr(sp_str_t haystack, const c8 *needle) {
    if (!needle || needle[0] == '\0' || haystack.len == 0 || !haystack.data) return false;
    u32 needle_len = (u32)strlen(needle);
    if (needle_len > haystack.len) return false;
    for (u32 i = 0; i + needle_len <= haystack.len; i++) {
        if (memcmp(haystack.data + i, needle, needle_len) == 0) return true;
    }
    return false;
}

static bool sp_str_extract_bracket_value(sp_str_t text, c8 *dst, u32 cap, const c8 *fallback) {
    if (!dst || cap == 0) return false;
    if (text.len == 0 || !text.data) {
        snprintf(dst, cap, "%s", fallback ? fallback : "");
        return false;
    }

    s32 open = -1;
    s32 close = -1;
    for (u32 i = 0; i < text.len; i++) {
        if (text.data[i] == '[') {
            open = (s32)i;
            break;
        }
    }
    if (open < 0) {
        snprintf(dst, cap, "%s", fallback ? fallback : "");
        return false;
    }
    for (u32 i = (u32)(open + 1); i < text.len; i++) {
        if (text.data[i] == ']') {
            close = (s32)i;
            break;
        }
    }
    if (close <= open + 1) {
        snprintf(dst, cap, "%s", fallback ? fallback : "");
        return false;
    }

    copy_sp_str_to_cstr(sp_str_sub(text, open + 1, close - open - 1), dst, cap, fallback);
    return true;
}

static void make_session_summary(c8 *buf, u32 cap) {
    if (!buf || cap == 0) return;

    c8 lang_buf[16];
    c8 ts_buf[16];
    c8 llm_buf[20];
    sp_str_t ts = treesitter_status();
    sp_str_t llm = llm_status();

    if (E.buffer.lang.len > 0) {
        copy_sp_str_to_cstr(E.buffer.lang, lang_buf, sizeof(lang_buf), "text");
    } else {
        snprintf(lang_buf, sizeof(lang_buf), "text");
    }

    if (treesitter_is_enabled()) {
        c8 inner[16];
        if (sp_str_extract_bracket_value(ts, inner, sizeof(inner), "tree")) {
            snprintf(ts_buf, sizeof(ts_buf), "ts:%s", inner);
        } else {
            snprintf(ts_buf, sizeof(ts_buf), "ts:on");
        }
    } else if (treesitter_is_available()) {
        snprintf(ts_buf, sizeof(ts_buf), "ts:idle");
    } else {
        snprintf(ts_buf, sizeof(ts_buf), "ts:off");
    }

    if (sp_str_contains_cstr(llm, "ready [")) {
        c8 inner[24];
        sp_str_extract_bracket_value(llm, inner, sizeof(inner), "ready");
        snprintf(llm_buf, sizeof(llm_buf), "llm:%s", inner);
    } else {
        snprintf(llm_buf, sizeof(llm_buf), "llm:off");
    }

    snprintf(buf, cap, "%s %s %s th:%s ui:%s", lang_buf, ts_buf, llm_buf,
             S.presets[S.active_theme].name, S.focused ? "on" : "off");
}

static void make_runtime_dock_message(c8 *buf, u32 cap) {
    if (!buf || cap == 0) return;
    u32 plugins = ext_loaded_plugin_count();
    u32 recognizers = ext_recognizer_count();
    u32 targets = input_operator_target_count();
    if (plugins == 0 && recognizers == 0 && targets == 0) {
        snprintf(buf, cap, "dock empty  run :plugins");
        return;
    }
    snprintf(buf, cap, "dock live  p%u r%u t%u", plugins, recognizers, targets);
}

static void iui_apply_mouse_state(void) {
    if (!S.ctx) return;
    iui_update_mouse_pos(S.ctx, S.mouse_x, S.mouse_y);
    iui_update_mouse_buttons(S.ctx, S.mouse_pressed, S.mouse_released);
}

static void iui_reset_mouse_state(void) {
    S.mouse_pressed = 0;
    S.mouse_released = 0;
}

static void tui_toggle_line_numbers(void) {
    E.config.show_line_numbers = !E.config.show_line_numbers;
    editor_set_message("Line numbers %s", E.config.show_line_numbers ? "enabled" : "disabled");
}

static void tui_toggle_syntax(void) {
    E.config.syntax_enabled = !E.config.syntax_enabled;
    for (u32 i = 0; i < E.buffer.line_count; i++) {
        E.buffer.lines[i].hl_dirty = true;
    }
    editor_set_message("Syntax %s", E.config.syntax_enabled ? "enabled" : "disabled");
}

static void tui_cycle_theme(void) {
    if (!S.ready) return;
    S.active_theme = (S.active_theme + 1) % tui_theme_count();
    tui_recompute_defaults();
    editor_set_message("UI theme: %s", S.presets[S.active_theme].name);
}

static int tui_current_tab_index(void) {
    if (S.active_tab != 2 && sketch_is_enabled()) return 1;
    return (int)S.active_tab;
}

static void tui_reload_runtime_dock(void) {
    sp_str_t err = sp_str_lit("");
    u32 loaded = ext_autoload_plugins(&err);
    if (err.len > 0) {
        editor_set_message("Runtime reload error: %.*s", (int)err.len, err.data);
        return;
    }
    editor_set_message("Runtime dock refreshed: %u plugins, %u recognizers, %u targets",
                       loaded, ext_recognizer_count(), input_operator_target_count());
}

static bool tui_mouse_in_rect(iui_rect_t rect) {
    return S.mouse_x >= rect.x && S.mouse_x < rect.x + rect.width &&
           S.mouse_y >= rect.y && S.mouse_y < rect.y + rect.height;
}

static bool tui_mouse_submit(iui_rect_t rect) {
    return (S.mouse_pressed & IUI_MOUSE_LEFT) && tui_mouse_in_rect(rect);
}

static void tui_draw_text_fit(iui_rect_t rect, float x_pad, u32 color, const c8 *text) {
    c8 clipped[96];
    size_t cap = sizeof(clipped) - 1;
    size_t len = text ? strlen(text) : 0;
    size_t max_chars = 0;
    if (rect.width > x_pad * 2.0f) {
        max_chars = (size_t)((rect.width - x_pad * 2.0f) / IUI_TUI_CELL_W);
    }
    if (max_chars == 0 || !text) return;
    if (len <= max_chars) {
        S.renderer.draw_text(rect.x + x_pad, rect.y, text, color, S.renderer.user);
        return;
    }
    if (max_chars < 4) {
        for (size_t i = 0; i < max_chars && i < cap; i++) clipped[i] = '.';
        clipped[max_chars < cap ? max_chars : cap] = '\0';
    } else {
        size_t keep = max_chars - 3;
        if (keep > cap) keep = cap;
        memcpy(clipped, text, keep);
        clipped[keep + 0] = '.';
        clipped[keep + 1] = '.';
        clipped[keep + 2] = '.';
        clipped[keep + 3] = '\0';
    }
    S.renderer.draw_text(rect.x + x_pad, rect.y, clipped, color, S.renderer.user);
}

static void tui_draw_compact_chip(iui_rect_t rect, const c8 *label, bool active, u32 accent, bool clickable) {
    const iui_theme_t *t = tui_active_theme();
    u32 bg = active ? accent : t->surface_container_lowest;
    u32 fg = active ? t->surface_container_lowest : t->on_surface;
    u32 line = active ? accent : t->outline_variant;
    if (clickable && tui_mouse_submit(rect)) {
        bg = t->surface_container_high;
        line = t->primary;
        fg = t->on_surface;
    }
    S.renderer.draw_box(rect, 0.0f, bg, S.renderer.user);
    S.renderer.draw_line(rect.x, rect.y, rect.x + rect.width, rect.y,
                         1.0f, active ? accent : t->outline_variant, S.renderer.user);
    S.renderer.draw_line(rect.x, rect.y + rect.height - 1.0f,
                         rect.x + rect.width, rect.y + rect.height - 1.0f,
                         1.0f, line, S.renderer.user);
    tui_draw_text_fit(rect, 2.0f, fg, label);
}

static void tui_draw_compact_segment(iui_rect_t rect, const c8 *label, const c8 *value, u32 accent) {
    const iui_theme_t *t = tui_active_theme();
    iui_rect_t label_rect = rect;
    iui_rect_t value_rect = rect;

    S.renderer.draw_box(rect, 0.0f, t->surface_container_lowest, S.renderer.user);
    S.renderer.draw_line(rect.x, rect.y, rect.x + rect.width, rect.y, 1.0f, accent, S.renderer.user);
    S.renderer.draw_line(rect.x, rect.y + rect.height - 1.0f,
                         rect.x + rect.width, rect.y + rect.height - 1.0f,
                         1.0f, t->outline_variant, S.renderer.user);

    label_rect.x += 2.0f;
    label_rect.width = 8.0f * 6.0f;
    value_rect.x += 2.0f + label_rect.width;
    value_rect.width -= 2.0f + label_rect.width;

    tui_draw_text_fit(label_rect, 0.0f, t->on_surface_variant, label);
    tui_draw_text_fit(value_rect, 2.0f, t->on_surface, value);
}

static void tui_draw_row_band(iui_rect_t rect, u32 fill, u32 line) {
    S.renderer.draw_box(rect, 0.0f, fill, S.renderer.user);
    S.renderer.draw_line(rect.x, rect.y + rect.height - 1.0f,
                         rect.x + rect.width, rect.y + rect.height - 1.0f,
                         1.0f, line, S.renderer.user);
}

static void tui_select_runtime_tab(int tab) {
    S.active_tab = (u8)tab;
    if (tab == 1 && !sketch_is_enabled()) {
        sketch_set_enabled(true);
        editor_set_message("Shape lab armed");
    } else if (tab != 1 && sketch_is_enabled()) {
        sketch_set_enabled(false);
        if (tab == 0) {
            editor_set_message("Editor deck armed");
        }
    }

    if (tab == 2) {
        c8 dock_buf[40];
        make_runtime_dock_message(dock_buf, sizeof(dock_buf));
        editor_set_message("Runtime dock: %s", dock_buf);
    }
}

static void make_deck_summary(c8 *buf, u32 cap) {
    if (!buf || cap == 0) return;
    if (S.active_tab == 2) {
        snprintf(buf, cap, "runtime");
    } else if (sketch_is_enabled()) {
        snprintf(buf, cap, "gesture %s", sketch_kind_label(sketch_preferred_kind()));
    } else {
        snprintf(buf, cap, "text deck");
    }
}

static void make_file_state_summary(c8 *buf, u32 cap) {
    if (!buf || cap == 0) return;
    snprintf(buf, cap, "%s", E.buffer.modified ? "dirty" : "saved");
}

static void tui_draw_header_row(iui_rect_t row_rect) {
    c8 file_buf[48];
    c8 deck_buf[32];
    c8 right_buf[32];
    sp_str_t filename = E.buffer.filename.len > 0 ? E.buffer.filename : sp_str_lit("[No Name]");
    iui_sizing_t sizes[] = { IUI_FIXED(18), IUI_FIXED(14), IUI_GROW(4), IUI_GROW(2) };

    tui_draw_row_band(row_rect,
                      tui_active_theme()->surface_container_lowest,
                      tui_active_theme()->outline_variant);
    copy_sp_str_to_cstr(filename, file_buf, sizeof(file_buf), "[No Name]");
    make_deck_summary(deck_buf, sizeof(deck_buf));
    make_file_state_summary(right_buf, sizeof(right_buf));

    iui_box_begin(S.ctx, &(iui_box_config_t){
        .direction = IUI_DIR_ROW,
        .child_count = 4,
        .sizes = sizes,
        .gap = 1.0f,
        .padding = IUI_PAD_XY(0.0f, 0.0f),
        .cross = row_rect.height,
        .align = IUI_CROSS_STRETCH,
    });

    tui_draw_compact_chip(iui_box_next(S.ctx), "TED//STUDIO", true, tui_active_theme()->primary, false);
    tui_draw_compact_chip(iui_box_next(S.ctx), deck_buf, true, tui_active_theme()->outline, false);

    iui_rect_t file_rect = iui_box_next(S.ctx);
    tui_draw_compact_chip(file_rect, file_buf, E.buffer.modified, tui_active_theme()->surface_container_highest, false);

    tui_draw_compact_chip(iui_box_next(S.ctx), right_buf, E.buffer.modified,
                          tui_active_theme()->tertiary_container, false);
    iui_box_end(S.ctx);
}

static void tui_draw_controls_row(iui_rect_t row_rect) {
    static const c8 *tab_labels[] = { "text", "sketch", "runtime" };
    bool sketch_clear_ready = sketch_is_enabled() ||
                              sketch_shape_count() > 0 ||
                              sketch_stroke_point_count() > 0;
    u32 accents[] = {
        tui_active_theme()->primary_container,
        tui_active_theme()->secondary_container,
        tui_active_theme()->tertiary_container,
        tui_active_theme()->secondary,
    };
    iui_sizing_t sizes[] = {
        IUI_GROW(2), IUI_GROW(2), IUI_GROW(2),
        IUI_GROW(1), IUI_GROW(1), IUI_GROW(1), IUI_GROW(1),
    };

    tui_draw_row_band(row_rect,
                      tui_active_theme()->surface_container_low,
                      tui_active_theme()->outline_variant);

    iui_box_begin(S.ctx, &(iui_box_config_t){
        .direction = IUI_DIR_ROW,
        .child_count = 7,
        .sizes = sizes,
        .gap = 1.0f,
        .padding = IUI_PAD_XY(0.0f, 0.0f),
        .cross = row_rect.height,
        .align = IUI_CROSS_STRETCH,
    });

    for (int i = 0; i < 3; i++) {
        iui_rect_t rect = iui_box_next(S.ctx);
        bool active = tui_current_tab_index() == i;
        if (tui_mouse_submit(rect)) tui_select_runtime_tab(i);
        tui_draw_compact_chip(rect, tab_labels[i], active, tui_active_theme()->primary, true);
    }

    for (int i = 0; i < 4; i++) {
        iui_rect_t rect = iui_box_next(S.ctx);
        c8 label[24];
        bool active = false;
        if (tui_mouse_submit(rect)) {
            switch (i) {
            case 0: tui_toggle_line_numbers(); break;
            case 1: tui_toggle_syntax(); break;
            case 2:
                tui_cycle_theme();
                break;
            case 3:
                sketch_clear();
                editor_set_message("Sketch canvas cleared");
                break;
            default: break;
            }
        }
        switch (i) {
        case 0:
            snprintf(label, sizeof(label), "num %s", E.config.show_line_numbers ? "on" : "off");
            active = E.config.show_line_numbers;
            break;
        case 1:
            snprintf(label, sizeof(label), "syn %s", E.config.syntax_enabled ? "on" : "off");
            active = E.config.syntax_enabled;
            break;
        case 2:
            snprintf(label, sizeof(label), "theme %s", S.presets[S.active_theme].name);
            active = true;
            break;
        case 3:
            snprintf(label, sizeof(label), "clear");
            active = sketch_clear_ready;
            break;
        default:
            label[0] = '\0';
            break;
        }
        tui_draw_compact_chip(rect, label, active, accents[i], true);
    }

    iui_box_end(S.ctx);
}

static void tui_draw_status_row(iui_rect_t row_rect) {
    c8 mode_buf[32];
    c8 geometry_buf[56];
    c8 runtime_buf[40];
    c8 session_buf[80];
    u32 shapes = sketch_shape_count();
    u32 stroke_pts = sketch_stroke_point_count();

    tui_draw_row_band(row_rect,
                      tui_active_theme()->surface_container_high,
                      tui_active_theme()->outline);

    if (sketch_is_enabled()) {
        snprintf(mode_buf, sizeof(mode_buf), "sketch:on %s", sketch_kind_label(sketch_preferred_kind()));
    } else {
        snprintf(mode_buf, sizeof(mode_buf), "sketch:off %s", editor_mode_label());
    }

    if (sketch_is_enabled()) {
        if (sketch_has_preview_shape()) {
            snprintf(geometry_buf, sizeof(geometry_buf), "gesture bank %u  %s  %.3f",
                     shapes,
                     sketch_kind_label(sketch_preview_kind()),
                     sketch_preview_score());
        } else if (stroke_pts > 0) {
            snprintf(geometry_buf, sizeof(geometry_buf), "gesture trace %u pts", stroke_pts);
        } else {
            snprintf(geometry_buf, sizeof(geometry_buf), "gesture bank %u  idle", shapes);
        }
    } else {
        snprintf(geometry_buf, sizeof(geometry_buf), "gesture idle");
    }

    make_plugin_summary(runtime_buf, sizeof(runtime_buf));
    make_session_summary(session_buf, sizeof(session_buf));
    iui_sizing_t sizes[] = { IUI_GROW(1), IUI_GROW(2), IUI_GROW(2), IUI_GROW(3) };
    iui_box_begin(S.ctx, &(iui_box_config_t){
        .direction = IUI_DIR_ROW,
        .child_count = 4,
        .sizes = sizes,
        .gap = 1.0f,
        .padding = IUI_PAD_XY(0.0f, 0.0f),
        .cross = row_rect.height,
        .align = IUI_CROSS_STRETCH,
    });

    tui_draw_compact_segment(iui_box_next(S.ctx), "mode", mode_buf, tui_active_theme()->primary);
    tui_draw_compact_segment(iui_box_next(S.ctx), "canvas", geometry_buf, tui_active_theme()->secondary);
    tui_draw_compact_segment(iui_box_next(S.ctx), "runtime", runtime_buf, tui_active_theme()->tertiary);
    tui_draw_compact_segment(iui_box_next(S.ctx), "session", session_buf, tui_active_theme()->outline);

    iui_box_end(S.ctx);
}

static void tui_draw_runtime_dock_row(iui_rect_t row_rect) {
    c8 plugins_buf[40];
    c8 recognizers_buf[40];
    c8 targets_buf[40];
    c8 action_buf[24];
    sp_str_t plugins = ext_list_loaded_plugins();
    sp_str_t recognizers = ext_list_recognizers();
    sp_str_t targets = input_list_operator_targets();
    bool dock_hot = false;
    iui_sizing_t sizes[] = { IUI_GROW(2), IUI_GROW(2), IUI_GROW(2), IUI_GROW(1) };

    tui_draw_row_band(row_rect,
                      tui_active_theme()->surface_container_high,
                      tui_active_theme()->outline);
    make_runtime_list_summary(plugins, ext_loaded_plugin_count(), "none", plugins_buf, sizeof(plugins_buf));
    make_runtime_list_summary(recognizers, ext_recognizer_count(), "none", recognizers_buf, sizeof(recognizers_buf));
    make_runtime_list_summary(targets, input_operator_target_count(), "none", targets_buf, sizeof(targets_buf));
    snprintf(action_buf, sizeof(action_buf), "%s", ext_loaded_plugin_count() > 0 ? "reload" : "scan");

    iui_box_begin(S.ctx, &(iui_box_config_t){
        .direction = IUI_DIR_ROW,
        .child_count = 4,
        .sizes = sizes,
        .gap = 1.0f,
        .padding = IUI_PAD_XY(0.0f, 0.0f),
        .cross = row_rect.height,
        .align = IUI_CROSS_STRETCH,
    });

    tui_draw_compact_segment(iui_box_next(S.ctx), "plug", plugins_buf, tui_active_theme()->primary);
    tui_draw_compact_segment(iui_box_next(S.ctx), "rec", recognizers_buf, tui_active_theme()->secondary);
    tui_draw_compact_segment(iui_box_next(S.ctx), "tgt", targets_buf, tui_active_theme()->tertiary);

    iui_rect_t action_rect = iui_box_next(S.ctx);
    dock_hot = tui_mouse_submit(action_rect);
    if (dock_hot) {
        tui_reload_runtime_dock();
    }
    tui_draw_compact_chip(action_rect, action_buf, ext_loaded_plugin_count() > 0,
                          tui_active_theme()->primary_container, true);
    iui_box_end(S.ctx);
}

void iui_tui_draw_toolbar(void) {
    if (!S.ready || !S.cells) return;
    tui_clear_cells();
    iui_set_theme(S.ctx, tui_active_theme());
    iui_apply_mouse_state();
    iui_begin_frame(S.ctx, S.frame_dt > 0.0f ? S.frame_dt : (1.0f / 60.0f));
    if (iui_begin_window(S.ctx, "", 0.0f, 0.0f, (float)S.pixel_w, (float)S.pixel_h, IUI_WINDOW_PINNED)) {
        iui_sizing_t rows[] = { IUI_FIXED(16), IUI_FIXED(16), IUI_FIXED(16) };
        iui_box_begin(S.ctx, &(iui_box_config_t){
            .direction = IUI_DIR_COLUMN,
            .child_count = 3,
            .sizes = rows,
            .gap = 0.0f,
            .padding = IUI_PAD_XY(0.0f, 0.0f),
            .cross = (float)S.pixel_w,
            .align = IUI_CROSS_STRETCH,
        });
        tui_draw_header_row(iui_box_next(S.ctx));
        tui_draw_controls_row(iui_box_next(S.ctx));
        if (S.active_tab == 2) {
            tui_draw_runtime_dock_row(iui_box_next(S.ctx));
        } else {
            tui_draw_status_row(iui_box_next(S.ctx));
        }
        iui_box_end(S.ctx);
        iui_end_window(S.ctx);
    }
    iui_end_frame(S.ctx);
    iui_reset_mouse_state();
}

void iui_tui_blit(sp_io_writer_t *out, u32 start_row) {
    if (!S.ready || !S.cells || !out) return;
    for (u32 r = 0; r < S.rows; r++) {
        sp_str_t pos = sp_format("\033[{};{}H", SP_FMT_U32(start_row + r + 1), SP_FMT_U32(1));
        sp_io_write_str(out, pos);
        int cur_fg = -1;
        int cur_bg = -1;
        for (u32 c = 0; c < S.cols; c++) {
            tui_cell_t cell = S.cells[r * S.cols + c];
            if ((int)cell.fg != cur_fg || (int)cell.bg != cur_bg) {
                sp_str_t style = sp_format("\033[38;5;{}m\033[48;5;{}m",
                                           SP_FMT_U32(cell.fg),
                                           SP_FMT_U32(cell.bg));
                sp_io_write_str(out, style);
                cur_fg = cell.fg;
                cur_bg = cell.bg;
            }
            sp_io_write(out, &cell.ch, 1);
        }
        sp_io_write_cstr(out, "\033[0m");
    }
}

bool iui_tui_set_theme(sp_str_t name) {
    if (!S.ready || name.len == 0) return false;
    if (sp_str_equal(name, sp_str_lit("next"))) {
        S.active_theme = (S.active_theme + 1) % tui_theme_count();
        tui_recompute_defaults();
        return true;
    }
    s32 idx = tui_theme_index(name);
    if (idx < 0) return false;
    S.active_theme = (u32)idx;
    tui_recompute_defaults();
    return true;
}

sp_str_t iui_tui_theme_name(void) {
    return sp_str_from_cstr(S.presets[S.active_theme].name);
}

sp_str_t iui_tui_theme_options(void) {
    return sp_str_lit("cyber,amber,mono");
}
