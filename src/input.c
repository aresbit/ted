/**
 * input.c - Input handling and key processing
 * FIXED VERSION
 */

#include "ted.h"
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

// Check if input is available without blocking
static bool input_available(void) {
    struct timeval tv = {0, 0};  // No wait
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

int input_read_key(void) {
    int c = 0;
    ssize_t nread;

    // Wait for input
    while (!input_available()) {
        usleep(10000); // Sleep 10ms to avoid busy waiting
    }

    // Read one character
    nread = read(STDIN_FILENO, &c, 1);
    if (nread != 1) {
        return 0;
    }

    // Handle escape sequences
    if (c == '\033') {
        // Check if there's more input (escape sequence)
        if (!input_available()) {
            return '\033'; // Just ESC key
        }

        c8 seq[16]; // Buffer for escape sequence
        u32 seq_len = 0;

        // Read the first character after ESC
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\033';
        seq_len = 1;

        if (seq[0] == '[') {
            // Read the rest of the sequence until we get a command character
            while (seq_len < sizeof(seq) - 1) {
                if (!input_available()) {
                    // No more data, wait a bit
                    usleep(1000);
                    if (!input_available()) break;
                }
                if (read(STDIN_FILENO, &seq[seq_len], 1) != 1) break;

                // Check if this is a command character (A-Z, a-z, ~)
                c8 c = seq[seq_len];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~') {
                    seq_len++;
                    break;
                }
                seq_len++;
            }

            // Null-terminate for easier debugging (not strictly needed)
            seq[seq_len] = '\0';

            // Parse the sequence
            // Format can be:
            // - Simple: "A", "B", "C", "D", "H", "F"
            // - With numeric prefix: "1~", "3~", "4~", "5~", "6~"
            // - With modifiers: "1;2A" (where ;2 is Shift modifier)

            // Check for modifiers (sequence contains ';')
            bool has_modifier = false;
            u32 modifier = 0;
            for (u32 i = 0; i < seq_len; i++) {
                if (seq[i] == ';') {
                    has_modifier = true;
                    // Parse modifier number after ';'
                    if (i + 1 < seq_len && seq[i + 1] >= '0' && seq[i + 1] <= '9') {
                        modifier = seq[i + 1] - '0';
                    }
                    break;
                }
            }

            // Get the command character (last character in sequence)
            c8 cmd = seq[seq_len - 1];

            // Map command to key code
            switch (cmd) {
                case 'A': // Up
                    return has_modifier && modifier == 2 ? KEY_SHIFT_UP : KEY_UP;
                case 'B': // Down
                    return has_modifier && modifier == 2 ? KEY_SHIFT_DOWN : KEY_DOWN;
                case 'C': // Right
                    return has_modifier && modifier == 2 ? KEY_SHIFT_RIGHT : KEY_RIGHT;
                case 'D': // Left
                    return has_modifier && modifier == 2 ? KEY_SHIFT_LEFT : KEY_LEFT;
                case 'H': // Home
                    return has_modifier && modifier == 2 ? KEY_SHIFT_HOME : KEY_HOME;
                case 'F': // End
                    return has_modifier && modifier == 2 ? KEY_SHIFT_END : KEY_END;
                case '~': // Special keys with numeric prefix
                    if (seq_len >= 2) {
                        switch (seq[0]) {
                            case '1': return KEY_HOME;    // Home (alternate)
                            case '3': return KEY_DELETE;  // Delete
                            case '4': return KEY_END;     // End (alternate)
                            case '5': return KEY_PAGE_UP;
                            case '6': return KEY_PAGE_DOWN;
                        }
                    }
                    break;
            }
        }

        return '\033';
    }

    return c;
}

