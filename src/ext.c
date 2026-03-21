/**
 * ext.c - Embedded MicroQuickJS backend with ted host APIs
 */

#include "ted.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <dirent.h>
#include <limits.h>

#include "mquickjs.h"

// Host callbacks required by mqjs_stdlib.h
static JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue js_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue js_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue js_setTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#include "mqjs_stdlib.h"

#define EXT_VM_MEM_SIZE (256 * 1024)
#define EXT_LOADED_PLUGIN_CAP 256
#define EXT_RECOGNIZER_CAP 64
#define BASE_CFUNC_COUNT ((u32)(sizeof(js_c_function_table) / sizeof(js_c_function_table[0])))

typedef enum {
    TED_CFUNC_VERSION = 0,
    TED_CFUNC_GET_TEXT,
    TED_CFUNC_SET_TEXT,
    TED_CFUNC_MESSAGE,
    TED_CFUNC_GOTO,
    TED_CFUNC_OPEN,
    TED_CFUNC_SAVE,
    TED_CFUNC_REGISTER_COMMAND,
    TED_CFUNC_REGISTER_LANGUAGE,
    TED_CFUNC_REGISTER_OPERATOR_TARGET,
    TED_CFUNC_REGISTER_RECOGNIZER,
    TED_CFUNC_SKETCH_MODE,
    TED_CFUNC_SKETCH_CLEAR,
    TED_CFUNC_SKETCH_STATUS,
    TED_CFUNC_SKETCH_SHAPES,
    TED_CFUNC_COUNT,
} ted_cfunc_id_t;

#define TED_CFUNC_INDEX(id) ((int)(BASE_CFUNC_COUNT + (id)))

static JSContext *G_ctx = SP_NULLPTR;
static u8 *G_mem = SP_NULLPTR;
static bool G_ext_ready = false;
static sp_str_t G_loaded_plugins[EXT_LOADED_PLUGIN_CAP];
static u32 G_loaded_plugin_count = 0;
typedef struct {
    sp_str_t name;
    sp_str_t code;
} ext_recognizer_t;
static ext_recognizer_t G_recognizers[EXT_RECOGNIZER_CAP];
static u32 G_recognizer_count = 0;

static JSCFunctionDef G_ext_cfunc_table[BASE_CFUNC_COUNT + TED_CFUNC_COUNT];
static JSSTDLibraryDef G_ext_stdlib;
static bool G_ext_stdlib_ready = false;

