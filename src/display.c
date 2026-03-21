/**
 * display.c - Terminal display and rendering
 * FIXED VERSION
 */

#include "ted.h"
#include "cui.h"
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#define ESC "\033["

static struct termios orig_termios;
static sp_io_writer_t stdout_writer;
static const cui_theme_t *CUI;
static bool G_stdin_is_tty = false;
static bool G_raw_mode_enabled = false;

static u32 display_panel_rows(void) {
    return iui_tui_panel_rows();
}

static u32 display_content_row0(void) {
    return display_panel_rows();
}

static const c8* display_context_hint(void) {
    if (sketch_is_enabled()) {
        return "Sketch mode. Drag mouse to draw. :sketch auto|line|rect|square|ellipse|circle";
    }
    if (E.mode != MODE_NORMAL) return "";
    if (E.buffer.modified) {
        return "Unsaved changes. Press Ctrl+S to save.";
    }
    if (E.buffer.filename.len == 0 || sp_str_equal(E.buffer.filename, sp_str_lit("[No Name]"))) {
        return "i to start typing, :w <file> to save, / to search.";
    }
    return "i edit, / search, :help for commands.";
}

static bool display_show_empty_state(void) {
    if (E.mode != MODE_NORMAL) return false;
    if (E.buffer.line_count != 1) return false;
    if (E.buffer.lines[0].text.len != 0) return false;
    return (E.buffer.filename.len == 0 ||
            sp_str_equal(E.buffer.filename, sp_str_lit("[No Name]")));
}

static void display_draw_centered(u32 row, const c8 *text, const c8 *style) {
    u32 len = (u32)strlen(text);
    u32 col = len < E.screen_cols ? (E.screen_cols - len) / 2 : 0;
    display_set_cursor(display_content_row0() + row, col);
    if (style) {
        sp_io_write_cstr(&stdout_writer, style);
    }
    sp_io_write_cstr(&stdout_writer, text);
    if (style) {
        sp_io_write_cstr(&stdout_writer, CUI->reset);
    }
}

// Check if a character at (file_row, file_col) is within selection
static bool is_selected(u32 file_row, u32 file_col) {
    if (!E.has_selection) return false;

    // Normalize selection start/end
    u32 start_row = E.select_start.row;
    u32 start_col = E.select_start.col;
    u32 end_row = E.cursor.row;
    u32 end_col = E.cursor.col;

    if (start_row > end_row || (start_row == end_row && start_col > end_col)) {
        // Swap start and end
        u32 tmp = start_row; start_row = end_row; end_row = tmp;
        tmp = start_col; start_col = end_col; end_col = tmp;
    }

    // Check if position is within selection bounds
    if (file_row < start_row || file_row > end_row) return false;

    if (file_row == start_row && file_row == end_row) {
        // Single line selection
        return file_col >= start_col && file_col < end_col;
    } else if (file_row == start_row) {
        // First line of multi-line selection
        return file_col >= start_col;
    } else if (file_row == end_row) {
        // Last line of multi-line selection
        return file_col < end_col;
    } else {
        // Middle line - entirely selected
        return true;
    }
}

// Restore terminal on exit
static void cleanup_terminal(void) {
    iui_tui_shutdown();
    if (G_raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    }

    // Clear screen and move cursor to top
    sp_io_writer_t out = sp_io_writer_from_fd(STDOUT_FILENO, SP_IO_CLOSE_MODE_NONE);
    sp_io_write_cstr(&out, ESC "2J");
    sp_io_write_cstr(&out, ESC "H");
    if (G_stdin_is_tty) {
        sp_io_write_cstr(&out, ESC "?1000l");
        sp_io_write_cstr(&out, ESC "?1002l");
        sp_io_write_cstr(&out, ESC "?1003l");
        sp_io_write_cstr(&out, ESC "?1006l");
    }
    sp_io_write_cstr(&out, ESC "?25h"); // Show cursor
    sp_io_flush(&out);
}

// Signal handler for clean exit
static void handle_sigint(int sig) {
    (void)sig;
    cleanup_terminal();
    exit(0);
}

static void handle_sigfatal(int sig) {
    cleanup_terminal();
    signal(sig, SIG_DFL);
    raise(sig);
}

