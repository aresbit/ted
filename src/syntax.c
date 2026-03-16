/**
 * syntax.c - Syntax highlighting for TED editor
 * FIXED VERSION
 */

#include "ted.h"
#include <ctype.h>
#include <string.h>

// C/C++ keywords
static const c8 *c_keywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while", "_Alignas", "_Alignof",
    "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary", "_Noreturn",
    "_Static_assert", "_Thread_local", "class", "public", "private",
    "protected", "virtual", "override", "template", "typename", "namespace",
    "using", "new", "delete", "try", "catch", "throw", "nullptr", "true",
    "false", "bool", "const_cast", "dynamic_cast", "reinterpret_cast",
    "static_cast", "explicit", "friend", "mutable", "operator", "this",
    "typeid", "decltype", "constexpr", "noexcept", "static_assert",
    "alignas", "alignof", "char8_t", "char16_t", "char32_t", "concept",
    "co_await", "co_return", "co_yield", "consteval", "constinit",
    "export", "import", "module", "requires", "include", "define", NULL
};

// Python keywords
static const c8 *python_keywords[] = {
    "False", "None", "True", "and", "as", "assert", "async", "await",
    "break", "class", "continue", "def", "del", "elif", "else", "except",
    "finally", "for", "from", "global", "if", "import", "in", "is",
    "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
    "while", "with", "yield", NULL
};

// JavaScript keywords
static const c8 *js_keywords[] = {
    "break", "case", "catch", "class", "const", "continue", "debugger",
    "default", "delete", "do", "else", "export", "extends", "finally",
    "for", "function", "if", "import", "in", "instanceof", "new",
    "return", "super", "switch", "this", "throw", "try", "typeof",
    "var", "void", "while", "with", "yield", "let", "static", "await",
    "async", "of", "null", "true", "false", "undefined", "NaN",
    "Infinity", NULL
};

// Shell keywords
static const c8 *sh_keywords[] = {
    "if", "then", "else", "elif", "fi", "case", "esac", "for", "select",
    "while", "until", "do", "done", "in", "function", "time", "{", "}",
    "!", "[[", "]]", "echo", "cd", "ls", "cat", "grep", "awk", "sed", NULL
};

// Type keywords (highlighted differently)
static const c8 *c_types[] = {
    "int", "char", "bool", "float", "double", "void", "long", "short",
    "signed", "unsigned", "size_t", "ssize_t", "off_t", "time_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t", "int16_t", "int32_t", "int64_t", NULL
};

typedef struct {
    sp_str_t name;
    const c8 **keywords;
    const c8 **types;
    const c8 **single_comments;
    const c8 **multi_comment_pairs;
    const c8 *string_delims;
    const c8 *identifier_extras;
    const c8 *number_mode;
    c8 escape_char;
    bool multi_line_strings;
} syntax_def_t;

typedef struct {
    bool used;
    language_t lang;
    syntax_def_t def;
} runtime_syntax_t;

#define RUNTIME_SYNTAX_CAP 32
static runtime_syntax_t G_runtime_syntax[RUNTIME_SYNTAX_CAP];

static const c8 *c_single_comments[] = { "//", NULL };
static const c8 *py_single_comments[] = { "#", NULL };
static const c8 *js_single_comments[] = { "//", NULL };
static const c8 *sh_single_comments[] = { "#", NULL };
static const c8 *c_multi_pairs[] = { "/*", "*/", NULL };
static const c8 *py_multi_pairs[] = { "\"\"\"", "\"\"\"", NULL };
static const c8 *js_multi_pairs[] = { "/*", "*/", NULL };

