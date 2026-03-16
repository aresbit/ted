/**
 * cui.c - Minimal terminal UI primitives for TED
 */

#include "cui.h"

static const cui_theme_t CUI_THEME_JOBS = {
    .reset = "\033[0m",
    .fg_text = "\033[38;5;231m",
    .fg_muted = "\033[38;5;110m",
    .fg_accent = "\033[38;5;51m",
    .fg_warning = "\033[38;5;220m",
    .bg_elevated = "\033[48;5;234m",
};

const cui_theme_t *cui_theme_jobs(void) {
    return &CUI_THEME_JOBS;
}

void cui_write_token(sp_io_writer_t *out, const c8 *token) {
    if (!out || !token) return;
    sp_io_write_cstr(out, token);
}

void cui_write_cstr(sp_io_writer_t *out, const c8 *style, const c8 *text) {
    if (!out || !text) return;
    if (style) sp_io_write_cstr(out, style);
    sp_io_write_cstr(out, text);
    if (style) sp_io_write_cstr(out, CUI_THEME_JOBS.reset);
}

void cui_write_repeat(sp_io_writer_t *out, c8 ch, u32 count) {
    if (!out) return;
    for (u32 i = 0; i < count; i++) {
        sp_io_write(out, &ch, 1);
    }
}

sp_str_t cui_truncate_ascii(sp_str_t text, u32 max_len) {
    if (text.len <= max_len) return text;
    return sp_str_sub(text, 0, (s32)max_len);
}