void display_clear(void) {
    sp_io_write_cstr(&stdout_writer, ESC "2J");
    sp_io_write_cstr(&stdout_writer, ESC "H");
    sp_io_flush(&stdout_writer);
}

void display_set_cursor(u32 row, u32 col) {
    // ANSI escape sequences are 1-indexed
    sp_str_t pos = sp_format(ESC "{};{}H", SP_FMT_U32(row + 1), SP_FMT_U32(col + 1));
    sp_io_write_str(&stdout_writer, pos);
}

u32 display_get_screen_rows(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_row == 0) {
        return 24; // Default
    }
    return ws.ws_row;
}

u32 display_get_screen_cols(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return 80; // Default
    }
    return ws.ws_col;
}

void display_init(void) {
    // Initialize stdout writer
    stdout_writer = sp_io_writer_from_fd(STDOUT_FILENO, SP_IO_CLOSE_MODE_NONE);
    CUI = cui_theme_jobs();

    G_stdin_is_tty = isatty(STDIN_FILENO);

    // Register cleanup
    atexit(cleanup_terminal);
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigfatal);
    signal(SIGABRT, handle_sigfatal);
    signal(SIGSEGV, handle_sigfatal);

    if (G_stdin_is_tty) {
        if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
            die("tcgetattr");
        }

        struct termios raw = orig_termios;
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
            die("tcsetattr");
        }
        G_raw_mode_enabled = true;

        // Enable pointer reporting for desktop mouse + mobile touch forwarding:
        // 1000: press/release, 1002: button-motion drag, 1003: any-motion, 1006: SGR coords
        sp_io_write_cstr(&stdout_writer, ESC "?1000h");
        sp_io_write_cstr(&stdout_writer, ESC "?1002h");
        sp_io_write_cstr(&stdout_writer, ESC "?1003h");
        sp_io_write_cstr(&stdout_writer, ESC "?1006h");
    }

    // Get screen size
    u32 reserved_rows = display_panel_rows() + 1; // toolbar + message bar
    u32 total_rows = display_get_screen_rows();
    E.screen_rows = total_rows > reserved_rows ? total_rows - reserved_rows : 1;
    E.screen_cols = display_get_screen_cols();
    iui_tui_init(E.screen_cols, display_panel_rows());

    // Clear screen initially
    display_clear();
}

