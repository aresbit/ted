/*
 * Software Rendering Utilities for Framebuffer-Based Ports
 *
 * Consolidated header providing:
 *   - Color manipulation and alpha blending (ARGB32)
 *   - Pixel-level drawing operations (rasterizer)
 *   - Vector path tessellation (Bezier curves)
 *
 * Architecture:
 *   This header is used by ports that perform software rendering to a
 *   framebuffer (headless.c, wasm.c). Ports using hardware acceleration
 *   (sdl2.c) can still use the path/Bezier utilities but handle primitive
 *   drawing through their native API (SDL_Renderer).
 *
 *   Components:
 *   1. Color functions (iui_color_*, iui_make_color, iui_blend_*)
 *      - Used by all software-rendering ports
 *      - Aliased by headless.h for test API consistency
 *
 *   2. Rasterizer (iui_raster_*)
 *      - Full software rasterizer with clipping and anti-aliasing
 *      - Used by headless.c and wasm.c
 *      - NOT used by sdl2.c (uses SDL_Renderer instead)
 *
 *   3. Path state and Bezier tessellation (iui_path_*)
 *      - Shared by ALL ports for vector font rendering
 *      - sdl2.c uses _scaled variants for HiDPI support
 *      - headless.c/wasm.c use unscaled variants
 *
 * Requirements:
 *   - Framebuffer in ARGB32 format (for rasterizer functions)
 */

#ifndef IUI_PORT_SW_H
#define IUI_PORT_SW_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Color Manipulation (ARGB32 format) */

static inline uint8_t iui_color_alpha(uint32_t c)
{
    return (c >> 24) & 0xFF;
}

static inline uint8_t iui_color_red(uint32_t c)
{
    return (c >> 16) & 0xFF;
}

static inline uint8_t iui_color_green(uint32_t c)
{
    return (c >> 8) & 0xFF;
}

static inline uint8_t iui_color_blue(uint32_t c)
{
    return c & 0xFF;
}

static inline uint32_t iui_make_color(uint8_t r,
                                      uint8_t g,
                                      uint8_t b,
                                      uint8_t a)
{
    return ((uint32_t) a << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) |
           b;
}

/* Alpha Blending: blend src over dst using standard Porter-Duff "over" */
static inline uint32_t iui_blend_pixel(uint32_t dst, uint32_t src)
{
    uint8_t sa = iui_color_alpha(src);
    if (sa == 0)
        return dst;
    if (sa == 255)
        return src;

    uint8_t da = iui_color_alpha(dst);
    uint8_t sr = iui_color_red(src), sg = iui_color_green(src),
            sb = iui_color_blue(src);
    uint8_t dr = iui_color_red(dst), dg = iui_color_green(dst),
            db = iui_color_blue(dst);

    /* Standard alpha compositing: out = src + dst * (1 - src_alpha) */
    uint32_t inv_sa = 255 - sa;
    uint8_t out_r = (uint8_t) ((sr * sa + dr * inv_sa) / 255);
    uint8_t out_g = (uint8_t) ((sg * sa + dg * inv_sa) / 255);
    uint8_t out_b = (uint8_t) ((sb * sa + db * inv_sa) / 255);
    uint8_t out_a = (uint8_t) (sa + (da * inv_sa) / 255);

    return iui_make_color(out_r, out_g, out_b, out_a);
}

/* Blend pixel with fractional alpha (for anti-aliasing) */
static inline uint32_t iui_blend_aa(uint32_t dst,
                                    uint32_t color,
                                    float brightness)
{
    if (brightness <= 0.0f)
        return dst;
    if (brightness > 1.0f)
        brightness = 1.0f;

    uint8_t base_alpha = iui_color_alpha(color);
    uint8_t new_alpha = (uint8_t) (base_alpha * brightness);
    uint32_t aa_color = (new_alpha << 24) | (color & 0x00FFFFFF);
    return iui_blend_pixel(dst, aa_color);
}

/* Rasterizer Context and Primitives */

/* Rasterizer context - minimal state for drawing operations */
typedef struct {
    uint32_t *framebuffer;
    int width, height;
    int clip_min_x, clip_min_y;
    int clip_max_x, clip_max_y;
    uint64_t pixels_drawn; /* Optional counter for profiling */
} iui_raster_ctx_t;

