/**
 * display.c - Terminal display and rendering
 */

#include "ted.h"
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define ESC "\033["

// ANSI color codes
#define COLOR_RESET      "\033[0m"
#define COLOR_KEYWORD    "\033[1;34m"   // Bold blue
#define COLOR_STRING     "\033[32m"     // Green
#define COLOR_COMMENT    "\033[90m"     // Gray
#define COLOR_NUMBER     "\033[33m"     // Yellow
#define COLOR_FUNCTION   "\033[35m"     // Magenta
#define COLOR_TYPE       "\033[36m"     // Cyan
#define COLOR_STATUS     "\033[44;37m"  // Blue bg, white fg
#define COLOR_MESSAGE    "\033[1;37m"   // Bold white

static struct termios orig_termios;
static sp_io_writer_t stdout_writer;

void display_clear(void) {
    sp_io_write_cstr(&stdout_writer, ESC "2J");
    sp_io_write_cstr(&stdout_writer, ESC "H");
    sp_io_flush(&stdout_writer);
}

void display_set_cursor(u32 row, u32 col) {
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

    // Check if stdin is a terminal
    if (!isatty(STDIN_FILENO)) {
        die("stdin is not a terminal");
    }

    // Save original terminal settings
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        die("tcgetattr");
    }

    // Enter raw mode
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

    // Get screen size
    E.screen_rows = display_get_screen_rows() - 2; // Reserve for status bars
    E.screen_cols = display_get_screen_cols();
}

void display_draw_rows(void) {
    u32 gutter_width = E.config.show_line_numbers ? 6 : 0;
    u32 text_width = E.screen_cols - gutter_width;

    for (u32 y = 0; y < E.screen_rows; y++) {
        u32 file_row = y + E.row_offset;

        // Move cursor to start of row
        sp_str_t move = sp_format(ESC "{};{}H", SP_FMT_U32(y + 1), SP_FMT_U32(1));
        sp_io_write_str(&stdout_writer, move);

        // Clear line
        sp_io_write_cstr(&stdout_writer, ESC "K");

        if (file_row < E.buffer.line_count) {
            // Draw line number
            if (E.config.show_line_numbers) {
                sp_str_t num = sp_format("{:4} ", SP_FMT_U32(file_row + 1));
                sp_io_write_cstr(&stdout_writer, "\033[90m"); // Gray
                sp_io_write_str(&stdout_writer, num);
                sp_io_write_cstr(&stdout_writer, COLOR_RESET);
            }

            // Get line content
            sp_str_t line = buffer_get_line(&E.buffer, file_row);

            // Apply syntax highlighting
            line_t *line_info = &E.buffer.lines[file_row];
            if (E.config.syntax_enabled && line_info->hl_dirty) {
                language_t *lang = syntax_detect_language(E.buffer.filename);
                syntax_highlight_line(line_info, lang);
                line_info->hl_dirty = false;
            }

            // Handle horizontal scrolling
            u32 col_start = E.col_offset;
            u32 col_end = col_start + text_width;
            if (col_end > line.len) col_end = line.len;

            // Render line with syntax highlighting
            highlight_type_t current_hl = HL_NORMAL;
            sp_io_writer_t out_writer = sp_io_writer_from_dyn_mem();
            sp_str_builder_t output = sp_str_builder_from_writer(&out_writer);

            for (u32 i = col_start; i < col_end; i++) {
                c8 c = line.data[i];

                // Apply syntax color
                if (E.config.syntax_enabled && line_info->hl) {
                    highlight_type_t hl = line_info->hl[i];
                    if (hl != current_hl) {
                        // Flush current buffer
                        sp_str_t segment = sp_str_builder_to_str(&output);
                        sp_io_write_str(&stdout_writer, segment);

                        // Reset and create new builder
                        out_writer = sp_io_writer_from_dyn_mem();
                        output = sp_str_builder_from_writer(&out_writer);

                        // Change color
                        sp_io_write_cstr(&stdout_writer, syntax_color_to_ansi(hl));
                        current_hl = hl;
                    }
                }

                // Handle tabs
                if (c == '\t') {
                    u32 spaces = E.config.tab_width - ((i - col_start) % E.config.tab_width);
                    for (u32 s = 0; s < spaces; s++) {
                        sp_str_builder_append_c8(&output, ' ');
                    }
                } else {
                    sp_str_builder_append_c8(&output, c);
                }
            }

            // Flush remaining content
            sp_str_t segment = sp_str_builder_to_str(&output);
            sp_io_write_str(&stdout_writer, segment);
            sp_io_write_cstr(&stdout_writer, COLOR_RESET);

        } else {
            // Empty line (past end of file)
            if (E.config.show_line_numbers) {
                sp_io_write_cstr(&stdout_writer, "~    ");
            }
        }
    }
}

