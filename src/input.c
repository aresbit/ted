/**
 * input.c - Input handling and key processing
 * FIXED VERSION
 */

#include "ted.h"
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <string.h>
#include <ctype.h>

static bool str_has_prefix(sp_str_t str, const c8 *prefix) {
    u32 p_len = (u32)strlen(prefix);
    if (str.len < p_len) return false;
    return memcmp(str.data, prefix, p_len) == 0;
}

typedef enum {
    COMP_SCOPE_NONE = 0,
    COMP_SCOPE_CMD,
    COMP_SCOPE_SET,
    COMP_SCOPE_SYNTAX,
    COMP_SCOPE_THEME,
} completion_scope_t;

typedef struct {
    bool active;
    completion_scope_t scope;
    c8 before[128];
    c8 after[128];
    const c8 *matches[32];
    u32 match_count;
    u32 total_match_count;
    u32 index;
} completion_cycle_t;

static completion_cycle_t G_completion_cycle = {0};
static const c8 *CMD_CANDIDATES[] = {
    "w", "write", "q", "quit", "wq", "q!", "goto", "g",
    "set", "syntax", "e", "edit", "e!", "edit!", "help", "h",
    "agent", "llm", "llmshow", "llmcopy", "llmstatus",
    "js", "source", "plugins", "langs", "targets", "theme"
};
static const c8 *SET_CANDIDATES[] = {
    "nu", "number", "nonu", "nonumber", "syntax", "nosyntax", "wrap", "nowrap"
};
static const c8 *SYNTAX_CANDIDATES[] = { "on", "off", "tree", "tree on", "tree off", "tree status" };
static const c8 *THEME_CANDIDATES[] = { "cyber", "warm", "night" };

typedef struct {
    sp_str_t seq;
    sp_str_t code;
} operator_target_t;

#define OP_TARGET_CAP 64
static operator_target_t G_op_targets[OP_TARGET_CAP];
static u32 G_op_target_count = 0;

static void completion_cycle_reset(void) {
    G_completion_cycle.active = false;
    G_completion_cycle.scope = COMP_SCOPE_NONE;
    G_completion_cycle.before[0] = '\0';
    G_completion_cycle.after[0] = '\0';
    G_completion_cycle.match_count = 0;
    G_completion_cycle.total_match_count = 0;
    G_completion_cycle.index = 0;
}

static sp_str_t str_from_cstr(const c8 *cstr) { return sp_str_from_cstr(cstr); }

static void cstr_copy_bounded(c8 *dst, u32 dst_size, sp_str_t src) {
    if (dst_size == 0) return;
    u32 n = src.len < (dst_size - 1) ? src.len : (dst_size - 1);
    if (n > 0) memcpy(dst, src.data, n);
    dst[n] = '\0';
}

static void completion_apply_current(void) {
    if (!G_completion_cycle.active || G_completion_cycle.match_count == 0) return;

    const c8 *cand = G_completion_cycle.matches[G_completion_cycle.index];
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    sp_str_builder_append_cstr(&b, G_completion_cycle.before);
    sp_str_builder_append_cstr(&b, cand);
    sp_str_builder_append_cstr(&b, G_completion_cycle.after);
    E.command_buffer = sp_str_builder_to_str(&b);
}

static sp_str_t completion_build_hint(const c8 **matches, u32 match_count, u32 total_match_count, u32 index) {
    if (match_count == 0) return sp_str_lit("");

    u32 max_hint_len = E.screen_cols > 20 ? E.screen_cols - 12 : 48;
    u32 current_len = 0;

    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    sp_str_builder_append_cstr(&b, "Tab ");
    sp_str_builder_append(&b, sp_format(
        "{} / {}",
        SP_FMT_U32(index + 1),
        SP_FMT_U32(total_match_count)));
    sp_str_builder_append_cstr(&b, ": ");
    current_len = 8;

    for (u32 i = 0; i < match_count; i++) {
        const c8 *cand = matches[i];
        u32 cand_len = (u32)strlen(cand);
        u32 added = cand_len + (i > 0 ? 2 : 0) + (i == index ? 2 : 0);
        if (current_len + added > max_hint_len) {
            sp_str_builder_append_cstr(&b, ", ...");
            break;
        }
        if (i > 0) {
            sp_str_builder_append_cstr(&b, ", ");
            current_len += 2;
        }
        if (i == index) {
            sp_str_builder_append_cstr(&b, "[");
            current_len += 1;
        }
        sp_str_builder_append_cstr(&b, cand);
        current_len += cand_len;
        if (i == index) {
            sp_str_builder_append_cstr(&b, "]");
            current_len += 1;
        }
    }

    if (total_match_count > match_count) {
        sp_str_builder_append_cstr(&b, " (+more)");
    }

    return sp_str_builder_to_str(&b);
}

