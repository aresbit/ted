/**
 * main.c - Entry point for TED editor
 */

#define SP_IMPLEMENTATION
#include "ted.h"
#include "digital_rain.h"
#include <errno.h>
#include <string.h>

editor_t E;

static void print_usage(const c8 *prog) {
    sp_io_writer_t stderr_writer = sp_io_writer_from_fd(STDERR_FILENO, SP_IO_CLOSE_MODE_NONE);
    sp_io_write_cstr(&stderr_writer, "Usage: ");
    sp_io_write_cstr(&stderr_writer, prog);
    sp_io_write_cstr(&stderr_writer, " [filename]\n\n");
    sp_io_write_cstr(&stderr_writer, "TED - Termux Editor v" TED_VERSION "\n");
    sp_io_write_cstr(&stderr_writer, "A modern, touch-friendly code editor for Termux\n\n");
    sp_io_write_cstr(&stderr_writer, "Controls:\n");
    sp_io_write_cstr(&stderr_writer, "  Ctrl+S  Save file\n");
    sp_io_write_cstr(&stderr_writer, "  Ctrl+Q  Quit\n");
    sp_io_write_cstr(&stderr_writer, "  Ctrl+F  Search\n");
    sp_io_write_cstr(&stderr_writer, "  Ctrl+G  Go to line\n");
    sp_io_write_cstr(&stderr_writer, "  Ctrl+Z  Undo\n");
    sp_io_write_cstr(&stderr_writer, "  Ctrl+Y  Redo\n");
    sp_io_write_cstr(&stderr_writer, "  Ctrl+D  Delete line\n");
    sp_io_write_cstr(&stderr_writer, "  Ctrl+C  Copy selection (or copy line)\n");
    sp_io_write_cstr(&stderr_writer, "  Ctrl+X  Cut selection (or cut line)\n");
    sp_io_write_cstr(&stderr_writer, "  Ctrl+V  Paste\n");
    sp_io_write_cstr(&stderr_writer, "  Mouse   Click to move cursor, drag to select\n");
    sp_io_write_cstr(&stderr_writer, "  Ctrl+L  Clear screen/redraw\n");
    sp_io_write_cstr(&stderr_writer, "  Esc     Switch to normal mode\n");
    sp_io_write_cstr(&stderr_writer, "  i       Enter insert mode (in normal mode)\n");
    sp_io_write_cstr(&stderr_writer, "  :       Enter command mode\n");
    sp_io_flush(&stderr_writer);
}

void die(const c8 *msg) {
    display_clear();
    sp_io_writer_t stderr_writer = sp_io_writer_from_fd(STDERR_FILENO, SP_IO_CLOSE_MODE_NONE);
    sp_io_write_cstr(&stderr_writer, "Error: ");
    sp_io_write_cstr(&stderr_writer, msg);

    // Add errno information if available
    if (errno != 0) {
        sp_io_write_cstr(&stderr_writer, " (");
        sp_io_write_cstr(&stderr_writer, strerror(errno));
        sp_io_write_cstr(&stderr_writer, ")");
    }

    sp_io_write_cstr(&stderr_writer, "\n");
    sp_io_flush(&stderr_writer);
    exit(1);
}

s32 main(s32 argc, c8 **argv) {
    // Parse arguments
    if (argc > 2) {
        print_usage(argv[0]);
        return 1;
    }

    // Show help if requested
    if (argc == 2) {
        if (sp_cstr_equal(argv[1], "-h") || sp_cstr_equal(argv[1], "--help")) {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Show digital rain animation for 2 seconds
    digital_rain_t rain = digital_rain_create();
    rain.frame_delay_ms = 80; // optional
    rain.alphabet_only = false;
    rain.use_colors = true;
    if (digital_rain_init(&rain)) {
        digital_rain_run_for_ms(&rain, 2000);
        digital_rain_destroy(&rain);
    } else {
        // Animation failed, continue anyway
    }

    // Initialize editor
    editor_init();

    // Open file if provided
    if (argc == 2) {
        sp_str_t filename = sp_str_from_cstr(argv[1]);
        editor_open(filename);
    } else {
        // Start with empty buffer
        buffer_insert_line(&E.buffer, 0, sp_str_lit(""));
        E.buffer.filename = sp_str_lit("[No Name]");
    }

    // Main loop
    while (true) {
        display_refresh();
        editor_process_keypress();
    }

    return 0;
}
