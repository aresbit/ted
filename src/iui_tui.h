/**
 * iui_tui.h - libiui terminal backend bridge
 */

#ifndef TED_IUI_TUI_H
#define TED_IUI_TUI_H

#include "ted.h"

u32 iui_tui_panel_rows(void);
void iui_tui_init(u32 cols, u32 rows);
void iui_tui_shutdown(void);
void iui_tui_resize(u32 cols, u32 rows);
bool iui_tui_handle_key(int key);
bool iui_tui_handle_mouse(u32 term_col_1b, u32 term_row_1b, bool pressed);
void iui_tui_draw_toolbar(void);
void iui_tui_blit(sp_io_writer_t *out, u32 start_row);
bool iui_tui_is_focused(void);
bool iui_tui_set_theme(sp_str_t name);
sp_str_t iui_tui_theme_name(void);
sp_str_t iui_tui_theme_options(void);

#endif
