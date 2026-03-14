/* Top App Bar component implementation */

#include "internal.h"

/* Top App Bar
 * Heights per variant (MD3 spec):
 * - Small/Center: 64dp fixed
 * - Medium: 112dp expanded -> 64dp collapsed
 * - Large: 152dp expanded -> 64dp collapsed
 *
 * Layout:
 * [16dp] [nav_icon 48dp] [16dp] [title] ... [action 48dp]* [16dp]
 */

bool iui_top_app_bar(iui_context *ctx,
                     const char *title,
                     iui_appbar_size_t size,
                     float scroll_offset)
{
    if (!ctx || !ctx->current_window)
        return false;

    const iui_theme_t *theme = &ctx->colors;
    bool nav_clicked = false;

    /* Calculate bar height based on size and scroll */
    float expanded_height, collapsed_height;
    switch (size) {
    case IUI_APPBAR_MEDIUM:
        expanded_height = IUI_APPBAR_MEDIUM_HEIGHT;
        collapsed_height = IUI_APPBAR_COLLAPSED_HEIGHT;
        break;
    case IUI_APPBAR_LARGE:
        expanded_height = IUI_APPBAR_LARGE_HEIGHT;
        collapsed_height = IUI_APPBAR_COLLAPSED_HEIGHT;
        break;
    case IUI_APPBAR_SMALL:
    case IUI_APPBAR_CENTER:
    default:
        expanded_height = IUI_APPBAR_SMALL_HEIGHT;
        collapsed_height = IUI_APPBAR_SMALL_HEIGHT;
        break;
    }

    /* Calculate collapse progress (0 = expanded, 1 = collapsed) */
    float collapse_range = expanded_height - collapsed_height;
    float collapse_progress = 0.f;
    if (collapse_range > 0.f && scroll_offset > 0.f) {
        collapse_progress = scroll_offset / collapse_range;
        if (collapse_progress > 1.f)
            collapse_progress = 1.f;
    }

    /* Current height interpolated between expanded and collapsed */
    float bar_height = expanded_height - (collapse_progress * collapse_range);

    /* Bar position - app bar spans full window width (edge-to-edge per MD3) */
    float bar_x = ctx->current_window->pos.x, bar_y = ctx->layout.y;
    float bar_width = ctx->current_window->width;

    /* Color transition: surface -> surface_container on scroll */
    uint32_t bg_color;
    if (collapse_progress > 0.f) {
        /* Interpolate colors based on collapse progress */
        bg_color = lerp_color(theme->surface, theme->surface_container,
                              collapse_progress);
    } else {
        bg_color = theme->surface;
    }

    /* Draw background (no corner radius for app bar) */
    iui_rect_t bar_rect = {bar_x, bar_y, bar_width, bar_height};
    iui_emit_box(ctx, bar_rect, 0.f, bg_color);

    /* Draw elevation shadow if scrolled (Level 2) */
    if (collapse_progress > 0.f) {
        iui_rect_t shadow_rect = {bar_x, bar_y, bar_width, bar_height};
        /* Only draw shadow after scroll threshold to avoid flicker at start.
         * This threshold ensures shadow appears smoothly as user scrolls.
         */
        if (collapse_progress > IUI_APPBAR_SHADOW_THRESHOLD)
            iui_draw_shadow(ctx, shadow_rect, 0.f, IUI_ELEVATION_2);
    }

    /* Navigation icon (leading) */
    float nav_x = bar_x + IUI_APPBAR_PADDING_H,
          nav_y = bar_y + (bar_height - IUI_APPBAR_ICON_BUTTON_SIZE) * 0.5f;

    /* For Medium/Large when expanded, nav icon stays at top */
    if ((size == IUI_APPBAR_MEDIUM || size == IUI_APPBAR_LARGE) &&
        collapse_progress < 1.f) {
        nav_y =
            bar_y +
            (IUI_APPBAR_COLLAPSED_HEIGHT - IUI_APPBAR_ICON_BUTTON_SIZE) * 0.5f;
    }

    uint32_t icon_color = theme->on_surface_variant;

    /* Draw navigation icon with hover/press state */
    iui_rect_t nav_rect = {nav_x, nav_y, IUI_APPBAR_ICON_BUTTON_SIZE,
                           IUI_APPBAR_ICON_BUTTON_SIZE};
    /* Check in_rect first (cheap) then input blocking (respects modals) */
    bool nav_hovered = in_rect(&nav_rect, ctx->mouse_pos) &&
                       iui_should_process_input(ctx, nav_rect);
    bool nav_pressed = nav_hovered && (ctx->mouse_held & IUI_MOUSE_LEFT);

    /* State layer on nav icon */
    if (nav_pressed) {
        uint32_t state_color =
            iui_state_layer(icon_color, IUI_STATE_PRESS_ALPHA);
        iui_emit_box(ctx, nav_rect, IUI_APPBAR_ICON_BUTTON_SIZE * 0.5f,
                     state_color);
    } else if (nav_hovered) {
        uint32_t state_color =
            iui_state_layer(icon_color, IUI_STATE_HOVER_ALPHA);
        iui_emit_box(ctx, nav_rect, IUI_APPBAR_ICON_BUTTON_SIZE * 0.5f,
                     state_color);
    }

    /* Draw menu icon */
    float icon_cx = nav_x + IUI_APPBAR_ICON_BUTTON_SIZE * 0.5f;
    float icon_cy = nav_y + IUI_APPBAR_ICON_BUTTON_SIZE * 0.5f;
    iui_draw_fab_icon(ctx, icon_cx, icon_cy, IUI_APPBAR_ICON_SIZE, "menu",
                      icon_color);

    /* Check for nav click */
    if (nav_hovered && (ctx->mouse_released & IUI_MOUSE_LEFT))
        nav_clicked = true;

    /* Title positioning */
    float title_x, title_y;

    /* Default title_x: inline with nav icon (for Small variant) */
    float inline_title_x =
        nav_x + IUI_APPBAR_ICON_BUTTON_SIZE + IUI_APPBAR_TITLE_MARGIN;

    /* Title typography and position based on variant */
    if (size == IUI_APPBAR_CENTER) {
        /* Center-aligned: title centered horizontally */
        float text_w = 0.f;
        if (title) {
            text_w = ctx->renderer.text_width
                         ? ctx->renderer.text_width(title, ctx->renderer.user)
                         : (float) strlen(title) * ctx->font_height * 0.5f;
        }
        title_x = bar_x + (bar_width - text_w) * 0.5f,
        title_y = bar_y + (bar_height - ctx->font_height) * 0.5f;
    } else if (size == IUI_APPBAR_MEDIUM || size == IUI_APPBAR_LARGE) {
        /* Medium/Large: title at bottom-left when expanded, inline when
         * collapsed. MD3 spec for Medium/Large top app bar:
         * - Expanded: title at bottom-left, aligned with content area
         * - Title margin 16dp from bar bottom
         * (m3_appbar_expanded_title_margin_bottom)
         * - Collapses to inline position on scroll
         */

        /* MD3: m3_appbar_expanded_title_margin_bottom = 16dp */
        float title_bottom_margin = IUI_APPBAR_TITLE_MARGIN_BOTTOM;

        /* Expanded: title with 16dp padding from window edge (aligned with nav
         * icon)
         */
        float expanded_title_x = bar_x + IUI_APPBAR_PADDING_H;
        float expanded_title_y =
            bar_y + bar_height - title_bottom_margin - ctx->font_height;

        /* Collapsed: title inline with nav icon, vertically centered */
        float collapsed_title_x = inline_title_x;
        float collapsed_title_y =
            bar_y + (IUI_APPBAR_COLLAPSED_HEIGHT - ctx->font_height) * 0.5f;

        /* Interpolate both x and y based on collapse progress */
        title_x = expanded_title_x +
                  collapse_progress * (collapsed_title_x - expanded_title_x);
        title_y = expanded_title_y +
                  collapse_progress * (collapsed_title_y - expanded_title_y);
    } else {
        /* Small: title inline with nav icon, vertically centered */
        title_x = inline_title_x;
        title_y = bar_y + (bar_height - ctx->font_height) * 0.5f;
    }

    /* Draw title */
    if (title)
        iui_internal_draw_text(ctx, title_x, title_y, title, theme->on_surface);

    /* Set up app bar state for action icons */
    ctx->appbar_active = true;
    ctx->appbar.bar_y = bar_y;
    ctx->appbar.bar_height = bar_height;
    ctx->appbar.action_count = 0;
    ctx->appbar.icon_color = icon_color;
    /* Actions start from right edge minus padding */
    ctx->appbar.action_x =
        bar_x + bar_width - IUI_APPBAR_PADDING_H - IUI_APPBAR_ICON_BUTTON_SIZE;

    /* Advance layout cursor */
    ctx->layout.y += bar_height;

    return nav_clicked;
}

