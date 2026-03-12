/**
 * treesitter.c - Built-in tree-sitter bridge
 *
 * Native integration:
 * - tree-sitter runtime is linked at build time.
 * - tree-sitter-c grammar is linked at build time.
 * - no dlopen/grammar .so lookup at runtime.
 */

#include "ted.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include <tree_sitter/api.h>

// Provided by vendor/tree-sitter-c/src/parser.c
const TSLanguage *tree_sitter_c(void);

typedef struct {
    TSParser *parser;
    bool available;
    bool enabled;
    const TSLanguage *active_lang;
    c8 active_grammar[32];
    sp_str_t last_status;
} treesitter_runtime_t;

static treesitter_runtime_t G_ts = {0};

static const c8 *TS_C_KEYWORDS[] = {
    "if", "else", "switch", "case", "default", "while", "do", "for",
    "return", "break", "continue", "goto", "typedef", "extern", "static",
    "const", "volatile", "restrict", "sizeof", "enum", "struct", "union",
    "inline", "register", "auto", "signed", "unsigned", "short", "long",
    "void", "int", "char", "float", "double", "_Atomic", "_Alignas",
    "_Alignof", "_Generic", "_Noreturn", "_Static_assert", NULL
};

static void ts_set_status(const c8 *fmt, ...) {
    c8 buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    G_ts.last_status = sp_str_from_cstr(buf);
}

static void ts_shutdown(void) {
    if (G_ts.parser) {
        ts_parser_delete(G_ts.parser);
        G_ts.parser = SP_NULLPTR;
    }
}

static bool ts_select_language_for_buffer(buffer_t *buf) {
    G_ts.active_lang = SP_NULLPTR;
    G_ts.active_grammar[0] = '\0';

    if (!buf || buf->lang.len == 0) {
        ts_set_status("no buffer language");
        return false;
    }

    // Native support in current phase: C only.
    if (sp_str_equal(buf->lang, sp_str_lit("c"))) {
        G_ts.active_lang = tree_sitter_c();
        if (!G_ts.active_lang) {
            ts_set_status("built-in c grammar missing");
            return false;
        }
        snprintf(G_ts.active_grammar, sizeof(G_ts.active_grammar), "c");
        return true;
    }

    ts_set_status("no built-in grammar for %.*s", (int)buf->lang.len, buf->lang.data);
    return false;
}

static bool ts_is_c_keyword(const c8 *type) {
    for (u32 i = 0; TS_C_KEYWORDS[i]; i++) {
        if (strcmp(type, TS_C_KEYWORDS[i]) == 0) return true;
    }
    return false;
}

static bool ts_type_starts_with(const c8 *type, const c8 *prefix) {
    if (!type || !prefix) return false;
    size_t n = strlen(prefix);
    return strncmp(type, prefix, n) == 0;
}

static highlight_type_t ts_map_c_node(TSNode node) {
    const c8 *type = ts_node_type(node);
    if (!type) return HL_NORMAL;

    if (strcmp(type, "comment") == 0) return HL_COMMENT;
    if (strcmp(type, "string_literal") == 0 ||
        strcmp(type, "system_lib_string") == 0 ||
        strcmp(type, "char_literal") == 0) return HL_STRING;
    if (strcmp(type, "number_literal") == 0) return HL_NUMBER;
    if (strcmp(type, "primitive_type") == 0 ||
        strcmp(type, "type_identifier") == 0 ||
        strcmp(type, "sized_type_specifier") == 0) return HL_TYPE;
    if (ts_type_starts_with(type, "preproc_")) return HL_KEYWORD;
    if (strcmp(type, "attribute_specifier") == 0) return HL_KEYWORD;
    if (strcmp(type, "field_identifier") == 0) return HL_FUNCTION;
    if (ts_is_c_keyword(type)) return HL_KEYWORD;

    if (strcmp(type, "identifier") == 0) {
        TSNode p = ts_node_parent(node);
        if (!ts_node_is_null(p)) {
            const c8 *pt = ts_node_type(p);
            if (pt && (strcmp(pt, "call_expression") == 0 ||
                       strcmp(pt, "function_declarator") == 0 ||
                       strcmp(pt, "preproc_function_def") == 0)) {
                return HL_FUNCTION;
            }
        }
    }

    return HL_NORMAL;
}

