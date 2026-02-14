/**
 * editor.c - Core editor logic
 * FIXED VERSION
 */

#include "ted.h"
#include <stdio.h>
#include <stdarg.h>

void editor_set_message(const c8 *fmt, ...) {
    c8 buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    E.message = sp_str_from_cstr(buf);
}

void editor_init(void) {
    sp_memset(&E, 0, sizeof(E));

    buffer_init(&E.buffer);
    undo_init(&E.undo);
    undo_init(&E.redo);
    search_init();

    // Default configuration
    E.config.show_line_numbers = true;
    E.config.syntax_enabled = true;
    E.config.auto_wrap = false;
    E.config.show_whitespace = false;
    E.config.tab_width = TAB_WIDTH_DEFAULT;

    E.mode = MODE_NORMAL;
    E.has_selection = false;

    // Initialize display (this sets up raw mode)
    display_init();

    editor_set_message("TED v" TED_VERSION " - Press i to insert, :q to quit");
}

void editor_open(sp_str_t filename) {
    buffer_load_file(&E.buffer, filename);
    E.cursor = (cursor_t){0, 0, 0};
    E.row_offset = 0;
    E.col_offset = 0;

    editor_set_message("Opened - %u lines", E.buffer.line_count);
}

void editor_save(void) {
    if (E.buffer.filename.len == 0 || sp_str_equal(E.buffer.filename, sp_str_lit("[No Name]"))) {
        E.mode = MODE_COMMAND;
        E.command_buffer = sp_str_lit("w ");
        editor_set_message("Enter filename:");
        return;
    }

    buffer_save_file(&E.buffer);
    editor_set_message("Saved %u lines", E.buffer.line_count);
}

static s32 force_quit_count = 0;

void editor_quit(void) {
    if (E.buffer.modified && force_quit_count == 0) {
        editor_set_message("Unsaved changes! Press Ctrl+Q again or :q! to force quit.");
        force_quit_count = 1;
        return;
    }

    display_clear();
    exit(0);
}

void editor_move_cursor(u32 key) {
    cursor_t *c = &E.cursor;
    buffer_t *buf = &E.buffer;

    // Ensure buffer has at least one line
    if (buf->line_count == 0) {
        c->row = 0;
        c->col = 0;
        c->render_col = 0;
        return;
    }

    // Clamp current position
    if (c->row >= buf->line_count) {
        c->row = buf->line_count - 1;
    }

    switch (key) {
        case 0x1000: // Up
            if (c->row > 0) {
                c->row--;
            }
            break;

        case 0x1001: // Down
            if (c->row + 1 < buf->line_count) {
                c->row++;
            }
            break;

        case 0x1002: // Right
            if (c->col < buf->lines[c->row].text.len) {
                c->col++;
            } else if (c->row + 1 < buf->line_count) {
                // Move to next line
                c->row++;
                c->col = 0;
            }
            break;

        case 0x1003: // Left
            if (c->col > 0) {
                c->col--;
            } else if (c->row > 0) {
                // Move to end of previous line
                c->row--;
                c->col = buf->lines[c->row].text.len;
            }
            break;

        case 0x1004: // Home
            c->col = 0;
            break;

        case 0x1005: // End
            c->col = buf->lines[c->row].text.len;
            break;
    }

    // Adjust column if past end of line
    if (c->row < buf->line_count && c->col > buf->lines[c->row].text.len) {
        c->col = buf->lines[c->row].text.len;
    }

    // Update render column
    c->render_col = buffer_row_to_render(buf, c->row, c->col);

    // Adjust scroll offset - vertical
    if (c->row < E.row_offset) {
        E.row_offset = c->row;
    } else if (c->row >= E.row_offset + E.screen_rows) {
        E.row_offset = c->row - E.screen_rows + 1;
    }

    // Adjust scroll offset - horizontal
    u32 gutter = E.config.show_line_numbers ? 6 : 0;
    u32 visible_cols = E.screen_cols - gutter;
    
    if (c->render_col < E.col_offset) {
        E.col_offset = c->render_col;
    } else if (c->render_col >= E.col_offset + visible_cols) {
        E.col_offset = c->render_col - visible_cols + 1;
    }
}

void editor_insert_char(c8 c) {
    if (E.mode != MODE_INSERT) return;
    if (E.cursor.row >= E.buffer.line_count) return;

    // Record for undo
    undo_record_insert(E.cursor.row, E.cursor.col, c);

    buffer_insert_char_at(&E.buffer, E.cursor.row, E.cursor.col, c);
    E.cursor.col++;
    E.cursor.render_col = buffer_row_to_render(&E.buffer, E.cursor.row, E.cursor.col);
    
    // Reset force quit counter on edit
    force_quit_count = 0;
}