static syntax_def_t syntax_defs[] = {
    {
        .name = SP_LIT("c"),
        .keywords = c_keywords,
        .types = c_types,
        .single_comments = c_single_comments,
        .multi_comment_pairs = c_multi_pairs,
        .string_delims = "\"'",
        .identifier_extras = "",
        .number_mode = "c-like",
        .escape_char = '\\',
        .multi_line_strings = false
    },
    {
        .name = SP_LIT("python"),
        .keywords = python_keywords,
        .types = NULL,
        .single_comments = py_single_comments,
        .multi_comment_pairs = py_multi_pairs,
        .string_delims = "\"'",
        .identifier_extras = "",
        .number_mode = "strict",
        .escape_char = '\\',
        .multi_line_strings = false
    },
    {
        .name = SP_LIT("javascript"),
        .keywords = js_keywords,
        .types = NULL,
        .single_comments = js_single_comments,
        .multi_comment_pairs = js_multi_pairs,
        .string_delims = "\"'",
        .identifier_extras = "$",
        .number_mode = "c-like",
        .escape_char = '\\',
        .multi_line_strings = false
    },
    {
        .name = SP_LIT("shell"),
        .keywords = sh_keywords,
        .types = NULL,
        .single_comments = sh_single_comments,
        .multi_comment_pairs = NULL,
        .string_delims = "\"'",
        .identifier_extras = "",
        .number_mode = "strict",
        .escape_char = '\\',
        .multi_line_strings = false
    },
    {
        .name = SP_LIT("text"),
        .keywords = NULL,
        .types = NULL,
        .single_comments = NULL,
        .multi_comment_pairs = NULL,
        .string_delims = NULL,
        .identifier_extras = "",
        .number_mode = "strict",
        .escape_char = '\\',
        .multi_line_strings = false
    }
};

static bool str_eq_icase(sp_str_t a, sp_str_t b) {
    if (a.len != b.len) return false;
    for (u32 i = 0; i < a.len; i++) {
        if (tolower((unsigned char)a.data[i]) != tolower((unsigned char)b.data[i])) {
            return false;
        }
    }
    return true;
}

static c8 *copy_cstr(sp_str_t s) {
    if (s.len == 0) return NULL;
    c8 *buf = sp_alloc_n(c8, s.len + 1);
    if (!buf) return NULL;
    memcpy(buf, s.data, s.len);
    buf[s.len] = '\0';
    return buf;
}

static bool is_word_sep(c8 c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',';
}

static c8 **parse_word_list(sp_str_t words) {
    u32 count = 0;
    u32 i = 0;
    while (i < words.len) {
        while (i < words.len && is_word_sep(words.data[i])) i++;
        if (i >= words.len) break;
        count++;
        while (i < words.len && !is_word_sep(words.data[i])) i++;
    }

    if (count == 0) return NULL;

    c8 **list = sp_alloc_n(c8 *, count + 1);
    if (!list) return NULL;

    u32 index = 0;
    i = 0;
    while (i < words.len && index < count) {
        while (i < words.len && is_word_sep(words.data[i])) i++;
        if (i >= words.len) break;
        u32 start = i;
        while (i < words.len && !is_word_sep(words.data[i])) i++;
        u32 len = i - start;
        c8 *item = sp_alloc_n(c8, len + 1);
        if (!item) break;
        memcpy(item, words.data + start, len);
        item[len] = '\0';
        list[index++] = item;
    }
    list[index] = NULL;
    return list;
}

static bool word_list_has_even_count(const c8 **list) {
    if (!list) return true;
    u32 count = 0;
    while (list[count]) count++;
    return (count % 2) == 0;
}

static bool extension_list_matches(sp_str_t extensions, sp_str_t ext) {
    if (ext.len == 0) return false;
    u32 i = 0;
    while (i < extensions.len) {
        while (i < extensions.len && is_word_sep(extensions.data[i])) i++;
        if (i >= extensions.len) break;
        u32 start = i;
        while (i < extensions.len && !is_word_sep(extensions.data[i])) i++;
        sp_str_t token = sp_str_sub(extensions, (s32)start, (s32)(i - start));
        if (str_eq_icase(token, ext)) {
            return true;
        }
    }
    return false;
}

static runtime_syntax_t *find_runtime_by_name(sp_str_t name) {
    for (u32 i = 0; i < RUNTIME_SYNTAX_CAP; i++) {
        if (!G_runtime_syntax[i].used) continue;
        if (str_eq_icase(G_runtime_syntax[i].lang.name, name)) {
            return &G_runtime_syntax[i];
        }
    }
    return NULL;
}

static bool has_builtin_language(sp_str_t name) {
    for (u32 i = 0; i < sizeof(syntax_defs) / sizeof(syntax_defs[0]); i++) {
        if (str_eq_icase(syntax_defs[i].name, name)) return true;
    }
    return false;
}

bool syntax_has_language(sp_str_t name) {
    if (name.len == 0) return false;
    if (has_builtin_language(name)) return true;
    return find_runtime_by_name(name) != NULL;
}

