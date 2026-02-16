/**
 * TED - Termux Editor
 * A modern, touch-friendly code editor for Termux
 */

#ifndef TED_H
#define TED_H

#include "sp.h"

// Version info
#define TED_VERSION "0.1.0"
#define TAB_WIDTH_DEFAULT 4
#define MAX_LINE_LENGTH 4096

// Special key codes (start at 0x1000 to avoid conflict with ASCII)
#define KEY_UP 0x1000
#define KEY_DOWN 0x1001
#define KEY_RIGHT 0x1002
#define KEY_LEFT 0x1003
#define KEY_HOME 0x1004
#define KEY_END 0x1005
#define KEY_DELETE 0x1006
#define KEY_PAGE_UP 0x1007
#define KEY_PAGE_DOWN 0x1008

// Shift+arrow key codes
#define KEY_SHIFT_UP 0x1100
#define KEY_SHIFT_DOWN 0x1101
#define KEY_SHIFT_RIGHT 0x1102
#define KEY_SHIFT_LEFT 0x1103
#define KEY_SHIFT_HOME 0x1104
#define KEY_SHIFT_END 0x1105

// Editor modes
typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_COMMAND,
    MODE_SEARCH,
    MODE_REPLACE,
} editor_mode_t;

// Highlight types for syntax highlighting
typedef enum {
    HL_NORMAL = 0,
    HL_KEYWORD,
    HL_STRING,
    HL_COMMENT,
    HL_NUMBER,
    HL_FUNCTION,
    HL_TYPE,
} highlight_type_t;

// Cursor position
typedef struct {
    u32 row;
    u32 col;
    u32 render_col;
} cursor_t;

// Text line with highlight info
typedef struct {
    sp_str_t text;
    highlight_type_t *hl;
    bool hl_dirty;
} line_t;

// Text buffer
typedef struct {
    line_t *lines;
    u32 line_count;
    u32 line_capacity;
    sp_str_t filename;
    bool modified;
    sp_str_t lang;
} buffer_t;

// Undo/Redo action types
typedef enum {
    ACTION_INSERT,
    ACTION_DELETE,
    ACTION_DELETE_LINE,
    ACTION_INSERT_LINE,
} action_type_t;

// Undo/Redo record
typedef struct {
    action_type_t type;
    u32 row;
    u32 col;
    sp_str_t text;
    sp_str_t old_text;
} action_t;

// Undo stack
typedef struct {
    action_t *actions;
    u32 count;
    u32 capacity;
    u32 current;
} undo_stack_t;

// Search state
typedef struct {
    sp_str_t query;
    u32 current_match;
    u32 match_count;
    bool case_sensitive;
    bool forward;
} search_state_t;

// Editor configuration
typedef struct {
    bool show_line_numbers;
    bool syntax_enabled;
    bool auto_wrap;
    bool show_whitespace;
    u32 tab_width;
} config_t;


// Main editor state
typedef struct {
    buffer_t buffer;
    cursor_t cursor;
    cursor_t saved_cursor;
    cursor_t select_start;
    bool has_selection;

    u32 row_offset;
    u32 col_offset;

    editor_mode_t mode;
    sp_str_t command_buffer;
    sp_str_t message;
    u32 message_time;

    search_state_t search;
    undo_stack_t undo;
    undo_stack_t redo;
    config_t config;
    sp_str_t clipboard;


    u32 screen_rows;
    u32 screen_cols;
} editor_t;

// Language definition for syntax highlighting
typedef struct {
    sp_str_t name;
    sp_str_t extensions;
    sp_str_t *keywords;
    u32 keyword_count;
    sp_str_t single_comment;
    sp_str_t multi_comment_start;
    sp_str_t multi_comment_end;
    c8 string_delim;
} language_t;

// Global editor instance
extern editor_t E;

// Function prototypes

// main.c
void die(const c8 *msg);

// editor.c
void editor_init(void);
void editor_open(sp_str_t filename);
bool editor_save(void);
void editor_quit(void);
void editor_process_keypress(void);
void editor_insert_char(c8 c);
void editor_insert_newline(void);
void editor_delete_char(void);
void editor_delete_line(u32 row);
void editor_copy_line(void);
void editor_cut_line(void);
void editor_paste(void);
void editor_move_cursor(u32 key);
void editor_goto_line(u32 line);
void editor_set_message(const c8 *fmt, ...);

// buffer.c
void buffer_init(buffer_t *buf);
void buffer_free(buffer_t *buf);
void buffer_load_file(buffer_t *buf, sp_str_t filename);
void buffer_save_file(buffer_t *buf);
void buffer_insert_line(buffer_t *buf, u32 at, sp_str_t text);
void buffer_delete_line(buffer_t *buf, u32 at);
void buffer_insert_char_at(buffer_t *buf, u32 row, u32 col, c8 c);
void buffer_delete_char_at(buffer_t *buf, u32 row, u32 col);
sp_str_t buffer_get_line(buffer_t *buf, u32 row);
u32 buffer_row_to_render(buffer_t *buf, u32 row, u32 col);
u32 buffer_render_to_row(buffer_t *buf, u32 row, u32 render_col);

// display.c
void display_init(void);
void display_refresh(void);
void display_draw_rows(void);
void display_draw_status_bar(void);
void display_draw_message_bar(void);
void display_clear(void);
void display_set_cursor(u32 row, u32 col);
u32 display_get_screen_rows(void);
u32 display_get_screen_cols(void);

// syntax.c
void syntax_init(void);
language_t* syntax_detect_language(sp_str_t filename);
void syntax_highlight_line(line_t *line, language_t *lang);
void syntax_highlight_buffer(buffer_t *buf);
c8* syntax_color_to_ansi(highlight_type_t type);

// search.c
void search_init(void);
void search_start(bool forward);
void search_next(void);
void search_prev(void);
void search_update_query(sp_str_t query);
void search_replace_current(sp_str_t replacement);
void search_replace_all(sp_str_t replacement);
void search_end(void);

// command.c
void command_init(void);
void command_handle_input(c8 c);
void command_execute(sp_str_t cmd);
void command_show_prompt(void);

// undo.c
void undo_init(undo_stack_t *stack);
void undo_push(undo_stack_t *stack, action_t *action);
action_t* undo_pop(undo_stack_t *stack);
void undo_clear(undo_stack_t *stack);
void undo_record_insert(u32 row, u32 col, c8 c);
void undo_record_delete(u32 row, u32 col, c8 c);
void undo_record_insert_line(u32 row, sp_str_t text);
void undo_record_delete_line(u32 row, sp_str_t text);
void undo_perform(void);
void redo_perform(void);

// input.c
int input_read_key(void);
bool input_read_escape_sequence(c8 *seq, u32 *len);
void input_process(c8 c);
void input_handle_normal(int c);
void input_handle_insert(int c);
void input_handle_command(int c);
void input_handle_search(int c);

#endif // TED_H
