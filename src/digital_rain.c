// digital_rain.c - Terminal Matrix Digital Rain Animation
// Modern C implementation using sp.h library
// Based on Arduino DigitalRainAnimation.hpp by Eric Nam

#include "digital_rain.h"
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

// Global instance for signal handler
static digital_rain_t* g_rain = NULL;

// Signal handler for clean exit
static void signal_handler(int sig) {
    (void)sig;
    if (g_rain) {
        digital_rain_stop(g_rain);
    }
}

// Simple xorshift64 PRNG
static u64 xorshift64(u64* state) {
    u64 x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

// Create a new digital rain instance
digital_rain_t digital_rain_create(void) {
    digital_rain_t rain = SP_ZERO_STRUCT(digital_rain_t);
    
    // Default settings (similar to Arduino version)
    rain.line_len_min = 3;
    rain.line_len_max = 20;
    rain.line_speed_min = 1;
    rain.line_speed_max = 3;
    rain.frame_delay_ms = 100;
    
    // Default colors (ANSI escape sequences)
    rain.head_color = sp_str_lit(DR_ESC DR_COLOR_FG_BRIGHT_WHITE "m");
    rain.text_color = sp_str_lit(DR_ESC DR_COLOR_FG_GREEN "m");
    rain.bg_color = sp_str_lit(DR_ESC DR_COLOR_BG_BLACK "m");
    
    // Flags
    rain.is_running = false;
    rain.alphabet_only = false;
    rain.use_colors = true;
    rain.terminal_modified = false;
    
    return rain;
}

// Destroy digital rain instance
void digital_rain_destroy(digital_rain_t* rain) {
    if (!rain) return;
    
    // Restore terminal if modified
    if (rain->terminal_modified) {
        digital_rain_restore_terminal(rain);
    }
    
    // Free dynamic arrays
    sp_dyn_array_free(rain->line_lengths);
    sp_dyn_array_free(rain->line_positions);
    sp_dyn_array_free(rain->line_speeds);
    sp_dyn_array_free(rain->line_seeds);
    
    // Reset to zero
    *rain = SP_ZERO_STRUCT(digital_rain_t);
}

// Generate random number in range [min, max]
static s32 random_range(s32 min, s32 max, u64* seed) {
    if (min >= max) return min;
    u64 r = xorshift64(seed);
    return min + (s32)(r % (u64)(max - min + 1));
}

// Generate random ASCII character
static c8 random_ascii(u64* seed, bool alphabet_only) {
    if (alphabet_only) {
        static const c8 glyphs[] = "01";
        s32 idx = random_range(0, (s32)(sizeof(glyphs) - 2), seed);
        return glyphs[idx];
    } else {
        static const c8 glyphs[] = "01#/\\|[]{}<>+=-*";
        s32 idx = random_range(0, (s32)(sizeof(glyphs) - 2), seed);
        return glyphs[idx];
    }
}

// Get terminal size
void digital_rain_get_terminal_size(s32* cols, s32* rows) {
    if (!cols || !rows) return;
    
    *cols = 80;
    *rows = 24;
    
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *cols = (s32)ws.ws_col;
        *rows = (s32)ws.ws_row;
    }
#elif defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        *cols = (s32)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
        *rows = (s32)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
    }
#endif
}

// Setup terminal for raw output
bool digital_rain_setup_terminal(digital_rain_t* rain) {
    if (!rain) return false;
    
    // Check if stdout is a terminal
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
    if (!isatty(STDOUT_FILENO)) {
        // Not a terminal, but we can still try to run
        SP_LOG("Warning: stdout is not a terminal, animation may not work properly");
        rain->terminal_modified = false;
        return true;
    }
    
    // Try to setup terminal input for non-blocking read
    if (isatty(STDIN_FILENO)) {
        if (tcgetattr(STDIN_FILENO, &rain->original_termios) == 0) {
            struct termios raw = rain->original_termios;
            raw.c_lflag &= ~(ICANON | ECHO);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            
            if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
                rain->terminal_modified = true;
            }
        }
    }
    
    return true;
#elif defined(_WIN32)
    rain->console_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (rain->console_handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    if (GetConsoleMode(rain->console_handle, &rain->original_console_mode)) {
        DWORD new_mode = rain->original_console_mode;
        new_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
        new_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        
        if (SetConsoleMode(rain->console_handle, new_mode)) {
            rain->terminal_modified = true;
        }
    }
    
    return true;
#else
    // Unknown platform, continue without terminal setup
    rain->terminal_modified = false;
    return true;
#endif
}

