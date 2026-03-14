#include "internal.h"

#include <float.h>

/* Ink-bounds tracking */

void iui_ink_bounds_init(iui_context *ctx)
{
    if (!ctx)
        return;
    ctx->ink_bounds.min_x = FLT_MAX;
    ctx->ink_bounds.min_y = FLT_MAX;
    ctx->ink_bounds.max_x = -FLT_MAX;
    ctx->ink_bounds.max_y = -FLT_MAX;
    ctx->ink_bounds.valid = false;
    ctx->ink_bounds.enabled = false;
}

void iui_ink_bounds_reset(iui_context *ctx)
{
    if (!ctx)
        return;
    ctx->ink_bounds.min_x = FLT_MAX;
    ctx->ink_bounds.min_y = FLT_MAX;
    ctx->ink_bounds.max_x = -FLT_MAX;
    ctx->ink_bounds.max_y = -FLT_MAX;
    ctx->ink_bounds.valid = false;
}

void iui_ink_bounds_enable(iui_context *ctx, bool enable)
{
    if (!ctx)
        return;
    ctx->ink_bounds.enabled = enable;
    if (enable)
        iui_ink_bounds_reset(ctx);
}

bool iui_ink_bounds_get(const iui_context *ctx, iui_rect_t *out)
{
    if (!ctx || !ctx->ink_bounds.valid || !out)
        return false;
    out->x = ctx->ink_bounds.min_x;
    out->y = ctx->ink_bounds.min_y;
    out->width = ctx->ink_bounds.max_x - ctx->ink_bounds.min_x;
    out->height = ctx->ink_bounds.max_y - ctx->ink_bounds.min_y;
    return true;
}

bool iui_ink_bounds_valid(const iui_context *ctx)
{
    return ctx && ctx->ink_bounds.valid;
}

/* Draw call batch add functions */

bool iui_batch_add_rect(iui_context *ctx,
                        float x,
                        float y,
                        float w,
                        float h,
                        float radius,
                        uint32_t color)
{
    if (!ctx || !ctx->batch.enabled)
        return false;
    if (ctx->batch.count >= IUI_DRAW_CMD_BUFFER_SIZE) {
        /* Buffer full: flush and retry */
        iui_batch_flush(ctx);
    }
    if (ctx->batch.count >= IUI_DRAW_CMD_BUFFER_SIZE)
        return false;

    iui_draw_cmd *cmd = &ctx->batch.commands[ctx->batch.count++];
    cmd->type = IUI_CMD_RECT;
    cmd->data.rect.x = x;
    cmd->data.rect.y = y;
    cmd->data.rect.w = w;
    cmd->data.rect.h = h;
    cmd->data.rect.radius = radius;
    cmd->color = color;
    cmd->clip = ctx->current_clip;
    return true;
}

bool iui_batch_add_text(iui_context *ctx,
                        float x,
                        float y,
                        const char *text,
                        uint32_t color)
{
    if (!ctx || !ctx->batch.enabled || !text)
        return false;
    if (ctx->batch.count >= IUI_DRAW_CMD_BUFFER_SIZE)
        iui_batch_flush(ctx);
    if (ctx->batch.count >= IUI_DRAW_CMD_BUFFER_SIZE)
        return false;

    iui_draw_cmd *cmd = &ctx->batch.commands[ctx->batch.count++];
    cmd->type = IUI_CMD_TEXT;
    cmd->data.text.x = x;
    cmd->data.text.y = y;
    /* Truncate text to inline buffer size */
    size_t len = strlen(text);
    if (len >= sizeof(cmd->data.text.text))
        len = sizeof(cmd->data.text.text) - 1;
    memcpy(cmd->data.text.text, text, len);
    cmd->data.text.text[len] = '\0';
    cmd->color = color;
    cmd->clip = ctx->current_clip;
    return true;
}

bool iui_batch_add_line(iui_context *ctx,
                        float x0,
                        float y0,
                        float x1,
                        float y1,
                        float width,
                        uint32_t color)
{
    if (!ctx || !ctx->batch.enabled)
        return false;
    if (ctx->batch.count >= IUI_DRAW_CMD_BUFFER_SIZE)
        iui_batch_flush(ctx);
    if (ctx->batch.count >= IUI_DRAW_CMD_BUFFER_SIZE)
        return false;

    iui_draw_cmd *cmd = &ctx->batch.commands[ctx->batch.count++];
    cmd->type = IUI_CMD_LINE;
    cmd->data.line.x0 = x0;
    cmd->data.line.y0 = y0;
    cmd->data.line.x1 = x1;
    cmd->data.line.y1 = y1;
    cmd->data.line.width = width;
    cmd->color = color;
    cmd->clip = ctx->current_clip;
    return true;
}

bool iui_batch_add_circle(iui_context *ctx,
                          float cx,
                          float cy,
                          float radius,
                          uint32_t fill_color,
                          uint32_t stroke_color,
                          float stroke_width)
{
    if (!ctx || !ctx->batch.enabled)
        return false;
    if (ctx->batch.count >= IUI_DRAW_CMD_BUFFER_SIZE)
        iui_batch_flush(ctx);
    if (ctx->batch.count >= IUI_DRAW_CMD_BUFFER_SIZE)
        return false;

    iui_draw_cmd *cmd = &ctx->batch.commands[ctx->batch.count++];
    cmd->type = IUI_CMD_CIRCLE;
    cmd->data.circle.cx = cx;
    cmd->data.circle.cy = cy;
    cmd->data.circle.radius = radius;
    cmd->data.circle.fill_color = fill_color;
    cmd->data.circle.stroke_width = stroke_width;
    cmd->color = fill_color;
    cmd->color2 = stroke_color;
    cmd->clip = ctx->current_clip;
    return true;
}