static void completion_update_hint(void) {
    if (!G_completion_cycle.active || G_completion_cycle.match_count == 0) {
        E.command_hint = sp_str_lit("");
        return;
    }

    E.command_hint = completion_build_hint(
        G_completion_cycle.matches,
        G_completion_cycle.match_count,
        G_completion_cycle.total_match_count,
        G_completion_cycle.index);
}

static u32 completion_collect(sp_str_t token, const c8 **candidates, u32 n, const c8 **out, u32 out_cap) {
    u32 count = 0;
    for (u32 i = 0; i < n; i++) {
        sp_str_t cand = str_from_cstr(candidates[i]);
        if (token.len > cand.len) continue;
        if (memcmp(cand.data, token.data, token.len) != 0) continue;
        if (count < out_cap) out[count] = candidates[i];
        count++;
    }
    return count;
}

static bool completion_resolve_context(
    sp_str_t buf,
    completion_scope_t *scope,
    const c8 ***candidates,
    u32 *candidate_count,
    sp_str_t *token,
    sp_str_t *before,
    sp_str_t *after,
    const c8 **fail_hint
) {
    *scope = COMP_SCOPE_NONE;
    *candidates = SP_NULLPTR;
    *candidate_count = 0;
    *token = sp_str_lit("");
    *before = sp_str_lit("");
    *after = sp_str_lit("");
    *fail_hint = "";

    if (str_has_prefix(buf, "set ")) {
        *scope = COMP_SCOPE_SET;
        *candidates = SET_CANDIDATES;
        *candidate_count = sizeof(SET_CANDIDATES) / sizeof(SET_CANDIDATES[0]);
        *before = sp_str_lit("set ");
        *token = sp_str_sub(buf, 4, (s32)(buf.len - 4));
        return true;
    }

    if (str_has_prefix(buf, "syntax ")) {
        *scope = COMP_SCOPE_SYNTAX;
        *candidates = SYNTAX_CANDIDATES;
        *candidate_count = sizeof(SYNTAX_CANDIDATES) / sizeof(SYNTAX_CANDIDATES[0]);
        *before = sp_str_lit("syntax ");
        *token = sp_str_sub(buf, 7, (s32)(buf.len - 7));
        return true;
    }

    if (str_has_prefix(buf, "theme ")) {
        *scope = COMP_SCOPE_THEME;
        *candidates = THEME_CANDIDATES;
        *candidate_count = sizeof(THEME_CANDIDATES) / sizeof(THEME_CANDIDATES[0]);
        *before = sp_str_lit("theme ");
        *token = sp_str_sub(buf, 6, (s32)(buf.len - 6));
        return true;
    }

    bool has_space = false;
    for (u32 i = 0; i < buf.len; i++) {
        if (buf.data[i] == ' ') {
            has_space = true;
            break;
        }
    }

    if (has_space) {
        *fail_hint = "No completion for this argument";
        return false;
    }

    *scope = COMP_SCOPE_CMD;
    *candidates = CMD_CANDIDATES;
    *candidate_count = sizeof(CMD_CANDIDATES) / sizeof(CMD_CANDIDATES[0]);
    *before = sp_str_lit("");
    *token = buf;
    return true;
}

static void input_update_command_hint_preview(void) {
    sp_str_t buf = E.command_buffer;
    completion_scope_t scope = COMP_SCOPE_NONE;
    const c8 **candidates = SP_NULLPTR;
    u32 candidate_count = 0;
    sp_str_t token = sp_str_lit("");
    sp_str_t before = sp_str_lit("");
    sp_str_t after = sp_str_lit("");
    const c8 *fail_hint = "";
    const c8 *preview_matches[32];

    if (!completion_resolve_context(
            buf, &scope, &candidates, &candidate_count, &token, &before, &after, &fail_hint)) {
        E.command_hint = sp_str_from_cstr(fail_hint);
        return;
    }

    u32 matched = completion_collect(token, candidates, candidate_count, preview_matches, 32);
    if (matched == 0) {
        E.command_hint = sp_str_lit("No completion match");
        return;
    }

    if (matched == 1) {
        E.command_hint = sp_format("Ready: {}", SP_FMT_CSTR(preview_matches[0]));
        return;
    }

    u32 shown = matched > 32 ? 32 : matched;
    E.command_hint = completion_build_hint(preview_matches, shown, matched, 0);
}

