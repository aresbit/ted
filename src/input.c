/**
 * input.c - Input handling and key processing
 */

#include "ted.h"
#include <termios.h>
#include <unistd.h>

c8 input_read_key(void) {
    c8 c = 0;
    ssize_t nread;

    // Read one character
    while ((nread = read(STDIN_FILENO, &c, 1)) == -1) {
        if (errno != EAGAIN && errno != EINTR) {
            die("read");
        }
    }

    // Handle escape sequences
    if (c == '\033') {
        c8 seq[3];

        // Try to read the rest of the sequence
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\033';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\033';

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return 0x1000; // Up
                case 'B': return 0x1001; // Down
                case 'C': return 0x1002; // Right
                case 'D': return 0x1003; // Left
                case 'H': return 0x1004; // Home
                case 'F': return 0x1005; // End
                case '3': // Delete key
                    if (read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~') {
                        return 0x1006; // Delete
                    }
                    break;
                case '5': // Ctrl+Home/Page Up
                    if (read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~') {
                        return 0x1007; // Page Up
                    }
                    return '5'; // Ctrl+Home
                case '6': // Ctrl+End/Page Down
                    if (read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~') {
                        return 0x1008; // Page Down
                    }
                    return '6'; // Ctrl+End
            }
        }

        // Not a recognized escape sequence, return Esc
        return '\033';
    }

    return c;
}

void input_handle_normal(c8 c) {
    switch (c) {
        // Navigation
        case 0x1000: // Up
        case 0x1001: // Down
        case 0x1002: // Right
        case 0x1003: // Left
        case 0x1004: // Home
        case 0x1005: // End
        case '5':    // Ctrl+Home
        case '6':    // Ctrl+End
            editor_move_cursor(c);
            break;

        // Mode switching
        case 'i':
            E.mode = MODE_INSERT;
            editor_set_message("-- INSERT --");
            break;

        case ':':
            E.mode = MODE_COMMAND;
            E.command_buffer = sp_str_lit("");
            break;

        // Quick commands
        case '/':
            E.mode = MODE_SEARCH;
            E.search.forward = true;
            E.command_buffer = sp_str_lit("");
            E.search.query = sp_str_lit("");
            break;

        case 'n':
            search_next();
            break;

        case 'N':
            search_prev();
            break;

        // Line navigation
        case 'g':
            E.cursor.row = 0;
            E.cursor.col = 0;
            break;

        case 'G':
            E.cursor.row = E.buffer.line_count - 1;
            E.cursor.col = 0;
            break;

        // Page scroll
        case ' ':
        case 0x1008: // Page Down
            E.row_offset += E.screen_rows;
            if (E.row_offset > E.buffer.line_count) {
                E.row_offset = E.buffer.line_count > 0 ? E.buffer.line_count - 1 : 0;
            }
            E.cursor.row = E.row_offset;
            break;

        case 0x1007: // Page Up
            if (E.row_offset >= E.screen_rows) {
                E.row_offset -= E.screen_rows;
            } else {
                E.row_offset = 0;
            }
            E.cursor.row = E.row_offset;
            break;

        // Delete
        case 'x':
            if (E.cursor.col < E.buffer.lines[E.cursor.row].text.len) {
                buffer_delete_char_at(&E.buffer, E.cursor.row, E.cursor.col);
            }
            break;

        case 'd':
            editor_delete_line(E.cursor.row);
            break;

        // Copy line
        case 'y':
            editor_copy_line();
            break;
    }
}

void input_handle_insert(c8 c) {
    switch (c) {
        // Escape - return to normal mode
        case '\033':
            E.mode = MODE_NORMAL;
            editor_set_message("");
            if (E.cursor.col > 0) {
                E.cursor.col--;
                E.cursor.render_col = buffer_row_to_render(&E.buffer, E.cursor.row, E.cursor.col);
            }
            break;

        // Ctrl+Q - quit
        case 17: // Ctrl+Q
            editor_quit();
            break;

        // Ctrl+S - save
        case 19: // Ctrl+S
            editor_save();
            break;

        // Ctrl+F - search
        case 6: // Ctrl+F
            E.mode = MODE_SEARCH;
            E.search.forward = true;
            E.command_buffer = sp_str_lit("");
            break;

        // Ctrl+G - goto line
        case 7: // Ctrl+G
            E.mode = MODE_COMMAND;
            E.command_buffer = sp_str_lit("goto ");
            break;

        // Ctrl+Z - undo
        case 26: // Ctrl+Z
            undo_perform();
            break;

        // Ctrl+Y - redo
        case 25: // Ctrl+Y
            redo_perform();
            break;

        // Ctrl+D - delete line
        case 4: // Ctrl+D
            editor_delete_line(E.cursor.row);
            break;

        // Ctrl+L - redraw
        case 12: // Ctrl+L
            display_clear();
            break;

        // Enter - new line
        case '\r':
        case '\n':
            editor_insert_newline();
            break;

        // Backspace
        case 127: // DEL
        case 8:   // BS
            editor_delete_char();
            break;

        // Tab
        case '\t':
            editor_insert_char('\t');
            break;

        // Regular character
        default:
            if (c >= 32 && c < 127) {
                editor_insert_char(c);
            }
            break;
    }
}

// Helper to truncate string by one character
static sp_str_t truncate_string(sp_str_t str) {
    if (str.len == 0) return str;
    return sp_str_sub(str, 0, (s32)str.len - 1);
}

void input_handle_command(c8 c) {
    switch (c) {
        case '\033': // Escape - cancel
            E.mode = MODE_NORMAL;
            E.command_buffer = sp_str_lit("");
            editor_set_message("Command cancelled");
            break;

        case '\r': // Enter - execute
        case '\n':
            command_execute(E.command_buffer);
            E.mode = MODE_NORMAL;
            E.command_buffer = sp_str_lit("");
            break;

        case 127: // Backspace
        case 8:
            if (E.command_buffer.len > 0) {
                E.command_buffer = truncate_string(E.command_buffer);
            } else {
                // Empty buffer, cancel command mode
                E.mode = MODE_NORMAL;
            }
            break;

        default:
            if (c >= 32 && c < 127) {
                sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
                sp_str_builder_t builder = sp_str_builder_from_writer(&writer);
                sp_str_builder_append(&builder, E.command_buffer);
                sp_str_builder_append_c8(&builder, c);
                E.command_buffer = sp_str_builder_to_str(&builder);
            }
            break;
    }
}

void input_handle_search(c8 c) {
    switch (c) {
        case '\033': // Escape - cancel
            E.mode = MODE_NORMAL;
            E.command_buffer = sp_str_lit("");
            E.search.query = sp_str_lit("");
            editor_set_message("Search cancelled");
            break;

        case '\r': // Enter - execute search
        case '\n':
            if (E.mode == MODE_SEARCH) {
                search_update_query(E.command_buffer);
                search_next();
            } else if (E.mode == MODE_REPLACE) {
                search_replace_current(E.command_buffer);
            }
            E.mode = MODE_NORMAL;
            E.command_buffer = sp_str_lit("");
            break;

        case 127: // Backspace
        case 8:
            if (E.command_buffer.len > 0) {
                E.command_buffer = truncate_string(E.command_buffer);
            } else {
                E.mode = MODE_NORMAL;
            }
            break;

        default:
            if (c >= 32 && c < 127) {
                sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
                sp_str_builder_t builder = sp_str_builder_from_writer(&writer);
                sp_str_builder_append(&builder, E.command_buffer);
                sp_str_builder_append_c8(&builder, c);
                E.command_buffer = sp_str_builder_to_str(&builder);
            }
            break;
    }
}
