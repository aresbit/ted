/**
 * buffer.c - Text buffer management
 */

#include "ted.h"

void buffer_init(buffer_t *buf) {
    buf->lines = SP_NULLPTR;
    buf->line_count = 0;
    buf->line_capacity = 0;
    buf->filename = sp_str_lit("");
    buf->modified = false;
    buf->lang = sp_str_lit("text");
}

void buffer_free(buffer_t *buf) {
    if (!buf) return;

    for (u32 i = 0; i < buf->line_count; i++) {
        if (buf->lines[i].hl) {
            sp_free(buf->lines[i].hl);
        }
    }

    if (buf->lines) {
        sp_free(buf->lines);
    }
}

void buffer_insert_line(buffer_t *buf, u32 at, sp_str_t text) {
    if (at > buf->line_count) {
        at = buf->line_count;
    }

    // Ensure capacity using dynamic array
    if (buf->line_count >= buf->line_capacity) {
        u32 new_cap = buf->line_capacity == 0 ? 16 : buf->line_capacity * 2;
        line_t *new_lines = sp_alloc(sizeof(line_t) * new_cap);
        if (!new_lines) return;

        for (u32 i = 0; i < buf->line_count; i++) {
            new_lines[i] = buf->lines[i];
        }

        if (buf->lines) {
            sp_free(buf->lines);
        }
        buf->lines = new_lines;
        buf->line_capacity = new_cap;
    }

    // Shift lines
    for (u32 i = buf->line_count; i > at; i--) {
        buf->lines[i] = buf->lines[i - 1];
    }

    // Insert new line
    buf->lines[at].text = text;
    buf->lines[at].hl = SP_NULLPTR;
    buf->lines[at].hl_dirty = true;
    buf->line_count++;
    buf->modified = true;
}

void buffer_delete_line(buffer_t *buf, u32 at) {
    if (at >= buf->line_count) return;

    if (buf->lines[at].hl) {
        sp_free(buf->lines[at].hl);
    }

    for (u32 i = at; i < buf->line_count - 1; i++) {
        buf->lines[i] = buf->lines[i + 1];
    }

    buf->line_count--;
    buf->modified = true;
}

void buffer_insert_char_at(buffer_t *buf, u32 row, u32 col, c8 c) {
    if (row >= buf->line_count) return;

    line_t *line = &buf->lines[row];
    sp_str_t old_text = line->text;

    u32 len = old_text.len;
    if (col > len) col = len;

    // Use dyn_mem writer for string building
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t builder = sp_str_builder_from_writer(&writer);

    // Append part before insertion point
    if (col > 0) {
        sp_str_t before = sp_str_sub(old_text, 0, (s32)col);
        sp_str_builder_append(&builder, before);
    }

    // Append new character
    sp_str_builder_append_c8(&builder, c);

    // Append part after insertion point
    if (col < len) {
        sp_str_t after = sp_str_sub(old_text, (s32)col, (s32)(len - col));
        sp_str_builder_append(&builder, after);
    }

    line->text = sp_str_builder_to_str(&builder);
    line->hl_dirty = true;
    buf->modified = true;
}

void buffer_delete_char_at(buffer_t *buf, u32 row, u32 col) {
    if (row >= buf->line_count) return;

    line_t *line = &buf->lines[row];
    u32 len = line->text.len;

    if (col >= len) return;

    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t builder = sp_str_builder_from_writer(&writer);

    if (col > 0) {
        sp_str_t before = sp_str_sub(line->text, 0, (s32)col);
        sp_str_builder_append(&builder, before);
    }

    if (col + 1 < len) {
        sp_str_t after = sp_str_sub(line->text, (s32)(col + 1), (s32)(len - col - 1));
        sp_str_builder_append(&builder, after);
    }

    line->text = sp_str_builder_to_str(&builder);
    line->hl_dirty = true;
    buf->modified = true;
}

sp_str_t buffer_get_line(buffer_t *buf, u32 row) {
    if (row >= buf->line_count) {
        return sp_str_lit("");
    }
    return buf->lines[row].text;
}

u32 buffer_row_to_render(buffer_t *buf, u32 row, u32 col) {
    if (row >= buf->line_count) return col;

    sp_str_t line = buf->lines[row].text;
    u32 render_col = 0;

    for (u32 i = 0; i < col && i < line.len; i++) {
        if (line.data[i] == '\t') {
            render_col += E.config.tab_width - (render_col % E.config.tab_width);
        } else {
            render_col++;
        }
    }

    return render_col;
}

u32 buffer_render_to_row(buffer_t *buf, u32 row, u32 render_col) {
    if (row >= buf->line_count) return render_col;

    sp_str_t line = buf->lines[row].text;
    u32 current_render = 0;
    u32 i;

    for (i = 0; i < line.len && current_render < render_col; i++) {
        if (line.data[i] == '\t') {
            u32 tab_stop = E.config.tab_width - (current_render % E.config.tab_width);
            // If adding this tab would exceed render_col, we're at the right position
            if (current_render + tab_stop > render_col) {
                break;
            }
            current_render += tab_stop;
        } else {
            current_render++;
        }
    }

    // i now points to the character whose render position is >= render_col
    // If we stopped because current_render >= render_col, return i
    // If we stopped because we reached end of line, return line.len
    return i;
}

void buffer_load_file(buffer_t *buf, sp_str_t filename) {
    buffer_free(buf);
    buffer_init(buf);

    buf->filename = filename;

    // Use sp_io_read_file to read entire file
    sp_str_t content = sp_io_read_file(filename);

    if (content.len == 0 && content.data == SP_NULLPTR) {
        // New file - start with empty line
        buffer_insert_line(buf, 0, sp_str_lit(""));
        return;
    }

    // Split content into lines
    u32 start = 0;
    for (u32 i = 0; i < content.len; i++) {
        if (content.data[i] == '\n') {
            u32 line_len = i - start;
            // Handle Windows \r\n
            if (line_len > 0 && content.data[i - 1] == '\r') {
                line_len--;
            }
            sp_str_t line = sp_str_sub(content, (s32)start, (s32)line_len);
            buffer_insert_line(buf, buf->line_count, line);
            start = i + 1;
        }
    }

    // Handle last line (may not end with newline)
    if (start < content.len) {
        sp_str_t line = sp_str_sub(content, (s32)start, (s32)(content.len - start));
        buffer_insert_line(buf, buf->line_count, line);
    }

    // Empty file
    if (buf->line_count == 0) {
        buffer_insert_line(buf, 0, sp_str_lit(""));
    }

    buf->modified = false;

    // Detect language
    language_t *lang = syntax_detect_language(filename);
    if (lang) {
        buf->lang = lang->name;
    }
}

void buffer_save_file(buffer_t *buf) {
    sp_io_writer_t writer = sp_io_writer_from_file(buf->filename, SP_IO_WRITE_MODE_OVERWRITE);

    for (u32 i = 0; i < buf->line_count; i++) {
        sp_io_write_str(&writer, buf->lines[i].text);
        sp_io_write_cstr(&writer, "\n");
    }

    sp_io_flush(&writer);
    sp_io_writer_close(&writer);

    buf->modified = false;
    editor_set_message("Saved file");
}