// ted host API callbacks
static JSValue ted_js_version(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_get_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_set_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_message(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_goto(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_save(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_register_command(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_register_language(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_register_operator_target(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_register_recognizer(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_sketch_mode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_sketch_clear(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_sketch_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
static JSValue ted_js_sketch_shapes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

static sp_str_t ted_buffer_to_text(void) {
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);

    for (u32 i = 0; i < E.buffer.line_count; i++) {
        sp_str_builder_append(&b, E.buffer.lines[i].text);
        if (i + 1 < E.buffer.line_count) {
            sp_str_builder_append_c8(&b, '\n');
        }
    }
    return sp_str_builder_to_str(&b);
}

static void ted_replace_text(sp_str_t text) {
    while (E.buffer.line_count > 0) {
        buffer_delete_line(&E.buffer, 0);
    }

    u32 start = 0;
    for (u32 i = 0; i < text.len; i++) {
        if (text.data[i] != '\n') continue;
        sp_str_t line = sp_str_sub(text, (s32)start, (s32)(i - start));
        buffer_insert_line(&E.buffer, E.buffer.line_count, line);
        start = i + 1;
    }

    if (start < text.len) {
        sp_str_t line = sp_str_sub(text, (s32)start, (s32)(text.len - start));
        buffer_insert_line(&E.buffer, E.buffer.line_count, line);
    } else if (text.len > 0 && text.data[text.len - 1] == '\n') {
        buffer_insert_line(&E.buffer, E.buffer.line_count, sp_str_lit(""));
    }

    if (E.buffer.line_count == 0) {
        buffer_insert_line(&E.buffer, 0, sp_str_lit(""));
    }

    E.buffer.modified = true;
    E.cursor.row = 0;
    E.cursor.col = 0;
    E.cursor.render_col = 0;
    E.row_offset = 0;
    E.col_offset = 0;
}

static JSValue ted_js_version(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewString(ctx, TED_VERSION);
}

static JSValue ted_js_get_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    sp_str_t text = ted_buffer_to_text();
    return JS_NewStringLen(ctx, text.data, text.len);
}

static JSValue ted_js_set_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "ted.setText: missing text");

    JSCStringBuf sb;
    const char *s = JS_ToCString(ctx, argv[0], &sb);
    if (!s) return JS_ThrowTypeError(ctx, "ted.setText: text must be string");

    ted_replace_text(sp_str_from_cstr(s));
    return JS_UNDEFINED;
}

static JSValue ted_js_message(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "ted.message: missing message");

    JSCStringBuf sb;
    const char *s = JS_ToCString(ctx, argv[0], &sb);
    if (!s) return JS_ThrowTypeError(ctx, "ted.message: message must be string");

    editor_set_message("%s", s);
    return JS_UNDEFINED;
}

static JSValue ted_js_goto(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "ted.goto: missing line number");

    int line = 0;
    if (JS_ToInt32(ctx, &line, argv[0])) {
        return JS_ThrowTypeError(ctx, "ted.goto: invalid line number");
    }
    if (line < 1) line = 1;
    editor_goto_line((u32)line);
    return JS_UNDEFINED;
}

static JSValue ted_js_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "ted.open: missing path");

    JSCStringBuf sb;
    const char *s = JS_ToCString(ctx, argv[0], &sb);
    if (!s) return JS_ThrowTypeError(ctx, "ted.open: invalid path");

    editor_open(sp_str_from_cstr(s));
    return JS_UNDEFINED;
}

static JSValue ted_js_save(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc >= 1) {
        JSCStringBuf sb;
        const char *s = JS_ToCString(ctx, argv[0], &sb);
        if (!s) return JS_ThrowTypeError(ctx, "ted.save: invalid path");
        E.buffer.filename = sp_str_from_cstr(s);
    }

    editor_save();
    return JS_UNDEFINED;
}

static JSValue ted_js_register_command(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 2) return JS_ThrowTypeError(ctx, "ted.registerCommand: need (name, code)");

    JSCStringBuf name_buf;
    JSCStringBuf code_buf;
    const char *name = JS_ToCString(ctx, argv[0], &name_buf);
    const char *code = JS_ToCString(ctx, argv[1], &code_buf);
    if (!name || !code) {
        return JS_ThrowTypeError(ctx, "ted.registerCommand: both args must be strings");
    }

    bool ok = command_register_js(sp_str_from_cstr(name), sp_str_from_cstr(code));
    if (!ok) {
        return JS_ThrowTypeError(ctx, "ted.registerCommand: registration failed");
    }
    return JS_UNDEFINED;
}

static JSValue ted_js_register_operator_target(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 2) return JS_ThrowTypeError(ctx, "ted.registerOperatorTarget: need (seq, code)");

    JSCStringBuf seq_buf;
    JSCStringBuf code_buf;
    const char *seq = JS_ToCString(ctx, argv[0], &seq_buf);
    const char *code = JS_ToCString(ctx, argv[1], &code_buf);
    if (!seq || !code) {
        return JS_ThrowTypeError(ctx, "ted.registerOperatorTarget: both args must be strings");
    }

    bool ok = input_register_operator_target(sp_str_from_cstr(seq), sp_str_from_cstr(code));
    if (!ok) {
        return JS_ThrowTypeError(ctx, "ted.registerOperatorTarget: registration failed");
    }
    return JS_UNDEFINED;
}

