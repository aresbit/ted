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
    int min_col = px_to_col(rect.x);
    int min_row = px_to_row(rect.y);
    int max_col = (int)ceilf((rect.x + rect.width) / IUI_TUI_CELL_W);
    int max_row = (int)ceilf((rect.y + rect.height) / IUI_TUI_CELL_H);
    clip_rect_apply(&min_col, &min_row, &max_col, &max_row);
    u8 bg = srgb_to_ansi256(color);
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
    u8 fg = srgb_to_ansi256(color);
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
    const iui_theme_t *light = iui_theme_light();

    S.presets[0].name = "cyber";
    S.presets[0].theme = *dark;
    S.presets[0].theme.primary = 0xFF00E5FF;
    S.presets[0].theme.on_primary = 0xFF00131A;
    S.presets[0].theme.primary_container = 0xFF003744;
    S.presets[0].theme.on_primary_container = 0xFFB8F4FF;
    S.presets[0].theme.secondary = 0xFFFF3D81;
    S.presets[0].theme.on_secondary = 0xFF22010E;
    S.presets[0].theme.secondary_container = 0xFF4D1028;
    S.presets[0].theme.on_secondary_container = 0xFFFFD9E7;
    S.presets[0].theme.tertiary = 0xFFB388FF;
    S.presets[0].theme.on_tertiary = 0xFF230A4A;
    S.presets[0].theme.surface = 0xFF0A0C11;
    S.presets[0].theme.surface_container = 0xFF131725;
    S.presets[0].theme.surface_container_high = 0xFF1A2233;
    S.presets[0].theme.surface_container_highest = 0xFF22304A;
    S.presets[0].theme.outline = 0xFF5D6C88;

    S.presets[1].name = "warm";
    S.presets[1].theme = *light;
    S.presets[1].theme.primary = 0xFFC75A1B;
    S.presets[1].theme.on_primary = 0xFFFFFFFF;
    S.presets[1].theme.primary_container = 0xFFFFD9C2;
    S.presets[1].theme.on_primary_container = 0xFF3A1703;
    S.presets[1].theme.secondary = 0xFF5D7E52;
    S.presets[1].theme.on_secondary = 0xFFFFFFFF;
    S.presets[1].theme.secondary_container = 0xFFDDECCD;
    S.presets[1].theme.on_secondary_container = 0xFF1A2A14;
    S.presets[1].theme.tertiary = 0xFF7A5BAA;
    S.presets[1].theme.on_tertiary = 0xFFFFFFFF;
    S.presets[1].theme.surface = 0xFFFFF8EF;
    S.presets[1].theme.on_surface = 0xFF1F1B17;
    S.presets[1].theme.surface_container = 0xFFF5ECE0;
    S.presets[1].theme.surface_container_high = 0xFFECE0D2;
    S.presets[1].theme.surface_container_highest = 0xFFE2D3C2;
    S.presets[1].theme.outline = 0xFF8A7665;

    S.presets[2].name = "night";
    S.presets[2].theme = *dark;
    S.presets[2].theme.primary = 0xFFFF8A4C;
    S.presets[2].theme.on_primary = 0xFF251003;
    S.presets[2].theme.primary_container = 0xFF4E2411;
    S.presets[2].theme.on_primary_container = 0xFFFFD8C4;
    S.presets[2].theme.secondary = 0xFF6BCB77;
    S.presets[2].theme.on_secondary = 0xFF0D2211;
    S.presets[2].theme.secondary_container = 0xFF1F3A26;
    S.presets[2].theme.on_secondary_container = 0xFFD6F7DB;
    S.presets[2].theme.tertiary = 0xFF89A8FF;
    S.presets[2].theme.on_tertiary = 0xFF101A40;
    S.presets[2].theme.surface = 0xFF0F0F12;
    S.presets[2].theme.surface_container = 0xFF181A20;
    S.presets[2].theme.surface_container_high = 0xFF20232C;
    S.presets[2].theme.surface_container_highest = 0xFF2A2E3A;
    S.presets[2].theme.outline = 0xFF6A7080;

    S.active_theme = 2;
    tui_recompute_defaults();
}

u32 iui_tui_panel_rows(void) {
    return 3;
}

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
    if (!S.ready || !S.ctx) return false;
    u32 start_row_1b = E.screen_rows + 1;
    u32 end_row_1b = start_row_1b + S.rows - 1;
    if (term_row_1b < start_row_1b || term_row_1b > end_row_1b) return false;
    if (term_col_1b == 0 || term_col_1b > E.screen_cols) return false;

    u32 local_col = term_col_1b - 1;
    u32 local_row = term_row_1b - start_row_1b;
    S.mouse_x = (float)local_col * IUI_TUI_CELL_W + (IUI_TUI_CELL_W * 0.5f);
    S.mouse_y = (float)local_row * IUI_TUI_CELL_H + (IUI_TUI_CELL_H * 0.5f);
    if (pressed) {
        S.mouse_pressed |= IUI_MOUSE_LEFT;
    } else {
        S.mouse_released |= IUI_MOUSE_LEFT;
    }
    S.focused = true;
    return true;
}