void display_draw_rows(void) {
    if (sketch_is_enabled()) {
        sketch_draw_canvas(&stdout_writer);
        return;
    }

    u32 gutter_width = E.config.show_line_numbers ? 5 : 0;
    u32 text_width = E.screen_cols - gutter_width;
    bool need_rehighlight = false;

    if (E.config.syntax_enabled) {
        for (u32 i = 0; i < E.buffer.line_count; i++) {
            if (!E.buffer.lines[i].hl_dirty) continue;
            need_rehighlight = true;
            break;
        }
    }
    if (need_rehighlight) {
        if (!treesitter_highlight_buffer(&E.buffer)) {
            syntax_highlight_buffer(&E.buffer);
        }
    }

    for (u32 y = 0; y < E.screen_rows; y++) {
        u32 file_row = y + E.row_offset;

        // Move cursor to start of row
        display_set_cursor(display_content_row0() + y, 0);

        // Clear line
        sp_io_write_cstr(&stdout_writer, ESC "K");

        if (file_row < E.buffer.line_count) {
            // Draw line number
            if (E.config.show_line_numbers) {
                sp_str_t num = sp_format("{:pad 4} ", SP_FMT_U32(file_row + 1));
                sp_io_write_cstr(&stdout_writer, CUI->fg_muted);
                sp_io_write_str(&stdout_writer, num);
                sp_io_write_cstr(&stdout_writer, CUI->reset);
            }

            // Get line content
            sp_str_t line = buffer_get_line(&E.buffer, file_row);

            // line_info->hl is maintained by buffer-level rehighlight above.
            line_t *line_info = &E.buffer.lines[file_row];

            // Handle horizontal scrolling
            u32 col_start = E.col_offset;
            u32 col_end = col_start + text_width;
            if (col_end > line.len) col_end = line.len;

            // Render line with syntax highlighting and selection
            highlight_type_t current_hl = HL_NORMAL;
            bool in_selection = false;

            for (u32 i = col_start; i < col_end; i++) {
                c8 c = line.data[i];
                bool selected = is_selected(file_row, i);

                // Apply syntax color if enabled
                if (E.config.syntax_enabled && line_info->hl && i < line.len) {
                    highlight_type_t hl = line_info->hl[i];
                    if (hl != current_hl) {
                        sp_io_write_cstr(&stdout_writer, syntax_color_to_ansi(hl));
                        current_hl = hl;
                    }
                }

                // Apply selection highlight
                if (selected != in_selection) {
                    if (selected) {
                        sp_io_write_cstr(&stdout_writer, "\033[7m"); // Reverse video
                    } else {
                        sp_io_write_cstr(&stdout_writer, "\033[27m"); // Reset reverse video
                    }
                    in_selection = selected;
                }

                // Handle tabs
                if (c == '\t') {
                    u32 tab_pos = i - col_start;
                    u32 spaces = E.config.tab_width - (tab_pos % E.config.tab_width);
                    for (u32 s = 0; s < spaces; s++) {
                        sp_io_write_cstr(&stdout_writer, " ");
                    }
                } else if (c >= 32 && c < 127) {
                    sp_io_writer_t c_writer = sp_io_writer_from_fd(STDOUT_FILENO, SP_IO_CLOSE_MODE_NONE);
                    sp_io_write(&c_writer, &c, 1);
                } else {
                    sp_io_write_cstr(&stdout_writer, ".");
                }
            }

            // Reset selection highlight if still active at end of line
            if (in_selection) {
                sp_io_write_cstr(&stdout_writer, "\033[27m");
            }

            // Reset color at end of line
            sp_io_write_cstr(&stdout_writer, CUI->reset);

        } else {
            if (display_show_empty_state()) {
                u32 hero_row = E.screen_rows / 2;
                if (hero_row > 3 && y == hero_row - 2) {
                    display_draw_centered(y, "TED", CUI->fg_accent);
                } else if (hero_row > 2 && y == hero_row) {
                    display_draw_centered(y, "Focus on what matters.", CUI->fg_text);
                } else if (hero_row + 2 < E.screen_rows && y == hero_row + 2) {
                    display_draw_centered(y, "i start typing   :help commands   Ctrl+S save", CUI->fg_muted);
                }
            } else {
                sp_io_write_cstr(&stdout_writer, CUI->fg_muted);
                sp_io_write_cstr(&stdout_writer, "~");
                sp_io_write_cstr(&stdout_writer, CUI->reset);
            }
        }
    }
}

void display_draw_status_bar(void) {
    iui_tui_resize(E.screen_cols, display_panel_rows());
    iui_tui_draw_toolbar();
    iui_tui_blit(&stdout_writer, 0);
}