bool ext_register_recognizer(sp_str_t name, sp_str_t code) {
    if (name.len == 0 || code.len == 0) return false;
    for (u32 i = 0; i < G_recognizer_count; i++) {
        if (!sp_str_equal(G_recognizers[i].name, name)) continue;
        G_recognizers[i].code = sp_str_copy(code);
        return true;
    }
    if (G_recognizer_count >= EXT_RECOGNIZER_CAP) return false;
    G_recognizers[G_recognizer_count].name = sp_str_copy(name);
    G_recognizers[G_recognizer_count].code = sp_str_copy(code);
    G_recognizer_count++;
    return true;
}

sp_str_t ext_list_recognizers(void) {
    if (G_recognizer_count == 0) return sp_str_lit("");
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    for (u32 i = 0; i < G_recognizer_count; i++) {
        if (i > 0) sp_str_builder_append_cstr(&b, ", ");
        sp_str_builder_append(&b, G_recognizers[i].name);
    }
    return sp_str_builder_to_str(&b);
}

u32 ext_recognizer_count(void) {
    return G_recognizer_count;
}

static JSValue ted_js_register_recognizer(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 2) return JS_ThrowTypeError(ctx, "ted.registerRecognizer: need (name, code)");

    JSCStringBuf name_buf;
    JSCStringBuf code_buf;
    const char *name = JS_ToCString(ctx, argv[0], &name_buf);
    const char *code = JS_ToCString(ctx, argv[1], &code_buf);
    if (!name || !code) {
        return JS_ThrowTypeError(ctx, "ted.registerRecognizer: both args must be strings");
    }
    if (!ext_register_recognizer(sp_str_from_cstr(name), sp_str_from_cstr(code))) {
        return JS_ThrowTypeError(ctx, "ted.registerRecognizer: registration failed");
    }
    return JS_UNDEFINED;
}

static JSValue ted_js_sketch_mode(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc == 0) {
        sp_str_t mode = sketch_mode_name();
        return JS_NewStringLen(ctx, mode.data, mode.len);
    }

    JSCStringBuf mode_buf;
    const char *mode = JS_ToCString(ctx, argv[0], &mode_buf);
    if (!mode) return JS_ThrowTypeError(ctx, "ted.sketchMode: mode must be string");
    if (!sketch_set_mode_name(sp_str_from_cstr(mode))) {
        return JS_ThrowTypeError(ctx, "ted.sketchMode: invalid mode");
    }
    sketch_set_enabled(true);
    return JS_UNDEFINED;
}

static JSValue ted_js_sketch_clear(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)ctx;
    (void)this_val;
    (void)argc;
    (void)argv;
    sketch_clear();
    return JS_UNDEFINED;
}

static JSValue ted_js_sketch_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    sp_str_t s = sketch_status();
    return JS_NewStringLen(ctx, s.data, s.len);
}

static JSValue ted_js_sketch_shapes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    sp_str_t s = sketch_shapes_json();
    return JS_NewStringLen(ctx, s.data, s.len);
}

static bool js_get_optional_prop_string(JSContext *ctx, JSValue obj, const c8 *prop, sp_str_t *out) {
    JSValue v = JS_GetPropertyStr(ctx, obj, prop);
    if (JS_IsUndefined(v) || JS_IsNull(v)) {
        *out = sp_str_lit("");
        return true;
    }

    JSCStringBuf sb;
    const char *s = JS_ToCString(ctx, v, &sb);
    if (!s) return false;
    *out = sp_str_from_cstr(s);
    return true;
}

static bool js_get_optional_prop_bool(JSContext *ctx, JSValue obj, const c8 *prop, bool *out) {
    JSValue v = JS_GetPropertyStr(ctx, obj, prop);
    if (JS_IsUndefined(v) || JS_IsNull(v)) {
        return true;
    }

    int i = 0;
    if (JS_ToInt32(ctx, &i, v)) {
        return false;
    }
    *out = (i != 0);
    return true;
}

static bool ext_str_eq_ci(sp_str_t s, const c8 *lit) {
    u32 n = (u32)strlen(lit);
    if (s.len != n) return false;
    for (u32 i = 0; i < n; i++) {
        if (tolower((unsigned char)s.data[i]) != tolower((unsigned char)lit[i])) {
            return false;
        }
    }
    return true;
}