/* Initialize raster context with full-screen clipping */
static inline void iui_raster_init(iui_raster_ctx_t *r,
                                   uint32_t *fb,
                                   int w,
                                   int h)
{
    r->framebuffer = fb;
    r->width = w;
    r->height = h;
    r->clip_min_x = 0;
    r->clip_min_y = 0;
    r->clip_max_x = w;
    r->clip_max_y = h;
    r->pixels_drawn = 0;
}

/* Set clipping rectangle */
static inline void iui_raster_set_clip(iui_raster_ctx_t *r,
                                       int min_x,
                                       int min_y,
                                       int max_x,
                                       int max_y)
{
    r->clip_min_x = min_x < 0 ? 0 : min_x;
    r->clip_min_y = min_y < 0 ? 0 : min_y;
    r->clip_max_x = max_x > r->width ? r->width : max_x;
    r->clip_max_y = max_y > r->height ? r->height : max_y;
}

/* Reset clipping to full framebuffer */
static inline void iui_raster_reset_clip(iui_raster_ctx_t *r)
{
    r->clip_min_x = 0;
    r->clip_min_y = 0;
    r->clip_max_x = r->width;
    r->clip_max_y = r->height;
}

/* Set pixel with clipping and alpha blending */
static inline void iui_raster_pixel(iui_raster_ctx_t *r,
                                    int x,
                                    int y,
                                    uint32_t color)
{
    if (x < r->clip_min_x || x >= r->clip_max_x || y < r->clip_min_y ||
        y >= r->clip_max_y)
        return;

    size_t idx = (size_t) y * (size_t) r->width + (size_t) x;
    r->framebuffer[idx] = iui_blend_pixel(r->framebuffer[idx], color);
    r->pixels_drawn++;
}

/* Set pixel with anti-aliasing brightness factor */
static inline void iui_raster_pixel_aa(iui_raster_ctx_t *r,
                                       int x,
                                       int y,
                                       uint32_t color,
                                       float brightness)
{
    if (brightness <= 0.0f)
        return;
    if (x < r->clip_min_x || x >= r->clip_max_x || y < r->clip_min_y ||
        y >= r->clip_max_y)
        return;

    size_t idx = (size_t) y * (size_t) r->width + (size_t) x;
    r->framebuffer[idx] = iui_blend_aa(r->framebuffer[idx], color, brightness);
    r->pixels_drawn++;
}

/* Draw horizontal line with clipping */
static inline void iui_raster_hline(iui_raster_ctx_t *r,
                                    int x0,
                                    int x1,
                                    int y,
                                    uint32_t color)
{
    if (y < r->clip_min_y || y >= r->clip_max_y)
        return;

    if (x0 > x1) {
        int tmp = x0;
        x0 = x1;
        x1 = tmp;
    }

    int start = x0 < r->clip_min_x ? r->clip_min_x : x0;
    int end = x1 >= r->clip_max_x ? r->clip_max_x - 1 : x1;

    uint8_t sa = iui_color_alpha(color);
    if (sa == 0)
        return;

    if (start > end)
        return;

    int count = end - start + 1;
    uint32_t *row = &r->framebuffer[(size_t) y * (size_t) r->width];

    if (sa == 255) {
        for (int x = start; x <= end; x++)
            row[x] = color;
    } else {
        for (int x = start; x <= end; x++)
            row[x] = iui_blend_pixel(row[x], color);
    }
    r->pixels_drawn += (uint64_t) count;
}

/* Fill rectangle (no rounding) */
static inline void iui_raster_fill_rect(iui_raster_ctx_t *r,
                                        int x,
                                        int y,
                                        int w,
                                        int h,
                                        uint32_t color)
{
    for (int row = 0; row < h; row++)
        iui_raster_hline(r, x, x + w - 1, y + row, color);
}

