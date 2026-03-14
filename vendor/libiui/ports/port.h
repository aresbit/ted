/*
 * ports/port.h - Backend Abstraction Layer for libiui
 *
 * This header defines the interface that platform backends must implement.
 *
 * Architecture:
 *   - Backends implement iui_port_t function pointers
 *   - Global g_iui_port symbol selected at compile-time
 *   - HiDPI scaling handled transparently by backends
 *   - No runtime dispatch overhead
 */

#ifndef IUI_PORT_H
#define IUI_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "iui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared constants for port implementations
 * These eliminate duplication across sdl2.c, wasm.c, etc.
 */
#ifndef IUI_PORT_PI
#define IUI_PORT_PI 3.14159265358979323846
#endif

#ifndef IUI_PORT_MAX_PATH_POINTS
#define IUI_PORT_MAX_PATH_POINTS 256
#endif

/* Calculate adaptive segment count for Bezier curves based on Manhattan
 * distance */
#define IUI_BEZIER_SEGMENTS(p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y)    \
    ((int) fminf(fmaxf((fabsf((p1x) - (p0x)) + fabsf((p1y) - (p0y)) +  \
                        fabsf((p2x) - (p1x)) + fabsf((p2y) - (p1y)) +  \
                        fabsf((p3x) - (p2x)) + fabsf((p3y) - (p2y))) * \
                           0.15f,                                      \
                       4.f),                                           \
                 12.f))

/* Calculate arc segment count based on radius and arc angle (clamped to [8,
 * 128]) */
#define IUI_ARC_SEGMENTS(radius, arc_angle)                      \
    ((int) fmaxf(8.f, fminf(128.f, (radius) * fabsf(arc_angle) / \
                                       (float) IUI_PORT_PI * 16.f)))

/* Circle segment count based on radius (clamped to [16, 128]) */
#define IUI_CIRCLE_SEGMENTS(radius) \
    ((int) fmaxf(16.f, fminf(128.f, (radius) * 2.f)))

/* No-op callback generator macros for headless/testing ports
 * Usage: IUI_PORT_NOOP_1(func_name, type1, arg1)
 *        IUI_PORT_NOOP_2(func_name, type1, arg1, type2, arg2)
 *        etc.
 */
#define IUI_PORT_NOOP_1(name, t1, a1)   \
    static void name(t1 a1, void *user) \
    {                                   \
        (void) a1;                      \
        (void) user;                    \
    }

#define IUI_PORT_NOOP_2(name, t1, a1, t2, a2)  \
    static void name(t1 a1, t2 a2, void *user) \
    {                                          \
        (void) a1;                             \
        (void) a2;                             \
        (void) user;                           \
    }

#define IUI_PORT_NOOP_3(name, t1, a1, t2, a2, t3, a3) \
    static void name(t1 a1, t2 a2, t3 a3, void *user) \
    {                                                 \
        (void) a1;                                    \
        (void) a2;                                    \
        (void) a3;                                    \
        (void) user;                                  \
    }

#define IUI_PORT_NOOP_4(name, t1, a1, t2, a2, t3, a3, t4, a4) \
    static void name(t1 a1, t2 a2, t3 a3, t4 a4, void *user)  \
    {                                                         \
        (void) a1;                                            \
        (void) a2;                                            \
        (void) a3;                                            \
        (void) a4;                                            \
        (void) user;                                          \
    }

#define IUI_PORT_NOOP_5(name, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5) \
    static void name(t1 a1, t2 a2, t3 a3, t4 a4, t5 a5, void *user)   \
    {                                                                 \
        (void) a1;                                                    \
        (void) a2;                                                    \
        (void) a3;                                                    \
        (void) a4;                                                    \
        (void) a5;                                                    \
        (void) user;                                                  \
    }

#define IUI_PORT_NOOP_6(name, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6) \
    static void name(t1 a1, t2 a2, t3 a3, t4 a4, t5 a5, t6 a6, void *user)    \
    {                                                                         \
        (void) a1;                                                            \
        (void) a2;                                                            \
        (void) a3;                                                            \
        (void) a4;                                                            \
        (void) a5;                                                            \
        (void) a6;                                                            \
        (void) user;                                                          \
    }

#define IUI_PORT_NOOP_7(name, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6, \
                        t7, a7)                                               \
    static void name(t1 a1, t2 a2, t3 a3, t4 a4, t5 a5, t6 a6, t7 a7,         \
                     void *user)                                              \
    {                                                                         \
        (void) a1;                                                            \
        (void) a2;                                                            \
        (void) a3;                                                            \
        (void) a4;                                                            \
        (void) a5;                                                            \
        (void) a6;                                                            \
        (void) a7;                                                            \
        (void) user;                                                          \
    }

/* Default frame timing constant (~60fps)
 * Used by headless and WASM ports for consistent timing behavior
 */
#ifndef IUI_PORT_FRAME_DT
#define IUI_PORT_FRAME_DT 0.016f
#endif

/* Forward declaration for opaque port context */
typedef struct iui_port_ctx iui_port_ctx;

/* Input event structure - backends queue these for the application to process.
 * This decouples event polling from input application.
 */
typedef struct {
    float mouse_x, mouse_y;
    uint8_t mouse_pressed;    /* bitfield of iui_mouse_button_t */
    uint8_t mouse_released;   /* bitfield of iui_mouse_button_t */
    iui_key_code_t key;       /* Key code or IUI_KEY_NONE (0) */
    uint32_t text;            /* Unicode codepoint for text input or 0 */
    float scroll_x, scroll_y; /* Horizontal/Vertical scroll delta */
    bool shift_down;          /* For Tab navigation */
} iui_port_input;