void display_draw_message_bar(void) {
    // Move to message bar position
    display_set_cursor(display_content_row0() + E.screen_rows, 0);

    // Clear line
    sp_io_write_cstr(&stdout_writer, ESC "K");

    // Show command buffer if in command/search mode
    switch (E.mode) {
        case MODE_COMMAND: {
            sp_io_write_cstr(&stdout_writer, CUI->fg_accent);
            sp_io_write_cstr(&stdout_writer, ":");
            sp_io_write_str(&stdout_writer, E.command_buffer);
            sp_io_write_cstr(&stdout_writer, CUI->reset);
            if (E.command_hint.len > 0) {
                sp_io_write_cstr(&stdout_writer, "  | ");
                sp_io_write_cstr(&stdout_writer, CUI->fg_muted);
                sp_io_write_str(&stdout_writer, E.command_hint);
                sp_io_write_cstr(&stdout_writer, CUI->reset);
            }
            break;
        }
        case MODE_SEARCH: {
            sp_io_write_cstr(&stdout_writer, CUI->fg_accent);
            sp_io_write_cstr(&stdout_writer, "/");
            sp_io_write_str(&stdout_writer, E.command_buffer);
            sp_io_write_cstr(&stdout_writer, CUI->reset);
            if (E.search.match_count > 0) {
                sp_str_t count = sp_format(" ({} matches)", SP_FMT_U32(E.search.match_count));
                sp_io_write_cstr(&stdout_writer, CUI->fg_muted);
                sp_io_write_str(&stdout_writer, count);
                sp_io_write_cstr(&stdout_writer, CUI->reset);
            }
            break;
        }
        case MODE_REPLACE: {
            sp_io_write_cstr(&stdout_writer, CUI->fg_accent);
            sp_io_write_cstr(&stdout_writer, "Replace: ");
            sp_io_write_str(&stdout_writer, E.search.query);
            sp_io_write_cstr(&stdout_writer, " -> ");
            sp_io_write_str(&stdout_writer, E.command_buffer);
            sp_io_write_cstr(&stdout_writer, CUI->reset);
            break;
        }
        default: {
            // Show status message
            if (E.message.len > 0) {
                sp_io_write_cstr(&stdout_writer, CUI->fg_text);
                
                // Truncate message if too long
                u32 max_len = E.screen_cols - 2;
                if (E.message.len > max_len) {
                    sp_str_t truncated = cui_truncate_ascii(E.message, max_len);
                    sp_io_write_str(&stdout_writer, truncated);
                } else {
                    sp_io_write_str(&stdout_writer, E.message);
                }
                
                sp_io_write_cstr(&stdout_writer, CUI->reset);
            } else {
                const c8 *hint = display_context_hint();
                if (hint[0] != '\0') {
                    sp_io_write_cstr(&stdout_writer, CUI->fg_muted);
                    sp_io_write_cstr(&stdout_writer, hint);
                    sp_io_write_cstr(&stdout_writer, CUI->reset);
                }
            }
            break;
        }
    }
}

void display_refresh(void) {
    // Update screen size (in case of resize)
    u32 reserved_rows = display_panel_rows() + 1;
    u32 total_rows = display_get_screen_rows();
    u32 new_rows = total_rows > reserved_rows ? total_rows - reserved_rows : 1;
    u32 new_cols = display_get_screen_cols();
    if (new_rows != E.screen_rows || new_cols != E.screen_cols) {
        E.screen_rows = new_rows;
        E.screen_cols = new_cols;
        iui_tui_resize(E.screen_cols, display_panel_rows());
    }

    // Hide cursor during update
    sp_io_write_cstr(&stdout_writer, ESC "?25l");

    // Draw content
    display_draw_rows();
    display_draw_status_bar();
    display_draw_message_bar();

    // Calculate cursor position (with bounds checking)
    u32 cursor_row = (E.cursor.row >= E.row_offset) ? (E.cursor.row - E.row_offset) : 0;
    u32 cursor_col = (E.cursor.render_col >= E.col_offset) ? (E.cursor.render_col - E.col_offset) : 0;

    // Account for line number gutter
    if (E.config.show_line_numbers) {
        cursor_col += 5; // "1234 " format (4 digits + 1 space)
    }

    // Adjust for command/search mode input
    if (E.mode == MODE_COMMAND || E.mode == MODE_SEARCH || E.mode == MODE_REPLACE) {
        cursor_row = display_content_row0() + E.screen_rows;
        cursor_col = E.command_buffer.len;
        if (E.mode == MODE_COMMAND) cursor_col += 1; // For ':' prefix
        if (E.mode == MODE_SEARCH) cursor_col += 1; // For '/' prefix
        if (E.mode == MODE_REPLACE) cursor_col += E.search.query.len + 12; // For "Replace: ... -> "
    }

    // Ensure cursor is within bounds (for normal/edit mode, not command/search)
    if (E.mode != MODE_COMMAND && E.mode != MODE_SEARCH && E.mode != MODE_REPLACE) {
        if (sketch_is_enabled()) {
            cursor_row = display_content_row0() + E.screen_rows;
            cursor_col = 0;
        } else {
            if (cursor_row >= E.screen_rows) cursor_row = E.screen_rows - 1;
            if (cursor_col >= E.screen_cols) cursor_col = E.screen_cols - 1;
            cursor_row += display_content_row0();
        }
    }

    display_set_cursor(cursor_row, cursor_col);

    // Show cursor
    sp_io_write_cstr(&stdout_writer, ESC "?25h");
    sp_io_flush(&stdout_writer);
}
