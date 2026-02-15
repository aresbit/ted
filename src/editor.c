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

// Forward declarations for static functions
static sp_str_t editor_get_selection(void);
static sp_str_t editor_delete_selection(void);

void editor_init(void) {
    sp_memset(&E, 0, sizeof(E));

    buffer_init(&E.buffer);
    undo_init(&E.undo);
    undo_init(&E.redo);
    search_init();
    E.clipboard = sp_str_lit("");

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

void editor_quit(void) {
    if (E.buffer.modified) {
        editor_set_message("Warning: Unsaved changes will be lost. Use :w to save.");
        display_refresh();
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
        case KEY_UP: // Up
            if (c->row > 0) {
                c->row--;
            }
            break;

        case KEY_DOWN: // Down
            if (c->row + 1 < buf->line_count) {
                c->row++;
            }
            break;

        case KEY_RIGHT: // Right
            if (c->col < buf->lines[c->row].text.len) {
                c->col++;
            } else if (c->row + 1 < buf->line_count) {
                // Move to next line
                c->row++;
                c->col = 0;
            }
            break;

        case KEY_LEFT: // Left
            if (c->col > 0) {
                c->col--;
            } else if (c->row > 0) {
                // Move to end of previous line
                c->row--;
                c->col = buf->lines[c->row].text.len;
            }
            break;

        case KEY_HOME: // Home
            c->col = 0;
            break;

        case KEY_END: // End
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
    u32 gutter = E.config.show_line_numbers ? 5 : 0;
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

    // Clear selection if any
    if (E.has_selection) {
        editor_delete_selection();
    }

    // Record for undo
    undo_record_insert(E.cursor.row, E.cursor.col, c);

    buffer_insert_char_at(&E.buffer, E.cursor.row, E.cursor.col, c);
    E.cursor.col++;
    E.cursor.render_col = buffer_row_to_render(&E.buffer, E.cursor.row, E.cursor.col);
}

void editor_insert_newline(void) {
    if (E.mode != MODE_INSERT) return;
    if (E.cursor.row >= E.buffer.line_count) return;

    // If there's a selection, delete it first
    if (E.has_selection) {
        editor_delete_selection();
    }

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
}

void editor_delete_char(void) {
    if (E.mode != MODE_INSERT) return;

    buffer_t *buf = &E.buffer;
    cursor_t *c = &E.cursor;

    if (c->row >= buf->line_count) return;

    // If there's a selection, delete it
    if (E.has_selection) {
        editor_delete_selection();
        return;
    }

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
    if (E.buffer.line_count == 0) {
        E.cursor.row = 0;
        E.cursor.col = 0;
    } else {
        if (E.cursor.row >= E.buffer.line_count) {
            E.cursor.row = E.buffer.line_count - 1;
        }
        if (E.cursor.col > E.buffer.lines[E.cursor.row].text.len) {
            E.cursor.col = E.buffer.lines[E.cursor.row].text.len;
        }
    }
}

// Get text from current selection
static sp_str_t editor_get_selection(void) {
    if (!E.has_selection) return sp_str_lit("");

    u32 start_row = E.select_start.row;
    u32 start_col = E.select_start.col;
    u32 end_row = E.cursor.row;
    u32 end_col = E.cursor.col;

    // Normalize selection (ensure start <= end)
    if (start_row > end_row || (start_row == end_row && start_col > end_col)) {
        u32 tmp = start_row; start_row = end_row; end_row = tmp;
        tmp = start_col; start_col = end_col; end_col = tmp;
    }

    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t builder = sp_str_builder_from_writer(&writer);

    for (u32 row = start_row; row <= end_row && row < E.buffer.line_count; row++) {
        sp_str_t line = E.buffer.lines[row].text;
        u32 line_len = line.len;

        if (row == start_row && row == end_row) {
            // Single line selection
            u32 len = end_col > line_len ? line_len : end_col;
            if (len > start_col) {
                sp_str_t sub = sp_str_sub(line, (s32)start_col, (s32)(len - start_col));
                sp_str_builder_append(&builder, sub);
            }
        } else if (row == start_row) {
            // First line of multi-line selection
            if (start_col < line_len) {
                sp_str_t sub = sp_str_sub(line, (s32)start_col, (s32)(line_len - start_col));
                sp_str_builder_append(&builder, sub);
            }
            sp_str_builder_append_c8(&builder, '\n');
        } else if (row == end_row) {
            // Last line of multi-line selection
            u32 len = end_col > line_len ? line_len : end_col;
            if (len > 0) {
                sp_str_t sub = sp_str_sub(line, 0, (s32)len);
                sp_str_builder_append(&builder, sub);
            }
        } else {
            // Middle line - take whole line
            sp_str_builder_append(&builder, line);
            sp_str_builder_append_c8(&builder, '\n');
        }
    }

    return sp_str_builder_to_str(&builder);
}

// Delete the current selection and return the deleted text
static sp_str_t editor_delete_selection(void) {
    if (!E.has_selection) return sp_str_lit("");

    u32 start_row = E.select_start.row;
    u32 start_col = E.select_start.col;
    u32 end_row = E.cursor.row;
    u32 end_col = E.cursor.col;

    // Normalize selection
    if (start_row > end_row || (start_row == end_row && start_col > end_col)) {
        u32 tmp = start_row; start_row = end_row; end_row = tmp;
        tmp = start_col; start_col = end_col; end_col = tmp;
    }

    sp_str_t deleted = editor_get_selection();

    if (start_row == end_row) {
        // Single line - just delete characters
        sp_str_t line = E.buffer.lines[start_row].text;
        sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
        sp_str_builder_t builder = sp_str_builder_from_writer(&writer);

        // Part before selection
        if (start_col > 0) {
            sp_str_t before = sp_str_sub(line, 0, (s32)start_col);
            sp_str_builder_append(&builder, before);
        }
        // Part after selection
        u32 line_len = line.len;
        if (end_col < line_len) {
            sp_str_t after = sp_str_sub(line, (s32)end_col, (s32)(line_len - end_col));
            sp_str_builder_append(&builder, after);
        }

        E.buffer.lines[start_row].text = sp_str_builder_to_str(&builder);
        E.buffer.lines[start_row].hl_dirty = true;
        E.buffer.modified = true;

        // Move cursor to start of selection
        E.cursor.row = start_row;
        E.cursor.col = start_col;
    } else {
        // Multi-line selection
        sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
        sp_str_builder_t builder = sp_str_builder_from_writer(&writer);

        // Keep part of first line before selection
        sp_str_t first_line = E.buffer.lines[start_row].text;
        if (start_col > 0 && start_col <= first_line.len) {
            sp_str_t before = sp_str_sub(first_line, 0, (s32)start_col);
            sp_str_builder_append(&builder, before);
        }

        // Append part of last line after selection
        sp_str_t last_line = E.buffer.lines[end_row].text;
        u32 last_len = last_line.len;
        if (end_col < last_len) {
            sp_str_t after = sp_str_sub(last_line, (s32)end_col, (s32)(last_len - end_col));
            sp_str_builder_append(&builder, after);
        }

        // Replace first line with combined content
        E.buffer.lines[start_row].text = sp_str_builder_to_str(&builder);
        E.buffer.lines[start_row].hl_dirty = true;

        // Delete lines between start and end (inclusive of end)
        for (u32 i = end_row; i > start_row; i--) {
            buffer_delete_line(&E.buffer, i);
        }

        E.buffer.modified = true;

        // Move cursor to start of selection
        E.cursor.row = start_row;
        E.cursor.col = start_col;
    }

    E.cursor.render_col = buffer_row_to_render(&E.buffer, E.cursor.row, E.cursor.col);
    E.has_selection = false;
    return deleted;
}

void editor_copy_line(void) {
    if (E.has_selection) {
        // Copy selection
        E.clipboard = editor_get_selection();
        E.has_selection = false;
        editor_set_message("Selection copied");
    } else if (E.cursor.row < E.buffer.line_count) {
        // Copy entire line
        E.clipboard = sp_str_copy(E.buffer.lines[E.cursor.row].text);
        editor_set_message("Line copied to clipboard");
    }
}

void editor_cut_line(void) {
    if (E.has_selection) {
        // Cut selection
        E.clipboard = editor_delete_selection();
        editor_set_message("Selection cut");
    } else if (E.cursor.row < E.buffer.line_count) {
        // Cut entire line
        E.clipboard = sp_str_copy(E.buffer.lines[E.cursor.row].text);
        editor_delete_line(E.cursor.row);
        editor_set_message("Line cut to clipboard");
    }
}

void editor_paste(void) {
    if (E.clipboard.len == 0) {
        editor_set_message("Clipboard is empty");
        return;
    }

    // Ensure cursor is within valid range
    if (E.buffer.line_count == 0) {
        buffer_insert_line(&E.buffer, 0, sp_str_lit(""));
    }
    if (E.cursor.row >= E.buffer.line_count) {
        E.cursor.row = E.buffer.line_count - 1;
    }

    // If there's a selection, delete it first
    if (E.has_selection) {
        editor_delete_selection();
    }

    // Check if clipboard contains newlines
    bool has_newlines = false;
    for (u32 i = 0; i < E.clipboard.len; i++) {
        if (E.clipboard.data[i] == '\n') {
            has_newlines = true;
            break;
        }
    }

    if (has_newlines) {
        // Multi-line paste: split clipboard and insert lines
        u32 start_row = E.cursor.row;
        u32 start_col = E.cursor.col;

        sp_str_t current_line = E.buffer.lines[start_row].text;

        // Split current line at cursor
        sp_str_t before_cursor = sp_str_lit("");
        sp_str_t after_cursor = sp_str_lit("");

        if (start_col > 0 && start_col <= current_line.len) {
            before_cursor = sp_str_sub(current_line, 0, (s32)start_col);
        }
        if (start_col < current_line.len) {
            after_cursor = sp_str_sub(current_line, (s32)start_col, (s32)(current_line.len - start_col));
        }

        // Parse clipboard and insert lines
        u32 line_start = 0;
        u32 inserted_count = 0;
        u32 last_line_len = 0;

        for (u32 i = 0; i <= E.clipboard.len; i++) {
            if (i == E.clipboard.len || E.clipboard.data[i] == '\n') {
                u32 line_len = i - line_start;
                sp_str_t line_text = sp_str_sub(E.clipboard, (s32)line_start, (s32)line_len);

                if (inserted_count == 0) {
                    // First line: prepend before_cursor
                    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
                    sp_str_builder_t builder = sp_str_builder_from_writer(&writer);
                    sp_str_builder_append(&builder, before_cursor);
                    sp_str_builder_append(&builder, line_text);
                    sp_str_t combined = sp_str_builder_to_str(&builder);

                    undo_record_delete_line(start_row, E.buffer.lines[start_row].text);
                    E.buffer.lines[start_row].text = combined;
                    E.buffer.lines[start_row].hl_dirty = true;
                    last_line_len = combined.len;
                } else {
                    // Subsequent lines
                    u32 insert_at = start_row + inserted_count;
                    sp_str_t text_to_insert = line_text;

                    if (i == E.clipboard.len || (i == E.clipboard.len - 1 && E.clipboard.data[i] == '\n')) {
                        // Last line: append after_cursor
                        sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
                        sp_str_builder_t builder = sp_str_builder_from_writer(&writer);
                        sp_str_builder_append(&builder, line_text);
                        sp_str_builder_append(&builder, after_cursor);
                        text_to_insert = sp_str_builder_to_str(&builder);
                    }

                    undo_record_insert_line(insert_at, text_to_insert);
                    buffer_insert_line(&E.buffer, insert_at, text_to_insert);
                    last_line_len = text_to_insert.len;
                }

                line_start = i + 1;
                inserted_count++;
            }
        }

        E.buffer.modified = true;

        // Move cursor to end of pasted content
        if (inserted_count == 1) {
            E.cursor.col = last_line_len;
        } else {
            E.cursor.row = start_row + inserted_count - 1;
            E.cursor.col = last_line_len;
        }
    } else {
        // Single-line paste: insert at cursor position
        u32 row = E.cursor.row;
        u32 col = E.cursor.col;

        sp_str_t line = E.buffer.lines[row].text;
        sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
        sp_str_builder_t builder = sp_str_builder_from_writer(&writer);

        // Part before cursor
        if (col > 0 && col <= line.len) {
            sp_str_t before = sp_str_sub(line, 0, (s32)col);
            sp_str_builder_append(&builder, before);
        }

        // Pasted text
        sp_str_builder_append(&builder, E.clipboard);

        // Part after cursor
        if (col < line.len) {
            sp_str_t after = sp_str_sub(line, (s32)col, (s32)(line.len - col));
            sp_str_builder_append(&builder, after);
        }

        undo_record_delete_line(row, E.buffer.lines[row].text);
        E.buffer.lines[row].text = sp_str_builder_to_str(&builder);
        E.buffer.lines[row].hl_dirty = true;
        E.buffer.modified = true;

        // Move cursor to end of pasted text
        E.cursor.col = col + E.clipboard.len;
    }

    E.cursor.render_col = buffer_row_to_render(&E.buffer, E.cursor.row, E.cursor.col);
    E.has_selection = false;

    editor_set_message("Pasted from clipboard");
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