static void input_complete_command_direction(bool forward) {
    sp_str_t buf = E.command_buffer;
    completion_scope_t scope = COMP_SCOPE_NONE;
    const c8 **candidates = SP_NULLPTR;
    u32 candidate_count = 0;
    sp_str_t token = sp_str_lit("");
    sp_str_t before = sp_str_lit("");
    sp_str_t after = sp_str_lit("");

    const c8 *fail_hint = "";
    if (!completion_resolve_context(
            buf, &scope, &candidates, &candidate_count, &token, &before, &after, &fail_hint)) {
        E.command_hint = sp_str_from_cstr(fail_hint);
        completion_cycle_reset();
        return;
    }

    if (G_completion_cycle.active && G_completion_cycle.scope == scope) {
        if (forward) {
            G_completion_cycle.index = (G_completion_cycle.index + 1) % G_completion_cycle.match_count;
        } else {
            G_completion_cycle.index = (G_completion_cycle.index + G_completion_cycle.match_count - 1) %
                G_completion_cycle.match_count;
        }
        completion_apply_current();
        completion_update_hint();
        return;
    }

    u32 matched = completion_collect(token, candidates, candidate_count, G_completion_cycle.matches, 32);
    if (matched == 0) {
        E.command_hint = sp_str_lit("No completion match");
        completion_cycle_reset();
        return;
    }

    if (matched == 1) {
        sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
        sp_str_builder_t b = sp_str_builder_from_writer(&writer);
        sp_str_builder_append(&b, before);
        sp_str_builder_append_cstr(&b, G_completion_cycle.matches[0]);
        sp_str_builder_append(&b, after);
        E.command_buffer = sp_str_builder_to_str(&b);
        E.command_hint = sp_str_lit("Completed");
        completion_cycle_reset();
        return;
    }

    G_completion_cycle.active = true;
    G_completion_cycle.scope = scope;
    G_completion_cycle.match_count = matched > 32 ? 32 : matched;
    G_completion_cycle.total_match_count = matched;
    G_completion_cycle.index = forward ? 0 : (G_completion_cycle.match_count - 1);
    cstr_copy_bounded(G_completion_cycle.before, sizeof(G_completion_cycle.before), before);
    cstr_copy_bounded(G_completion_cycle.after, sizeof(G_completion_cycle.after), after);
    completion_apply_current();
    completion_update_hint();
}

static void input_complete_command(void) {
    input_complete_command_direction(true);
}

bool input_register_operator_target(sp_str_t seq, sp_str_t code) {
    if (seq.len == 0 || code.len == 0) return false;
    if (seq.len >= sizeof(E.pending_motion)) return false;

    for (u32 i = 0; i < G_op_target_count; i++) {
        if (!sp_str_equal(G_op_targets[i].seq, seq)) continue;
        G_op_targets[i].code = sp_str_copy(code);
        return true;
    }

    if (G_op_target_count >= OP_TARGET_CAP) return false;
    G_op_targets[G_op_target_count].seq = sp_str_copy(seq);
    G_op_targets[G_op_target_count].code = sp_str_copy(code);
    G_op_target_count++;
    return true;
}

sp_str_t input_list_operator_targets(void) {
    if (G_op_target_count == 0) return sp_str_lit("");
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    for (u32 i = 0; i < G_op_target_count; i++) {
        if (i > 0) sp_str_builder_append_cstr(&b, ", ");
        sp_str_builder_append(&b, G_op_targets[i].seq);
    }
    return sp_str_builder_to_str(&b);
}

static bool op_target_find_exact(sp_str_t seq, sp_str_t *code) {
    for (u32 i = 0; i < G_op_target_count; i++) {
        if (!sp_str_equal(G_op_targets[i].seq, seq)) continue;
        *code = G_op_targets[i].code;
        return true;
    }
    return false;
}

static bool op_target_has_prefix(sp_str_t seq) {
    for (u32 i = 0; i < G_op_target_count; i++) {
        sp_str_t cand = G_op_targets[i].seq;
        if (seq.len > cand.len) continue;
        if (memcmp(cand.data, seq.data, seq.len) == 0) return true;
    }
    return false;
}

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

            // Parse SGR mouse event: ESC [ < b ; x ; y (M/m)
            // b=button, x/y are 1-based terminal coordinates
            if (seq_len >= 6 && seq[1] == '<' &&
                (seq[seq_len - 1] == 'M' || seq[seq_len - 1] == 'm')) {
                u32 i = 2;
                u32 b = 0, x = 0, y = 0;
                while (i < seq_len && seq[i] >= '0' && seq[i] <= '9') {
                    b = b * 10 + (u32)(seq[i] - '0');
                    i++;
                }
                if (i < seq_len && seq[i] == ';') i++;
                while (i < seq_len && seq[i] >= '0' && seq[i] <= '9') {
                    x = x * 10 + (u32)(seq[i] - '0');
                    i++;
                }
                if (i < seq_len && seq[i] == ';') i++;
                while (i < seq_len && seq[i] >= '0' && seq[i] <= '9') {
                    y = y * 10 + (u32)(seq[i] - '0');
                    i++;
                }

                bool is_press = seq[seq_len - 1] == 'M';
                bool left_button = ((b & 0x3) == 0);
                if (left_button && x > 0 && y > 0) {
                    if (iui_tui_handle_mouse(x, y, is_press)) {
                        return 0;
                    }
                }
                return 0;
            }

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
                    return has_modifier && (modifier == 2 || modifier == 3) ? KEY_SHIFT_UP : KEY_UP;
                case 'B': // Down
                    return has_modifier && (modifier == 2 || modifier == 3) ? KEY_SHIFT_DOWN : KEY_DOWN;
                case 'C': // Right
                    return has_modifier && (modifier == 2 || modifier == 3) ? KEY_SHIFT_RIGHT : KEY_RIGHT;
                case 'D': // Left
                    return has_modifier && (modifier == 2 || modifier == 3) ? KEY_SHIFT_LEFT : KEY_LEFT;
                case 'H': // Home
                    return has_modifier && (modifier == 2 || modifier == 3) ? KEY_SHIFT_HOME : KEY_HOME;
                case 'F': // End
                    return has_modifier && (modifier == 2 || modifier == 3) ? KEY_SHIFT_END : KEY_END;
                case 'Z': // Shift+Tab
                    return KEY_SHIFT_TAB;
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

