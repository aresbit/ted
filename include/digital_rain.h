#pragma once

#include <sp.h>

// System headers for terminal control
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ANSI escape codes for colors and cursor control
#define DR_ESC "\x1b["
#define DR_CLEAR DR_ESC "2J"
#define DR_CLEAR_LINE DR_ESC "2K"
#define DR_CURSOR_HOME DR_ESC "H"
#define DR_CURSOR_HIDE DR_ESC "?25l"
#define DR_CURSOR_SHOW DR_ESC "?25h"
#define DR_RESET_COLORS DR_ESC "0m"

// ANSI color codes
#define DR_COLOR_FG_BLACK "30"
#define DR_COLOR_FG_RED "31"
#define DR_COLOR_FG_GREEN "32"
#define DR_COLOR_FG_YELLOW "33"
#define DR_COLOR_FG_BLUE "34"
#define DR_COLOR_FG_MAGENTA "35"
#define DR_COLOR_FG_CYAN "36"
#define DR_COLOR_FG_WHITE "37"
#define DR_COLOR_FG_BRIGHT_BLACK "90"
#define DR_COLOR_FG_BRIGHT_RED "91"
#define DR_COLOR_FG_BRIGHT_GREEN "92"
#define DR_COLOR_FG_BRIGHT_YELLOW "93"
#define DR_COLOR_FG_BRIGHT_BLUE "94"
#define DR_COLOR_FG_BRIGHT_MAGENTA "95"
#define DR_COLOR_FG_BRIGHT_CYAN "96"
#define DR_COLOR_FG_BRIGHT_WHITE "97"

#define DR_COLOR_BG_BLACK "40"
#define DR_COLOR_BG_RED "41"
#define DR_COLOR_BG_GREEN "42"
#define DR_COLOR_BG_YELLOW "43"
#define DR_COLOR_BG_BLUE "44"
#define DR_COLOR_BG_MAGENTA "45"
#define DR_COLOR_BG_CYAN "46"
#define DR_COLOR_BG_WHITE "47"

// Terminal digital rain animation
typedef struct {
    // Terminal dimensions
    s32 term_cols;
    s32 term_rows;
    
    // Animation settings
    s32 line_len_min;
    s32 line_len_max;
    s32 line_speed_min;
    s32 line_speed_max;
    s32 frame_delay_ms;
    
    // Colors (ANSI color codes)
    sp_str_t head_color;
    sp_str_t text_color;
    sp_str_t bg_color;
    
    // Column states
    sp_dyn_array(s32) line_lengths;   // Length of each column
    sp_dyn_array(s32) line_positions; // Current position (row index)
    sp_dyn_array(s32) line_speeds;    // Speed of each column (rows per frame)
    sp_dyn_array(u64) line_seeds;     // Random seed for each column
    
    // Timing
    sp_tm_timer_t timer;
    u64 last_frame_time;
    
    // Control flags
    bool is_running;
    bool alphabet_only;
    bool use_colors;
    
    // Terminal state
    bool terminal_modified;
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
    struct termios original_termios;
#elif defined(_WIN32)
    DWORD original_console_mode;
    HANDLE console_handle;
#endif
} digital_rain_t;

// API Functions
digital_rain_t digital_rain_create(void);
void digital_rain_destroy(digital_rain_t* rain);
bool digital_rain_init(digital_rain_t* rain);
void digital_rain_run(digital_rain_t* rain);
void digital_rain_run_for_ms(digital_rain_t* rain, u64 duration_ms);
void digital_rain_stop(digital_rain_t* rain);

// Utility functions
s32 digital_rain_random_range(s32 min, s32 max, u64* seed);
c8 digital_rain_random_ascii(u64* seed, bool alphabet_only);
void digital_rain_get_terminal_size(s32* cols, s32* rows);
bool digital_rain_setup_terminal(digital_rain_t* rain);
void digital_rain_restore_terminal(digital_rain_t* rain);
void digital_rain_clear_screen(void);
void digital_rain_set_cursor_position(s32 col, s32 row);
void digital_rain_set_color(sp_str_t color);

#ifdef __cplusplus
}
#endif