bool iui_top_app_bar_action(iui_context *ctx, const char *icon)
{
    if (!ctx || !ctx->appbar_active || !icon)
        return false;

    /* Limit to max actions */
    if (ctx->appbar.action_count >= IUI_APPBAR_MAX_ACTIONS)
        return false;

    bool clicked = false;

    /* Action icon position */
    float action_x = ctx->appbar.action_x;
    float action_y =
        ctx->appbar.bar_y +
        (ctx->appbar.bar_height - IUI_APPBAR_ICON_BUTTON_SIZE) * 0.5f;

    /* For consistency with nav icon when bar is tall, keep actions at top */
    if (ctx->appbar.bar_height > IUI_APPBAR_COLLAPSED_HEIGHT) {
        action_y =
            ctx->appbar.bar_y +
            (IUI_APPBAR_COLLAPSED_HEIGHT - IUI_APPBAR_ICON_BUTTON_SIZE) * 0.5f;
    }

    iui_rect_t action_rect = {action_x, action_y, IUI_APPBAR_ICON_BUTTON_SIZE,
                              IUI_APPBAR_ICON_BUTTON_SIZE};
    /* Check in_rect first (cheap) then input blocking (respects modals) */
    bool hovered = in_rect(&action_rect, ctx->mouse_pos) &&
                   iui_should_process_input(ctx, action_rect);
    bool pressed = hovered && (ctx->mouse_held & IUI_MOUSE_LEFT);

    uint32_t icon_color = ctx->appbar.icon_color;

    /* State layer */
    if (pressed) {
        uint32_t state_color =
            iui_state_layer(icon_color, IUI_STATE_PRESS_ALPHA);
        iui_emit_box(ctx, action_rect, IUI_APPBAR_ICON_BUTTON_SIZE * 0.5f,
                     state_color);
    } else if (hovered) {
        uint32_t state_color =
            iui_state_layer(icon_color, IUI_STATE_HOVER_ALPHA);
        iui_emit_box(ctx, action_rect, IUI_APPBAR_ICON_BUTTON_SIZE * 0.5f,
                     state_color);
    }

    /* Draw action icon */
    float icon_cx = action_x + IUI_APPBAR_ICON_BUTTON_SIZE * 0.5f;
    float icon_cy = action_y + IUI_APPBAR_ICON_BUTTON_SIZE * 0.5f;
    iui_draw_fab_icon(ctx, icon_cx, icon_cy, IUI_APPBAR_ICON_SIZE, icon,
                      icon_color);

    /* Check for click */
    if (hovered && (ctx->mouse_released & IUI_MOUSE_LEFT))
        clicked = true;

    /* Move next action position to the left */
    ctx->appbar.action_x -= IUI_APPBAR_ICON_BUTTON_SIZE + IUI_APPBAR_ACTION_GAP;
    ctx->appbar.action_count++;

    return clicked;
}