static bool op_is_word_char(c8 c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

static void op_finish_pending(void);

static void op_sync_cursor(void) {
    if (E.buffer.line_count == 0) {
        E.cursor.row = 0;
        E.cursor.col = 0;
        E.cursor.render_col = 0;
        return;
    }
    if (E.cursor.row >= E.buffer.line_count) {
        E.cursor.row = E.buffer.line_count - 1;
    }
    sp_str_t line = E.buffer.lines[E.cursor.row].text;
    if (E.cursor.col > line.len) {
        E.cursor.col = line.len;
    }
    E.cursor.render_col = buffer_row_to_render(&E.buffer, E.cursor.row, E.cursor.col);
}

static bool op_find_inner_word_bounds(sp_str_t line, u32 col, u32 *start, u32 *end) {
    if (line.len == 0) return false;
    if (col >= line.len && line.len > 0) col = line.len - 1;

    if (!op_is_word_char(line.data[col])) {
        u32 r = col;
        while (r < line.len && !op_is_word_char(line.data[r])) r++;
        if (r < line.len) {
            col = r;
        } else {
            if (col == 0) return false;
            u32 l = col - 1;
            while (true) {
                if (op_is_word_char(line.data[l])) {
                    col = l;
                    break;
                }
                if (l == 0) return false;
                l--;
            }
        }
    }

    u32 s = col;
    while (s > 0 && op_is_word_char(line.data[s - 1])) s--;
    u32 e = col + 1;
    while (e < line.len && op_is_word_char(line.data[e])) e++;
    if (e <= s) return false;
    *start = s;
    *end = e;
    return true;
}

static bool op_copy_range_current_line(u32 start, u32 end) {
    if (E.cursor.row >= E.buffer.line_count) return false;
    sp_str_t line = E.buffer.lines[E.cursor.row].text;
    if (start > line.len) start = line.len;
    if (end > line.len) end = line.len;
    if (end <= start) return false;
    E.clipboard = sp_str_copy(sp_str_sub(line, (s32)start, (s32)(end - start)));
    return true;
}

static bool op_delete_range_current_line(u32 start, u32 end) {
    if (E.cursor.row >= E.buffer.line_count) return false;
    sp_str_t line = E.buffer.lines[E.cursor.row].text;
    if (start > line.len) start = line.len;
    if (end > line.len) end = line.len;
    if (end <= start) return false;

    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    if (start > 0) {
        sp_str_builder_append(&b, sp_str_sub(line, 0, (s32)start));
    }
    if (end < line.len) {
        sp_str_builder_append(&b, sp_str_sub(line, (s32)end, (s32)(line.len - end)));
    }

    undo_record_delete_line(E.cursor.row, line);
    E.buffer.lines[E.cursor.row].text = sp_str_builder_to_str(&b);
    E.buffer.lines[E.cursor.row].hl_dirty = true;
    E.buffer.modified = true;
    E.cursor.col = start;
    op_sync_cursor();
    return true;
}

static u32 op_pending_count(void) {
    return E.pending_count > 0 ? E.pending_count : 1;
}

static void op_enter_insert_from_pending(void) {
    E.mode = MODE_INSERT;
    E.pending_operator = 0;
    E.pending_count = 0;
    E.pending_motion_len = 0;
    E.pending_motion[0] = '\0';
    editor_set_message("-- INSERT --");
}

static void op_apply_range(c8 op, u32 start, u32 end, const c8 *label) {
    if (start > end) {
        u32 tmp = start;
        start = end;
        end = tmp;
    }
    if (start == end) {
        editor_set_message("Nothing to %s", label);
        op_finish_pending();
        return;
    }

    bool copied = op_copy_range_current_line(start, end);
    if (op == 'y') {
        editor_set_message(copied ? "Yanked %s" : "Nothing to yank", label);
        op_finish_pending();
        return;
    }

    bool deleted = op_delete_range_current_line(start, end);
    if (op == 'd') {
        editor_set_message(deleted ? "Deleted %s" : "Nothing to delete", label);
        op_finish_pending();
        return;
    }

    if (op == 'c') {
        op_enter_insert_from_pending();
        return;
    }

    op_finish_pending();
}

static bool op_find_next_word_start(sp_str_t line, u32 col, u32 *out) {
    if (col >= line.len) return false;
    u32 i = col;
    if (op_is_word_char(line.data[i])) {
        while (i < line.len && op_is_word_char(line.data[i])) i++;
    }
    while (i < line.len && !op_is_word_char(line.data[i])) i++;
    if (i >= line.len) return false;
    *out = i;
    return true;
}

static bool op_find_prev_word_start(sp_str_t line, u32 col, u32 *out) {
    if (line.len == 0) return false;
    u32 i = col > line.len ? line.len : col;
    if (i > 0) i--;

    while (true) {
        if (op_is_word_char(line.data[i])) break;
        if (i == 0) return false;
        i--;
    }
    while (i > 0 && op_is_word_char(line.data[i - 1])) i--;
    *out = i;
    return true;
}

static bool op_find_word_end_exclusive(sp_str_t line, u32 col, u32 *out) {
    if (line.len == 0 || col >= line.len) return false;
    u32 i = col;
    while (i < line.len && !op_is_word_char(line.data[i])) i++;
    if (i >= line.len) return false;
    while (i < line.len && op_is_word_char(line.data[i])) i++;
    *out = i;
    return true;
}

static bool op_is_escaped(sp_str_t line, u32 idx) {
    if (idx == 0 || idx > line.len) return false;
    u32 bs = 0;
    s32 i = (s32)idx - 1;
    while (i >= 0 && line.data[i] == '\\') {
        bs++;
        i--;
    }
    return (bs % 2) == 1;
}

static bool op_find_quote_pair_around(sp_str_t line, u32 origin, u32 *left, u32 *right) {
    if (line.len < 2) return false;
    if (origin >= line.len) origin = line.len - 1;

    s32 open = -1;
    for (u32 i = 0; i < line.len; i++) {
        if (line.data[i] != '"' || op_is_escaped(line, i)) continue;
        if (open < 0) {
            open = (s32)i;
            continue;
        }
        u32 l = (u32)open;
        u32 r = i;
        if (origin >= l && origin <= r) {
            *left = l;
            *right = r;
            return true;
        }
        open = -1;
    }
    return false;
}

static bool op_find_paren_pair_around(sp_str_t line, u32 origin, u32 *left, u32 *right) {
    if (line.len < 2) return false;
    if (origin >= line.len) origin = line.len - 1;

    s32 l = -1;
    s32 depth = 0;
    for (s32 i = (s32)origin; i >= 0; i--) {
        c8 c = line.data[i];
        if (c == ')') {
            depth++;
        } else if (c == '(') {
            if (depth == 0) {
                l = i;
                break;
            }
            depth--;
        }
    }
    if (l < 0) return false;

    depth = 1;
    for (u32 i = (u32)l + 1; i < line.len; i++) {
        c8 c = line.data[i];
        if (c == '(') depth++;
        if (c == ')') {
            depth--;
            if (depth == 0) {
                *left = (u32)l;
                *right = i;
                return true;
            }
        }
    }
    return false;
}

static void op_apply_text_object_builtin(c8 op, bool around, c8 obj) {
    if (E.pending_origin_row >= E.buffer.line_count) {
        op_finish_pending();
        return;
    }
    E.cursor.row = E.pending_origin_row;
    op_sync_cursor();
    sp_str_t line = E.buffer.lines[E.cursor.row].text;
    u32 origin = E.pending_origin_col > line.len ? line.len : E.pending_origin_col;
    u32 left = 0;
    u32 right = 0;
    bool found = false;

    if (obj == '"') {
        found = op_find_quote_pair_around(line, origin, &left, &right);
        if (!found) {
            editor_set_message("No surrounding quotes");
            op_finish_pending();
            return;
        }
    } else if (obj == '(' || obj == ')') {
        found = op_find_paren_pair_around(line, origin, &left, &right);
        if (!found) {
            editor_set_message("No surrounding parentheses");
            op_finish_pending();
            return;
        }
    }

    u32 start = around ? left : (left + 1);
    u32 end = around ? (right + 1) : right;
    op_apply_range(op, start, end, around ? "around text object" : "inner text object");
}

static u32 op_find_first_non_blank(sp_str_t line) {
    u32 i = 0;
    while (i < line.len && (line.data[i] == ' ' || line.data[i] == '\t')) i++;
    return i;
}

static void op_apply_motion_builtin(c8 op, c8 motion) {
    if (E.pending_origin_row >= E.buffer.line_count) {
        op_finish_pending();
        return;
    }
    E.cursor.row = E.pending_origin_row;
    op_sync_cursor();
    sp_str_t line = E.buffer.lines[E.cursor.row].text;
    u32 origin = E.pending_origin_col > line.len ? line.len : E.pending_origin_col;

    if (motion == '0') {
        op_apply_range(op, 0, origin, "to line start");
        return;
    }
    if (motion == '^') {
        u32 target = op_find_first_non_blank(line);
        op_apply_range(op, target, origin, "to first non-blank");
        return;
    }

    u32 count = op_pending_count();

    if (motion == 'w') {
        u32 target = origin;
        for (u32 i = 0; i < count; i++) {
            if (!op_find_next_word_start(line, target, &target)) {
                editor_set_message("No next word");
                op_finish_pending();
                return;
            }
        }
        op_apply_range(op, origin, target, count > 1 ? "to next words" : "to next word");
        return;
    }
    if (motion == 'b') {
        u32 target = origin;
        for (u32 i = 0; i < count; i++) {
            if (!op_find_prev_word_start(line, target, &target)) {
                editor_set_message("No previous word");
                op_finish_pending();
                return;
            }
        }
        op_apply_range(op, target, origin, count > 1 ? "to previous words" : "to previous word");
        return;
    }
    if (motion == 'e') {
        u32 target = origin;
        for (u32 i = 0; i < count; i++) {
            if (!op_find_word_end_exclusive(line, target, &target)) {
                editor_set_message("No word end");
                op_finish_pending();
                return;
            }
        }
        op_apply_range(op, origin, target, count > 1 ? "to word ends" : "to word end");
        return;
    }
}

static void op_enter_pending(c8 op) {
    E.pending_operator = op;
    E.pending_count = E.normal_count > 0 ? E.normal_count : 1;
    E.normal_count = 0;
    E.pending_origin_row = E.cursor.row;
    E.pending_origin_col = E.cursor.col;
    E.pending_motion_len = 0;
    E.pending_motion[0] = '\0';
    E.mode = MODE_OPERATOR_PENDING;
    editor_set_message("%c pending: %c%c, $, iw, w/b/e/0/^, i\"/a\"/i(/a( or plugin", op, op, op);
}

static void op_finish_pending(void) {
    E.mode = MODE_NORMAL;
    E.pending_operator = 0;
    E.pending_count = 0;
    E.pending_motion_len = 0;
    E.pending_motion[0] = '\0';
}

static void op_apply_linewise(c8 op) {
    if (E.cursor.row >= E.buffer.line_count) {
        op_finish_pending();
        return;
    }

    u32 count = op_pending_count();
    u32 row = E.cursor.row;
    u32 last = row + count - 1;
    if (last >= E.buffer.line_count) last = E.buffer.line_count - 1;

    if (op == 'y') {
        sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
        sp_str_builder_t b = sp_str_builder_from_writer(&writer);
        for (u32 i = row; i <= last; i++) {
            if (i > row) sp_str_builder_append_c8(&b, '\n');
            sp_str_builder_append(&b, E.buffer.lines[i].text);
        }
        E.clipboard = sp_str_builder_to_str(&b);
        editor_set_message(last > row ? "Yanked %u lines" : "Yanked current line", (last - row + 1));
        op_finish_pending();
        return;
    }

    {
        sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
        sp_str_builder_t b = sp_str_builder_from_writer(&writer);
        for (u32 i = row; i <= last; i++) {
            if (i > row) sp_str_builder_append_c8(&b, '\n');
            sp_str_builder_append(&b, E.buffer.lines[i].text);
        }
        E.clipboard = sp_str_builder_to_str(&b);
    }

    if (op == 'd') {
        u32 to_delete = last - row + 1;
        for (u32 i = 0; i < to_delete; i++) {
            editor_delete_line(row);
        }
        op_sync_cursor();
        editor_set_message(to_delete > 1 ? "Deleted %u lines" : "Deleted current line", to_delete);
        op_finish_pending();
        return;
    }

    if (op == 'c') {
        u32 to_delete = last - row + 1;
        for (u32 i = 0; i < to_delete; i++) {
            editor_delete_line(row);
        }
        E.cursor.row = row;
        E.cursor.col = 0;
        op_sync_cursor();
        op_enter_insert_from_pending();
        return;
    }

    op_finish_pending();
}

static void op_apply_to_eol(c8 op) {
    if (E.cursor.row >= E.buffer.line_count) {
        op_finish_pending();
        return;
    }
    sp_str_t line = E.buffer.lines[E.cursor.row].text;
    u32 start = E.cursor.col;
    if (start > line.len) start = line.len;
    u32 end = line.len;

    bool copied = op_copy_range_current_line(start, end);
    bool deleted = false;
    if (op == 'd' || op == 'c') {
        deleted = op_delete_range_current_line(start, end);
    }

    if (op == 'y') {
        editor_set_message(copied ? "Yanked to end of line" : "Nothing to yank");
        op_finish_pending();
        return;
    }

    if (op == 'd') {
        editor_set_message(deleted ? "Deleted to end of line" : "Nothing to delete");
        op_finish_pending();
        return;
    }

    if (op == 'c') {
        op_enter_insert_from_pending();
        return;
    }

    op_finish_pending();
}

static void op_apply_inner_word(c8 op) {
    if (E.cursor.row >= E.buffer.line_count) {
        op_finish_pending();
        return;
    }
    sp_str_t line = E.buffer.lines[E.cursor.row].text;
    u32 start = 0;
    u32 end = 0;
    if (!op_find_inner_word_bounds(line, E.cursor.col, &start, &end)) {
        editor_set_message("No word under cursor");
        op_finish_pending();
        return;
    }

    op_copy_range_current_line(start, end);
    if (op == 'y') {
        editor_set_message("Yanked inner word");
        op_finish_pending();
        return;
    }

    op_delete_range_current_line(start, end);
    if (op == 'd') {
        editor_set_message("Deleted inner word");
        op_finish_pending();
        return;
    }

    if (op == 'c') {
        op_enter_insert_from_pending();
        return;
    }

    op_finish_pending();
}

void input_handle_normal(int c) {
    if (c >= '1' && c <= '9') {
        E.normal_count = E.normal_count * 10 + (u32)(c - '0');
        editor_set_message("count: %u", E.normal_count);
        return;
    }
    if (c == '0' && E.normal_count > 0) {
        E.normal_count *= 10;
        editor_set_message("count: %u", E.normal_count);
        return;
    }

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
        case '0':
            E.cursor.col = 0;
            E.cursor.render_col = buffer_row_to_render(&E.buffer, E.cursor.row, E.cursor.col);
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
            E.command_hint = sp_str_lit("Tab/Shift+Tab to complete commands");
            completion_cycle_reset();
            break;

        // Quick commands
        case '/':
            E.mode = MODE_SEARCH;
            E.search.forward = true;
            E.command_buffer = sp_str_lit("");
            E.search.query = sp_str_lit("");
            break;
        case '?':
            editor_set_message("Tips: i insert | / search | n/N next/prev | :w save | :q quit");
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
        case 'c':
        case 'y':
            op_enter_pending((c8)c);
            break;

        // Quit (in normal mode, allow q/Q)
        case 'q':
        case 'Q':
            editor_quit();
            break;

    }

    E.normal_count = 0;
}