sp_str_t syntax_list_languages(void) {
    sp_str_t names[64];
    u32 count = 0;

    for (u32 i = 0; i < sizeof(syntax_defs) / sizeof(syntax_defs[0]); i++) {
        if (count >= 64) break;
        names[count++] = syntax_defs[i].name;
    }

    for (u32 i = 0; i < RUNTIME_SYNTAX_CAP; i++) {
        if (!G_runtime_syntax[i].used) continue;
        bool exists = false;
        for (u32 j = 0; j < count; j++) {
            if (str_eq_icase(names[j], G_runtime_syntax[i].lang.name)) {
                exists = true;
                break;
            }
        }
        if (!exists && count < 64) {
            names[count++] = G_runtime_syntax[i].lang.name;
        }
    }

    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    for (u32 i = 0; i < count; i++) {
        if (i > 0) sp_str_builder_append_cstr(&b, ", ");
        sp_str_builder_append(&b, names[i]);
    }
    return sp_str_builder_to_str(&b);
}

static syntax_def_t *find_syntax_def(language_t *lang) {
    if (!lang) return NULL;

    runtime_syntax_t *rt = find_runtime_by_name(lang->name);
    if (rt) {
        return &rt->def;
    }

    for (u32 i = 0; i < sizeof(syntax_defs) / sizeof(syntax_defs[0]); i++) {
        if (sp_str_equal(syntax_defs[i].name, lang->name) ||
            (lang->name.len > 0 && syntax_defs[i].name.len > 0 &&
             lang->name.data && syntax_defs[i].name.data &&
             tolower((unsigned char)lang->name.data[0]) == tolower((unsigned char)syntax_defs[i].name.data[0]))) {
            return &syntax_defs[i];
        }
    }

    return &syntax_defs[4];
}

static bool is_keyword(const c8 **keywords, sp_str_t word) {
    if (!keywords) return false;
    for (u32 i = 0; keywords[i]; i++) {
        u32 kw_len = (u32)strlen(keywords[i]);
        // FIXED: was != 0, should be == 0
        if (word.len == kw_len && strncmp(word.data, keywords[i], kw_len) == 0) {
            return true;
        }
    }
    return false;
}

void syntax_init(void) {
    // Syntax definitions are statically initialized
}

bool syntax_register_language(sp_str_t name,
                              sp_str_t extensions,
                              sp_str_t keywords,
                              sp_str_t types,
                              sp_str_t single_comments,
                              sp_str_t multi_comment_pairs,
                              sp_str_t string_delims,
                              sp_str_t identifier_extras,
                              sp_str_t number_mode,
                              c8 escape_char,
                              bool multi_line_strings,
                              syntax_conflict_policy_t policy) {
    if (name.len == 0 || extensions.len == 0) return false;

    runtime_syntax_t *slot = find_runtime_by_name(name);
    bool exists = (slot != NULL) || has_builtin_language(name);
    if (exists && policy == SYNTAX_CONFLICT_SKIP) return true;
    if (exists && policy == SYNTAX_CONFLICT_ERROR) return false;

    if (!slot) {
        for (u32 i = 0; i < RUNTIME_SYNTAX_CAP; i++) {
            if (G_runtime_syntax[i].used) continue;
            slot = &G_runtime_syntax[i];
            break;
        }
    }
    if (!slot) return false;

    slot->used = true;
    slot->lang.name = sp_str_copy(name);
    slot->lang.extensions = sp_str_copy(extensions);
    slot->lang.keywords = NULL;
    slot->lang.keyword_count = 0;
    slot->lang.single_comment = sp_str_copy(single_comments);
    slot->lang.multi_comment_start = sp_str_lit("");
    slot->lang.multi_comment_end = sp_str_lit("");
    slot->lang.string_delim = string_delims.len > 0 ? string_delims.data[0] : 0;

    slot->def.name = slot->lang.name;
    slot->def.keywords = (const c8 **)parse_word_list(keywords);
    slot->def.types = (const c8 **)parse_word_list(types);
    slot->def.single_comments = (const c8 **)parse_word_list(single_comments);
    slot->def.multi_comment_pairs = (const c8 **)parse_word_list(multi_comment_pairs);
    if (slot->def.multi_comment_pairs && !word_list_has_even_count(slot->def.multi_comment_pairs)) {
        return false;
    }
    if (slot->def.multi_comment_pairs && slot->def.multi_comment_pairs[0]) {
        slot->lang.multi_comment_start = sp_str_from_cstr(slot->def.multi_comment_pairs[0]);
    }
    if (slot->def.multi_comment_pairs && slot->def.multi_comment_pairs[1]) {
        slot->lang.multi_comment_end = sp_str_from_cstr(slot->def.multi_comment_pairs[1]);
    }
    slot->def.string_delims = copy_cstr(string_delims);
    slot->def.identifier_extras = copy_cstr(identifier_extras);
    slot->def.number_mode = copy_cstr(number_mode);
    slot->def.escape_char = escape_char;
    slot->def.multi_line_strings = multi_line_strings;
    return true;
}