/* Fill rounded rectangle with anti-aliased corners */
static inline void iui_raster_rounded_rect(iui_raster_ctx_t *r,
                                           float fx,
                                           float fy,
                                           float fw,
                                           float fh,
                                           float radius,
                                           uint32_t color)
{
    int x = (int) floorf(fx), y = (int) floorf(fy);
    int w = (int) ceilf(fx + fw) - x, h = (int) ceilf(fy + fh) - y;

    if (w <= 0 || h <= 0)
        return;

    if (radius <= 0.5f) {
        iui_raster_fill_rect(r, x, y, w, h, color);
        return;
    }

    /* Clamp radius to half of smaller dimension */
    if (radius > (float) w / 2.0f)
        radius = (float) w / 2.0f;
    if (radius > (float) h / 2.0f)
        radius = (float) h / 2.0f;

    float r2 = radius * radius;
    int ir = (int) ceilf(radius);

    for (int row = 0; row < h; row++) {
        int line_y = y + row;
        int x_start = x;
        int x_end = x + w - 1;
        float aa_left = 0.0f, aa_right = 0.0f;

        if (row < ir) {
            /* Top rounded corners */
            float dy = radius - (float) row - 0.5f;
            if (dy > 0.0f) {
                float dy2 = dy * dy;
                if (dy2 < r2) {
                    float dx = sqrtf(r2 - dy2);
                    float inset_f = radius - dx;
                    int inset = (int) floorf(inset_f);
                    aa_left = inset_f - (float) inset;
                    aa_right = aa_left;
                    if (inset >= 0) {
                        x_start = x + inset + 1;
                        x_end = x + w - 1 - inset - 1;
                    }
                } else {
                    continue;
                }
            }
        } else if (row >= h - ir) {
            /* Bottom rounded corners */
            float dy = (float) row - (float) (h - 1) + radius - 0.5f;
            if (dy > 0.0f) {
                float dy2 = dy * dy;
                if (dy2 < r2) {
                    float dx = sqrtf(r2 - dy2);
                    float inset_f = radius - dx;
                    int inset = (int) floorf(inset_f);
                    aa_left = inset_f - (float) inset;
                    aa_right = aa_left;
                    if (inset >= 0) {
                        x_start = x + inset + 1;
                        x_end = x + w - 1 - inset - 1;
                    }
                } else {
                    continue;
                }
            }
        }

        if (x_start <= x_end)
            iui_raster_hline(r, x_start, x_end, line_y, color);

        if (aa_left > 0.01f && x_start > x)
            iui_raster_pixel_aa(r, x_start - 1, line_y, color, 1.0f - aa_left);
        if (aa_right > 0.01f && x_end < x + w - 1)
            iui_raster_pixel_aa(r, x_end + 1, line_y, color, 1.0f - aa_right);
    }
}

/* Draw capsule (rounded rectangle / stadium shape) using signed distance field.
 * A capsule is a line segment with radius - perfect for thick stroke rendering.
 * Uses per-pixel distance calculation with AA at edges.
 *
 * Optimizations:
 * - Squared distance early-out avoids sqrtf for solid core pixels
 * - Tighter AA fringe for thin lines (radius <= 0.5) improves crispness
 * - Pre-clipped bounding box eliminates redundant per-pixel bounds checks
 */