void input_handle_normal(int c) {
    switch (c) {
        // Navigation (without Shift)
        case KEY_UP:
        case KEY_DOWN:
        case KEY_RIGHT:
        case KEY_LEFT:
        case KEY_HOME:
        case KEY_END:
            editor_move_cursor(c);
            break;

        // Navigation with Shift (extend selection)
        case KEY_SHIFT_UP:
        case KEY_SHIFT_DOWN:
        case KEY_SHIFT_RIGHT:
        case KEY_SHIFT_LEFT:
        case KEY_SHIFT_HOME:
        case KEY_SHIFT_END:
            // Start or extend selection
            if (!E.has_selection) {
                E.select_start = E.cursor;
                E.has_selection = true;
            }
            editor_move_cursor(c - 0x100); // Convert to regular key code for movement
            break;

        // Vim-style navigation
        case 'h':
            editor_move_cursor(KEY_LEFT); // Left
            break;
        case 'j':
            editor_move_cursor(KEY_DOWN); // Down
            break;
        case 'k':
            editor_move_cursor(KEY_UP); // Up
            break;
        case 'l':
            editor_move_cursor(KEY_RIGHT); // Right
            break;

        // Mode switching
        case 'i':
        case 'a':
            E.mode = MODE_INSERT;
            if (c == 'a' && E.cursor.col < E.buffer.lines[E.cursor.row].text.len) {
                E.cursor.col++;
            }
            editor_set_message("-- INSERT --");
            break;

        case 'A': // Insert at end of line
            E.mode = MODE_INSERT;
            E.cursor.col = E.buffer.lines[E.cursor.row].text.len;
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
            if (E.buffer.line_count > 0) {
                E.cursor.row = E.buffer.line_count - 1;
            }
            E.cursor.col = 0;
            break;

        // Page scroll
        case ' ':
        case KEY_PAGE_DOWN: // Page Down
            if (E.buffer.line_count > 0) {
                E.row_offset += E.screen_rows;
                if (E.row_offset >= E.buffer.line_count) {
                    E.row_offset = E.buffer.line_count - 1;
                }
                E.cursor.row = E.row_offset;
            }
            break;

        case KEY_PAGE_UP: // Page Up
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
            if (E.buffer.line_count > 1) {
                editor_delete_line(E.cursor.row);
            }
            break;

        // Copy line
        case 'y':
            editor_copy_line();
            break;

        // Quit (in normal mode, allow q/Q)
        case 'q':
        case 'Q':
            editor_quit();
            break;

    }
}

void input_handle_insert(int c) {
    switch (c) {
        // Escape - return to normal mode
        case '\033':
            E.mode = MODE_NORMAL;
            editor_set_message("");
            if (E.cursor.col > 0 && 
                E.cursor.col == E.buffer.lines[E.cursor.row].text.len) {
                E.cursor.col--;
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
            if (E.buffer.line_count > 1) {
                editor_delete_line(E.cursor.row);
            }
            break;

        // Ctrl+L - redraw
        case 12: // Ctrl+L
            display_clear();
            break;

        // Arrow keys in insert mode (without Shift)
        case KEY_UP:
        case KEY_DOWN:
        case KEY_RIGHT:
        case KEY_LEFT:
        case KEY_HOME:
        case KEY_END:
            E.has_selection = false;
            editor_move_cursor(c);
            break;

        // Arrow keys with Shift in insert mode (extend selection)
        case KEY_SHIFT_UP:
        case KEY_SHIFT_DOWN:
        case KEY_SHIFT_RIGHT:
        case KEY_SHIFT_LEFT:
        case KEY_SHIFT_HOME:
        case KEY_SHIFT_END:
            // Start or extend selection
            if (!E.has_selection) {
                E.select_start = E.cursor;
                E.has_selection = true;
            }
            editor_move_cursor(c - 0x100); // Convert to regular key code for movement
            break;


        // Delete key
        case KEY_DELETE:
            if (E.cursor.col < E.buffer.lines[E.cursor.row].text.len) {
                buffer_delete_char_at(&E.buffer, E.cursor.row, E.cursor.col);
            } else if (E.cursor.row + 1 < E.buffer.line_count) {
                // Join with next line
                sp_str_t current = E.buffer.lines[E.cursor.row].text;
                sp_str_t next = E.buffer.lines[E.cursor.row + 1].text;
                
                sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
                sp_str_builder_t builder = sp_str_builder_from_writer(&writer);
                sp_str_builder_append(&builder, current);
                sp_str_builder_append(&builder, next);
                E.buffer.lines[E.cursor.row].text = sp_str_builder_to_str(&builder);
                
                buffer_delete_line(&E.buffer, E.cursor.row + 1);
            }
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

        // Clipboard operations
        case 3: // Ctrl+C
            editor_copy_line();
            break;
        case 24: // Ctrl+X
            editor_cut_line();
            break;
        case 22: // Ctrl+V
            editor_paste();
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

void input_handle_command(int c) {
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

void input_handle_search(int c) {
    switch (c) {
        case '\033': // Escape - cancel
            E.mode = MODE_NORMAL;
            E.command_buffer = sp_str_lit("");
            editor_set_message("Search mode exited");
            break;

        case '\r': // Enter - execute search
        case '\n':
            if (E.mode == MODE_SEARCH) {
                // Only update query if it changed
                if (!sp_str_equal(E.command_buffer, E.search.query)) {
                    search_update_query(E.command_buffer);
                }
                search_next();
                // Stay in search mode to allow pressing Enter again for next match
                // User can press Esc to exit search mode
            } else if (E.mode == MODE_REPLACE) {
                search_replace_current(E.command_buffer);
                E.mode = MODE_NORMAL;
                E.command_buffer = sp_str_lit("");
            }
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