// Restore terminal to original state
void digital_rain_restore_terminal(digital_rain_t* rain) {
    if (!rain || !rain->terminal_modified) return;
    
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
    tcsetattr(STDIN_FILENO, TCSANOW, &rain->original_termios);
#elif defined(_WIN32)
    if (rain->console_handle != INVALID_HANDLE_VALUE) {
        SetConsoleMode(rain->console_handle, rain->original_console_mode);
    }
#endif
    
    sp_os_print(sp_str_lit(DR_CURSOR_SHOW DR_RESET_COLORS));
    rain->terminal_modified = false;
}

// Clear screen
void digital_rain_clear_screen(void) {
    sp_os_print(sp_str_lit(DR_CLEAR DR_CURSOR_HOME));
}

// Set cursor position
void digital_rain_set_cursor_position(s32 col, s32 row) {
    sp_str_t cmd = sp_format(DR_ESC "{};{}H", 
                            SP_FMT_S32(row + 1), SP_FMT_S32(col + 1));
    sp_os_print(cmd);
}

// Set color
void digital_rain_set_color(sp_str_t color) {
    sp_os_print(color);
}

// Initialize digital rain with current terminal
bool digital_rain_init(digital_rain_t* rain) {
    if (!rain) return false;
    
    // Get terminal size
    digital_rain_get_terminal_size(&rain->term_cols, &rain->term_rows);
    if (rain->term_cols <= 0 || rain->term_rows <= 0) {
        SP_LOG("Failed to get terminal size");
        return false;
    }
    
    // Setup terminal
    if (!digital_rain_setup_terminal(rain)) {
        return false;
    }
    
    // Initialize random seeds
    sp_tm_epoch_t now = sp_tm_now_epoch();
    u64 base_seed = now.s ^ now.ns;
    
    // Create columns
    s32 num_columns = rain->term_cols;
    rain->line_lengths = SP_NULLPTR;
    rain->line_positions = SP_NULLPTR;
    rain->line_speeds = SP_NULLPTR;
    rain->line_seeds = SP_NULLPTR;
    
    for (s32 i = 0; i < num_columns; i++) {
        u64 seed = base_seed ^ (i * 0x9e3779b97f4a7c15ULL);
        xorshift64(&seed); // Mix
        
        s32 length = random_range(rain->line_len_min, rain->line_len_max, &seed);
        s32 speed = random_range(rain->line_speed_min, rain->line_speed_max, &seed);
        s32 phase = random_range(0, 3, &seed);
        s32 position = -random_range(0, rain->term_rows * (phase + 1), &seed);
        
        sp_dyn_array_push(rain->line_lengths, length);
        sp_dyn_array_push(rain->line_positions, position);
        sp_dyn_array_push(rain->line_speeds, speed);
        sp_dyn_array_push(rain->line_seeds, seed);
    }
    
    // Setup timer
    rain->timer = sp_tm_start_timer();
    rain->last_frame_time = 0;
    rain->is_running = true;
    
    // Clear screen
    digital_rain_clear_screen();
    
    return true;
}

