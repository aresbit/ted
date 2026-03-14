/**
 * cui.h - Minimal terminal UI primitives for TED
 */

#ifndef TED_CUI_H
#define TED_CUI_H

#include "ted.h"

typedef struct {
    const c8 *reset;
    const c8 *fg_text;
    const c8 *fg_muted;
    const c8 *fg_accent;
    const c8 *fg_warning;
    const c8 *bg_elevated;
} cui_theme_t;

const cui_theme_t *cui_theme_jobs(void);
void cui_write_token(sp_io_writer_t *out, const c8 *token);
void cui_write_cstr(sp_io_writer_t *out, const c8 *style, const c8 *text);
void cui_write_repeat(sp_io_writer_t *out, c8 ch, u32 count);
sp_str_t cui_truncate_ascii(sp_str_t text, u32 max_len);

#endif