static inline void iui_raster_capsule(iui_raster_ctx_t *r,
                                      float x0,
                                      float y0,
                                      float x1,
                                      float y1,
                                      float radius,
                                      uint32_t color)
{
    if (radius <= 0.0f)
        return;

    /* Adaptive AA fringe: tighter for thin lines to improve crispness.
     * Smoothly interpolate between 0.35 (crisp) and 0.5 (SDL2-compatible)
     * over the radius range [0.4, 0.6] to avoid sudden width jumps.
     */
    float aa_half;
    if (radius <= 0.4f)
        aa_half = 0.35f;
    else if (radius >= 0.6f)
        aa_half = 0.5f;
    else
        aa_half = 0.35f + (radius - 0.4f) * (0.5f - 0.35f) / (0.6f - 0.4f);

    /* Precompute squared thresholds for early-out optimization */
    float inner_r = radius - aa_half;
    float outer_r = radius + aa_half;
    float inner_r2 = (inner_r > 0.0f) ? inner_r * inner_r : 0.0f;
    float outer_r2 = outer_r * outer_r;
    float aa_width = 2.0f * aa_half;

    /* Compute bounding box with tighter margin */
    float margin = outer_r + 0.5f;
    float min_xf = fminf(x0, x1) - margin;
    float max_xf = fmaxf(x0, x1) + margin;
    float min_yf = fminf(y0, y1) - margin;
    float max_yf = fmaxf(y0, y1) + margin;

    int min_x = (int) floorf(min_xf);
    int max_x = (int) ceilf(max_xf);
    int min_y = (int) floorf(min_yf);
    int max_y = (int) ceilf(max_yf);

    /* Clip to framebuffer - after this, no per-pixel bounds check needed */
    if (min_x < r->clip_min_x)
        min_x = r->clip_min_x;
    if (max_x > r->clip_max_x)
        max_x = r->clip_max_x;
    if (min_y < r->clip_min_y)
        min_y = r->clip_min_y;
    if (max_y > r->clip_max_y)
        max_y = r->clip_max_y;

    if (min_x >= max_x || min_y >= max_y)
        return;

    float dx = x1 - x0;
    float dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    /* Threshold 1e-6 matches iui_raster_path_stroke's degenerate check */
    float inv_len2 = (len2 > 0.000001f) ? 1.0f / len2 : 0.0f;

    /* Precompute scaled direction for incremental dot product */
    float dx_scaled = dx * inv_len2;
    float dy_scaled = dy * inv_len2;
    float fx_start = (float) min_x + 0.5f;
    float fx_x0 = fx_start - x0;

    uint32_t *row_base = r->framebuffer + (size_t) min_y * (size_t) r->width;

    /* For each pixel, compute distance to line segment */
    for (int py = min_y; py < max_y; py++) {
        float fy = (float) py + 0.5f;
        float fy_y0 = fy - y0;

        /* Incremental dot product: start value for this row */
        float dot_base = fx_x0 * dx_scaled + fy_y0 * dy_scaled;
        float fx = fx_start;

        for (int px = min_x; px < max_x; px++) {
            /* Project point onto line segment, clamp t to [0,1] */
            float t;
            if (inv_len2 == 0.0f) {
                t = 0.0f;
            } else {
                t = dot_base;
                if (t < 0.0f)
                    t = 0.0f;
                else if (t > 1.0f)
                    t = 1.0f;
            }

            /* Closest point on segment */
            float cx = x0 + t * dx;
            float cy = y0 + t * dy;

            /* Squared distance from pixel center to closest point */
            float dist_x = fx - cx;
            float dist_y = fy - cy;
            float dist2 = dist_x * dist_x + dist_y * dist_y;

            /* Early-out using squared distance comparisons (avoids sqrtf) */
            if (dist2 < inner_r2) {
                /* Fully inside solid core - direct write, no bounds check */
                row_base[px] = iui_blend_pixel(row_base[px], color);
                r->pixels_drawn++;
            } else if (dist2 < outer_r2) {
                /* In AA band - need sqrtf for accurate coverage */
                float dist = sqrtf(dist2);
                float coverage = (outer_r - dist) / aa_width;
                row_base[px] = iui_blend_aa(row_base[px], color, coverage);
                r->pixels_drawn++;
            }
            /* else: outside capsule, skip */

            /* Increment for next pixel */
            dot_base += dx_scaled;
            fx += 1.0f;
        }
        row_base += r->width;
    }
}

/* Draw line with thickness using capsule SDF.
 * Uses the same rendering approach as path_stroke for consistency.
 * Minimum stroke width is enforced at 1.0px to match SDL2 behavior.
 */
static inline void iui_raster_line(iui_raster_ctx_t *r,
                                   float x0,
                                   float y0,
                                   float x1,
                                   float y1,
                                   float width,
                                   uint32_t color)
{
    /* Enforce minimum stroke width like SDL2 and path_stroke */
    if (width < 1.0f)
        width = 1.0f;

    float radius = width * 0.5f;
    iui_raster_capsule(r, x0, y0, x1, y1, radius, color);
}

/* Fill circle with anti-aliased edges */
static inline void iui_raster_circle_fill(iui_raster_ctx_t *r,
                                          float cx,
                                          float cy,
                                          float radius,
                                          uint32_t color)
{
    if (radius <= 0.5f)
        return;

    float r2 = radius * radius;
    int ir = (int) ceilf(radius);

    for (int y = -ir; y <= ir; y++) {
        float fy = (float) y;
        float dy2 = fy * fy;

        if (dy2 > r2)
            continue;

        float x_extent = sqrtf(r2 - dy2);
        float left_edge = cx - x_extent;
        float right_edge = cx + x_extent;

        int x_left = (int) floorf(left_edge);
        int x_right = (int) ceilf(right_edge);
        int iy = (int) cy + y;

        float left_coverage = 1.0f - (left_edge - (float) x_left);
        float right_coverage = right_edge - floorf(right_edge);

        if (left_coverage > 1.0f)
            left_coverage = 1.0f;
        if (right_coverage > 1.0f)
            right_coverage = 1.0f;

        if (left_coverage > 0.01f)
            iui_raster_pixel_aa(r, x_left, iy, color, left_coverage);

        if (x_left + 1 <= x_right - 1)
            iui_raster_hline(r, x_left + 1, x_right - 1, iy, color);

        if (x_right != x_left && right_coverage > 0.01f)
            iui_raster_pixel_aa(r, x_right, iy, color, right_coverage);
    }
}