void editor_insert_newline(void) {
    if (E.mode != MODE_INSERT) return;
    if (E.cursor.row >= E.buffer.line_count) return;

    sp_str_t current_line = buffer_get_line(&E.buffer, E.cursor.row);
    sp_str_t new_line_text;

    // Split line at cursor
    if (E.cursor.col < current_line.len) {
        new_line_text = sp_str_sub(current_line, (s32)E.cursor.col, (s32)(current_line.len - E.cursor.col));

        // Truncate current line
        sp_str_t truncated = sp_str_sub(current_line, 0, (s32)E.cursor.col);
        E.buffer.lines[E.cursor.row].text = truncated;
        E.buffer.lines[E.cursor.row].hl_dirty = true;
    } else {
        new_line_text = sp_str_lit("");
    }

    // Record for undo
    undo_record_insert_line(E.cursor.row + 1, new_line_text);

    // Insert new line
    buffer_insert_line(&E.buffer, E.cursor.row + 1, new_line_text);
    E.cursor.row++;
    E.cursor.col = 0;
    E.cursor.render_col = 0;
    
    // Adjust scroll if needed
    if (E.cursor.row >= E.row_offset + E.screen_rows) {
        E.row_offset = E.cursor.row - E.screen_rows + 1;
    }
    
    force_quit_count = 0;
}

void editor_delete_char(void) {
    if (E.mode != MODE_INSERT) return;

    buffer_t *buf = &E.buffer;
    cursor_t *c = &E.cursor;

    if (c->row >= buf->line_count) return;

    if (c->col > 0) {
        // Delete char before cursor
        c8 deleted = buf->lines[c->row].text.data[c->col - 1];
        undo_record_delete(c->row, c->col - 1, deleted);

        buffer_delete_char_at(buf, c->row, c->col - 1);
        c->col--;
    } else if (c->row > 0) {
        // Join with previous line
        u32 prev_len = buf->lines[c->row - 1].text.len;
        sp_str_t current = buf->lines[c->row].text;

        // Record for undo
        undo_record_delete_line(c->row, current);

        // Append current line to previous
        sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
        sp_str_builder_t builder = sp_str_builder_from_writer(&writer);
        sp_str_builder_append(&builder, buf->lines[c->row - 1].text);
        sp_str_builder_append(&builder, current);
        buf->lines[c->row - 1].text = sp_str_builder_to_str(&builder);
        buf->lines[c->row - 1].hl_dirty = true;

        // Delete current line
        buffer_delete_line(buf, c->row);
        c->row--;
        c->col = prev_len;
    }

    c->render_col = buffer_row_to_render(buf, c->row, c->col);
    force_quit_count = 0;
}

void editor_delete_line(u32 row) {
    if (row >= E.buffer.line_count) return;
    if (E.buffer.line_count <= 1) {
        // Don't delete last line, just clear it
        E.buffer.lines[0].text = sp_str_lit("");
        E.buffer.lines[0].hl_dirty = true;
        E.buffer.modified = true;
        E.cursor.col = 0;
        E.cursor.render_col = 0;
        return;
    }

    // Record for undo
    sp_str_t line_text = E.buffer.lines[row].text;
    undo_record_delete_line(row, line_text);

    buffer_delete_line(&E.buffer, row);

    // Adjust cursor if needed
    if (E.cursor.row >= E.buffer.line_count) {
        E.cursor.row = E.buffer.line_count - 1;
    }
    if (E.cursor.col > E.buffer.lines[E.cursor.row].text.len) {
        E.cursor.col = E.buffer.lines[E.cursor.row].text.len;
    }
    
    force_quit_count = 0;
}

void editor_copy_line(void) {
    if (E.cursor.row >= E.buffer.line_count) return;

    // Copy line to clipboard would go here
    // For now, just set a message
    editor_set_message("Line copied (clipboard support TODO)");
}

void editor_goto_line(u32 line) {
    if (line == 0) line = 1;
    if (line > E.buffer.line_count) line = E.buffer.line_count;
    if (E.buffer.line_count == 0) return;

    E.cursor.row = line - 1;
    E.cursor.col = 0;
    E.cursor.render_col = 0;

    // Center the line on screen if possible
    if (E.screen_rows > 0) {
        if (E.cursor.row >= E.screen_rows / 2) {
            E.row_offset = E.cursor.row - E.screen_rows / 2;
        } else {
            E.row_offset = 0;
        }
    }
    
    // Make sure we don't scroll past the end
    if (E.row_offset + E.screen_rows > E.buffer.line_count) {
        if (E.buffer.line_count > E.screen_rows) {
            E.row_offset = E.buffer.line_count - E.screen_rows;
        } else {
            E.row_offset = 0;
        }
    }
}

void editor_process_keypress(void) {
    int c = input_read_key();

    if (c == 0) return;

    // Handle mode-specific input
    switch (E.mode) {
        case MODE_NORMAL:
            input_handle_normal(c);
            break;
        case MODE_INSERT:
            input_handle_insert(c);
            break;
        case MODE_COMMAND:
            input_handle_command(c);
            break;
        case MODE_SEARCH:
        case MODE_REPLACE:
            input_handle_search(c);
            break;
    }
}