language_t* syntax_detect_language(sp_str_t filename) {
    // Find file extension
    sp_str_t ext = sp_str_lit("");
    for (s32 i = (s32)filename.len - 1; i >= 0; i--) {
        if (filename.data[i] == '.') {
            ext = sp_str_sub(filename, i, (s32)(filename.len - i));
            break;
        }
        if (filename.data[i] == '/') break; // Stop at directory separator
    }

    // JS-registered language mappings override builtin defaults.
    for (u32 i = 0; i < RUNTIME_SYNTAX_CAP; i++) {
        if (!G_runtime_syntax[i].used) continue;
        if (extension_list_matches(G_runtime_syntax[i].lang.extensions, ext)) {
            return &G_runtime_syntax[i].lang;
        }
    }

    // Map extension to language
    if (sp_str_equal(ext, sp_str_lit(".c")) ||
        sp_str_equal(ext, sp_str_lit(".h")) ||
        sp_str_equal(ext, sp_str_lit(".cpp")) ||
        sp_str_equal(ext, sp_str_lit(".hpp")) ||
        sp_str_equal(ext, sp_str_lit(".cc")) ||
        sp_str_equal(ext, sp_str_lit(".cxx"))) {
        static language_t c_lang = {
            .name = SP_LIT("C"),
            .extensions = SP_LIT(".c .h .cpp .hpp"),
            .keywords = NULL,
            .keyword_count = 0,
            .single_comment = SP_LIT("//"),
            .multi_comment_start = SP_LIT("/*"),
            .multi_comment_end = SP_LIT("*/"),
            .string_delim = '"'
        };
        return &c_lang;
    }

    if (sp_str_equal(ext, sp_str_lit(".py"))) {
        static language_t py_lang = {
            .name = SP_LIT("Python"),
            .extensions = SP_LIT(".py"),
            .keywords = NULL,
            .keyword_count = 0,
            .single_comment = SP_LIT("#"),
            .multi_comment_start = SP_LIT("\"\"\""),
            .multi_comment_end = SP_LIT("\"\"\""),
            .string_delim = '"'
        };
        return &py_lang;
    }

    if (sp_str_equal(ext, sp_str_lit(".js")) ||
        sp_str_equal(ext, sp_str_lit(".mjs"))) {
        static language_t js_lang = {
            .name = SP_LIT("JS"),
            .extensions = SP_LIT(".js .mjs"),
            .keywords = NULL,
            .keyword_count = 0,
            .single_comment = SP_LIT("//"),
            .multi_comment_start = SP_LIT("/*"),
            .multi_comment_end = SP_LIT("*/"),
            .string_delim = '"'
        };
        return &js_lang;
    }

    if (sp_str_equal(ext, sp_str_lit(".sh")) ||
        sp_str_equal(ext, sp_str_lit(".bash")) ||
        sp_str_equal(ext, sp_str_lit(".zsh"))) {
        static language_t sh_lang = {
            .name = SP_LIT("Shell"),
            .extensions = SP_LIT(".sh .bash .zsh"),
            .keywords = NULL,
            .keyword_count = 0,
            .single_comment = SP_LIT("#"),
            .multi_comment_start = SP_LIT(""),
            .multi_comment_end = SP_LIT(""),
            .string_delim = '"'
        };
        return &sh_lang;
    }

    if (sp_str_equal(ext, sp_str_lit(".md"))) {
        static language_t md_lang = {
            .name = SP_LIT("Markdown"),
            .extensions = SP_LIT(".md"),
            .keywords = NULL,
            .keyword_count = 0,
            .single_comment = SP_LIT(""),
            .multi_comment_start = SP_LIT(""),
            .multi_comment_end = SP_LIT(""),
            .string_delim = 0
        };
        return &md_lang;
    }

    static language_t text_lang = {
        .name = SP_LIT("Text"),
        .extensions = SP_LIT(""),
        .keywords = NULL,
        .keyword_count = 0,
        .single_comment = SP_LIT(""),
        .multi_comment_start = SP_LIT(""),
        .multi_comment_end = SP_LIT(""),
        .string_delim = 0
    };
    return &text_lang;
}