/* Stroke circle outline using SDF (signed distance field) for perfect AA.
 * Renders an annulus (ring) by computing distance to the circle center
 * and checking if it falls within the stroke band.
 */
static inline void iui_raster_circle_stroke(iui_raster_ctx_t *r,
                                            float cx,
                                            float cy,
                                            float radius,
                                            float width,
                                            uint32_t color)
{
    if (radius <= 0.0f || width <= 0.0f)
        return;

    float half_w = width * 0.5f;
    if (half_w < 0.4f)
        half_w = 0.4f;

    float outer_r = radius + half_w + 1.0f;

    /* Bounding box */
    int min_x = (int) floorf(cx - outer_r);
    int max_x = (int) ceilf(cx + outer_r);
    int min_y = (int) floorf(cy - outer_r);
    int max_y = (int) ceilf(cy + outer_r);

    /* Clip to framebuffer */
    if (min_x < r->clip_min_x)
        min_x = r->clip_min_x;
    if (max_x > r->clip_max_x)
        max_x = r->clip_max_x;
    if (min_y < r->clip_min_y)
        min_y = r->clip_min_y;
    if (max_y > r->clip_max_y)
        max_y = r->clip_max_y;

    for (int py = min_y; py < max_y; py++) {
        float fy = (float) py + 0.5f - cy;
        float fy2 = fy * fy;

        for (int px = min_x; px < max_x; px++) {
            float fx = (float) px + 0.5f - cx;

            /* Distance from pixel center to circle center */
            float dist_to_center = sqrtf(fx * fx + fy2);

            /* Distance to the ring (annulus) - how far from radius line */
            float dist_to_ring = fabsf(dist_to_center - radius);

            /* AA zone is 1 pixel wide centered on stroke boundary */
            if (dist_to_ring < half_w - 0.5f) {
                /* Fully inside stroke */
                iui_raster_pixel(r, px, py, color);
            } else if (dist_to_ring < half_w + 0.5f) {
                /* AA edge */
                float coverage = (half_w + 0.5f) - dist_to_ring;
                iui_raster_pixel_aa(r, px, py, color, coverage);
            }
        }
    }
}

/* Normalize angle to [0, 2*PI) range */
static inline float iui_normalize_angle(float angle)
{
    const float two_pi = (float) IUI_PORT_PI * 2.0f;
    while (angle < 0.0f)
        angle += two_pi;
    while (angle >= two_pi)
        angle -= two_pi;
    return angle;
}

/* Check if angle is within arc range (handles wraparound) */
static inline int iui_angle_in_arc(float angle, float start, float end)
{
    angle = iui_normalize_angle(angle);
    start = iui_normalize_angle(start);
    end = iui_normalize_angle(end);

    if (start <= end) {
        return angle >= start && angle <= end;
    } else {
        /* Arc crosses 0/2PI boundary */
        return angle >= start || angle <= end;
    }
}

/* Draw arc using SDF for perfect AA.
 * Combines radial distance check with angular bounds check.
 */