static JSValue ted_js_register_language(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "ted.registerLanguage: need (name, spec)");
    }

    JSCStringBuf name_buf;
    const char *name = JS_ToCString(ctx, argv[0], &name_buf);
    if (!name) {
        return JS_ThrowTypeError(ctx, "ted.registerLanguage: name must be string");
    }

    sp_str_t extensions = sp_str_lit("");
    sp_str_t keywords = sp_str_lit("");
    sp_str_t types = sp_str_lit("");
    sp_str_t single_comments = sp_str_lit("");
    sp_str_t multi_start = sp_str_lit("");
    sp_str_t multi_end = sp_str_lit("");
    sp_str_t multi_pairs = sp_str_lit("");
    sp_str_t string_delims = sp_str_lit("\"'");
    sp_str_t identifier_extras = sp_str_lit("");
    sp_str_t number_mode = sp_str_lit("c-like");
    sp_str_t escape_char = sp_str_lit("\\");
    bool multi_line_strings = false;

    if (!js_get_optional_prop_string(ctx, argv[1], "extensions", &extensions)) {
        return JS_ThrowTypeError(ctx, "ted.registerLanguage: spec.extensions must be string/array");
    }
    if (extensions.len == 0) {
        return JS_ThrowTypeError(ctx, "ted.registerLanguage: spec.extensions is required");
    }

    if (!js_get_optional_prop_string(ctx, argv[1], "keywords", &keywords) ||
        !js_get_optional_prop_string(ctx, argv[1], "types", &types) ||
        !js_get_optional_prop_string(ctx, argv[1], "singleComment", &single_comments) ||
        !js_get_optional_prop_string(ctx, argv[1], "multiCommentStart", &multi_start) ||
        !js_get_optional_prop_string(ctx, argv[1], "multiCommentEnd", &multi_end)) {
        return JS_ThrowTypeError(ctx, "ted.registerLanguage: invalid spec field");
    }
    if (!js_get_optional_prop_string(ctx, argv[1], "multiCommentPairs", &multi_pairs)) {
        return JS_ThrowTypeError(ctx, "ted.registerLanguage: spec.multiCommentPairs must be string");
    }
    sp_str_t single_comments_override = sp_str_lit("");
    if (!js_get_optional_prop_string(ctx, argv[1], "singleComments", &single_comments_override)) {
        return JS_ThrowTypeError(ctx, "ted.registerLanguage: spec.singleComments must be string");
    }
    if (single_comments_override.len > 0) {
        single_comments = single_comments_override;
    }
    if (multi_pairs.len == 0 && multi_start.len > 0 && multi_end.len > 0) {
        sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
        sp_str_builder_t b = sp_str_builder_from_writer(&writer);
        sp_str_builder_append(&b, multi_start);
        sp_str_builder_append_c8(&b, ' ');
        sp_str_builder_append(&b, multi_end);
        multi_pairs = sp_str_builder_to_str(&b);
    }

    sp_str_t delim = sp_str_lit("");
    if (!js_get_optional_prop_string(ctx, argv[1], "stringDelim", &delim) ||
        !js_get_optional_prop_string(ctx, argv[1], "stringDelims", &string_delims) ||
        !js_get_optional_prop_string(ctx, argv[1], "identifierExtras", &identifier_extras) ||
        !js_get_optional_prop_string(ctx, argv[1], "numberMode", &number_mode) ||
        !js_get_optional_prop_string(ctx, argv[1], "escapeChar", &escape_char) ||
        !js_get_optional_prop_bool(ctx, argv[1], "multiLineStrings", &multi_line_strings)) {
        return JS_ThrowTypeError(ctx, "ted.registerLanguage: invalid string spec");
    }
    if (number_mode.len == 0) {
        number_mode = sp_str_lit("c-like");
    }
    if (!ext_str_eq_ci(number_mode, "c-like") && !ext_str_eq_ci(number_mode, "strict")) {
        return JS_ThrowTypeError(ctx, "ted.registerLanguage: numberMode must be c-like|strict");
    }
    if (string_delims.len == 0 && delim.len > 0) {
        string_delims = delim;
    }
    c8 esc = (escape_char.len > 0) ? escape_char.data[0] : '\\';

    syntax_conflict_policy_t policy = SYNTAX_CONFLICT_OVERRIDE;
    sp_str_t conflict = sp_str_lit("");
    if (!js_get_optional_prop_string(ctx, argv[1], "onConflict", &conflict)) {
        return JS_ThrowTypeError(ctx, "ted.registerLanguage: spec.onConflict must be string");
    }
    if (conflict.len > 0) {
        if (ext_str_eq_ci(conflict, "override")) {
            policy = SYNTAX_CONFLICT_OVERRIDE;
        } else if (ext_str_eq_ci(conflict, "skip")) {
            policy = SYNTAX_CONFLICT_SKIP;
        } else if (ext_str_eq_ci(conflict, "error")) {
            policy = SYNTAX_CONFLICT_ERROR;
        } else {
            return JS_ThrowTypeError(ctx, "ted.registerLanguage: onConflict must be override/skip/error");
        }
    }

    bool ok = syntax_register_language(
        sp_str_from_cstr(name),
        extensions,
        keywords,
        types,
        single_comments,
        multi_pairs,
        string_delims,
        identifier_extras,
        number_mode,
        esc,
        multi_line_strings,
        policy);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "ted.registerLanguage: registration failed");
    }
    return JS_UNDEFINED;
}