void iui_tui_draw_toolbar(void) {
    if (!S.ready || !S.ctx || !S.cells) return;
    tui_clear_cells();

    iui_set_theme(S.ctx, tui_active_theme());
    iui_update_mouse_pos(S.ctx, S.mouse_x, S.mouse_y);
    iui_update_mouse_buttons(S.ctx, S.mouse_pressed, S.mouse_released);
    iui_begin_frame(S.ctx, 0.016f);
    if (iui_begin_window(S.ctx, "TED-TUI", 0.f, 0.f, (float)S.pixel_w, (float)S.pixel_h, IUI_WINDOW_PINNED)) {
        char ln_label[32];
        char syn_label[32];
        char wrap_label[32];
        char theme_label[32];
        snprintf(ln_label, sizeof(ln_label), "Lines:%s", E.config.show_line_numbers ? "on" : "off");
        snprintf(syn_label, sizeof(syn_label), "Syntax:%s", E.config.syntax_enabled ? "on" : "off");
        snprintf(wrap_label, sizeof(wrap_label), "Wrap:%s", E.config.auto_wrap ? "on" : "off");
        snprintf(theme_label, sizeof(theme_label), "Theme:%s", S.presets[S.active_theme].name);

        float cell_w = ((float)S.pixel_w - 40.f) / 5.f;
        if (cell_w < 76.f) cell_w = 76.f;
        iui_grid_begin(S.ctx, 5, cell_w, 36.f, 4.f);

        if (iui_button(S.ctx, theme_label, IUI_ALIGN_CENTER)) {
            S.active_theme = (S.active_theme + 1) % 3;
            tui_recompute_defaults();
            editor_set_message("Theme: %s", S.presets[S.active_theme].name);
        }
        iui_grid_next(S.ctx);

        if (iui_button(S.ctx, ln_label, IUI_ALIGN_CENTER)) {
            E.config.show_line_numbers = !E.config.show_line_numbers;
            editor_set_message("Line numbers %s", E.config.show_line_numbers ? "enabled" : "disabled");
        }
        iui_grid_next(S.ctx);

        if (iui_button(S.ctx, syn_label, IUI_ALIGN_CENTER)) {
            E.config.syntax_enabled = !E.config.syntax_enabled;
            for (u32 i = 0; i < E.buffer.line_count; i++) {
                E.buffer.lines[i].hl_dirty = true;
            }
            editor_set_message("Syntax %s", E.config.syntax_enabled ? "enabled" : "disabled");
        }
        iui_grid_next(S.ctx);

        if (iui_button(S.ctx, wrap_label, IUI_ALIGN_CENTER)) {
            E.config.auto_wrap = !E.config.auto_wrap;
            editor_set_message("Wrap %s", E.config.auto_wrap ? "enabled" : "disabled");
        }
        iui_grid_next(S.ctx);

        if (iui_button(S.ctx, S.focused ? "Focus:ON" : "Focus:OFF", IUI_ALIGN_CENTER)) {
            S.focused = !S.focused;
            editor_set_message(S.focused ? "UI focus ON (Tab/Enter/Esc)" : "UI focus OFF");
        }
        iui_grid_end(S.ctx);

        iui_text(S.ctx, IUI_ALIGN_LEFT, "Mode:%s  Ln:%u Col:%u  Ctrl+T focus",
                 (E.mode == MODE_INSERT) ? "INSERT" :
                 (E.mode == MODE_COMMAND) ? "COMMAND" :
                 (E.mode == MODE_SEARCH) ? "SEARCH" :
                 (E.mode == MODE_REPLACE) ? "REPLACE" :
                 (E.mode == MODE_OPERATOR_PENDING) ? "OP" : "NORMAL",
                 E.cursor.row + 1, E.cursor.col + 1);
        iui_end_window(S.ctx);
    }
    iui_end_frame(S.ctx);
    S.mouse_pressed = 0;
    S.mouse_released = 0;
}

void iui_tui_blit(sp_io_writer_t *out, u32 start_row) {
    if (!S.ready || !S.cells || !out) return;
    for (u32 r = 0; r < S.rows; r++) {
        sp_str_t pos = sp_format("\033[{};{}H", SP_FMT_U32(start_row + r + 1), SP_FMT_U32(1));
        sp_io_write_str(out, pos);
        u8 cur_fg = 255;
        u8 cur_bg = 234;
        for (u32 c = 0; c < S.cols; c++) {
            tui_cell_t cell = S.cells[r * S.cols + c];
            if (cell.fg != cur_fg || cell.bg != cur_bg) {
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
    if (name.len == 0) return false;
    for (u32 i = 0; i < 3; i++) {
        sp_str_t n = sp_str_from_cstr(S.presets[i].name);
        if (!sp_str_equal(name, n)) continue;
        S.active_theme = i;
        tui_recompute_defaults();
        return true;
    }
    return false;
}

sp_str_t iui_tui_theme_name(void) {
    return sp_str_from_cstr(S.presets[S.active_theme].name);
}

sp_str_t iui_tui_theme_options(void) {
    return sp_str_lit("cyber|warm|night");
}