// Draw a single frame
static void draw_frame(digital_rain_t* rain) {
    if (!rain || !rain->is_running) return;
    
    s32 num_columns = sp_dyn_array_size(rain->line_lengths);
    if (num_columns == 0) return;
    
    // Hide cursor
    sp_os_print(sp_str_lit(DR_CURSOR_HIDE));
    
    digital_rain_set_color(rain->bg_color);

    // For each column
    for (s32 col = 0; col < num_columns; col++) {
        s32 length = rain->line_lengths[col];
        s32 position = rain->line_positions[col];
        s32 speed = rain->line_speeds[col];
        u64 seed = rain->line_seeds[col];
        s32 next_position = position + speed;

        for (s32 i = 0; i < speed; i++) {
            s32 clear_row = position + i;
            if (clear_row >= 0 && clear_row < rain->term_rows) {
                digital_rain_set_cursor_position(col, clear_row);
                sp_os_print(sp_str_lit(" "));
            }
        }

        // Draw rain column
        for (s32 i = 0; i < length; i++) {
            s32 row_pos = next_position + i;
            
            if (row_pos >= 0 && row_pos < rain->term_rows) {
                digital_rain_set_cursor_position(col, row_pos);
                
                // Set color
                if (i == length - 1) {
                    if (rain->use_colors) {
                        digital_rain_set_color(rain->head_color);
                    }
                } else {
                    if (rain->use_colors) {
                        s32 green = 245 - (i * 150 / length);
                        s32 blue = 200 - (i * 140 / length);
                        if (green < 70) green = 70;
                        if (blue < 30) blue = 30;
                        sp_str_t color = sp_format(DR_ESC "38;2;0;{};{}m",
                                                  SP_FMT_S32(green),
                                                  SP_FMT_S32(blue));
                        digital_rain_set_color(color);
                    }
                }
                
                // Draw character
                c8 ch = random_ascii(&seed, rain->alphabet_only);
                sp_str_t ch_str = sp_format("{}", SP_FMT_C8(ch));
                sp_os_print(ch_str);
                
                rain->line_seeds[col] = seed;
            }
        }
        
        rain->line_positions[col] = next_position;
        
        // Reset if off screen
        if (next_position > rain->term_rows + length) {
            u64 new_seed = rain->line_seeds[col] ^ (next_position * 0x9e3779b97f4a7c15ULL);
            xorshift64(&new_seed);
            
            rain->line_lengths[col] = random_range(
                rain->line_len_min, rain->line_len_max, &new_seed);
            rain->line_positions[col] = -random_range(
                0, rain->term_rows * random_range(1, 3, &new_seed), &new_seed);
            rain->line_speeds[col] = random_range(
                rain->line_speed_min, rain->line_speed_max, &new_seed);
            rain->line_seeds[col] = new_seed;
        }
    }
    
    // Reset colors
    if (rain->use_colors) {
        sp_os_print(sp_str_lit(DR_RESET_COLORS));
    }
}

// Main animation loop
void digital_rain_run(digital_rain_t* rain) {
    if (!rain || !rain->is_running) return;
    
    // Setup signal handlers
    g_rain = rain;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Animation loop
    while (rain->is_running) {
        u64 current_time = sp_tm_read_timer(&rain->timer);
        u64 elapsed_ms = current_time / 1000000;
        
        if (elapsed_ms - rain->last_frame_time >= (u64)rain->frame_delay_ms) {
            draw_frame(rain);
            rain->last_frame_time = elapsed_ms;
        }
        
        // Check for key press to exit
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
        c8 ch;
        if (read(STDIN_FILENO, &ch, 1) > 0) {
            break;
        }
#elif defined(_WIN32)
        if (_kbhit()) {
            break;
        }
#endif
        
        sp_os_sleep_ms(1);
    }
    
    // Cleanup
    digital_rain_stop(rain);
    g_rain = NULL;
}

// Run animation for specified duration in milliseconds
void digital_rain_run_for_ms(digital_rain_t* rain, u64 duration_ms) {
    if (!rain || !rain->is_running) return;

    g_rain = rain;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    u64 start_time = sp_tm_read_timer(&rain->timer);
    u64 start_ms = start_time / 1000000;
    u64 end_ms = start_ms + duration_ms;

    while (rain->is_running) {
        u64 current_time = sp_tm_read_timer(&rain->timer);
        u64 current_ms = current_time / 1000000;
        if (current_ms >= end_ms) {
            break;
        }

        if (current_ms - rain->last_frame_time >= (u64)rain->frame_delay_ms) {
            draw_frame(rain);
            rain->last_frame_time = current_ms;
        }

        // Check for key press to exit (optional)
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
        c8 ch;
        if (read(STDIN_FILENO, &ch, 1) > 0) {
            break;
        }
#elif defined(_WIN32)
        if (_kbhit()) {
            break;
        }
#endif

        sp_os_sleep_ms(1);
    }

    digital_rain_stop(rain);
    g_rain = NULL;
}

// Stop animation
void digital_rain_stop(digital_rain_t* rain) {
    if (!rain) return;
    
    rain->is_running = false;
    digital_rain_restore_terminal(rain);
    digital_rain_clear_screen();
    sp_os_print(sp_str_lit(DR_CURSOR_SHOW DR_RESET_COLORS));
    
}