void display_draw_status_bar(void) {
    // Move to status bar position
    sp_str_t move = sp_format(ESC "{};{}H", SP_FMT_U32(E.screen_rows + 1), SP_FMT_U32(1));
    sp_io_write_str(&stdout_writer, move);

    // Set status bar color
    sp_io_write_cstr(&stdout_writer, COLOR_STATUS);

    // Build status string
    sp_str_t filename = E.buffer.filename.len > 0 ? E.buffer.filename : sp_str_lit("[No Name]");
    sp_str_t modified = E.buffer.modified ? sp_str_lit("[+]") : sp_str_lit("");

    sp_str_t left = sp_format(" {} {} | Ln {}, Col {} | {} ",
        SP_FMT_STR(filename),
        SP_FMT_STR(modified),
        SP_FMT_U32(E.cursor.row + 1),
        SP_FMT_U32(E.cursor.col + 1),
        SP_FMT_STR(E.buffer.lang));

    // Calculate right-aligned info
    sp_str_t right = sp_format(" {} lines ",
        SP_FMT_U32(E.buffer.line_count));

    // Pad to fill screen
    u32 padding = E.screen_cols - left.len - right.len;
    if ((s32)padding < 0) padding = 0;

    sp_io_write_str(&stdout_writer, left);
    for (u32 i = 0; i < padding; i++) {
        sp_io_write_cstr(&stdout_writer, " ");
    }
    sp_io_write_str(&stdout_writer, right);

    // Reset color
    sp_io_write_cstr(&stdout_writer, COLOR_RESET);
}

void display_draw_message_bar(void) {
    // Move to message bar position
    sp_str_t move = sp_format(ESC "{};{}H", SP_FMT_U32(E.screen_rows + 2), SP_FMT_U32(1));
    sp_io_write_str(&stdout_writer, move);

    // Clear line
    sp_io_write_cstr(&stdout_writer, ESC "K");

    // Show command buffer if in command/search mode
    switch (E.mode) {
        case MODE_COMMAND: {
            sp_str_t msg = sp_format(":{}", SP_FMT_STR(E.command_buffer));
            sp_io_write_str(&stdout_writer, msg);
            break;
        }
        case MODE_SEARCH: {
            sp_str_t msg = sp_format("/{} ({} matches)",
                SP_FMT_STR(E.search.query),
                SP_FMT_U32(E.search.match_count));
            sp_io_write_str(&stdout_writer, msg);
            break;
        }
        case MODE_REPLACE: {
            sp_str_t msg = sp_format("Replace: {} -> {}",
                SP_FMT_STR(E.search.query),
                SP_FMT_STR(E.command_buffer));
            sp_io_write_str(&stdout_writer, msg);
            break;
        }
        default: {
            // Show status message
            if (E.message.len > 0) {
                sp_io_write_cstr(&stdout_writer, COLOR_MESSAGE);
                sp_io_write_str(&stdout_writer, E.message);
                sp_io_write_cstr(&stdout_writer, COLOR_RESET);
            }
            break;
        }
    }
}

void display_refresh(void) {
    // Hide cursor
    sp_io_write_cstr(&stdout_writer, ESC "?25l");

    // Draw content
    display_draw_rows();
    display_draw_status_bar();
    display_draw_message_bar();

    // Position cursor
    u32 cursor_row = E.cursor.row - E.row_offset;
    u32 cursor_col = E.cursor.render_col - E.col_offset;

    if (E.config.show_line_numbers) {
        cursor_col += 5; // Account for gutter
    }

    // Adjust for command/search mode input
    if (E.mode == MODE_COMMAND || E.mode == MODE_SEARCH || E.mode == MODE_REPLACE) {
        cursor_row = E.screen_rows + 1;
        cursor_col = E.command_buffer.len + 1;
        if (E.mode == MODE_SEARCH) cursor_col++; // For '/' prefix
        if (E.mode == MODE_REPLACE) cursor_col += E.search.query.len + 5; // For "Replace: " prefix
    }

    display_set_cursor(cursor_row, cursor_col);

    // Show cursor
    sp_io_write_cstr(&stdout_writer, ESC "?25h");
    sp_io_flush(&stdout_writer);
}
