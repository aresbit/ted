/**
 * command.c - Command mode handling
 *
 * Command shell design:
 * - parse command + arg once
 * - dispatch by command registry table
 * - handlers call functional editor core primitives
 */

#include "ted.h"

typedef bool (*command_handler_fn)(sp_str_t arg);

typedef struct {
    const c8 *name;
    command_handler_fn handler;
} command_spec_t;

typedef struct {
    sp_str_t name;
    sp_str_t code;
} dynamic_command_t;

#define DYN_COMMAND_CAP 64
static dynamic_command_t G_dyn_commands[DYN_COMMAND_CAP];
static u32 G_dyn_command_count = 0;
static sp_str_t G_last_llm_response = SP_LIT("");

void command_init(void) {
    // Commands are registered statically in COMMANDS table.
}

static sp_str_t get_arg(sp_str_t cmd) {
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

static u32 parse_u32(sp_str_t s) {
    u32 value = 0;
    for (u32 i = 0; i < s.len; i++) {
        if (s.data[i] < '0' || s.data[i] > '9') continue;
        value = value * 10 + (u32)(s.data[i] - '0');
    }
    return value;
}

bool command_register_js(sp_str_t name, sp_str_t code) {
    if (name.len == 0 || code.len == 0) return false;

    // Update existing registration
    for (u32 i = 0; i < G_dyn_command_count; i++) {
        if (!sp_str_equal(G_dyn_commands[i].name, name)) continue;
        G_dyn_commands[i].code = sp_str_copy(code);
        return true;
    }

    if (G_dyn_command_count >= DYN_COMMAND_CAP) {
        return false;
    }

    G_dyn_commands[G_dyn_command_count].name = sp_str_copy(name);
    G_dyn_commands[G_dyn_command_count].code = sp_str_copy(code);
    G_dyn_command_count++;
    return true;
}

static bool command_try_execute_dynamic(sp_str_t name, sp_str_t arg) {
    for (u32 i = 0; i < G_dyn_command_count; i++) {
        if (!sp_str_equal(G_dyn_commands[i].name, name)) continue;

        sp_str_t out = sp_str_lit("");
        sp_str_t err = sp_str_lit("");
        if (!ext_invoke_registered_command(G_dyn_commands[i].code, arg, &out, &err)) {
            editor_set_message("Plugin command error: %.*s", (int)err.len, err.data);
            return true;
        }

        if (out.len > 0) {
            editor_set_message("%.*s", (int)out.len, out.data);
        }
        return true;
    }

    return false;
}

static bool cmd_write(sp_str_t arg) {
    if (arg.len > 0) {
        E.buffer.filename = arg;
    }
    editor_save();
    return true;
}

static bool cmd_quit(sp_str_t arg) {
    (void)arg;
    editor_quit();
    return true;
}

static bool cmd_write_quit(sp_str_t arg) {
    (void)arg;
    editor_save();
    editor_quit();
    return true;
}

static bool cmd_force_quit(sp_str_t arg) {
    (void)arg;
    display_clear();
    exit(0);
}

static bool cmd_goto(sp_str_t arg) {
    if (arg.len == 0) {
        editor_set_message("Usage: :goto <line>");
        return true;
    }
    editor_goto_line(parse_u32(arg));
    return true;
}

static bool cmd_set(sp_str_t arg) {
    if (sp_str_equal(arg, sp_str_lit("nu")) || sp_str_equal(arg, sp_str_lit("number"))) {
        E.config.show_line_numbers = true;
        editor_set_message("Line numbers enabled");
    } else if (sp_str_equal(arg, sp_str_lit("nonu")) || sp_str_equal(arg, sp_str_lit("nonumber"))) {
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
        editor_set_message("Unknown option: %.*s", (int)arg.len, arg.data);
    }
    return true;
}

static bool cmd_syntax(sp_str_t arg) {
    if (sp_str_equal(arg, sp_str_lit("on"))) {
        E.config.syntax_enabled = true;
        editor_set_message("Syntax highlighting enabled");
    } else if (sp_str_equal(arg, sp_str_lit("off"))) {
        E.config.syntax_enabled = false;
        editor_set_message("Syntax highlighting disabled");
    } else if (sp_str_equal(arg, sp_str_lit("tree on"))) {
        sp_str_t reason = sp_str_lit("");
        if (!treesitter_set_enabled(true, &reason)) {
            editor_set_message("tree-sitter unavailable: %.*s", (int)reason.len, reason.data);
        } else {
            editor_set_message("tree-sitter enabled");
        }
    } else if (sp_str_equal(arg, sp_str_lit("tree off"))) {
        sp_str_t reason = sp_str_lit("");
        treesitter_set_enabled(false, &reason);
        editor_set_message("tree-sitter disabled");
    } else if (sp_str_equal(arg, sp_str_lit("tree status"))) {
        sp_str_t s = treesitter_status();
        editor_set_message("tree-sitter: %.*s", (int)s.len, s.data);
    } else if (sp_str_equal(arg, sp_str_lit("tree inspect"))) {
        sp_str_t s = treesitter_describe_cursor(&E.buffer, E.cursor.row, E.cursor.col);
        editor_set_message("tree-sitter: %.*s", (int)s.len, s.data);
    } else {
        editor_set_message("Usage: :syntax on|off|tree on|tree off|tree status|tree inspect");
    }
    return true;
}

static bool cmd_edit(sp_str_t arg) {
    if (arg.len == 0) {
        editor_set_message("No filename specified");
        return true;
    }
    if (E.buffer.modified) {
        editor_set_message("Unsaved changes! Use :w first or :e! to force");
        return true;
    }
    editor_open(arg);
    return true;
}

static bool cmd_edit_force(sp_str_t arg) {
    if (arg.len == 0) {
        editor_set_message("No filename specified");
        return true;
    }
    editor_open(arg);
    return true;
}

static bool cmd_help(sp_str_t arg) {
    (void)arg;
    editor_set_message("TED v" TED_VERSION " | :llm :js :source");
    return true;
}

static bool cmd_agent(sp_str_t arg) {
    (void)arg;
    if (E.buffer.modified) {
        editor_set_message("Agent: You have unsaved changes. Press Ctrl+S or use :w");
        return true;
    }
    if (E.buffer.filename.len == 0 || sp_str_equal(E.buffer.filename, sp_str_lit("[No Name]"))) {
        editor_set_message("Agent: This buffer is unnamed. Use :w <filename> to save");
        return true;
    }
    if (E.search.query.len > 0) {
        editor_set_message("Agent: Search is ready. Use n/N for next/prev result");
        return true;
    }
    editor_set_message("Agent: i to edit, / to search, :set nu to toggle line numbers");
    return true;
}

static bool cmd_js(sp_str_t arg) {
    if (arg.len == 0) {
        editor_set_message("Usage: :js <code>");
        return true;
    }

    sp_str_t out = sp_str_lit("");
    sp_str_t err = sp_str_lit("");
    if (!ext_eval(arg, &out, &err)) {
        editor_set_message("JS error: %.*s", (int)err.len, err.data);
        return true;
    }

    if (out.len > 0) {
        editor_set_message("JS: %.*s", (int)out.len, out.data);
    } else {
        editor_set_message("JS executed");
    }
    return true;
}

static bool cmd_llm(sp_str_t arg) {
    if (arg.len == 0) {
        editor_set_message("Usage: :llm <prompt>");
        return true;
    }

    sp_str_t out = sp_str_lit("");
    sp_str_t err = sp_str_lit("");
    if (!llm_query(arg, true, &out, &err)) {
        editor_set_message("LLM error: %.*s", (int)err.len, err.data);
        return true;
    }
    if (out.len == 0) {
        editor_set_message("LLM returned empty response");
        return true;
    }

    G_last_llm_response = sp_str_copy(out);
    if (out.len <= 100) {
        editor_set_message("LLM: %.*s | :llmcopy to copy", (int)out.len, out.data);
    } else {
        editor_set_message("LLM ready (%u chars). Use :llmshow or :llmcopy", out.len);
    }
    return true;
}

static bool cmd_llmstatus(sp_str_t arg) {
    (void)arg;
    sp_str_t s = llm_status();
    editor_set_message("LLM: %.*s", (int)s.len, s.data);
    return true;
}

static bool cmd_llmshow(sp_str_t arg) {
    (void)arg;
    if (G_last_llm_response.len == 0) {
        editor_set_message("No LLM response yet. Use :llm <prompt>");
        return true;
    }

    u32 max_len = E.screen_cols > 20 ? E.screen_cols - 10 : 70;
    if (G_last_llm_response.len <= max_len) {
        editor_set_message("LLM: %.*s", (int)G_last_llm_response.len, G_last_llm_response.data);
    } else {
        editor_set_message("LLM: %.*s...", (int)max_len, G_last_llm_response.data);
    }
    return true;
}

static bool cmd_llmcopy(sp_str_t arg) {
    (void)arg;
    if (G_last_llm_response.len == 0) {
        editor_set_message("No LLM response to copy. Use :llm <prompt>");
        return true;
    }

    E.clipboard = sp_str_copy(G_last_llm_response);
    editor_set_message("LLM response copied to clipboard (%u chars)", G_last_llm_response.len);
    return true;
}

static bool cmd_source(sp_str_t arg) {
    if (arg.len == 0) {
        editor_set_message("Usage: :source <file.js>");
        return true;
    }

    sp_str_t out = sp_str_lit("");
    sp_str_t err = sp_str_lit("");
    if (!ext_run_file(arg, &out, &err)) {
        editor_set_message("Source error: %.*s", (int)err.len, err.data);
        return true;
    }

    if (out.len > 0) {
        editor_set_message("Source: %.*s", (int)out.len, out.data);
    } else {
        editor_set_message("Source executed");
    }
    return true;
}

static bool cmd_plugins(sp_str_t arg) {
    (void)arg;
    sp_str_t err = sp_str_lit("");
    u32 n = ext_autoload_plugins(&err);
    sp_str_t list = ext_list_loaded_plugins();
    if (err.len > 0) {
        if (list.len > 0) {
            editor_set_message("Plugins: loaded %u [%.*s], last error: %.*s",
                               n, (int)list.len, list.data, (int)err.len, err.data);
        } else {
            editor_set_message("Plugins: loaded %u, last error: %.*s", n, (int)err.len, err.data);
        }
    } else {
        if (list.len > 0) {
            editor_set_message("Plugins: loaded %u [%.*s]", n, (int)list.len, list.data);
        } else {
            editor_set_message("Plugins: loaded %u", n);
        }
    }
    return true;
}

static bool cmd_langs(sp_str_t arg) {
    (void)arg;
    sp_str_t langs = syntax_list_languages();
    if (langs.len == 0) {
        editor_set_message("Languages: (none)");
    } else {
        editor_set_message("Languages: %.*s", (int)langs.len, langs.data);
    }
    return true;
}

static bool cmd_targets(sp_str_t arg) {
    (void)arg;
    sp_str_t seqs = input_list_operator_targets();
    if (seqs.len == 0) {
        editor_set_message("Operator targets: (none)");
    } else {
        editor_set_message("Operator targets: %.*s", (int)seqs.len, seqs.data);
    }
    return true;
}

static bool cmd_recognizers(sp_str_t arg) {
    (void)arg;
    sp_str_t list = ext_list_recognizers();
    if (list.len == 0) {
        editor_set_message("Recognizers: (none)");
    } else {
        editor_set_message("Recognizers: %.*s", (int)list.len, list.data);
    }
    return true;
}

static bool cmd_sketch(sp_str_t arg) {
    if (arg.len == 0 || sp_str_equal(arg, sp_str_lit("status"))) {
        sp_str_t s = sketch_status();
        editor_set_message("%.*s", (int)s.len, s.data);
        return true;
    }

    if (sp_str_equal(arg, sp_str_lit("on"))) {
        sketch_set_enabled(true);
        editor_set_message("Sketch mode enabled");
        return true;
    }
    if (sp_str_equal(arg, sp_str_lit("off"))) {
        sketch_set_enabled(false);
        editor_set_message("Sketch mode disabled");
        return true;
    }
    if (sp_str_equal(arg, sp_str_lit("clear"))) {
        sketch_clear();
        editor_set_message("Sketch canvas cleared");
        return true;
    }

    if (sketch_set_mode_name(arg)) {
        sketch_set_enabled(true);
        sp_str_t mode = sketch_mode_name();
        editor_set_message("Sketch recognizer set to %.*s", (int)mode.len, mode.data);
        return true;
    }

    editor_set_message("Usage: :sketch on|off|clear|status|auto|line|rect|square|ellipse|circle");
    return true;
}

static const command_spec_t COMMANDS[] = {
    { "w", cmd_write },
    { "write", cmd_write },
    { "q", cmd_quit },
    { "quit", cmd_quit },
    { "wq", cmd_write_quit },
    { "q!", cmd_force_quit },
    { "goto", cmd_goto },
    { "g", cmd_goto },
    { "set", cmd_set },
    { "syntax", cmd_syntax },
    { "e", cmd_edit },
    { "edit", cmd_edit },
    { "e!", cmd_edit_force },
    { "edit!", cmd_edit_force },
    { "help", cmd_help },
    { "h", cmd_help },
    { "agent", cmd_agent },
    { "llm", cmd_llm },
    { "llmshow", cmd_llmshow },
    { "llmcopy", cmd_llmcopy },
    { "llmstatus", cmd_llmstatus },
    { "js", cmd_js },
    { "source", cmd_source },
    { "plugins", cmd_plugins },
    { "langs", cmd_langs },
    { "targets", cmd_targets },
    { "recognizers", cmd_recognizers },
    { "sketch", cmd_sketch },
};

void command_execute(sp_str_t cmd) {
    if (cmd.len == 0) return;

    sp_str_t name = get_command(cmd);
    sp_str_t arg = get_arg(cmd);

    // Dynamic plugin commands take precedence.
    if (command_try_execute_dynamic(name, arg)) {
        return;
    }

    for (u32 i = 0; i < (u32)(sizeof(COMMANDS) / sizeof(COMMANDS[0])); i++) {
        if (!sp_str_equal(name, sp_str_from_cstr(COMMANDS[i].name))) continue;
        COMMANDS[i].handler(arg);
        return;
    }

    editor_set_message("Unknown command: %.*s", (int)name.len, name.data);
}

void command_show_prompt(void) {
    // Handled in display_draw_message_bar
}
