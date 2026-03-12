/**
 * search.c - Search and replace functionality
 */

#include "ted.h"

static c8 normalize_char(c8 c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static bool search_match_at(sp_str_t line, u32 col, sp_str_t query, bool case_sensitive) {
    if (col + query.len > line.len) return false;

    for (u32 i = 0; i < query.len; i++) {
        c8 lc = line.data[col + i];
        c8 qc = query.data[i];
        if (!case_sensitive) {
            lc = normalize_char(lc);
            qc = normalize_char(qc);
        }
        if (lc != qc) return false;
    }
    return true;
}

void search_init(void) {
    E.search.query = sp_str_lit("");
    E.search.current_match = 0;
    E.search.match_count = 0;
    E.search.case_sensitive = false;
    E.search.forward = true;
}

void search_update_query(sp_str_t query) {
    E.search.query = query;
    E.search.current_match = 0;

    // Count matches
    E.search.match_count = 0;
    if (query.len == 0) return;

    for (u32 row = 0; row < E.buffer.line_count; row++) {
        sp_str_t line = E.buffer.lines[row].text;
        u32 col = 0;

        while (col + query.len <= line.len) {
            if (search_match_at(line, col, query, E.search.case_sensitive)) {
                E.search.match_count++;
                col += query.len;
            } else {
                col++;
            }
        }
    }
}

void search_next(void) {
    if (E.search.query.len == 0) return;

    u32 start_row = E.cursor.row;
    u32 start_col = E.cursor.col + 1;
    bool wrapped = false;

    for (u32 pass = 0; pass < 2; pass++) {
        u32 row_begin = (pass == 0) ? start_row : 0;
        u32 row_end = (pass == 0) ? E.buffer.line_count : (start_row + 1);

        for (u32 row = row_begin; row < row_end; row++) {
            sp_str_t line = E.buffer.lines[row].text;
            u32 col = 0;
            u32 col_limit = line.len;

            if (pass == 0 && row == start_row) {
                col = start_col;
            } else if (pass == 1 && row == start_row) {
                col_limit = start_col;
            }

            while (col + E.search.query.len <= col_limit) {
                if (!search_match_at(line, col, E.search.query, E.search.case_sensitive)) {
                    col++;
                    continue;
                }

                E.cursor.row = row;
                E.cursor.col = col;
                E.cursor.render_col = buffer_row_to_render(&E.buffer, row, col);
                E.search.current_match++;

                // Adjust scroll
                if (row < E.row_offset) {
                    E.row_offset = row;
                } else if (row >= E.row_offset + E.screen_rows) {
                    E.row_offset = row - E.screen_rows / 2;
                }

                if (wrapped) {
                    editor_set_message("Match found (wrapped)");
                } else {
                    editor_set_message("Match found");
                }
                return;
            }
        }

        wrapped = true;
    }

    editor_set_message("Pattern not found");
}

void search_prev(void) {
    if (E.search.query.len == 0) return;

    E.search.forward = false;

    u32 start_row = E.cursor.row;
    u32 start_col = (E.cursor.col > 0) ? E.cursor.col - 1 : 0;
    bool wrapped = false;

    for (u32 pass = 0; pass < 2; pass++) {
        s32 row_begin = (pass == 0) ? (s32)start_row : (s32)(E.buffer.line_count - 1);
        s32 row_end = (pass == 0) ? 0 : (s32)start_row;

        for (s32 row = row_begin; row >= row_end; row--) {
            sp_str_t line = E.buffer.lines[row].text;
            s32 col = (s32)(line.len - E.search.query.len);

            if (pass == 0 && row == (s32)start_row) {
                s32 max_col = (s32)start_col - (s32)E.search.query.len + 1;
                if (max_col < col) col = max_col;
            } else if (pass == 1 && row == (s32)start_row) {
                col = (s32)(line.len - E.search.query.len);
            }

            if (col < 0) continue;

            while (col >= 0) {
                if (!search_match_at(line, (u32)col, E.search.query, E.search.case_sensitive)) {
                    col--;
                    continue;
                }

                E.cursor.row = (u32)row;
                E.cursor.col = (u32)col;
                E.cursor.render_col = buffer_row_to_render(&E.buffer, (u32)row, (u32)col);

                if ((u32)row < E.row_offset) {
                    E.row_offset = (u32)row;
                } else if ((u32)row >= E.row_offset + E.screen_rows) {
                    E.row_offset = (u32)row - E.screen_rows / 2;
                }

                if (wrapped) {
                    editor_set_message("Previous match found (wrapped)");
                } else {
                    editor_set_message("Previous match found");
                }
                return;
            }
        }

        wrapped = true;
    }

    editor_set_message("Pattern not found");
}

void search_replace_current(sp_str_t replacement) {
    if (E.search.query.len == 0) return;

    // Find match at current position
    u32 row = E.cursor.row;
    u32 col = E.cursor.col;

    if (row >= E.buffer.line_count) return;

    sp_str_t line = E.buffer.lines[row].text;

    // Verify match at this position
    bool match = true;
    for (u32 i = 0; i < E.search.query.len; i++) {
        if (col + i >= line.len || line.data[col + i] != E.search.query.data[i]) {
            match = false;
            break;
        }
    }

    if (!match) {
        editor_set_message("No match at cursor position");
        return;
    }

    // Build new line with replacement
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t builder = sp_str_builder_from_writer(&writer);

    // Part before match
    if (col > 0) {
        sp_str_builder_append(&builder, sp_str_sub(line, 0, (s32)col));
    }

    // Replacement
    sp_str_builder_append(&builder, replacement);

    // Part after match
    if (col + E.search.query.len < line.len) {
        sp_str_builder_append(&builder,
            sp_str_sub(line, (s32)(col + E.search.query.len),
                (s32)(line.len - col - E.search.query.len)));
    }

    // Update line
    E.buffer.lines[row].text = sp_str_builder_to_str(&builder);
    E.buffer.lines[row].hl_dirty = true;
    E.buffer.modified = true;

    editor_set_message("Replaced match");
}

void search_replace_all(sp_str_t replacement) {
    if (E.search.query.len == 0) return;

    u32 count = 0;

    for (u32 row = 0; row < E.buffer.line_count; row++) {
        sp_str_t line = E.buffer.lines[row].text;
        sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
        sp_str_builder_t new_line = sp_str_builder_from_writer(&writer);
        u32 col = 0;

        while (col <= line.len) {
            // Check for match at this position
            bool match = true;
            if (col + E.search.query.len > line.len) {
                match = false;
            } else {
                for (u32 i = 0; i < E.search.query.len; i++) {
                    c8 lc = line.data[col + i];
                    c8 qc = E.search.query.data[i];
                    if (!E.search.case_sensitive) {
                        lc = (lc >= 'A' && lc <= 'Z') ? lc + 32 : lc;
                        qc = (qc >= 'A' && qc <= 'Z') ? qc + 32 : qc;
                    }
                    if (lc != qc) {
                        match = false;
                        break;
                    }
                }
            }

            if (match) {
                sp_str_builder_append(&new_line, replacement);
                col += E.search.query.len;
                count++;
            } else {
                if (col < line.len) {
                    sp_str_builder_append_c8(&new_line, line.data[col]);
                }
                col++;
            }
        }

        E.buffer.lines[row].text = sp_str_builder_to_str(&new_line);
        E.buffer.lines[row].hl_dirty = true;
    }

    E.buffer.modified = true;
    editor_set_message("Replaced %u occurrences", count);
}

void search_end(void) {
    E.search.query = sp_str_lit("");
    E.search.match_count = 0;
}