void input_handle_operator_pending(int c) {
    c8 op = E.pending_operator;
    if (op != 'd' && op != 'c' && op != 'y') {
        op_finish_pending();
        return;
    }

    if (c == '\033') {
        op_finish_pending();
        editor_set_message("Operator cancelled");
        return;
    }

    if (!isprint(c)) {
        editor_set_message("Unsupported key in operator-pending");
        op_finish_pending();
        return;
    }
    if (E.pending_motion_len + 1 >= sizeof(E.pending_motion)) {
        editor_set_message("Operator sequence too long");
        op_finish_pending();
        return;
    }
    E.pending_motion[E.pending_motion_len++] = (c8)c;
    E.pending_motion[E.pending_motion_len] = '\0';

    sp_str_t seq = sp_str_from_cstr(E.pending_motion);

    if (seq.len == 1 && seq.data[0] == op) {
        op_apply_linewise(op);
        return;
    }
    if (seq.len == 1 && seq.data[0] == '$') {
        op_apply_to_eol(op);
        return;
    }
    if (sp_str_equal(seq, sp_str_lit("iw"))) {
        op_apply_inner_word(op);
        return;
    }
    if (sp_str_equal(seq, sp_str_lit("i\""))) {
        op_apply_text_object_builtin(op, false, '"');
        return;
    }
    if (sp_str_equal(seq, sp_str_lit("a\""))) {
        op_apply_text_object_builtin(op, true, '"');
        return;
    }
    if (sp_str_equal(seq, sp_str_lit("i(")) || sp_str_equal(seq, sp_str_lit("i)"))) {
        op_apply_text_object_builtin(op, false, '(');
        return;
    }
    if (sp_str_equal(seq, sp_str_lit("a(")) || sp_str_equal(seq, sp_str_lit("a)"))) {
        op_apply_text_object_builtin(op, true, '(');
        return;
    }
    if (seq.len == 1 &&
        (seq.data[0] == 'w' || seq.data[0] == 'b' || seq.data[0] == 'e' ||
         seq.data[0] == '0' || seq.data[0] == '^')) {
        op_apply_motion_builtin(op, seq.data[0]);
        return;
    }
    if (sp_str_equal(seq, sp_str_lit("i")) || sp_str_equal(seq, sp_str_lit("a"))) {
        editor_set_message("%c pending: waiting text object (iw, i\"/a\", i(/a( or plugin)", op);
        return;
    }

    sp_str_t target_code = sp_str_lit("");
    if (op_target_find_exact(seq, &target_code)) {
        sp_str_t out = sp_str_lit("");
        sp_str_t err = sp_str_lit("");
        if (!ext_invoke_operator_target(
                target_code, op, seq, op_pending_count(),
                E.pending_origin_row, E.pending_origin_col, &out, &err)) {
            editor_set_message("Operator target error: %.*s", (int)err.len, err.data);
            op_finish_pending();
            return;
        }

        if (sp_str_equal(out, sp_str_lit("line"))) {
            op_apply_linewise(op);
            return;
        }
        if (sp_str_equal(out, sp_str_lit("eol"))) {
            op_apply_to_eol(op);
            return;
        }
        if (sp_str_equal(out, sp_str_lit("word"))) {
            op_apply_inner_word(op);
            return;
        }
        if (str_has_prefix(out, "range:")) {
            u32 start = 0;
            u32 end = 0;
            u32 i = 6;
            while (i < out.len && out.data[i] >= '0' && out.data[i] <= '9') {
                start = start * 10 + (u32)(out.data[i] - '0');
                i++;
            }
            if (i >= out.len || out.data[i] != ':') {
                editor_set_message("Operator target invalid range");
                op_finish_pending();
                return;
            }
            i++;
            while (i < out.len && out.data[i] >= '0' && out.data[i] <= '9') {
                end = end * 10 + (u32)(out.data[i] - '0');
                i++;
            }
            bool copied = op_copy_range_current_line(start, end);
            if (op == 'y') {
                editor_set_message(copied ? "Yanked plugin range" : "Nothing to yank");
                op_finish_pending();
                return;
            }
            bool deleted = op_delete_range_current_line(start, end);
            if (op == 'd') {
                editor_set_message(deleted ? "Deleted plugin range" : "Nothing to delete");
                op_finish_pending();
                return;
            }
            op_enter_insert_from_pending();
            return;
        }

        if (out.len > 0) {
            editor_set_message("%.*s", (int)out.len, out.data);
        } else {
            editor_set_message("Operator target returned no action");
        }
        op_finish_pending();
        return;
    }

    if (op_target_has_prefix(seq)) {
        editor_set_message("%c pending: waiting target seq (%.*s...)", op, (int)seq.len, seq.data);
        return;
    }

    editor_set_message("Unsupported motion for %c: %.*s", op, (int)seq.len, seq.data);
    op_finish_pending();
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
            E.search.query = sp_str_lit("");
            E.search.match_count = 0;
            break;

        // Ctrl+G - goto line
        case 7: // Ctrl+G
            E.mode = MODE_COMMAND;
            E.command_buffer = sp_str_lit("goto ");
            E.command_hint = sp_str_lit("Enter line number, Tab for command completion");
            completion_cycle_reset();
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
            E.command_hint = sp_str_lit("");
            completion_cycle_reset();
            editor_set_message("Command cancelled");
            break;

        case '\r': // Enter - execute
        case '\n':
            command_execute(E.command_buffer);
            E.mode = MODE_NORMAL;
            E.command_buffer = sp_str_lit("");
            E.command_hint = sp_str_lit("");
            completion_cycle_reset();
            break;

        case 127: // Backspace
        case 8:
            if (E.command_buffer.len > 0) {
                E.command_buffer = truncate_string(E.command_buffer);
                completion_cycle_reset();
                input_update_command_hint_preview();
            } else {
                // Empty buffer, cancel command mode
                E.mode = MODE_NORMAL;
                E.command_hint = sp_str_lit("");
                completion_cycle_reset();
            }
            break;
        case '\t': // Tab completion
            input_complete_command();
            break;
        case KEY_SHIFT_TAB: // Shift+Tab reverse completion
            input_complete_command_direction(false);
            break;

        default:
            if (c >= 32 && c < 127) {
                sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
                sp_str_builder_t builder = sp_str_builder_from_writer(&writer);
                sp_str_builder_append(&builder, E.command_buffer);
                sp_str_builder_append_c8(&builder, c);
                E.command_buffer = sp_str_builder_to_str(&builder);
                completion_cycle_reset();
                input_update_command_hint_preview();
            }
            break;
    }
}

void input_handle_search(int c) {
    switch (c) {
        case '\033': // Escape - cancel
            E.mode = MODE_NORMAL;
            E.command_buffer = sp_str_lit("");
            search_end();
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
                if (E.mode == MODE_SEARCH) {
                    search_update_query(E.command_buffer);
                }
            } else {
                if (E.mode == MODE_SEARCH) {
                    search_end();
                }
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
                if (E.mode == MODE_SEARCH) {
                    search_update_query(E.command_buffer);
                }
            }
            break;
    }
}