static inline void iui_raster_arc(iui_raster_ctx_t *r,
                                  float cx,
                                  float cy,
                                  float radius,
                                  float start_angle,
                                  float end_angle,
                                  float width,
                                  uint32_t color)
{
    if (radius <= 0.0f || width <= 0.0f)
        return;

    float half_w = width * 0.5f;
    if (half_w < 0.4f)
        half_w = 0.4f;

    float outer_r = radius + half_w + 1.0f;

    /* Bounding box */
    int min_x = (int) floorf(cx - outer_r);
    int max_x = (int) ceilf(cx + outer_r);
    int min_y = (int) floorf(cy - outer_r);
    int max_y = (int) ceilf(cy + outer_r);

    /* Clip to framebuffer */
    if (min_x < r->clip_min_x)
        min_x = r->clip_min_x;
    if (max_x > r->clip_max_x)
        max_x = r->clip_max_x;
    if (min_y < r->clip_min_y)
        min_y = r->clip_min_y;
    if (max_y > r->clip_max_y)
        max_y = r->clip_max_y;

    /* Precompute arc endpoint positions for cap rendering */
    float start_x = cx + cosf(start_angle) * radius;
    float start_y = cy + sinf(start_angle) * radius;
    float end_x = cx + cosf(end_angle) * radius;
    float end_y = cy + sinf(end_angle) * radius;

    for (int py = min_y; py < max_y; py++) {
        float fy = (float) py + 0.5f - cy;
        float fy2 = fy * fy;

        for (int px = min_x; px < max_x; px++) {
            float fx = (float) px + 0.5f - cx;
            float dist_to_center = sqrtf(fx * fx + fy2);

            /* Skip if too far from the arc radius */
            if (dist_to_center < radius - half_w - 1.0f ||
                dist_to_center > radius + half_w + 1.0f)
                continue;

            /* Calculate angle of this pixel relative to center */
            float pixel_angle = atan2f(fy, fx);

            /* Check if within arc angular range */
            int in_arc = iui_angle_in_arc(pixel_angle, start_angle, end_angle);

            float dist;
            if (in_arc) {
                /* Inside arc angular range - use radial distance */
                dist = fabsf(dist_to_center - radius);
            } else {
                /* Outside arc - compute distance to nearest endpoint (cap) */
                float dx_start = (float) px + 0.5f - start_x;
                float dy_start = (float) py + 0.5f - start_y;
                float dist_start =
                    sqrtf(dx_start * dx_start + dy_start * dy_start);

                float dx_end = (float) px + 0.5f - end_x;
                float dy_end = (float) py + 0.5f - end_y;
                float dist_end = sqrtf(dx_end * dx_end + dy_end * dy_end);

                dist = fminf(dist_start, dist_end);
            }

            /* AA zone is 1 pixel wide centered on stroke boundary */
            if (dist < half_w - 0.5f) {
                iui_raster_pixel(r, px, py, color);
            } else if (dist < half_w + 0.5f) {
                float coverage = (half_w + 0.5f) - dist;
                iui_raster_pixel_aa(r, px, py, color, coverage);
            }
        }
    }
}

/* Clear framebuffer to a solid color */
static inline void iui_raster_clear(iui_raster_ctx_t *r, uint32_t color)
{
    size_t count = (size_t) r->width * (size_t) r->height;
    for (size_t i = 0; i < count; i++)
        r->framebuffer[i] = color;
}

/* Vector Path State and Bezier Tessellation */

/* Vector path state container - embed in port context structure */
typedef struct {
    float points_x[IUI_PORT_MAX_PATH_POINTS];
    float points_y[IUI_PORT_MAX_PATH_POINTS];
    int count;
    float pen_x, pen_y;
} iui_path_state_t;

/* Initialize/reset path state */
static inline void iui_path_reset(iui_path_state_t *p)
{
    p->count = 0;
    p->pen_x = 0.0f;
    p->pen_y = 0.0f;
}

/* Move pen to position, starting a new subpath */
static inline void iui_path_move_to(iui_path_state_t *p, float x, float y)
{
    p->pen_x = x;
    p->pen_y = y;
    p->count = 0;

    if (p->count < IUI_PORT_MAX_PATH_POINTS) {
        p->points_x[p->count] = x;
        p->points_y[p->count] = y;
        p->count++;
    }
}

/* Add line segment to current position */
static inline void iui_path_line_to(iui_path_state_t *p, float x, float y)
{
    p->pen_x = x;
    p->pen_y = y;

    if (p->count < IUI_PORT_MAX_PATH_POINTS) {
        p->points_x[p->count] = x;
        p->points_y[p->count] = y;
        p->count++;
    }
}

/* Add cubic Bezier curve using adaptive tessellation
 * Control points: p0 (current pen), p1 (x1,y1), p2 (x2,y2), p3 (x3,y3)
 */