static void ext_prepare_stdlib(void) {
    if (G_ext_stdlib_ready) return;

    memcpy(G_ext_cfunc_table, js_c_function_table, sizeof(js_c_function_table));

    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_VERSION)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_version }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 0, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_GET_TEXT)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_get_text }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 0, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_SET_TEXT)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_set_text }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 1, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_MESSAGE)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_message }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 1, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_GOTO)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_goto }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 1, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_OPEN)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_open }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 1, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_SAVE)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_save }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 1, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_REGISTER_COMMAND)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_register_command }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 2, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_REGISTER_LANGUAGE)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_register_language }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 2, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_REGISTER_OPERATOR_TARGET)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_register_operator_target }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 2, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_REGISTER_RECOGNIZER)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_register_recognizer }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 2, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_SKETCH_MODE)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_sketch_mode }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 1, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_SKETCH_CLEAR)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_sketch_clear }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 0, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_SKETCH_STATUS)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_sketch_status }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 0, .magic = 0 };
    G_ext_cfunc_table[TED_CFUNC_INDEX(TED_CFUNC_SKETCH_SHAPES)] =
        (JSCFunctionDef){ .func = { .generic = ted_js_sketch_shapes }, .name = JS_UNDEFINED, .def_type = JS_CFUNC_generic, .arg_count = 0, .magic = 0 };

    G_ext_stdlib = js_stdlib;
    G_ext_stdlib.c_function_table = G_ext_cfunc_table;

    G_ext_stdlib_ready = true;
}

static void ext_register_host_api(void) {
    JSValue global = JS_GetGlobalObject(G_ctx);
    JSValue ted = JS_NewObject(G_ctx);

    JS_SetPropertyStr(G_ctx, ted, "version", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_VERSION), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "getText", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_GET_TEXT), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "setText", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_SET_TEXT), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "message", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_MESSAGE), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "goto", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_GOTO), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "open", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_OPEN), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "save", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_SAVE), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "registerCommand", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_REGISTER_COMMAND), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "registerLanguage", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_REGISTER_LANGUAGE), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "registerOperatorTarget", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_REGISTER_OPERATOR_TARGET), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "registerRecognizer", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_REGISTER_RECOGNIZER), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "sketchMode", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_SKETCH_MODE), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "sketchClear", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_SKETCH_CLEAR), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "sketchStatus", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_SKETCH_STATUS), JS_UNDEFINED));
    JS_SetPropertyStr(G_ctx, ted, "sketchShapes", JS_NewCFunctionParams(G_ctx, TED_CFUNC_INDEX(TED_CFUNC_SKETCH_SHAPES), JS_UNDEFINED));

    JS_SetPropertyStr(G_ctx, global, "ted", ted);
}

static JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    for (int i = 0; i < argc; i++) {
        if (i > 0) fputc(' ', stdout);
        JSCStringBuf sb;
        const char *s = JS_ToCString(ctx, argv[i], &sb);
        if (s) fputs(s, stdout);
    }
    fputc('\n', stdout);
    return JS_UNDEFINED;
}

static JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return JS_NewInt64(ctx, (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000));
}

static JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    return js_date_now(ctx, this_val, argc, argv);
}

static JSValue js_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    JS_GC(ctx);
    return JS_UNDEFINED;
}

static JSValue js_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "load: missing filename");

    JSCStringBuf sb;
    const char *path = JS_ToCString(ctx, argv[0], &sb);
    if (!path) return JS_ThrowTypeError(ctx, "load: invalid filename");

    FILE *f = fopen(path, "rb");
    if (!f) return JS_ThrowTypeError(ctx, "load: cannot open file");
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return JS_ThrowTypeError(ctx, "load: invalid file");
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return JS_ThrowOutOfMemory(ctx);
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    if (n != (size_t)sz || ferror(f)) {
        free(buf);
        fclose(f);
        return JS_ThrowTypeError(ctx, "load: failed to read file");
    }
    fclose(f);
    buf[n] = '\0';

    JSValue v = JS_Eval(ctx, buf, n, path, JS_EVAL_RETVAL);
    free(buf);
    return v;
}

static JSValue js_setTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, 0);
}

static JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_UNDEFINED;
}

static void ext_shutdown(void) {
    if (G_ctx) {
        JS_FreeContext(G_ctx);
        G_ctx = SP_NULLPTR;
    }
    if (G_mem) {
        free(G_mem);
        G_mem = SP_NULLPTR;
    }
    G_ext_ready = false;
}

static bool ext_ensure_ready(sp_str_t *error) {
    if (G_ext_ready && G_ctx) return true;

    ext_prepare_stdlib();

    if (!G_mem) {
        G_mem = malloc(EXT_VM_MEM_SIZE);
        if (!G_mem) {
            if (error) *error = sp_str_lit("ext: failed to allocate VM memory");
            return false;
        }
    }

    if (!G_ctx) {
        G_ctx = JS_NewContext(G_mem, EXT_VM_MEM_SIZE, &G_ext_stdlib);
        if (!G_ctx) {
            if (error) *error = sp_str_lit("ext: failed to initialize mquickjs context");
            return false;
        }
        ext_register_host_api();
        atexit(ext_shutdown);
    }

    G_ext_ready = true;
    return true;
}

static bool ext_eval_with_filename(sp_str_t code, const c8 *filename, sp_str_t *output, sp_str_t *error) {
    if (!ext_ensure_ready(error)) {
        if (output) *output = sp_str_lit("");
        return false;
    }

    JSValue val = JS_Eval(G_ctx, code.data, code.len, filename, JS_EVAL_RETVAL);
    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(G_ctx);
        JSCStringBuf buf;
        const char *msg = JS_ToCString(G_ctx, exc, &buf);
        if (error) {
            *error = msg ? sp_str_from_cstr(msg) : sp_str_lit("ext: javascript exception");
        }
        if (output) *output = sp_str_lit("");
        return false;
    }

    if (output) {
        if (JS_IsUndefined(val) || JS_IsNull(val)) {
            *output = sp_str_lit("");
        } else {
            JSCStringBuf out_buf;
            const char *out = JS_ToCString(G_ctx, val, &out_buf);
            *output = out ? sp_str_from_cstr(out) : sp_str_lit("");
        }
    }

    if (error) *error = sp_str_lit("");
    return true;
}

void ext_init(void) {
    sp_str_t err = sp_str_lit("");
    G_ext_ready = ext_ensure_ready(&err);
}

