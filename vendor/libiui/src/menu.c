/* Menu component implementation */

#include "internal.h"

/* Menu system */

void iui_menu_open(iui_menu_state *menu, const char *id, float x, float y)
{
    if (!menu || !id)
        return;

    menu->open = true;
    menu->x = x;
    menu->y = y;
    menu->id = iui_hash_str(id);
    menu->hovered_index = -1;
    menu->frames_since_open = 0;
    menu->width = 0.f; /* Will be computed in iui_menu_begin */
    menu->height = 0.f;
}

void iui_menu_close(iui_menu_state *menu)
{
    if (!menu)
        return;

    menu->open = false;
    menu->hovered_index = -1;
    menu->frames_since_open = 0;
}

bool iui_menu_is_open(const iui_menu_state *menu)
{
    return menu && menu->open;
}

bool iui_menu_begin(iui_context *ctx,
                    iui_menu_state *menu,
                    const iui_menu_options *options)
{
    if (!ctx || !menu || !menu->open)
        return false;

    /* Reset hover state each frame (updated by iui_menu_add_item) */
    menu->hovered_index = -1;

    /* Note: frame counter is incremented in iui_menu_end() AFTER the
     * click protection check, ensuring the first frame after opening
     * (frames_since_open == 0) blocks clicks.
     */

    /* Get menu dimensions from options or use defaults */
    float min_width = (options && options->min_width > 0.f)
                          ? options->min_width
                          : IUI_MENU_MIN_WIDTH,
          max_width = (options && options->max_width > 0.f)
                          ? options->max_width
                          : IUI_MENU_MAX_WIDTH;

    /* Use min_width as the starting width */
    if (menu->width < min_width)
        menu->width = min_width;
    if (menu->width > max_width)
        menu->width = max_width;

    /* Use stored height from previous frame, or estimate if first frame */
    float bg_height = (ctx->menu_prev_height > 0.f)
                          ? ctx->menu_prev_height
                          : (IUI_MENU_PADDING_V * 2.f +
                             IUI_MENU_ITEM_HEIGHT * 4); /* Default ~4 items */

    /* MD3 corner radius for menus */
    float corner = 4.f;

    /* Draw shadow first (elevation level 3 for menus per MD3) */
    iui_rect_t bg_rect = {
        .x = menu->x,
        .y = menu->y,
        .width = menu->width,
        .height = bg_height,
    };
    iui_draw_shadow(ctx, bg_rect, corner, IUI_ELEVATION_3);

    /* Draw menu background */
    iui_emit_box(ctx, bg_rect, corner, ctx->colors.surface_container);

    /* Track component for MD3 validation */
    IUI_MD3_TRACK_MENU(bg_rect, corner);

    /* Reset height counter for this frame (will be accumulated by items) */
    menu->height = IUI_MENU_PADDING_V;

    /* Reset item index counter for hover tracking */
    ctx->menu_item_index = 0;

    /* Enable modal blocking for click-outside-to-close.
     * This blocks input to widgets rendered before the menu.
     */
    iui_begin_modal(ctx, "menu_modal");

    /* Register blocking region for input layer system */
    iui_register_blocking_region(ctx, bg_rect);

    return true;
}