static bool str_eq_cstr_ci(const c8 *a, const c8 *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool is_identifier_char(const syntax_def_t *def, c8 c) {
    if (isalnum((unsigned char)c) || c == '_') return true;
    if (!def || !def->identifier_extras) return false;
    for (u32 i = 0; def->identifier_extras[i] != '\0'; i++) {
        if (def->identifier_extras[i] == c) return true;
    }
    return false;
}

static bool is_identifier_start(const syntax_def_t *def, c8 c) {
    if (isalpha((unsigned char)c) || c == '_') return true;
    if (!def || !def->identifier_extras) return false;
    for (u32 i = 0; def->identifier_extras[i] != '\0'; i++) {
        if (def->identifier_extras[i] == c) return true;
    }
    return false;
}

static bool is_string_delim_char(const syntax_def_t *def, c8 c) {
    if (!def || !def->string_delims || def->string_delims[0] == '\0') return false;
    for (u32 i = 0; def->string_delims[i] != '\0'; i++) {
        if (def->string_delims[i] == c) return true;
    }
    return false;
}

static bool match_token_at(sp_str_t text, u32 at, const c8 *token, u32 *token_len_out) {
    if (!token) return false;
    u32 len = (u32)strlen(token);
    if (len == 0 || at + len > text.len) return false;
    if (memcmp(text.data + at, token, len) != 0) return false;
    if (token_len_out) *token_len_out = len;
    return true;
}

static bool match_any_token_at(sp_str_t text, u32 at, const c8 **tokens, u32 *token_len_out) {
    if (!tokens) return false;
    for (u32 i = 0; tokens[i]; i++) {
        u32 len = 0;
        if (!match_token_at(text, at, tokens[i], &len)) continue;
        if (token_len_out) *token_len_out = len;
        return true;
    }
    return false;
}

static bool has_multi_pairs(const syntax_def_t *def) {
    return def && def->multi_comment_pairs && def->multi_comment_pairs[0] &&
           def->multi_comment_pairs[1];
}

static bool match_multi_start_at(const syntax_def_t *def, sp_str_t text, u32 at, u32 *pair_index, u32 *start_len) {
    if (!has_multi_pairs(def)) return false;
    for (u32 i = 0; def->multi_comment_pairs[i] && def->multi_comment_pairs[i + 1]; i += 2) {
        u32 len = 0;
        if (!match_token_at(text, at, def->multi_comment_pairs[i], &len)) continue;
        if (pair_index) *pair_index = i / 2;
        if (start_len) *start_len = len;
        return true;
    }
    return false;
}

static const c8 *multi_end_token(const syntax_def_t *def, u32 pair_index, u32 *end_len) {
    if (!has_multi_pairs(def)) return NULL;
    u32 i = pair_index * 2 + 1;
    if (!def->multi_comment_pairs[i]) return NULL;
    if (end_len) *end_len = (u32)strlen(def->multi_comment_pairs[i]);
    return def->multi_comment_pairs[i];
}

typedef struct {
    bool in_multiline_comment;
    u32 multi_pair_index;
    bool in_string;
    c8 string_delim;
    bool in_markdown_code_block;
    c8 markdown_fence_char;
} syntax_line_state_t;

typedef enum {
    TOKEN_STATE_NORMAL = 0,
    TOKEN_STATE_NUMBER,
} token_state_t;

static void highlight_span(line_t *line, u32 start, u32 end, highlight_type_t type) {
    for (u32 j = start; j < end; j++) {
        line->hl[j] = type;
    }
}

static bool is_number_start(const syntax_def_t *def, sp_str_t text, u32 i) {
    c8 c = text.data[i];
    c8 prev = (i > 0) ? text.data[i - 1] : ' ';
    if ((c >= '0' && c <= '9') && !is_identifier_char(def, prev)) return true;
    if (c == '.' && i + 1 < text.len &&
        text.data[i + 1] >= '0' && text.data[i + 1] <= '9' &&
        !is_identifier_char(def, prev)) {
        return true;
    }
    return false;
}

static bool is_number_char(const syntax_def_t *def, c8 c) {
    bool strict = def && def->number_mode && str_eq_cstr_ci(def->number_mode, "strict");
    if (strict) {
        return isdigit((unsigned char)c) || c == '.' || c == '_';
    }
    return isdigit((unsigned char)c) || c == '.' || c == '_' ||
           c == 'x' || c == 'X' || c == 'e' || c == 'E' ||
           c == 'a' || c == 'A' || c == 'b' || c == 'B' ||
           c == 'c' || c == 'C' || c == 'd' || c == 'D' ||
           c == 'f' || c == 'F' || c == 'u' || c == 'U' ||
           c == 'l' || c == 'L' || c == '+' || c == '-';
}

static u32 consume_identifier(line_t *line, const syntax_def_t *def, u32 start) {
    u32 i = start;
    while (i < line->text.len && is_identifier_char(def, line->text.data[i])) {
        i++;
    }

    sp_str_t word = sp_str_sub(line->text, (s32)start, (s32)(i - start));
    if (is_keyword(def->keywords, word)) {
        highlight_span(line, start, i, HL_KEYWORD);
    } else if (def->types && is_keyword(def->types, word)) {
        highlight_span(line, start, i, HL_TYPE);
    } else if (i < line->text.len && line->text.data[i] == '(') {
        highlight_span(line, start, i, HL_FUNCTION);
    } else {
        highlight_span(line, start, i, HL_NORMAL);
    }
    return i;
}

static void save_line_state(syntax_line_state_t *state, bool in_ml, u32 ml_pair_index, bool in_string, c8 string_delim) {
    if (!state) return;
    state->in_multiline_comment = in_ml;
    state->multi_pair_index = ml_pair_index;
    state->in_string = in_string;
    state->string_delim = string_delim;
}

static bool markdown_is_language(language_t *lang) {
    if (!lang || !lang->name.data) return false;
    return str_eq_cstr_ci(lang->name.data, "Markdown");
}

static u32 markdown_leading_spaces(sp_str_t text) {
    u32 i = 0;
    while (i < text.len && (text.data[i] == ' ' || text.data[i] == '\t')) i++;
    return i;
}

static bool markdown_match_fence(sp_str_t text, u32 start, c8 *fence_char, u32 *fence_len) {
    if (start >= text.len) return false;
    c8 ch = text.data[start];
    if (ch != '`' && ch != '~') return false;
    u32 i = start;
    while (i < text.len && text.data[i] == ch) i++;
    if (i - start < 3) return false;
    if (fence_char) *fence_char = ch;
    if (fence_len) *fence_len = i - start;
    return true;
}

static bool markdown_is_hr(sp_str_t text, u32 start) {
    if (start >= text.len) return false;
    c8 ch = text.data[start];
    if (ch != '-' && ch != '*' && ch != '_') return false;
    u32 count = 0;
    for (u32 i = start; i < text.len; i++) {
        c8 c = text.data[i];
        if (c == ch) {
            count++;
            continue;
        }
        if (c != ' ' && c != '\t') return false;
    }
    return count >= 3;
}

static void markdown_highlight_inline(line_t *line, u32 start) {
    bool in_code = false;
    bool in_link_text = false;
    bool in_link_url = false;
    bool in_emphasis = false;
    c8 emphasis_char = 0;

    for (u32 i = start; i < line->text.len; i++) {
        c8 c = line->text.data[i];

        if (in_code) {
            line->hl[i] = HL_STRING;
            if (c == '`') in_code = false;
            continue;
        }

        if (in_link_url) {
            line->hl[i] = HL_STRING;
            if (c == ')') in_link_url = false;
            continue;
        }

        if (c == '`') {
            line->hl[i] = HL_STRING;
            in_code = true;
            continue;
        }

        if (c == '[' || c == ']') {
            line->hl[i] = HL_KEYWORD;
            in_link_text = (c == '[');
            continue;
        }

        if (c == '(' && i > 0 && line->text.data[i - 1] == ']') {
            line->hl[i] = HL_STRING;
            in_link_url = true;
            continue;
        }

        if (c == '*' || c == '_') {
            if (in_emphasis && c == emphasis_char) {
                line->hl[i] = HL_TYPE;
                in_emphasis = false;
                emphasis_char = 0;
                continue;
            }
            line->hl[i] = HL_TYPE;
            in_emphasis = true;
            emphasis_char = c;
            continue;
        }

        if (in_link_text) {
            line->hl[i] = HL_FUNCTION;
        }
    }
}

static void syntax_highlight_markdown_line(line_t *line, syntax_line_state_t *state) {
    if (line->text.len == 0) {
        if (line->hl) {
            sp_free(line->hl);
            line->hl = NULL;
        }
        return;
    }

    if (line->hl) {
        sp_free(line->hl);
        line->hl = NULL;
    }
    line->hl = sp_alloc(sizeof(highlight_type_t) * line->text.len);
    if (!line->hl) return;
    for (u32 i = 0; i < line->text.len; i++) line->hl[i] = HL_NORMAL;

    u32 start = markdown_leading_spaces(line->text);
    c8 fence_char = 0;
    u32 fence_len = 0;

    if (state && state->in_markdown_code_block) {
        highlight_span(line, 0, line->text.len, HL_STRING);
        if (markdown_match_fence(line->text, start, &fence_char, &fence_len) &&
            fence_char == state->markdown_fence_char) {
            highlight_span(line, start, SP_MIN(start + fence_len, line->text.len), HL_KEYWORD);
            state->in_markdown_code_block = false;
            state->markdown_fence_char = 0;
        }
        return;
    }

    if (markdown_match_fence(line->text, start, &fence_char, &fence_len)) {
        highlight_span(line, start, SP_MIN(start + fence_len, line->text.len), HL_KEYWORD);
        if (start + fence_len < line->text.len) {
            highlight_span(line, start + fence_len, line->text.len, HL_COMMENT);
        }
        if (state) {
            state->in_markdown_code_block = true;
            state->markdown_fence_char = fence_char;
        }
        return;
    }

    if (start < line->text.len && line->text.data[start] == '#') {
        u32 i = start;
        while (i < line->text.len && line->text.data[i] == '#') i++;
        highlight_span(line, start, i, HL_KEYWORD);
        if (i < line->text.len) {
            highlight_span(line, i, line->text.len, HL_FUNCTION);
        }
        return;
    }

    if (start < line->text.len && line->text.data[start] == '>') {
        highlight_span(line, start, line->text.len, HL_COMMENT);
        return;
    }

    if (markdown_is_hr(line->text, start)) {
        highlight_span(line, start, line->text.len, HL_KEYWORD);
        return;
    }

    if (start < line->text.len &&
        (line->text.data[start] == '-' || line->text.data[start] == '*' || line->text.data[start] == '+') &&
        start + 1 < line->text.len &&
        (line->text.data[start + 1] == ' ' || line->text.data[start + 1] == '\t')) {
        highlight_span(line, start, start + 1, HL_KEYWORD);
        if (start + 5 <= line->text.len &&
            line->text.data[start + 2] == '[' &&
            (line->text.data[start + 3] == ' ' || line->text.data[start + 3] == 'x' || line->text.data[start + 3] == 'X') &&
            line->text.data[start + 4] == ']') {
            highlight_span(line, start + 2, start + 5, HL_TYPE);
            markdown_highlight_inline(line, start + 5);
            return;
        }
        markdown_highlight_inline(line, start + 1);
        return;
    }

    if (start < line->text.len && isdigit((unsigned char)line->text.data[start])) {
        u32 i = start;
        while (i < line->text.len && isdigit((unsigned char)line->text.data[i])) i++;
        if (i + 1 < line->text.len && line->text.data[i] == '.' &&
            (line->text.data[i + 1] == ' ' || line->text.data[i + 1] == '\t')) {
            highlight_span(line, start, i + 1, HL_KEYWORD);
            markdown_highlight_inline(line, i + 1);
            return;
        }
    }

    markdown_highlight_inline(line, 0);
}

static void syntax_highlight_line_impl(line_t *line, language_t *lang, syntax_line_state_t *state) {
    if (!line || !lang) return;
    if (!line->text.data && line->text.len > 0) return;

    if (markdown_is_language(lang)) {
        syntax_highlight_markdown_line(line, state);
        if (state) {
            state->in_multiline_comment = false;
            state->multi_pair_index = 0;
            state->in_string = false;
            state->string_delim = 0;
        }
        return;
    }

    // Free old highlight
    if (line->hl) {
        sp_free(line->hl);
        line->hl = NULL;
    }
    
    // Return early for empty lines
    if (line->text.len == 0) {
        line->hl = NULL;
        return;
    }

    // Allocate highlight array
    line->hl = sp_alloc(sizeof(highlight_type_t) * line->text.len);
    if (!line->hl) {
        return; // Allocation failed
    }

    // Initialize all to normal
    for (u32 i = 0; i < line->text.len; i++) {
        line->hl[i] = HL_NORMAL;
    }

    syntax_def_t *def = find_syntax_def(lang);
    bool in_ml = state ? state->in_multiline_comment : false;
    u32 ml_pair_index = state ? state->multi_pair_index : 0;
    bool in_string = state ? state->in_string : false;
    c8 open_string_delim = state ? state->string_delim : 0;
    bool has_multi = has_multi_pairs(def);
    bool has_single = def->single_comments && def->single_comments[0] != NULL;

    token_state_t token_state = TOKEN_STATE_NORMAL;

    c8 string_delim = open_string_delim;

    u32 i = 0;
    while (i < line->text.len) {
        c8 c = line->text.data[i];
        c8 prev = (i > 0) ? line->text.data[i - 1] : ' ';

        if (in_string) {
            line->hl[i] = HL_STRING;
            if (c == string_delim && prev != def->escape_char) {
                in_string = false;
                string_delim = 0;
            }
            i++;
            continue;
        }

        if (in_ml) {
            line->hl[i] = HL_COMMENT;
            u32 ml_end_len = 0;
            const c8 *ml_end = multi_end_token(def, ml_pair_index, &ml_end_len);
            if (ml_end && match_token_at(line->text, i, ml_end, NULL)) {
                highlight_span(line, i, SP_MIN(i + ml_end_len, line->text.len), HL_COMMENT);
                i += ml_end_len;
                in_ml = false;
            } else {
                i++;
            }
            continue;
        }

        if (token_state == TOKEN_STATE_NUMBER) {
            if (is_number_char(def, c)) {
                line->hl[i] = HL_NUMBER;
                i++;
                continue;
            }
            token_state = TOKEN_STATE_NORMAL;
            continue; // re-process current character in normal mode
        }

        u32 ml_start_len = 0;
        u32 start_pair_index = 0;
        if (has_multi && match_multi_start_at(def, line->text, i, &start_pair_index, &ml_start_len)) {
            highlight_span(line, i, SP_MIN(i + ml_start_len, line->text.len), HL_COMMENT);
            i += ml_start_len;
            in_ml = true;
            ml_pair_index = start_pair_index;
            continue;
        }

        if (has_single && match_any_token_at(line->text, i, def->single_comments, NULL)) {
            highlight_span(line, i, line->text.len, HL_COMMENT);
            save_line_state(state, in_ml, ml_pair_index, in_string, string_delim);
            return;
        }

        if (is_string_delim_char(def, c)) {
            line->hl[i] = HL_STRING;
            string_delim = c;
            in_string = true;
            i++;
            continue;
        }

        if (is_number_start(def, line->text, i)) {
            line->hl[i] = HL_NUMBER;
            token_state = TOKEN_STATE_NUMBER;
            i++;
            continue;
        }

        if (is_identifier_start(def, c)) {
            i = consume_identifier(line, def, i);
            continue;
        }

        line->hl[i] = HL_NORMAL;
        i++;
    }

    if (!def->multi_line_strings && in_string) {
        in_string = false;
        string_delim = 0;
    }

    save_line_state(state, in_ml, ml_pair_index, in_string, string_delim);
}

void syntax_highlight_line(line_t *line, language_t *lang) {
    syntax_line_state_t state = {0};
    syntax_highlight_line_impl(line, lang, &state);
}

void syntax_highlight_buffer(buffer_t *buf) {
    if (!buf) return;

    language_t *lang = syntax_detect_language(buf->filename);
    syntax_line_state_t state = {0};

    for (u32 i = 0; i < buf->line_count; i++) {
        syntax_highlight_line_impl(&buf->lines[i], lang, &state);
        buf->lines[i].hl_dirty = false;
    }
}

c8* syntax_color_to_ansi(highlight_type_t type) {
    switch (type) {
        case HL_KEYWORD:    return "\033[1;34m";  // Bold blue
        case HL_STRING:     return "\033[32m";    // Green
        case HL_COMMENT:    return "\033[90m";    // Gray
        case HL_NUMBER:     return "\033[33m";    // Yellow
        case HL_FUNCTION:   return "\033[35m";    // Magenta
        case HL_TYPE:       return "\033[36m";    // Cyan
        case HL_NORMAL:
        default:            return "\033[0m";     // Reset
    }
}