bool ext_eval(sp_str_t code, sp_str_t *output, sp_str_t *error) {
    return ext_eval_with_filename(code, "<ted-js>", output, error);
}

bool ext_run_file(sp_str_t path, sp_str_t *output, sp_str_t *error) {
    if (path.len == 0 || path.data == SP_NULLPTR) {
        if (error) *error = sp_str_lit("ext: empty source path");
        if (output) *output = sp_str_lit("");
        return false;
    }

    sp_str_t code = sp_io_read_file(path);
    if (code.len == 0 && code.data == SP_NULLPTR) {
        if (error) *error = sp_str_lit("ext: failed to read source file");
        if (output) *output = sp_str_lit("");
        return false;
    }

    c8 filename[256];
    u32 n = path.len < sizeof(filename) - 1 ? path.len : sizeof(filename) - 1;
    if (n > 0) {
        memcpy(filename, path.data, n);
    }
    filename[n] = '\0';

    return ext_eval_with_filename(code, filename, output, error);
}

bool ext_invoke_registered_command(sp_str_t code, sp_str_t arg, sp_str_t *output, sp_str_t *error) {
    if (!ext_ensure_ready(error)) {
        if (output) *output = sp_str_lit("");
        return false;
    }

    JSValue global = JS_GetGlobalObject(G_ctx);
    JS_SetPropertyStr(G_ctx, global, "__ted_arg", JS_NewStringLen(G_ctx, arg.data, arg.len));

    return ext_eval_with_filename(code, "<ted-plugin-command>", output, error);
}

bool ext_invoke_operator_target(
    sp_str_t code, c8 op, sp_str_t seq, u32 count, u32 row, u32 col,
    sp_str_t *output, sp_str_t *error) {
    if (!ext_ensure_ready(error)) {
        if (output) *output = sp_str_lit("");
        return false;
    }

    JSValue global = JS_GetGlobalObject(G_ctx);
    c8 op_buf[2] = { op, '\0' };
    JS_SetPropertyStr(G_ctx, global, "__ted_op", JS_NewString(G_ctx, op_buf));
    JS_SetPropertyStr(G_ctx, global, "__ted_seq", JS_NewStringLen(G_ctx, seq.data, seq.len));
    JS_SetPropertyStr(G_ctx, global, "__ted_count", JS_NewInt32(G_ctx, (int)count));
    JS_SetPropertyStr(G_ctx, global, "__ted_row", JS_NewInt32(G_ctx, (int)row));
    JS_SetPropertyStr(G_ctx, global, "__ted_col", JS_NewInt32(G_ctx, (int)col));

    return ext_eval_with_filename(code, "<ted-plugin-op-target>", output, error);
}

bool ext_invoke_recognizer(sp_str_t stroke_json, sp_str_t *output, sp_str_t *error) {
    if (output) *output = sp_str_lit("");
    if (!ext_ensure_ready(error)) return false;

    for (u32 i = 0; i < G_recognizer_count; i++) {
        JSValue global = JS_GetGlobalObject(G_ctx);
        JS_SetPropertyStr(G_ctx, global, "__ted_stroke", JS_NewStringLen(G_ctx, stroke_json.data, stroke_json.len));
        JS_SetPropertyStr(G_ctx, global, "__ted_recognizer", JS_NewStringLen(G_ctx,
            G_recognizers[i].name.data, G_recognizers[i].name.len));

        sp_str_t local_out = sp_str_lit("");
        sp_str_t local_err = sp_str_lit("");
        if (!ext_eval_with_filename(G_recognizers[i].code, "<ted-plugin-recognizer>", &local_out, &local_err)) {
            if (error) *error = local_err;
            return false;
        }
        if (local_out.len > 0 && !sp_str_equal(local_out, sp_str_lit("null")) &&
            !sp_str_equal(local_out, sp_str_lit("undefined"))) {
            if (output) *output = local_out;
            if (error) *error = sp_str_lit("");
            return true;
        }
    }

    if (error) *error = sp_str_lit("");
    return true;
}

