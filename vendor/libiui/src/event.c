/* Input event handling */

#include "internal.h"

/* Mouse position update */
void iui_update_mouse_pos(iui_context *ctx, float x, float y)
{
    if (!ctx)
        return;
    ctx->mouse_pos = (iui_vec2) {x, y};
}

/* Multi-button mouse update */
void iui_update_mouse_buttons(
    iui_context *ctx,
    uint8_t pressed,  /* buttons pressed this frame */
    uint8_t released) /* buttons released this frame */
{
    if (!ctx)
        return;
    ctx->mouse_pressed = pressed;
    ctx->mouse_released = released;
    /* Update held buttons: add newly pressed, remove newly released */
    ctx->mouse_held |= pressed;
    ctx->mouse_held &= ~released;

    /* Auto-release input capture when left mouse button is released */
    if ((released & IUI_MOUSE_LEFT) && ctx->input_capture.active)
        iui_release_capture(ctx);
}

/* Keyboard key update (navigation keys) */
void iui_update_key(iui_context *ctx, int key)
{
    if (!ctx)
        return;
    ctx->key_pressed = key;
}

/* Text character input */
void iui_update_char(iui_context *ctx, int codepoint)
{
    if (!ctx)
        return;
    ctx->char_input = codepoint;
}

/* Modifier keys update (Ctrl/Shift/Alt) */
void iui_update_modifiers(iui_context *ctx, uint8_t modifiers)
{
    if (!ctx)
        return;
    ctx->modifiers = modifiers;
}

/* Scroll wheel update */
void iui_update_scroll(iui_context *ctx, float dx, float dy)
{
    if (!ctx)
        return;
    /* Accumulate scroll delta for this frame
     * Will be applied in iui_scroll_begin if mouse is over viewport
     */
    ctx->scroll_wheel_dx += dx;
    ctx->scroll_wheel_dy += dy;
}

/* Frame start: clear per-frame input state
 * Called by iui_begin_frame() in layout.c
 */
void iui_input_frame_begin(iui_context *ctx)
{
    if (!ctx)
        return;
    ctx->mouse_pressed = 0;
    ctx->mouse_released = 0;
    ctx->key_pressed = IUI_KEY_NONE;
    ctx->char_input = 0;
    /* Note: scroll_wheel_dx/dy cleared after scroll regions process them */
}
