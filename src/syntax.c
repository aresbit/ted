/**
 * syntax.c - Syntax highlighting for TED editor
 */

#include "ted.h"
#include <ctype.h>

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
    "export", "import", "module", "requires", NULL
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
    "!", "[[", "]]", NULL
};

// Type keywords (highlighted differently)
static const c8 *c_types[] = {
    "int", "char", "bool", "float", "double", "void", "long", "short",
    "signed", "unsigned", "size_t", "ssize_t", "off_t", "time_t", NULL
};

typedef struct {
    sp_str_t name;
    const c8 **keywords;
    const c8 **types;
    const c8 *single_comment;
    const c8 *multi_start;
    const c8 *multi_end;
    c8 string_delim;
} syntax_def_t;

static syntax_def_t syntax_defs[] = {
    {
        .name = SP_LIT("c"),
        .keywords = c_keywords,
        .types = c_types,
        .single_comment = "//",
        .multi_start = "/*",
        .multi_end = "*/",
        .string_delim = '"'
    },
    {
        .name = SP_LIT("python"),
        .keywords = python_keywords,
        .types = NULL,
        .single_comment = "#",
        .multi_start = "\"\"\"",
        .multi_end = "\"\"\"",
        .string_delim = '"'
    },
    {
        .name = SP_LIT("javascript"),
        .keywords = js_keywords,
        .types = NULL,
        .single_comment = "//",
        .multi_start = "/*",
        .multi_end = "*/",
        .string_delim = '"'
    },
    {
        .name = SP_LIT("shell"),
        .keywords = sh_keywords,
        .types = NULL,
        .single_comment = "#",
        .multi_start = NULL,
        .multi_end = NULL,
        .string_delim = '"'
    },
    {
        .name = SP_LIT("text"),
        .keywords = NULL,
        .types = NULL,
        .single_comment = NULL,
        .multi_start = NULL,
        .multi_end = NULL,
        .string_delim = 0
    }
};

static bool is_keyword(const c8 **keywords, sp_str_t word) {
    if (!keywords) return false;
    for (u32 i = 0; keywords[i]; i++) {
        u32 kw_len = (u32)strlen(keywords[i]);
        if (word.len == kw_len && strncmp(word.data, keywords[i], kw_len)) {
            return true;
        }
    }
    return false;
}

static bool is_separator(c8 c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '(' || c == ')' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == ';' || c == ',' ||
           c == '.' || c == '+' || c == '-' || c == '*' ||
           c == '/' || c == '%' || c == '&' || c == '|' ||
           c == '=' || c == '<' || c == '>' || c == '!' ||
           c == '~' || c == '^' || c == ':' || c == '"' ||
           c == '\'' || c == '#'; // Added '#' as separator for Python comments
}

void syntax_init(void) {
    // Syntax definitions are statically initialized
}

language_t* syntax_detect_language(sp_str_t filename) {
    // Find file extension
    sp_str_t ext = sp_str_lit("");
    for (s32 i = (s32)filename.len - 1; i >= 0; i--) {
        if (filename.data[i] == '.') {
            ext = sp_str_sub(filename, i, (s32)(filename.len - i));
            break;
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
            .name = SP_LIT("c"),
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
            .name = SP_LIT("python"),
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
            .name = SP_LIT("javascript"),
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
            .name = SP_LIT("shell"),
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
            .name = SP_LIT("markdown"),
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
        .name = SP_LIT("text"),
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

void syntax_highlight_line(line_t *line, language_t *lang) {
    if (!line || !lang) return;

    // Allocate highlight array
    if (line->hl) {
        sp_free(line->hl);
    }
    line->hl = sp_alloc(sizeof(highlight_type_t) * line->text.len);

    // Find matching syntax definition
    syntax_def_t *def = &syntax_defs[4]; // Default to text
    for (u32 i = 0; i < sizeof(syntax_defs) / sizeof(syntax_defs[0]); i++) {
        if (sp_str_equal(syntax_defs[i].name, lang->name)) {
            def = &syntax_defs[i];
            break;
        }
    }

    // Simple state machine for highlighting
    enum {
        STATE_NORMAL,
        STATE_STRING,
        STATE_COMMENT,
        STATE_NUMBER
    } state = STATE_NORMAL;

    c8 string_delim = 0;

    for (u32 i = 0; i < line->text.len; i++) {
        c8 c = line->text.data[i];
        c8 prev = (i > 0) ? line->text.data[i - 1] : 0;

        switch (state) {
            case STATE_NORMAL: {
                // Check for string start
                if (c == '"' || c == '\'') {
                    state = STATE_STRING;
                    string_delim = c;
                    line->hl[i] = HL_STRING;
                    break;
                }

                // Check for single-line comment
                if (def->single_comment && c == def->single_comment[0]) {
                    u32 clen = (u32)strlen(def->single_comment);
                    if (i + clen <= line->text.len) {
                        bool match = true;
                        for (u32 j = 0; j < clen; j++) {
                            if (line->text.data[i + j] != def->single_comment[j]) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            // Comment to end of line
                            for (u32 j = i; j < line->text.len; j++) {
                                line->hl[j] = HL_COMMENT;
                            }
                            return;
                        }
                    }
                }

                // Check for number
                if ((c >= '0' && c <= '9') ||
                    (c == '.' && i + 1 < line->text.len &&
                     line->text.data[i + 1] >= '0' && line->text.data[i + 1] <= '9')) {
                    state = STATE_NUMBER;
                    line->hl[i] = HL_NUMBER;
                    break;
                }

                // Check for keyword
                if (isalpha(c) || c == '_') {
                    // Find word boundaries
                    u32 start = i;
                    while (i < line->text.len &&
                           (isalnum(line->text.data[i]) || line->text.data[i] == '_')) {
                        i++;
                    }
                    sp_str_t word = sp_str_sub(line->text, (s32)start, (s32)(i - start));

                    if (is_keyword(def->keywords, word)) {
                        for (u32 j = start; j < i; j++) {
                            line->hl[j] = HL_KEYWORD;
                        }
                    } else if (def->types && is_keyword(def->types, word)) {
                        for (u32 j = start; j < i; j++) {
                            line->hl[j] = HL_TYPE;
                        }
                    } else {
                        for (u32 j = start; j < i; j++) {
                            line->hl[j] = HL_NORMAL;
                        }
                    }
                    i--; // Compensate for loop increment
                    break;
                }

                line->hl[i] = HL_NORMAL;
                break;
            }

            case STATE_STRING: {
                line->hl[i] = HL_STRING;
                // Check for string end (handle escape sequences)
                if (c == string_delim && prev != '\\') {
                    state = STATE_NORMAL;
                }
                break;
            }

            case STATE_NUMBER: {
                if (isdigit(c) || c == '.' || c == 'x' || c == 'X' ||
                    c == 'a' || c == 'A' || c == 'b' || c == 'B' ||
                    c == 'c' || c == 'C' || c == 'e' || c == 'E' ||
                    c == 'f' || c == 'F' || c == 'u' || c == 'U' ||
                    c == 'l' || c == 'L') {
                    line->hl[i] = HL_NUMBER;
                } else {
                    state = STATE_NORMAL;
                    line->hl[i] = HL_NORMAL;
                }
                break;
            }

            default:
                line->hl[i] = HL_NORMAL;
                break;
        }
    }
}

void syntax_highlight_buffer(buffer_t *buf) {
    if (!buf) return;

    language_t *lang = syntax_detect_language(buf->filename);

    for (u32 i = 0; i < buf->line_count; i++) {
        syntax_highlight_line(&buf->lines[i], lang);
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