bool iui_batch_add_arc(iui_context *ctx,
                       float cx,
                       float cy,
                       float radius,
                       float start_angle,
                       float end_angle,
                       float width,
                       uint32_t color)
{
    if (!ctx || !ctx->batch.enabled)
        return false;
    if (ctx->batch.count >= IUI_DRAW_CMD_BUFFER_SIZE)
        iui_batch_flush(ctx);
    if (ctx->batch.count >= IUI_DRAW_CMD_BUFFER_SIZE)
        return false;

    iui_draw_cmd *cmd = &ctx->batch.commands[ctx->batch.count++];
    cmd->type = IUI_CMD_ARC;
    cmd->data.arc.cx = cx;
    cmd->data.arc.cy = cy;
    cmd->data.arc.radius = radius;
    cmd->data.arc.start_angle = start_angle;
    cmd->data.arc.end_angle = end_angle;
    cmd->data.arc.width = width;
    cmd->color = color;
    cmd->clip = ctx->current_clip;
    return true;
}

/* Text width caching */

void iui_text_cache_init(iui_context *ctx)
{
    if (!ctx)
        return;

    memset(ctx->text_cache.entries, 0, sizeof(ctx->text_cache.entries));
    ctx->text_cache.hits = 0, ctx->text_cache.misses = 0;
    ctx->text_cache.decay_idx = 0, ctx->text_cache.enabled = false;
}

void iui_text_cache_enable(iui_context *ctx, bool enable)
{
    if (!ctx)
        return;
    ctx->text_cache.enabled = enable;
}

void iui_text_cache_clear(iui_context *ctx)
{
    if (!ctx)
        return;
    memset(ctx->text_cache.entries, 0, sizeof(ctx->text_cache.entries));
}

/* Mix font_height bits into text hash to prevent cross-size cache poisoning.
 * Typography paths temporarily mutate ctx->font_height, so same text at
 * different sizes must hash to different slots.
 */
static uint32_t text_cache_hash(const char *text, float font_height)
{
    uint32_t h = iui_hash_str(text);
    uint32_t fh;
    memcpy(&fh, &font_height, sizeof(fh));
    h ^= fh * 2654435761u; /* Knuth multiplicative hash mixing */
    if (h == 0)
        h = 1;
    return h;
}

static bool text_cache_get(iui_context *ctx, const char *text, float *width)
{
    if (!ctx->text_cache.enabled || !text || !width)
        return false;

    uint32_t hash = text_cache_hash(text, ctx->font_height);

    /* Bitwise AND for power-of-2 cache size (faster than modulo) */
    int start = hash & (IUI_TEXT_CACHE_SIZE - 1);
    for (int i = 0; i < IUI_TEXT_CACHE_PROBE_LEN; i++) {
        int idx = (start + i) & (IUI_TEXT_CACHE_SIZE - 1);
        iui_text_cache_entry *entry = &ctx->text_cache.entries[idx];

        if (entry->hash == 0) {
            ctx->text_cache.misses++;
            return false;
        }
        /* Compare hash, pointer, and font size to handle collisions */
        if (entry->hash == hash && entry->text == text &&
            entry->font_height == ctx->font_height) {
            *width = entry->width;
            if (entry->hits < 255)
                entry->hits++;
            ctx->text_cache.hits++;
            return true;
        }
    }
    ctx->text_cache.misses++;
    return false;
}

static void text_cache_put(iui_context *ctx, const char *text, float width)
{
    if (!ctx->text_cache.enabled || !text)
        return;

    uint32_t hash = text_cache_hash(text, ctx->font_height);

    /* Bitwise AND for power-of-2 cache size (faster than modulo) */
    int start = hash & (IUI_TEXT_CACHE_SIZE - 1), evict_idx = -1;
    uint8_t min_hits = 255;

    for (int i = 0; i < IUI_TEXT_CACHE_PROBE_LEN; i++) {
        int idx = (start + i) & (IUI_TEXT_CACHE_SIZE - 1);
        iui_text_cache_entry *entry = &ctx->text_cache.entries[idx];

        /* Empty slot or exact match (same hash, pointer, and font size) */
        if (entry->hash == 0 || (entry->hash == hash && entry->text == text &&
                                 entry->font_height == ctx->font_height)) {
            entry->hash = hash;
            entry->text = text;
            entry->font_height = ctx->font_height;
            entry->width = width;
            entry->hits = 1;
            return;
        }
        if (entry->hits < min_hits) {
            min_hits = entry->hits;
            evict_idx = idx;
        }
    }

    /* Evict least-used entry within probe window */
    if (evict_idx >= 0) {
        iui_text_cache_entry *entry = &ctx->text_cache.entries[evict_idx];
        entry->hash = hash;
        entry->text = text;
        entry->font_height = ctx->font_height;
        entry->width = width;
        entry->hits = 1;
    }
}

void iui_text_cache_stats(const iui_context *ctx, int *hits, int *misses)
{
    if (!ctx)
        return;
    if (hits)
        *hits = ctx->text_cache.hits;
    if (misses)
        *misses = ctx->text_cache.misses;
}

void iui_text_cache_frame_end(iui_context *ctx)
{
    if (!ctx || !ctx->text_cache.enabled)
        return;

    /* Amortized decay - O(k) instead of O(N) */
    for (int i = 0; i < IUI_TEXT_CACHE_DECAY_COUNT; i++) {
        iui_text_cache_entry *entry =
            &ctx->text_cache.entries[ctx->text_cache.decay_idx];
        if (entry->hits > 0)
            entry->hits--;
        ctx->text_cache.decay_idx =
            (ctx->text_cache.decay_idx + 1) & (IUI_TEXT_CACHE_SIZE - 1);
    }
}