static inline void iui_path_curve_to(iui_path_state_t *p,
                                     float x1,
                                     float y1,
                                     float x2,
                                     float y2,
                                     float x3,
                                     float y3)
{
    float p0x = p->pen_x, p0y = p->pen_y;
    float p1x = x1, p1y = y1;
    float p2x = x2, p2y = y2;
    float p3x = x3, p3y = y3;

    /* Adaptive segments based on curve size (Manhattan distance) */
    int segments = IUI_BEZIER_SEGMENTS(p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y);
    if (segments < 1)
        segments = 1; /* Guard against divide-by-zero */
    float inv_seg = 1.0f / (float) segments;

    for (int i = 1; i <= segments; i++) {
        float t = (float) i * inv_seg;
        float t2 = t * t;
        float t3 = t2 * t;
        float mt = 1.0f - t;
        float mt2 = mt * mt;
        float mt3 = mt2 * mt;

        /* Cubic Bezier formula: B(t) = (1-t)³P0 + 3(1-t)²tP1 + 3(1-t)t²P2 +
         * t³P3
         */
        float px =
            mt3 * p0x + 3.0f * mt2 * t * p1x + 3.0f * mt * t2 * p2x + t3 * p3x;
        float py =
            mt3 * p0y + 3.0f * mt2 * t * p1y + 3.0f * mt * t2 * p2y + t3 * p3y;

        if (p->count < IUI_PORT_MAX_PATH_POINTS) {
            p->points_x[p->count] = px;
            p->points_y[p->count] = py;
            p->count++;
        }
    }

    p->pen_x = p3x;
    p->pen_y = p3y;
}

/* Scaled versions for HiDPI (SDL2 uses these) */

static inline void iui_path_move_to_scaled(iui_path_state_t *p,
                                           float x,
                                           float y,
                                           float scale)
{
    iui_path_move_to(p, x * scale, y * scale);
}

static inline void iui_path_line_to_scaled(iui_path_state_t *p,
                                           float x,
                                           float y,
                                           float scale)
{
    iui_path_line_to(p, x * scale, y * scale);
}

static inline void iui_path_curve_to_scaled(iui_path_state_t *p,
                                            float x1,
                                            float y1,
                                            float x2,
                                            float y2,
                                            float x3,
                                            float y3,
                                            float scale)
{
    /* Scale control points but use unscaled pen position
     * (pen is already in scaled coordinates from previous move/line/curve)
     */
    float p0x = p->pen_x, p0y = p->pen_y;
    float p1x = x1 * scale, p1y = y1 * scale;
    float p2x = x2 * scale, p2y = y2 * scale;
    float p3x = x3 * scale, p3y = y3 * scale;

    int segments = IUI_BEZIER_SEGMENTS(p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y);
    if (segments < 1)
        segments = 1; /* Guard against divide-by-zero */
    float inv_seg = 1.0f / (float) segments;

    for (int i = 1; i <= segments; i++) {
        float t = (float) i * inv_seg;
        float t2 = t * t;
        float t3 = t2 * t;
        float mt = 1.0f - t;
        float mt2 = mt * mt;
        float mt3 = mt2 * mt;

        float px =
            mt3 * p0x + 3.0f * mt2 * t * p1x + 3.0f * mt * t2 * p2x + t3 * p3x;
        float py =
            mt3 * p0y + 3.0f * mt2 * t * p1y + 3.0f * mt * t2 * p2y + t3 * p3y;

        if (p->count < IUI_PORT_MAX_PATH_POINTS) {
            p->points_x[p->count] = px;
            p->points_y[p->count] = py;
            p->count++;
        }
    }

    p->pen_x = p3x;
    p->pen_y = p3y;
}

/* Stroke path with round caps - matches SDL2's geometry-based rendering.
 * Key behaviors to match SDL2:
 * - Minimum stroke width of 1.0px
 * - Consistent 0.5px AA fringe
 * - Round caps at path endpoints
 * - Uses capsule SDF for all segments (consistent AA regardless of angle)
 */
static inline void iui_raster_path_stroke(iui_raster_ctx_t *r,
                                          iui_path_state_t *p,
                                          float width,
                                          uint32_t color)
{
    if (p->count < 2)
        return;

    /* Enforce minimum stroke width like SDL2 */
    if (width < 1.0f)
        width = 1.0f;

    float radius = width * 0.5f;

    /* Draw all segments using capsule SDF for consistent AA.
     * Capsule geometry inherently provides round caps at endpoints,
     * so no explicit cap drawing is needed.
     */
    for (int i = 0; i < p->count - 1; i++) {
        float x0 = p->points_x[i], y0 = p->points_y[i];
        float x1 = p->points_x[i + 1], y1 = p->points_y[i + 1];

        /* Skip degenerate segments (threshold matches iui_raster_capsule) */
        float dx = x1 - x0, dy = y1 - y0;
        if (dx * dx + dy * dy < 0.001f * 0.001f)
            continue;

        iui_raster_capsule(r, x0, y0, x1, y1, radius, color);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* IUI_PORT_SW_H */