static void ts_mark_span(buffer_t *buf, TSPoint start, TSPoint end, highlight_type_t hl) {
    if (hl == HL_NORMAL) return;
    if (start.row >= buf->line_count) return;
    if (end.row >= buf->line_count) end.row = buf->line_count - 1;
    if (start.row > end.row) return;

    for (u32 row = start.row; row <= end.row; row++) {
        line_t *line = &buf->lines[row];
        if (!line->hl || line->text.len == 0) continue;

        u32 from = 0;
        u32 to = line->text.len;
        if (row == start.row) from = start.column;
        if (row == end.row) to = end.column;
        if (from > line->text.len) from = line->text.len;
        if (to > line->text.len) to = line->text.len;
        if (to < from) continue;

        for (u32 col = from; col < to; col++) {
            // Keep comments/strings strongest if already set.
            if (line->hl[col] == HL_COMMENT || line->hl[col] == HL_STRING) continue;
            line->hl[col] = hl;
        }
    }
}

static void ts_walk_and_highlight_c(buffer_t *buf, TSNode node) {
    if (ts_node_is_null(node)) return;

    highlight_type_t hl = ts_map_c_node(node);
    if (hl != HL_NORMAL) {
        TSPoint s = ts_node_start_point(node);
        TSPoint e = ts_node_end_point(node);
        ts_mark_span(buf, s, e, hl);
    }

    u32 child_count = ts_node_child_count(node);
    for (u32 i = 0; i < child_count; i++) {
        ts_walk_and_highlight_c(buf, ts_node_child(node, i));
    }
}

static void ts_prepare_highlight_arrays(buffer_t *buf) {
    for (u32 i = 0; i < buf->line_count; i++) {
        line_t *line = &buf->lines[i];
        if (line->hl) {
            sp_free(line->hl);
            line->hl = SP_NULLPTR;
        }
        if (line->text.len > 0) {
            line->hl = sp_alloc(sizeof(highlight_type_t) * line->text.len);
            if (line->hl) {
                for (u32 j = 0; j < line->text.len; j++) line->hl[j] = HL_NORMAL;
            }
        }
        line->hl_dirty = false;
    }
}

static sp_str_t ts_buffer_text(buffer_t *buf) {
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    for (u32 i = 0; i < buf->line_count; i++) {
        sp_str_builder_append(&b, buf->lines[i].text);
        if (i + 1 < buf->line_count) sp_str_builder_append_c8(&b, '\n');
    }
    return sp_str_builder_to_str(&b);
}

void treesitter_init(void) {
    G_ts.enabled = false;
    G_ts.available = false;
    G_ts.last_status = sp_str_lit("not initialized");
    G_ts.active_lang = SP_NULLPTR;
    G_ts.active_grammar[0] = '\0';

    G_ts.parser = ts_parser_new();
    if (!G_ts.parser) {
        ts_set_status("failed to create parser");
        return;
    }

    G_ts.available = true;
    ts_set_status("built-in runtime ready (c grammar)");
    atexit(ts_shutdown);
}

bool treesitter_set_enabled(bool enable, sp_str_t *reason) {
    if (!G_ts.available) {
        if (reason) *reason = G_ts.last_status;
        return false;
    }

    G_ts.enabled = enable;
    if (reason) *reason = sp_str_lit("");
    if (enable) {
        ts_set_status("enabled");
    } else {
        ts_set_status("disabled");
    }
    return true;
}

bool treesitter_is_enabled(void) {
    return G_ts.enabled && G_ts.available;
}

bool treesitter_is_available(void) {
    return G_ts.available;
}

sp_str_t treesitter_status(void) {
    if (!G_ts.available) {
        return sp_format("unavailable ({})", SP_FMT_STR(G_ts.last_status));
    }
    if (!G_ts.enabled) {
        return sp_format("available, disabled ({})", SP_FMT_STR(G_ts.last_status));
    }
    if (G_ts.active_grammar[0] == '\0') {
        return sp_format("enabled, no grammar ({})", SP_FMT_STR(G_ts.last_status));
    }
    return sp_format("enabled [{}] ({})", SP_FMT_CSTR(G_ts.active_grammar), SP_FMT_STR(G_ts.last_status));
}

bool treesitter_highlight_buffer(buffer_t *buf) {
    if (!buf || !treesitter_is_enabled()) return false;
    if (!ts_select_language_for_buffer(buf)) return false;

    if (!ts_parser_set_language(G_ts.parser, G_ts.active_lang)) {
        ts_set_status("failed to set %s grammar", G_ts.active_grammar);
        return false;
    }

    sp_str_t text = ts_buffer_text(buf);
    TSTree *tree = ts_parser_parse_string(G_ts.parser, SP_NULLPTR, text.data, text.len);
    if (!tree) {
        ts_set_status("parse failed (%s)", G_ts.active_grammar);
        return false;
    }

    ts_prepare_highlight_arrays(buf);

    if (strcmp(G_ts.active_grammar, "c") == 0) {
        TSNode root = ts_tree_root_node(tree);
        ts_walk_and_highlight_c(buf, root);
    }

    ts_tree_delete(tree);
    ts_set_status("highlighted %s (%u lines)", G_ts.active_grammar, buf->line_count);
    return true;
}