/* Text Width and Drawing */
float iui_get_text_width(iui_context *ctx, const char *text)
{
    if (!ctx || !text)
        return 0.f;

    /* Check cache first */
    float cached_width;
    if (text_cache_get(ctx, text, &cached_width))
        return cached_width;

    /* Cache miss - compute width */
    float width;
    if (ctx->renderer.text_width)
        width = ctx->renderer.text_width(text, ctx->renderer.user);
    else
        width = iui_text_width_vec(text, ctx->font_height);

    /* Store in cache */
    text_cache_put(ctx, text, width);
    return width;
}

/* Get width of a Unicode codepoint. Uses renderer callback if available,
 * otherwise falls back to built-in vector font.
 *
 * Note: For custom renderers, this measures single codepoints independently.
 * Fonts with kerning may show slight cursor drift vs rendered text. For the
 * built-in vector font (no kerning), width accumulation is exact.
 */
float iui_get_codepoint_width(iui_context *ctx, uint32_t cp)
{
    /* Clamp invalid codepoints to replacement character */
    if (cp > 0x10FFFF)
        cp = 0xFFFD;

    if (ctx->renderer.text_width) {
        /* Encode codepoint to UTF-8 for renderer callback */
        char tmp[5] = {0};
        if (cp < 0x80) {
            tmp[0] = (char) cp;
        } else if (cp < 0x800) {
            tmp[0] = (char) (0xC0 | (cp >> 6));
            tmp[1] = (char) (0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            tmp[0] = (char) (0xE0 | (cp >> 12));
            tmp[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
            tmp[2] = (char) (0x80 | (cp & 0x3F));
        } else {
            tmp[0] = (char) (0xF0 | (cp >> 18));
            tmp[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
            tmp[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
            tmp[3] = (char) (0x80 | (cp & 0x3F));
        }
        return ctx->renderer.text_width(tmp, ctx->renderer.user);
    }
    return iui_codepoint_width_vec(cp, ctx->font_height);
}

void iui_internal_draw_text(iui_context *ctx,
                            float x,
                            float y,
                            const char *text,
                            uint32_t color)
{
    /* Track text bounding box in ink-bounds: (x, y) to (x + width, y + h) */
    if (ctx->ink_bounds.enabled && text && *text) {
        float tw = iui_get_text_width(ctx, text);
        iui_ink_bounds_extend(ctx, x, y, tw, ctx->font_height);
    }

    /* Route through batch to preserve draw order with boxes.
     * Only batch when the backend has draw_text; vector font rendering
     * goes through iui_emit_box which is already batch-aware.
     * Fall back to direct draw for text exceeding inline buffer (64 bytes)
     * to avoid silent truncation producing different rendering.
     */
    if (ctx->batch.enabled && ctx->renderer.draw_text) {
        if (text && strlen(text) >= 64) {
            iui_batch_flush(ctx);
            ctx->renderer.draw_text(x, y, text, color, ctx->renderer.user);
            return;
        }
        iui_batch_add_text(ctx, x, y, text, color);
        return;
    }

    if (ctx->renderer.draw_text)
        ctx->renderer.draw_text(x, y, text, color, ctx->renderer.user);
    else
        iui_draw_text_vec(ctx, x, y, text, color);
}

void draw_align_text(iui_context *ctx,
                     const iui_rect_t *rect,
                     const char *text,
                     uint32_t srgb_color,
                     iui_text_alignment_t alignment)
{
    if (alignment == IUI_ALIGN_LEFT)
        iui_internal_draw_text(ctx, rect->x, rect->y, text, srgb_color);
    else {
        float text_width = iui_get_text_width(ctx, text);
        if (alignment == IUI_ALIGN_RIGHT)
            iui_internal_draw_text(ctx, rect->x + rect->width - text_width,
                                   rect->y, text, srgb_color);
        else
            iui_internal_draw_text(
                ctx, rect->x + rect->width * .5f - text_width * .5f, rect->y,
                text, srgb_color);
    }
}

/* Text rendering - always available regardless of MODULE_BASIC */

void iui_text(iui_context *ctx,
              iui_text_alignment_t alignment,
              const char *string,
              ...)
{
    if (!ctx->current_window || !string)
        return;

    va_list args;
    va_start(args, string);
    int written =
        vsnprintf(ctx->string_buffer, IUI_STRING_BUFFER_SIZE, string, args);
    if (written >= IUI_STRING_BUFFER_SIZE) {
        /* Ensure null termination if buffer was too small */
        ctx->string_buffer[IUI_STRING_BUFFER_SIZE - 1] = '\0';
    }
    va_end(args);

    /* MD3: Use on_surface for body text */
    draw_align_text(ctx, &ctx->layout, ctx->string_buffer,
                    ctx->colors.on_surface, alignment);
}

/* Helper function to draw text with specific font size */
static void iui_text_with_size(iui_context *ctx,
                               iui_text_alignment_t alignment,
                               const char *string,
                               float font_size)
{
    if (!ctx->current_window)
        return;

    /* Temporarily change font metrics for rendering */
    float original_height = ctx->font_height,
          original_ascent = ctx->font_ascent_px,
          original_descent = ctx->font_descent_px,
          original_width = iui_vector_pen_for_height(original_height);

    ctx->font_height = font_size;
    iui_compute_vector_metrics(ctx->font_height, &ctx->font_ascent_px,
                               &ctx->font_descent_px);
    ctx->pen_width = iui_vector_pen_for_height(ctx->font_height);

    /* Calculate text width with the new font size */
    float text_width = iui_get_text_width(ctx, string);

    iui_rect_t text_rect = ctx->layout;
    text_rect.height = font_size * 1.2f; /* Adjust height based on font size */

    if (alignment == IUI_ALIGN_LEFT)
        iui_internal_draw_text(ctx, text_rect.x, text_rect.y, string,
                               ctx->colors.on_surface);
    else {
        float x_pos =
            (alignment == IUI_ALIGN_RIGHT)
                ? text_rect.x + text_rect.width - text_width
                : text_rect.x + text_rect.width * .5f - text_width * .5f;
        iui_internal_draw_text(ctx, x_pos, text_rect.y, string,
                               ctx->colors.on_surface);
    }

    /* Restore original font metrics */
    ctx->font_height = original_height;
    ctx->font_ascent_px = original_ascent;
    ctx->font_descent_px = original_descent;
    ctx->pen_width = original_width;

    /* Advance layout for next element */
    ctx->layout.y += text_rect.height;
}

#ifdef CONFIG_FEATURE_TYPOGRAPHY
/* MD3 Typography function implementations using macro */
IUI_DEFINE_TEXT_FUNC(headline_small, headline_small)
IUI_DEFINE_TEXT_FUNC(title_large, title_large)
IUI_DEFINE_TEXT_FUNC(title_medium, title_medium)
IUI_DEFINE_TEXT_FUNC(title_small, title_small)
IUI_DEFINE_TEXT_FUNC(body_large, body_large)
IUI_DEFINE_TEXT_FUNC(body_medium, body_medium)
IUI_DEFINE_TEXT_FUNC(body_small, body_small)
IUI_DEFINE_TEXT_FUNC(label_large, label_large)
IUI_DEFINE_TEXT_FUNC(label_medium, label_medium)
IUI_DEFINE_TEXT_FUNC(label_small, label_small)
#else
/* Stub: Typography disabled - use standard iui_text() for all variants */
IUI_DEFINE_TEXT_STUB(headline_small)
IUI_DEFINE_TEXT_STUB(title_large)
IUI_DEFINE_TEXT_STUB(title_medium)
IUI_DEFINE_TEXT_STUB(title_small)
IUI_DEFINE_TEXT_STUB(body_large)
IUI_DEFINE_TEXT_STUB(body_medium)
IUI_DEFINE_TEXT_STUB(body_small)
IUI_DEFINE_TEXT_STUB(label_large)
IUI_DEFINE_TEXT_STUB(label_medium)
IUI_DEFINE_TEXT_STUB(label_small)
#endif /* CONFIG_FEATURE_TYPOGRAPHY */

/* Dividers and separators - always available regardless of MODULE_BASIC */

static void iui_divider_internal(iui_context *ctx,
                                 float left_inset,
                                 float right_inset)
{
    if (!ctx->current_window)
        return;

    /* MD3: 8dp vertical margin above */
    ctx->layout.y += 8.f;

    /* MD3: 1dp height, outline_variant color */
    float w = ctx->layout.width - left_inset - right_inset;

    /* Guard against negative width in narrow containers */
    if (w > 0.f) {
        float x = ctx->layout.x + left_inset;
        iui_emit_box(ctx, (iui_rect_t) {x, ctx->layout.y, w, 1.f}, 0.f,
                     ctx->colors.outline_variant);
    }

    /* MD3: 8dp vertical margin below (advance by 1dp line + 8dp margin) */
    ctx->layout.y += 1.f + 8.f;
}

void iui_divider(iui_context *ctx)
{
    iui_divider_internal(ctx, 0.f, 0.f);
}

void iui_divider_inset(iui_context *ctx)
{
    /* MD3: 16dp left inset */
    iui_divider_internal(ctx, 16.f, 0.f);
}

void iui_divider_middle_inset(iui_context *ctx)
{
    /* MD3: 16dp left and right inset */
    iui_divider_internal(ctx, 16.f, 16.f);
}

/* Drawing Helper: Rectangle Outline */

void iui_draw_rect_outline(iui_context *ctx,
                           iui_rect_t rect,
                           float width,
                           uint32_t color)
{
    /* Top */
    iui_emit_box(ctx, (iui_rect_t) {rect.x, rect.y, rect.width, width}, 0.f,
                 color);
    /* Bottom */
    iui_emit_box(
        ctx,
        (iui_rect_t) {rect.x, rect.y + rect.height - width, rect.width, width},
        0.f, color);

    /* Left */
    iui_emit_box(ctx, (iui_rect_t) {rect.x, rect.y, width, rect.height}, 0.f,
                 color);
    /* Right */
    iui_emit_box(
        ctx,
        (iui_rect_t) {rect.x + rect.width - width, rect.y, width, rect.height},
        0.f, color);
}

/* Internal: Draw line with fallback to box */
void iui_draw_line_soft(iui_context *ctx,
                        float x0,
                        float y0,
                        float x1,
                        float y1,
                        float width,
                        uint32_t color)
{
    if (ctx->renderer.draw_line) {
        /* Conservative AABB: bounding box of endpoints + half-width */
        float hw = width * 0.5f;
        iui_ink_bounds_extend(ctx, fminf(x0, x1) - hw, fminf(y0, y1) - hw,
                              fabsf(x1 - x0) + width, fabsf(y1 - y0) + width);
        /* Route through batch to preserve draw order with boxes */
        if (ctx->batch.enabled) {
            iui_batch_add_line(ctx, x0, y0, x1, y1, width, color);
            return;
        }
        ctx->renderer.draw_line(x0, y0, x1, y1, width, color,
                                ctx->renderer.user);
    } else {
        /* Fallback: draw line as rotated-ish box (axis aligned approx) */
        float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
        if (len < 0.1f)
            return;

        /* For simple horizontal/vertical lines, draw_box is perfect */
        if (fabsf(dy) < 0.1f) {
            iui_emit_box(ctx,
                         (iui_rect_t) {fminf(x0, x1), y0 - width * 0.5f,
                                       fabsf(dx), width},
                         0.f, color);
        } else if (fabsf(dx) < 0.1f) {
            iui_emit_box(ctx,
                         (iui_rect_t) {x0 - width * 0.5f, fminf(y0, y1), width,
                                       fabsf(dy)},
                         0.f, color);
        } else {
            /* For diagonal lines without rotation support, approximate with
             * bounding box or thin rects.
             * Since rotation is not available in draw_box, best effort with
             * either horizontal or vertical approximation or just skipping if
             * strictly diagonal is required but unsupported. However, for
             * icons like 'X' or checkmark, axis-aligned boxes are poor
             * approximations. A better fallback for diagonals in immediate mode
             * without rotation is often just a box that covers the extent, or
             * multiple small boxes (Bresenham style) which is too expensive
             * here. Let's stick to the behavior used in icons.c previously: use
             * a box that approximates the major axis.
             */
            if (fabsf(dx) > fabsf(dy)) {
                float my = (y0 + y1) * 0.5f;
                iui_emit_box(ctx,
                             (iui_rect_t) {fminf(x0, x1), my - width * 0.5f,
                                           fabsf(dx), width},
                             0.f, color);
            } else {
                float mx = (x0 + x1) * 0.5f;
                iui_emit_box(ctx,
                             (iui_rect_t) {mx - width * 0.5f, fminf(y0, y1),
                                           width, fabsf(dy)},
                             0.f, color);
            }
        }
    }
}

/* Internal: Draw circle with fallback to box */
void iui_draw_circle_soft(iui_context *ctx,
                          float cx,
                          float cy,
                          float radius,
                          uint32_t fill_color,
                          uint32_t stroke_color,
                          float stroke_width)
{
    if (ctx->renderer.draw_circle) {
        float outer = radius + stroke_width;
        iui_ink_bounds_extend(ctx, cx - outer, cy - outer, outer * 2.f,
                              outer * 2.f);
        /* Route through batch to preserve draw order with boxes */
        if (ctx->batch.enabled) {
            iui_batch_add_circle(ctx, cx, cy, radius, fill_color, stroke_color,
                                 stroke_width);
            return;
        }
        ctx->renderer.draw_circle(cx, cy, radius, fill_color, stroke_color,
                                  stroke_width, ctx->renderer.user);
    } else {
        /* Fallback: draw box (square) approximating circle */
        if (fill_color) {
            iui_emit_box(ctx,
                         (iui_rect_t) {cx - radius, cy - radius, radius * 2.f,
                                       radius * 2.f},
                         radius, fill_color);
        }
        /* If stroked (border), simulate with two boxes or just one filled box
         * with stroke color if no fill. 'draw_box' does not support borders
         * natively. Assuming 'draw_box' is filled.
         * To fake a stroke with 'draw_box', a larger box would need to be drawn
         * behind a smaller box, but the background color cannot be guaranteed.
         * So, just draw the filled shape for stroke color if fill is empty.
         */
        if (stroke_color && !fill_color) {
            /* Crude approximation: just draw a filled square for the outline */
            iui_emit_box(ctx,
                         (iui_rect_t) {cx - radius, cy - radius, radius * 2.f,
                                       radius * 2.f},
                         radius, stroke_color);
        }
    }
}

/* Internal: Draw arc with fallback to box */
void iui_draw_arc_soft(iui_context *ctx,
                       float cx,
                       float cy,
                       float radius,
                       float start_angle,
                       float end_angle,
                       float width,
                       uint32_t color)
{
    if (ctx->renderer.draw_arc) {
        /* Conservative: full circle bounding box + arc width */
        float outer = radius + width * 0.5f;
        iui_ink_bounds_extend(ctx, cx - outer, cy - outer, outer * 2.f,
                              outer * 2.f);
        /* Route through batch to preserve draw order with boxes */
        if (ctx->batch.enabled) {
            iui_batch_add_arc(ctx, cx, cy, radius, start_angle, end_angle,
                              width, color);
            return;
        }
        ctx->renderer.draw_arc(cx, cy, radius, start_angle, end_angle, width,
                               color, ctx->renderer.user);
    } else {
        /* Fallback: very rough approximation using a box.
         * Since arcs are often used for parts of circles (like eyes), a box
         * might be ugly but functional. Try to bound the arc.
         *
         * Simplified: just draw a small box at the center to indicate something
         * is there
         */
        iui_emit_box(
            ctx,
            (iui_rect_t) {cx - radius, cy - radius, radius * 2.f, radius * 2.f},
            radius, color);
    }
}

/* MD3 Divider implementation */

/* Vector Drawing Primitives */

bool iui_has_vector_primitives(const iui_context *ctx)
{
    if (!ctx)
        return false;
    return ctx->renderer.draw_line || ctx->renderer.draw_circle ||
           ctx->renderer.draw_arc;
}

bool iui_draw_line(iui_context *ctx,
                   float x0,
                   float y0,
                   float x1,
                   float y1,
                   float width,
                   uint32_t color)
{
    if (!ctx || !ctx->renderer.draw_line)
        return false;
    float hw = width * 0.5f;
    iui_ink_bounds_extend(ctx, fminf(x0, x1) - hw, fminf(y0, y1) - hw,
                          fabsf(x1 - x0) + width, fabsf(y1 - y0) + width);
    if (ctx->batch.enabled) {
        iui_batch_add_line(ctx, x0, y0, x1, y1, width, color);
        return true;
    }
    ctx->renderer.draw_line(x0, y0, x1, y1, width, color, ctx->renderer.user);
    return true;
}

bool iui_draw_circle(iui_context *ctx,
                     float cx,
                     float cy,
                     float radius,
                     uint32_t fill_color,
                     uint32_t stroke_color,
                     float stroke_width)
{
    if (!ctx || !ctx->renderer.draw_circle)
        return false;
    float outer = radius + stroke_width;
    iui_ink_bounds_extend(ctx, cx - outer, cy - outer, outer * 2.f,
                          outer * 2.f);
    if (ctx->batch.enabled) {
        iui_batch_add_circle(ctx, cx, cy, radius, fill_color, stroke_color,
                             stroke_width);
        return true;
    }
    ctx->renderer.draw_circle(cx, cy, radius, fill_color, stroke_color,
                              stroke_width, ctx->renderer.user);
    return true;
}

bool iui_draw_arc(iui_context *ctx,
                  float cx,
                  float cy,
                  float radius,
                  float start_angle,
                  float end_angle,
                  float width,
                  uint32_t color)
{
    if (!ctx || !ctx->renderer.draw_arc)
        return false;
    float outer = radius + width * 0.5f;
    iui_ink_bounds_extend(ctx, cx - outer, cy - outer, outer * 2.f,
                          outer * 2.f);
    if (ctx->batch.enabled) {
        iui_batch_add_arc(ctx, cx, cy, radius, start_angle, end_angle, width,
                          color);
        return true;
    }
    ctx->renderer.draw_arc(cx, cy, radius, start_angle, end_angle, width, color,
                           ctx->renderer.user);
    return true;
}

/* Component state helper implementations */
iui_state_t iui_get_component_state(iui_context *ctx,
                                    iui_rect_t bounds,
                                    bool disabled)
{
    if (disabled)
        return IUI_STATE_DISABLED;

    /* Modal blocking: when a modal is active but NOT inside the modal
     * (rendering=false), block all input. Widgets inside the modal have
     * rendering=true and can interact normally.
     * Pattern from raym3: DialogComponent::IsActive() &&
     * !DialogComponent::IsRendering()
     */
    if (ctx->modal.active && !ctx->modal.rendering)
        return IUI_STATE_DEFAULT; /* No hover, no press - input blocked */

    bool hovered = in_rect(&bounds, ctx->mouse_pos);

    if (hovered) {
        if (ctx->modal.rendering && (ctx->mouse_pressed & IUI_MOUSE_LEFT))
            ctx->modal.clicked_inside = true;

        if (ctx->mouse_pressed & IUI_MOUSE_LEFT)
            return IUI_STATE_PRESSED;

        return IUI_STATE_HOVERED;
    }

    return IUI_STATE_DEFAULT;
}

/* MD3 State Layers and Shadows */

uint32_t iui_get_state_color(iui_context *ctx,
                             iui_state_t state,
                             uint32_t base_color,
                             uint32_t hover_color,
                             uint32_t pressed_color)
{
    (void) ctx; /* Parameter intentionally unused for now */
    switch (state) {
    case IUI_STATE_PRESSED:
        return pressed_color;
    case IUI_STATE_HOVERED:
        return hover_color;
    case IUI_STATE_DISABLED:
        /* Apply 38% alpha for disabled state (MD3 spec) */
        return iui_state_layer(base_color, IUI_STATE_DISABLE_ALPHA);
    case IUI_STATE_FOCUSED:
        /* Apply focus state layer over base color */
        return iui_blend_color(
            base_color, iui_state_layer(base_color, IUI_STATE_FOCUS_ALPHA));
    case IUI_STATE_DRAGGED:
        /* Apply drag state layer over base color (16% opacity) */
        return iui_blend_color(
            base_color, iui_state_layer(base_color, IUI_STATE_DRAG_ALPHA));
    case IUI_STATE_DEFAULT:
    default:
        return base_color;
    }
}

/* Draw MD3 state layer overlay based on component state */
void iui_draw_state_layer(iui_context *ctx,
                          iui_rect_t bounds,
                          float corner_radius,
                          uint32_t content_color,
                          iui_state_t state)
{
    uint8_t alpha = iui_state_get_alpha(state);
    if (alpha == 0)
        return;

    /* Create state layer color with appropriate alpha */
    uint32_t layer_color = iui_state_layer(content_color, alpha);

    /* Draw the state layer overlay */
    iui_emit_box(ctx, bounds, corner_radius, layer_color);
}

/* MD3 Elevation Shadow System
 * Multi-layer shadow rendering following Material Design 3 guidelines
 *
 * Material Design 3 shadows consist of two components:
 * 1. Key shadow (umbra): Directional shadow from light source above
 * - Has vertical offset (light from top)
 * - Sharp edges, high contrast
 * - Creates primary depth perception
 *
 * 2. Ambient shadow (penumbra): Omnidirectional diffuse shadow
 * - No offset, only expansion
 * - Soft edges, low contrast
 * - Creates subtle depth around edges
 *
 * Shadow parameters per elevation level (dp values from MD3 spec):
 * elevation_0: No shadow (flat)
 * elevation_1: key=1dp, ambient=1dp (cards, chips)
 * elevation_2: key=2dp, ambient=1dp (buttons, FAB at rest)
 * elevation_3: key=4dp, ambient=2dp (menus, dialogs)
 * elevation_4: key=6dp, ambient=3dp (navigation drawers)
 * elevation_5: key=8dp, ambient=4dp (modals, FAB pressed)
 */

#ifdef CONFIG_FEATURE_SHADOWS

/* Shadow configuration constants defined in iui-spec.h */

/* Per-level shadow parameters (key_offset, key_spread, ambient_spread)
 * Based on MD3 elevation dp values scaled for typical UI
 */
static const float iui_shadow_params[][3] = {
    {0.0f, 0.0f, 0.0f}, /* elevation_0: no shadow */
    {1.5f, 1.0f, 1.0f}, /* elevation_1: subtle (cards) */
    {3.0f, 1.5f, 1.5f}, /* elevation_2: medium (buttons) */
    {5.0f, 2.5f, 2.0f}, /* elevation_3: pronounced (dialogs) */
    {7.0f, 3.5f, 2.5f}, /* elevation_4: high (drawers) */
    {9.0f, 4.5f, 3.0f}, /* elevation_5: maximum (modals) */
};

/* Alpha falloff weights for multi-layer rendering (outer to inner) */
static const float iui_shadow_key_weights[] = {0.45f, 0.35f, 0.20f};
static const float iui_shadow_ambient_weights[] = {0.6f, 0.4f};

void iui_draw_shadow(iui_context *ctx,
                     iui_rect_t bounds,
                     float corner_radius,
                     iui_elevation_t level)
{
    if (!ctx)
        return;

    /* Clamp level to valid range */
    if (level < IUI_ELEVATION_0)
        level = IUI_ELEVATION_0;
    if (level > IUI_ELEVATION_5)
        level = IUI_ELEVATION_5;
    if (level == IUI_ELEVATION_0)
        return;

    /* Get shadow color from theme (strip alpha, apply own alpha) */
    uint32_t shadow_rgb = ctx->colors.shadow & 0x00FFFFFF;

    /* Get parameters for this elevation level */
    float key_offset = iui_shadow_params[level][0];
    float key_spread = iui_shadow_params[level][1];
    float ambient_spread = iui_shadow_params[level][2];

    /* Render ambient shadow (omnidirectional, no offset)
     * Draws first (furthest back) to create soft halo effect
     */
    for (int i = 0; i < IUI_SHADOW_AMBIENT_LAYERS; i++) {
        /* Outer layers have more spread, less alpha */
        float layer_factor = (float) (IUI_SHADOW_AMBIENT_LAYERS - i) /
                             (float) IUI_SHADOW_AMBIENT_LAYERS;
        float spread = ambient_spread * (1.0f + layer_factor * 0.5f);
        float alpha = IUI_SHADOW_AMBIENT_ALPHA * iui_shadow_ambient_weights[i];

        uint8_t alpha_byte = (uint8_t) (alpha * 255.f + 0.5f);
        if (alpha_byte == 0)
            continue;

        uint32_t color = shadow_rgb | ((uint32_t) alpha_byte << 24);
        iui_rect_t rect = {
            .x = bounds.x - spread,
            .y = bounds.y - spread,
            .width = bounds.width + spread * 2.f,
            .height = bounds.height + spread * 2.f,
        };

        iui_emit_box(ctx, rect, corner_radius + spread * 0.5f, color);
    }

    /* Render key shadow (directional, offset down)
     * Light source is above, so shadow falls below with slight right offset
     */
    for (int i = 0; i < IUI_SHADOW_KEY_LAYERS; i++) {
        /* Outer layers: more offset/spread, less alpha */
        float layer_factor =
            (float) (IUI_SHADOW_KEY_LAYERS - i) / (float) IUI_SHADOW_KEY_LAYERS;
        float offset_y = key_offset * layer_factor;
        float offset_x = offset_y * 0.15f; /* Slight horizontal offset */
        float spread = key_spread * layer_factor;
        float alpha = IUI_SHADOW_KEY_ALPHA * iui_shadow_key_weights[i];

        uint8_t alpha_byte = (uint8_t) (alpha * 255.f + 0.5f);
        if (alpha_byte == 0)
            continue;

        uint32_t color = shadow_rgb | ((uint32_t) alpha_byte << 24);
        iui_rect_t rect = {
            .x = bounds.x + offset_x - spread * 0.5f,
            .y = bounds.y + offset_y - spread * 0.25f,
            .width = bounds.width + spread,
            .height = bounds.height + spread,
        };

        iui_emit_box(ctx, rect, corner_radius + spread * 0.3f, color);
    }
}

#else /* !CONFIG_FEATURE_SHADOWS */

/* Stub: shadows disabled */
void iui_draw_shadow(iui_context *ctx,
                     iui_rect_t bounds,
                     float corner_radius,
                     iui_elevation_t level)
{
    (void) ctx;
    (void) bounds;
    (void) corner_radius;
    (void) level;
}

#endif /* CONFIG_FEATURE_SHADOWS */

void iui_draw_elevated_box(iui_context *ctx,
                           iui_rect_t bounds,
                           float corner_radius,
                           iui_elevation_t level,
                           uint32_t color)
{
    if (!ctx)
        return;

#ifdef CONFIG_FEATURE_SHADOWS
    /* Draw shadow first (behind the box) */
    iui_draw_shadow(ctx, bounds, corner_radius, level);
#else
    (void) level;
#endif

    /* Draw the box on top */
    iui_emit_box(ctx, bounds, corner_radius, color);
}

/* Clip stack functions */
bool iui_push_clip(iui_context *ctx, iui_rect_t rect)
{
    if (ctx->clip.depth >= IUI_CLIP_STACK_SIZE)
        return false; /* Stack overflow - return error */

    /* Intersect with current clip if any */
    if (ctx->clip.depth > 0) {
        iui_rect_t current = ctx->clip.stack[ctx->clip.depth - 1];
        /* Compute right/bottom BEFORE clamping origin so the original
         * rect.x + rect.width is used, not the shifted value.
         */
        float right = fminf(rect.x + rect.width, current.x + current.width);
        float bottom = fminf(rect.y + rect.height, current.y + current.height);
        rect.x = fmaxf(rect.x, current.x);
        rect.y = fmaxf(rect.y, current.y);
        rect.width = right - rect.x;
        rect.height = bottom - rect.y;
    }

    /* Degenerate intersection: push zero rect so everything clips */
    if (rect.width <= 0 || rect.height <= 0)
        rect = (iui_rect_t) {0, 0, 0, 0};

    ctx->clip.stack[ctx->clip.depth] = rect;
    ctx->clip.depth++;

    /* Update renderer (clamp to uint16 range) */
    uint16_t clip_minx = iui_float_to_u16(rect.x);
    uint16_t clip_miny = iui_float_to_u16(rect.y);
    uint16_t clip_maxx = iui_float_to_u16(rect.x + rect.width);
    uint16_t clip_maxy = iui_float_to_u16(rect.y + rect.height);
    ctx->current_clip =
        (iui_clip_rect) {clip_minx, clip_miny, clip_maxx, clip_maxy};
    ctx->renderer.set_clip_rect(clip_minx, clip_miny, clip_maxx, clip_maxy,
                                ctx->renderer.user);
    return true;
}

void iui_pop_clip(iui_context *ctx)
{
    /* Floor at depth 1 while inside a window to protect the window clip.
     * iui_end_window bypasses this by clearing current_window first. */
    int floor = (ctx->current_window != NULL) ? 1 : 0;
    if (ctx->clip.depth <= floor)
        return;

    ctx->clip.depth--;

    if (ctx->clip.depth > 0) {
        iui_rect_t prev = ctx->clip.stack[ctx->clip.depth - 1];
        uint16_t clip_minx = iui_float_to_u16(prev.x);
        uint16_t clip_miny = iui_float_to_u16(prev.y);
        uint16_t clip_maxx = iui_float_to_u16(prev.x + prev.width);
        uint16_t clip_maxy = iui_float_to_u16(prev.y + prev.height);
        ctx->current_clip =
            (iui_clip_rect) {clip_minx, clip_miny, clip_maxx, clip_maxy};
        ctx->renderer.set_clip_rect(clip_minx, clip_miny, clip_maxx, clip_maxy,
                                    ctx->renderer.user);
    } else {
        /* No clips left: reset to full screen */
        ctx->current_clip = (iui_clip_rect) {0, 0, UINT16_MAX, UINT16_MAX};
        ctx->renderer.set_clip_rect(0, 0, UINT16_MAX, UINT16_MAX,
                                    ctx->renderer.user);
    }
}

bool iui_is_clipped(iui_context *ctx, iui_rect_t rect)
{
    if (!ctx || ctx->clip.depth <= 0)
        return false;

    iui_rect_t clip_rect = ctx->clip.stack[ctx->clip.depth - 1];

    /* Check if rect is completely outside clip area */
    return (rect.x > clip_rect.x + clip_rect.width ||
            rect.x + rect.width < clip_rect.x ||
            rect.y > clip_rect.y + clip_rect.height ||
            rect.y + rect.height < clip_rect.y);
}

/* Draw call batching */

static void batch_flush_internal(iui_context *ctx)
{
    if (!ctx || ctx->batch.count == 0)
        return;

    iui_clip_rect last_clip = {0, 0, UINT16_MAX, UINT16_MAX};
    /* Set initial clip state before loop */
    ctx->renderer.set_clip_rect(last_clip.minx, last_clip.miny, last_clip.maxx,
                                last_clip.maxy, ctx->renderer.user);

    for (int i = 0; i < ctx->batch.count; i++) {
        iui_draw_cmd *cmd = &ctx->batch.commands[i];

        /* Inline comparison faster than memcmp, avoids padding issues */
        if (cmd->clip.minx != last_clip.minx ||
            cmd->clip.miny != last_clip.miny ||
            cmd->clip.maxx != last_clip.maxx ||
            cmd->clip.maxy != last_clip.maxy) {
            ctx->renderer.set_clip_rect(cmd->clip.minx, cmd->clip.miny,
                                        cmd->clip.maxx, cmd->clip.maxy,
                                        ctx->renderer.user);
            last_clip = cmd->clip;
        }

        switch (cmd->type) {
        case IUI_CMD_RECT:
            ctx->renderer.draw_box(
                (iui_rect_t) {cmd->data.rect.x, cmd->data.rect.y,
                              cmd->data.rect.w, cmd->data.rect.h},
                cmd->data.rect.radius, cmd->color, ctx->renderer.user);
            break;
        case IUI_CMD_TEXT:
            if (ctx->renderer.draw_text)
                ctx->renderer.draw_text(cmd->data.text.x, cmd->data.text.y,
                                        cmd->data.text.text, cmd->color,
                                        ctx->renderer.user);
            break;
        case IUI_CMD_LINE:
            if (ctx->renderer.draw_line)
                ctx->renderer.draw_line(cmd->data.line.x0, cmd->data.line.y0,
                                        cmd->data.line.x1, cmd->data.line.y1,
                                        cmd->data.line.width, cmd->color,
                                        ctx->renderer.user);
            break;
        case IUI_CMD_CIRCLE:
            if (ctx->renderer.draw_circle)
                ctx->renderer.draw_circle(
                    cmd->data.circle.cx, cmd->data.circle.cy,
                    cmd->data.circle.radius, cmd->data.circle.fill_color,
                    cmd->color2, cmd->data.circle.stroke_width,
                    ctx->renderer.user);
            break;
        case IUI_CMD_ARC:
            if (ctx->renderer.draw_arc)
                ctx->renderer.draw_arc(
                    cmd->data.arc.cx, cmd->data.arc.cy, cmd->data.arc.radius,
                    cmd->data.arc.start_angle, cmd->data.arc.end_angle,
                    cmd->data.arc.width, cmd->color, ctx->renderer.user);
            break;
        }
    }
    ctx->batch.count = 0;

    /* Restore the logical clip state so subsequent direct draws (e.g. long-text
     * fallback) use the correct clip rectangle, not whatever the last batched
     * command happened to set.
     */
    ctx->renderer.set_clip_rect(ctx->current_clip.minx, ctx->current_clip.miny,
                                ctx->current_clip.maxx, ctx->current_clip.maxy,
                                ctx->renderer.user);
}

void iui_batch_init(iui_context *ctx)
{
    if (!ctx)
        return;
    ctx->batch.count = 0;
    ctx->batch.enabled = false;
}

void iui_batch_enable(iui_context *ctx, bool enable)
{
    if (!ctx)
        return;
    if (ctx->batch.enabled && !enable)
        batch_flush_internal(ctx);
    ctx->batch.enabled = enable;
}

void iui_batch_flush(iui_context *ctx)
{
    if (ctx && ctx->batch.enabled)
        batch_flush_internal(ctx);
}

int iui_batch_count(const iui_context *ctx)
{
    return ctx ? ctx->batch.count : 0;
}