bool iui_menu_add_item(iui_context *ctx,
                       iui_menu_state *menu,
                       const iui_menu_item *item)
{
    if (!ctx || !menu || !menu->open || !item)
        return false;

    bool clicked = false;

    /* Handle divider */
    if (item->is_divider) {
        float div_y = menu->y + menu->height, div_h = IUI_MENU_DIVIDER_HEIGHT;
        float line_y = div_y + div_h * 0.5f;
        iui_emit_box(ctx,
                     (iui_rect_t) {menu->x + IUI_MENU_PADDING_H, line_y - 0.5f,
                                   menu->width - IUI_MENU_PADDING_H * 2.f, 1.f},
                     0.f, ctx->colors.outline_variant);

        menu->height += div_h;
        return false; /* Dividers are not clickable */
    }

    /* Handle gap (spacing without line) */
    if (item->is_gap) {
        menu->height += IUI_MENU_GAP_HEIGHT;
        return false; /* Gaps are not clickable */
    }

    /* Regular menu item */
    float item_y = menu->y + menu->height, item_h = IUI_MENU_ITEM_HEIGHT;

    iui_rect_t item_rect = {
        .x = menu->x,
        .y = item_y,
        .width = menu->width,
        .height = item_h,
    };

    /* Check interaction state */
    iui_state_t state = IUI_STATE_DEFAULT;
    if (!item->disabled) {
        state = iui_get_component_state(ctx, item_rect, false);
    } else {
        state = IUI_STATE_DISABLED;
    }

    /* Update hovered_index for keyboard navigation */
    if (iui_state_is_interactive(state))
        menu->hovered_index = ctx->menu_item_index;

    /* Draw hover/press state layer */
    iui_draw_state_layer(ctx, item_rect, 0.f, ctx->colors.on_surface, state);

    /* Calculate text color based on state */
    uint32_t text_color =
        item->disabled
            ? iui_state_layer(ctx->colors.on_surface, IUI_STATE_DISABLE_ALPHA)
            : ctx->colors.on_surface;

    /* Layout positions */
    float content_x = menu->x + IUI_MENU_PADDING_H,
          content_y = item_y + (item_h - ctx->font_height) * 0.5f;

    /* Draw leading icon if present */
    if (item->leading_icon) {
        float icon_x = content_x + IUI_MENU_ICON_SIZE * 0.5f,
              icon_y = item_y + item_h * 0.5f;
        iui_draw_fab_icon(ctx, icon_x, icon_y, IUI_MENU_ICON_SIZE,
                          item->leading_icon, text_color);
        content_x += IUI_MENU_ICON_SIZE + IUI_MENU_PADDING_H * 0.5f;
    }

    /* Draw text */
    if (item->text)
        iui_internal_draw_text(ctx, content_x, content_y, item->text,
                               text_color);

    /* Draw trailing text (shortcuts) if present */
    if (item->trailing_text) {
        float trailing_width = iui_get_text_width(ctx, item->trailing_text);
        float trailing_x =
            menu->x + menu->width - IUI_MENU_PADDING_H - trailing_width;
        if (item->trailing_icon)
            trailing_x -= IUI_MENU_ICON_SIZE + IUI_MENU_PADDING_H * 0.5f;
        /* Use muted color for shortcut text */
        uint32_t shortcut_color =
            item->disabled ? iui_state_layer(ctx->colors.on_surface_variant,
                                             IUI_STATE_DISABLE_ALPHA)
                           : ctx->colors.on_surface_variant;
        iui_internal_draw_text(ctx, trailing_x, content_y, item->trailing_text,
                               shortcut_color);
    }

    /* Draw trailing icon if present */
    if (item->trailing_icon) {
        float icon_x = menu->x + menu->width - IUI_MENU_PADDING_H -
                       IUI_MENU_ICON_SIZE * 0.5f;
        float icon_y = item_y + item_h * 0.5f;
        iui_draw_fab_icon(ctx, icon_x, icon_y, IUI_MENU_ICON_SIZE,
                          item->trailing_icon, text_color);
    }

    /* Handle click */
    if (state == IUI_STATE_PRESSED)
        clicked = true;

    menu->height += item_h;
    ctx->menu_item_index++; /* Increment for next item */
    return clicked;
}

void iui_menu_end(iui_context *ctx, iui_menu_state *menu)
{
    if (!ctx || !menu || !menu->open)
        return;

    /* Add bottom padding to final height */
    menu->height += IUI_MENU_PADDING_V;

    /* Store height for next frame's background drawing */
    ctx->menu_prev_height = menu->height;

    /* Menu bounds for hit testing */
    iui_rect_t menu_rect = {
        .x = menu->x,
        .y = menu->y,
        .width = menu->width,
        .height = menu->height,
    };

    /* End modal blocking */
    iui_end_modal(ctx);

    /* Check for outside-click-to-close.
     * Use mouse PRESS (not release) to avoid closing from the button release
     * that opened the menu.
     * Check BEFORE incrementing to ensure first frame (value 0) is protected.
     */
    if (menu->frames_since_open >= 1) {
        bool mouse_in_menu = in_rect(&menu_rect, ctx->mouse_pos);
        bool mouse_pressed = (ctx->mouse_pressed & IUI_MOUSE_LEFT);

        /* Close on NEW click outside menu bounds */
        if (!mouse_in_menu && mouse_pressed) {
            iui_menu_close(menu);
            iui_close_modal(ctx);
        }
    }

    /* Increment frame counter AFTER protection check */
    menu->frames_since_open++;
}