/* Port lifecycle helper: consume queued input and clear per-frame fields.
 * Call in get_input() to copy input to destination and reset source.
 */
static inline void iui_port_consume_input(iui_port_input *dst,
                                          iui_port_input *src)
{
    *dst = *src;
    /* Clear per-frame fields, preserve mouse position */
    src->mouse_pressed = 0;
    src->mouse_released = 0;
    src->key = IUI_KEY_NONE;
    src->text = 0;
    src->scroll_x = 0.f;
    src->scroll_y = 0.f;
}

/* Port lifecycle helper: request exit from application.
 * Sets both running and exit_requested flags consistently.
 */
static inline void iui_port_request_exit(bool *running, bool *exit_requested)
{
    *running = false;
    *exit_requested = true;
}

/* Backend interface - function pointers for platform abstraction.
 *
 * All coordinates are in logical (window) units. Backends handle HiDPI
 * scaling internally and transparently.
 */
typedef struct {
    /* Initialize the backend and create a window.
     * Returns opaque context or NULL on failure.
     */
    iui_port_ctx *(*init)(int width, int height, const char *title);

    /* Shutdown the backend and release all resources. */
    void (*shutdown)(iui_port_ctx *ctx);

    /* Perform platform-specific configuration after init.
     * Called once after init() to set up DPI scaling, color format, etc.
     */
    void (*configure)(iui_port_ctx *ctx);

    /* Poll and process platform events.
     * Returns false when the application should exit (window closed).
     * Queues input events internally for retrieval via get_input().
     */
    bool (*poll_events)(iui_port_ctx *ctx);

    /* Check if exit was requested (window close, quit event) */
    bool (*should_exit)(iui_port_ctx *ctx);

    /* Signal the backend to shut down (from application code) */
    void (*request_exit)(iui_port_ctx *ctx);

    /* Get queued input state since last poll_events().
     * - Mouse position: latest value
     * - Mouse buttons: bitfields of presses/releases this frame
     * - Key/text: first event (to avoid losing fast input)
     * - Scroll: accumulated deltas
     */
    void (*get_input)(iui_port_ctx *ctx, iui_port_input *input);

    /* Clear screen and prepare for a new frame */
    void (*begin_frame)(iui_port_ctx *ctx);

    /* Present the rendered frame to the display */
    void (*end_frame)(iui_port_ctx *ctx);

    /* Get renderer callbacks for iui_def initialization.
     * These callbacks handle HiDPI scaling internally.
     */
    iui_renderer_t (*get_renderer_callbacks)(iui_port_ctx *ctx);

    /* Get vector font callbacks for iui_def initialization.
     * Returns NULL if vector font is not supported.
     */
    const iui_vector_t *(*get_vector_callbacks)(iui_port_ctx *ctx);

    /* Get time elapsed since last frame in seconds */
    float (*get_delta_time)(iui_port_ctx *ctx);

    /* Get window dimensions in logical (pre-DPI-scaled) units */
    void (*get_window_size)(iui_port_ctx *ctx, int *width, int *height);

    /* Set window dimensions in logical units */
    void (*set_window_size)(iui_port_ctx *ctx, int width, int height);

    /* Get HiDPI scale factor (physical pixels / logical pixels).
     * Returns 1.0 on non-HiDPI displays, 2.0 on Retina, etc.
     */
    float (*get_dpi_scale)(iui_port_ctx *ctx);

    /* Check if window currently has keyboard focus */
    bool (*is_window_focused)(iui_port_ctx *ctx);

    /* Check if window is visible (not minimized) */
    bool (*is_window_visible)(iui_port_ctx *ctx);

    /* Get clipboard text (platform-specific).
     * Returns NULL if clipboard is empty or unavailable.
     * Returned string is valid until next clipboard operation.
     */
    const char *(*get_clipboard_text)(iui_port_ctx *ctx);

    /* Set clipboard text (platform-specific) */
    void (*set_clipboard_text)(iui_port_ctx *ctx, const char *text);

    /* Get the backend's SDL_Renderer (or equivalent) for direct drawing.
     * Used by demos that need raw rendering access (nyancat, etc.).
     * Returns NULL if not applicable to the backend.
     */
    void *(*get_native_renderer)(iui_port_ctx *ctx);
} iui_port_t;

/* Global backend instance - selected at compile-time.
 *
 * Each backend (sdl2.c, wasm.c, etc.) defines this symbol.
 * Only one backend is linked into each build.
 */
extern const iui_port_t g_iui_port;

/* Helper: Apply queued input to an iui_context.
 * This is a convenience function that can be called by applications
 * after poll_events() to update the UI context.
 */
static inline void iui_port_apply_input(iui_context *ui,
                                        const iui_port_input *input)
{
    if (!ui || !input)
        return;

    iui_update_mouse_pos(ui, input->mouse_x, input->mouse_y);

    iui_update_mouse_buttons(ui, input->mouse_pressed, input->mouse_released);

    if (input->key != IUI_KEY_NONE) {
        /* Handle focus navigation (Tab/Shift+Tab) at port layer */
        if (input->key == IUI_KEY_TAB) {
            if (input->shift_down) {
                iui_focus_prev(ui);
            } else {
                iui_focus_next(ui);
            }
        } else if (input->key == IUI_KEY_ESCAPE) {
            iui_clear_focus(ui);
        } else {
            iui_update_key(ui, input->key);
        }
    }

    if (input->text != 0)
        iui_update_char(ui, (int) input->text);

    if (input->scroll_x != 0.0f || input->scroll_y != 0.0f)
        iui_update_scroll(ui, input->scroll_x, input->scroll_y);
}

#ifdef __cplusplus
}
#endif

#endif /* IUI_PORT_H */