static bool ext_has_js_suffix(const c8 *name) {
    size_t n = strlen(name);
    return n > 3 && strcmp(name + n - 3, ".js") == 0;
}

static int ext_cmp_cstr(const void *a, const void *b) {
    const c8 *aa = *(const c8 *const *)a;
    const c8 *bb = *(const c8 *const *)b;
    return strcmp(aa, bb);
}

static void ext_loaded_plugins_reset(void) {
    G_loaded_plugin_count = 0;
}

static void ext_loaded_plugins_add(const c8 *name) {
    if (!name || name[0] == '\0') return;
    if (G_loaded_plugin_count >= EXT_LOADED_PLUGIN_CAP) return;
    G_loaded_plugins[G_loaded_plugin_count++] = sp_str_from_cstr(name);
}

sp_str_t ext_list_loaded_plugins(void) {
    if (G_loaded_plugin_count == 0) return sp_str_lit("");
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    for (u32 i = 0; i < G_loaded_plugin_count; i++) {
        if (i > 0) sp_str_builder_append_cstr(&b, ", ");
        sp_str_builder_append(&b, G_loaded_plugins[i]);
    }
    return sp_str_builder_to_str(&b);
}

u32 ext_loaded_plugin_count(void) {
    return G_loaded_plugin_count;
}

static u32 ext_autoload_plugins_from_dir(const c8 *dir_path, const c8 *prefix, sp_str_t *last_error) {
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    c8 *paths[256];
    u32 path_count = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (!ext_has_js_suffix(ent->d_name)) continue;
        if (path_count >= 256) break;

        size_t full_len = strlen(dir_path) + 1 + strlen(ent->d_name) + 1;
        c8 *full = malloc(full_len);
        if (!full) continue;
        snprintf(full, full_len, "%s/%s", dir_path, ent->d_name);
        paths[path_count++] = full;
    }
    closedir(dir);

    qsort(paths, path_count, sizeof(paths[0]), ext_cmp_cstr);

    u32 loaded = 0;
    for (u32 i = 0; i < path_count; i++) {
        sp_str_t out = sp_str_lit("");
        sp_str_t err = sp_str_lit("");
        bool ok = ext_run_file(sp_str_from_cstr(paths[i]), &out, &err);
        if (ok) {
            loaded++;
            const c8 *base = strrchr(paths[i], '/');
            base = base ? base + 1 : paths[i];
            c8 label[256];
            if (prefix && prefix[0] != '\0') {
                snprintf(label, sizeof(label), "%s%s", prefix, base);
            } else {
                snprintf(label, sizeof(label), "%s", base);
            }
            ext_loaded_plugins_add(label);
        } else if (last_error) {
            const c8 *base = strrchr(paths[i], '/');
            base = base ? base + 1 : paths[i];
            sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
            sp_str_builder_t b = sp_str_builder_from_writer(&writer);
            sp_str_builder_append_cstr(&b, "Plugin ");
            sp_str_builder_append_cstr(&b, base);
            sp_str_builder_append_cstr(&b, " failed: ");
            sp_str_builder_append(&b, err);
            *last_error = sp_str_builder_to_str(&b);
        }
        free(paths[i]);
    }

    return loaded;
}

u32 ext_autoload_plugins(sp_str_t *last_error) {
    if (last_error) *last_error = sp_str_lit("");
    ext_loaded_plugins_reset();

    const c8 *home = getenv("HOME");
    if (!home || home[0] == '\0') return 0;

    c8 dir_root[PATH_MAX];
    c8 dir_lang[PATH_MAX];
    snprintf(dir_root, sizeof(dir_root), "%s/.ted/plugins", home);
    snprintf(dir_lang, sizeof(dir_lang), "%s/.ted/plugins/lang", home);

    // Deterministic order: root plugins first, then language packages.
    u32 loaded = 0;
    loaded += ext_autoload_plugins_from_dir(dir_root, "", last_error);
    loaded += ext_autoload_plugins_from_dir(dir_lang, "lang/", last_error);
    return loaded;
}
