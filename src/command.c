/**
 * command.c - Command mode handling
 */

#include "ted.h"

void command_init(void) {
    // Commands are parsed dynamically
}

static bool starts_with(sp_str_t str, const c8 *prefix) {
    u32 len = (u32)strlen(prefix);
    if (str.len < len) return false;
    return strncmp(str.data, prefix, len) == 0;
}

static sp_str_t get_arg(sp_str_t cmd) {
    // Find first space
    for (u32 i = 0; i < cmd.len; i++) {
        if (cmd.data[i] == ' ') {
            if (i + 1 < cmd.len) {
                return sp_str_sub(cmd, (s32)(i + 1), (s32)(cmd.len - i - 1));
            }
            break;
        }
    }
    return sp_str_lit("");
}

static sp_str_t get_command(sp_str_t cmd) {
    for (u32 i = 0; i < cmd.len; i++) {
        if (cmd.data[i] == ' ') {
            return sp_str_sub(cmd, 0, (s32)i);
        }
    }
    return cmd;
}

void command_execute(sp_str_t cmd) {
    if (cmd.len == 0) return;

    sp_str_t command = get_command(cmd);
    sp_str_t arg = get_arg(cmd);

    // :w - save
    if (sp_str_equal(command, sp_str_lit("w")) ||
        sp_str_equal(command, sp_str_lit("write"))) {
        if (arg.len > 0) {
            E.buffer.filename = sp_str_copy(arg);
        }
        editor_save();
        return;
    }

    // :q - quit
    if (sp_str_equal(command, sp_str_lit("q")) ||
        sp_str_equal(command, sp_str_lit("quit"))) {
        editor_quit();
        return;
    }

    // :wq - save and quit
    if (sp_str_equal(command, sp_str_lit("wq"))) {
        if (arg.len > 0) {
            E.buffer.filename = sp_str_copy(arg);
        }
        if (editor_save()) {
            editor_quit();
        }
        return;
    }

    // :q! - force quit
    if (sp_str_equal(command, sp_str_lit("q!"))) {
        display_clear();
        exit(0);
    }

    // :goto N - goto line
    if (sp_str_equal(command, sp_str_lit("goto")) ||
        sp_str_equal(command, sp_str_lit("g"))) {
        if (arg.len > 0) {
            u32 line = 0;
            for (u32 i = 0; i < arg.len; i++) {
                if (arg.data[i] >= '0' && arg.data[i] <= '9') {
                    line = line * 10 + (arg.data[i] - '0');
                }
            }
            editor_goto_line(line);
        }
        return;
    }

    // :set nu / :set nonu - toggle line numbers
    if (sp_str_equal(command, sp_str_lit("set"))) {
        if (sp_str_equal(arg, sp_str_lit("nu")) ||
            sp_str_equal(arg, sp_str_lit("number"))) {
            E.config.show_line_numbers = true;
            editor_set_message("Line numbers enabled");
        } else if (sp_str_equal(arg, sp_str_lit("nonu")) ||
                   sp_str_equal(arg, sp_str_lit("nonumber"))) {
            E.config.show_line_numbers = false;
            editor_set_message("Line numbers disabled");
        } else if (sp_str_equal(arg, sp_str_lit("syntax"))) {
            E.config.syntax_enabled = true;
            editor_set_message("Syntax highlighting enabled");
        } else if (sp_str_equal(arg, sp_str_lit("nosyntax"))) {
            E.config.syntax_enabled = false;
            editor_set_message("Syntax highlighting disabled");
        } else if (sp_str_equal(arg, sp_str_lit("wrap"))) {
            E.config.auto_wrap = true;
            editor_set_message("Auto wrap enabled");
        } else if (sp_str_equal(arg, sp_str_lit("nowrap"))) {
            E.config.auto_wrap = false;
            editor_set_message("Auto wrap disabled");
        } else {
            editor_set_message("Unknown option: {}", SP_FMT_STR(arg));
        }
        return;
    }

    // :syntax on/off
    if (sp_str_equal(command, sp_str_lit("syntax"))) {
        if (sp_str_equal(arg, sp_str_lit("on"))) {
            E.config.syntax_enabled = true;
            editor_set_message("Syntax highlighting enabled");
        } else if (sp_str_equal(arg, sp_str_lit("off"))) {
            E.config.syntax_enabled = false;
            editor_set_message("Syntax highlighting disabled");
        }
        return;
    }

    // :e filename - open file
    if (sp_str_equal(command, sp_str_lit("e")) ||
        sp_str_equal(command, sp_str_lit("edit"))) {
        if (arg.len > 0) {
            // Save current buffer if modified
            if (E.buffer.modified) {
                editor_set_message("Unsaved changes! Use :w first or :e! to force");
                return;
            }
            editor_open(arg);
        } else {
            editor_set_message("No filename specified");
        }
        return;
    }

    // :e! filename - force open
    if (sp_str_equal(command, sp_str_lit("e!")) ||
        sp_str_equal(command, sp_str_lit("edit!"))) {
        if (arg.len > 0) {
            editor_open(arg);
        } else {
            editor_set_message("No filename specified");
        }
        return;
    }

    // :help - show help
    if (sp_str_equal(command, sp_str_lit("help")) ||
        sp_str_equal(command, sp_str_lit("h"))) {
        editor_set_message("TED v" TED_VERSION " | Ctrl+Q=quit Ctrl+S=save Ctrl+F=search Ctrl+G=goto");
        return;
    }

    // Unknown command
    editor_set_message("Unknown command: {}", SP_FMT_STR(command));
}

void command_show_prompt(void) {
    // Handled in display_draw_message_bar
}
