/*
 * A Forth dialect compatible with Forth Salon (forthsalon.appspot.com)
 * used as a pixel shader.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../include/iui.h"
#include "../ports/port.h"

/* Canvas resolution — 256x256 gives near-display-native detail at ~400px
 * output and avoids visible pixel grid artifacts. */
#define FS_CANVAS_W 256
#define FS_CANVAS_H 256

/* Interpreter limits */
#define FS_MAX_CODE 2048
#define FS_MAX_WORDS 64
#define FS_MAX_WORD_NAME 32
#define FS_D_STACK_SIZE 64
#define FS_R_STACK_SIZE 32
#define FS_MEMORY_SLOTS 16

/* Bytecode instruction: either an opcode or a double immediate */
typedef union {
    int op;
    double num;
    int target;
} fs_inst_t;

/* Compiled Forth program */
typedef struct {
    fs_inst_t code[FS_MAX_CODE];
    int code_len;
    int canvas_w, canvas_h;
} fs_program_t;

/* Per-window state — embed in demo_state_t */
typedef struct {
    fs_program_t program;
    uint32_t canvas[FS_CANVAS_H][FS_CANVAS_W]; /* ARGB pixel buffer */
    double time;
    int tab; /* active example index */
    bool compiled;
} forthsalon_state_t;

/* Initialize state and compile the first example. */
void forthsalon_init(forthsalon_state_t *fs);

/* Draw the Forth Salon demo window. */
void draw_forthsalon_window(iui_context *ui,
                            iui_port_ctx *port,
                            forthsalon_state_t *fs,
                            float dt,
                            float win_h);
