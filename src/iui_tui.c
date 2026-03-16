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

typedef enum {
    HIT_NONE = 0,
    HIT_LINES,
    HIT_SYNTAX,
    HIT_WRAP,
    HIT_FOCUS,
} hit_action_t;

typedef struct {
    u32 col_start;
    u32 col_end; // exclusive
    hit_action_t action;
} hit_box_t;

typedef struct {
    bool ready;
    bool focused;
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
    hit_box_t hits[8];
    u32 hit_count;
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

static void tui_recompute_defaults(void) {
    const iui_theme_t *t = tui_active_theme();
    S.default_fg = srgb_to_ansi256(t->on_surface);
    S.default_bg = srgb_to_ansi256(t->surface);
}

static void tui_presets_init(void) {
    const iui_theme_t *dark = iui_theme_dark();

    // Monochrome-only preset.
    S.presets[0].name = "mono";
    S.presets[0].theme = *dark;
    S.presets[0].theme.primary = 0xFF3A3A3A;
    S.presets[0].theme.on_primary = 0xFFEDEDED;
    S.presets[0].theme.primary_container = 0xFF2D2D2D;
    S.presets[0].theme.on_primary_container = 0xFFE0E0E0;
    S.presets[0].theme.secondary = 0xFF444444;
    S.presets[0].theme.on_secondary = 0xFFEAEAEA;
    S.presets[0].theme.secondary_container = 0xFF2C2C2C;
    S.presets[0].theme.on_secondary_container = 0xFFE0E0E0;
    S.presets[0].theme.tertiary = 0xFF505050;
    S.presets[0].theme.on_tertiary = 0xFFEAEAEA;
    S.presets[0].theme.surface = 0xFF111111;
    S.presets[0].theme.on_surface = 0xFFD8D8D8;
    S.presets[0].theme.surface_container = 0xFF1A1A1A;
    S.presets[0].theme.surface_container_high = 0xFF222222;
    S.presets[0].theme.surface_container_highest = 0xFF2A2A2A;
    S.presets[0].theme.outline = 0xFF5A5A5A;

    // Keep API compatibility; all map to monochrome.
    S.presets[1] = S.presets[0];
    S.presets[2] = S.presets[0];
    S.active_theme = 0;
    tui_recompute_defaults();
}

u32 iui_tui_panel_rows(void) { return 1; }

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
        .draw_line = SP_NULLPTR,
        .draw_circle = SP_NULLPTR,
        .draw_arc = SP_NULLPTR,
        .user = SP_NULLPTR,
    };

    iui_config_t cfg = iui_make_config(S.iui_mem, S.renderer, 14.0f, SP_NULLPTR);
    S.ctx = iui_init(&cfg);
    if (!S.ctx) return;

    tui_presets_init();
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
    u32 start_row_1b = E.screen_rows + 1;
    u32 end_row_1b = start_row_1b + S.rows - 1;
    if (term_row_1b < start_row_1b || term_row_1b > end_row_1b) return false;
    if (term_col_1b == 0 || term_col_1b > E.screen_cols) return false;

    if (pressed) return true;

    u32 local_col = term_col_1b - 1;
    hit_action_t action = HIT_NONE;
    for (u32 i = 0; i < S.hit_count; i++) {
        if (local_col < S.hits[i].col_start || local_col >= S.hits[i].col_end) continue;
        action = S.hits[i].action;
        break;
    }

    switch (action) {
    case HIT_LINES:
        E.config.show_line_numbers = !E.config.show_line_numbers;
        editor_set_message("Line numbers %s", E.config.show_line_numbers ? "enabled" : "disabled");
        break;
    case HIT_SYNTAX:
        E.config.syntax_enabled = !E.config.syntax_enabled;
        for (u32 i = 0; i < E.buffer.line_count; i++) {
            E.buffer.lines[i].hl_dirty = true;
        }
        editor_set_message("Syntax %s", E.config.syntax_enabled ? "enabled" : "disabled");
        break;
    case HIT_WRAP:
        E.config.auto_wrap = !E.config.auto_wrap;
        editor_set_message("Wrap %s", E.config.auto_wrap ? "enabled" : "disabled");
        break;
    case HIT_FOCUS:
        S.focused = !S.focused;
        editor_set_message(S.focused ? "UI focus ON (Tab/Enter/Esc)" : "UI focus OFF");
        break;
    default:
        break;
    }
    return true;
}

static void toolbar_write_text(u32 *col, const c8 *text, u8 fg, u8 bg) {
    if (!text) return;
    u32 len = (u32)strlen(text);
    for (u32 i = 0; i < len && *col < S.cols; i++) {
        tui_cell_t *cell = cell_at((int)*col, 0);
        if (cell) {
            cell->ch = text[i];
            cell->fg = fg;
            cell->bg = bg;
        }
        (*col)++;
    }
}

static void toolbar_add_hit(u32 start, u32 end, hit_action_t action) {
    if (S.hit_count >= 8 || end <= start) return;
    S.hits[S.hit_count].col_start = start;
    S.hits[S.hit_count].col_end = end;
    S.hits[S.hit_count].action = action;
    S.hit_count++;
}

void iui_tui_draw_toolbar(void) {
    if (!S.ready || !S.cells) return;
    tui_clear_cells();
    S.hit_count = 0;
    const u8 fg_dim = 246;
    const u8 fg_label = 250;
    const u8 fg_value = 254;
    const u8 bg = 234;

    u32 col = 0;
    toolbar_write_text(&col, " ted ", fg_dim, bg);
    toolbar_write_text(&col, "| ", fg_dim, bg);

    u32 s = col;
    toolbar_write_text(&col, "lines:", fg_label, bg);
    toolbar_write_text(&col, E.config.show_line_numbers ? "on " : "off ", fg_value, bg);
    toolbar_add_hit(s, col, HIT_LINES);

    toolbar_write_text(&col, "| ", fg_dim, bg);
    s = col;
    toolbar_write_text(&col, "syntax:", fg_label, bg);
    toolbar_write_text(&col, E.config.syntax_enabled ? "on " : "off ", fg_value, bg);
    toolbar_add_hit(s, col, HIT_SYNTAX);

    toolbar_write_text(&col, "| ", fg_dim, bg);
    s = col;
    toolbar_write_text(&col, "wrap:", fg_label, bg);
    toolbar_write_text(&col, E.config.auto_wrap ? "on " : "off ", fg_value, bg);
    toolbar_add_hit(s, col, HIT_WRAP);

    toolbar_write_text(&col, "| ", fg_dim, bg);
    s = col;
    toolbar_write_text(&col, S.focused ? "focus:on" : "focus:off", fg_value, bg);
    toolbar_add_hit(s, col, HIT_FOCUS);

    if (col < S.cols) {
        toolbar_write_text(&col, "  (click)", fg_dim, bg);
    }
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
    (void)name;
    return false;
}

sp_str_t iui_tui_theme_name(void) {
    return sp_str_lit("mono");
}

sp_str_t iui_tui_theme_options(void) {
    return sp_str_lit("mono");
}
