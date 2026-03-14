/*
 * md3-validate.h - MD3 Specification Validation
 *
 * Stateless validation functions for Material Design 3 compliance.
 * These functions are used by tests (test-md3-gen.inc) to verify
 * that component dimensions meet MD3 specifications.
 *
 * Usage:
 *   #include "md3-validate.h"
 *   md3_violation_t v = md3_check_button(height, scale);
 *
 * Extended validators are generated from DSL: see md3-validate-gen.inc
 */

#ifndef IUI_MD3_VALIDATE_H
#define IUI_MD3_VALIDATE_H

#include "iui-spec.h"

/* Violation flags enum - generated from scripts/gen-md3-validate.py */
#include "md3-flags-gen.inc"

/* Helper: dp to px with scale factor */
static inline int md3_dp_to_px(float scale, float dp)
{
    if (scale <= 0.f || dp <= 0.f)
        return 0;
    int px = (int) (dp * scale + 0.5f);
    return (px < 1) ? 1 : px;
}

/* Helper: round pixel dimension to nearest int */
static inline int md3_round_px(float px)
{
    if (px <= 0.f)
        return 0;
    return (int) (px + 0.5f);
}

/* Helper: check 4dp grid alignment */
static inline md3_violation_t md3_check_grid_align(int px, float scale)
{
    int grid_unit = md3_dp_to_px(scale, 4.f);
    if (grid_unit <= 0)
        return MD3_OK; /* Avoid division by zero */
    return (px % grid_unit != 0) ? MD3_GRID_ALIGN : MD3_OK;
}

/* Helper: check minimum touch target (48dp) */
static inline md3_violation_t md3_check_touch_target(int w_px,
                                                     int h_px,
                                                     float scale)
{
    int min_target = md3_dp_to_px(scale, 48.f);
    if (w_px < min_target || h_px < min_target)
        return MD3_TOUCH_TARGET;
    return MD3_OK;
}

/* Component validators - return bitmask, no side effects */

/* Button: 40dp height */
static inline md3_violation_t md3_check_button(int height_px, float scale)
{
    int min_h = md3_dp_to_px(scale, IUI_BUTTON_HEIGHT);
    return (height_px < min_h) ? MD3_HEIGHT_LOW : MD3_OK;
}

/* FAB Standard: 56dp size (exact, ±1px tolerance) */
static inline md3_violation_t md3_check_fab(int size_px, float scale)
{
    int expected = md3_dp_to_px(scale, IUI_FAB_SIZE);
    /* Allow 1px tolerance for rounding */
    int diff = size_px - expected;
    if (diff < -1 || diff > 1)
        return MD3_SIZE_MISMATCH;
    return MD3_OK;
}

/* FAB Large: 96dp size (exact, ±1px tolerance) */
static inline md3_violation_t md3_check_fab_large(int size_px, float scale)
{
    int expected = md3_dp_to_px(scale, IUI_FAB_LARGE_SIZE);
    int diff = size_px - expected;
    if (diff < -1 || diff > 1)
        return MD3_SIZE_MISMATCH;
    return MD3_OK;
}

/* Chip: 32dp height */
static inline md3_violation_t md3_check_chip(int height_px, float scale)
{
    int min_h = md3_dp_to_px(scale, IUI_CHIP_HEIGHT);
    return (height_px < min_h) ? MD3_HEIGHT_LOW : MD3_OK;
}

/* TextField: 56dp height */
static inline md3_violation_t md3_check_textfield(int height_px, float scale)
{
    int min_h = md3_dp_to_px(scale, IUI_TEXTFIELD_HEIGHT);
    return (height_px < min_h) ? MD3_HEIGHT_LOW : MD3_OK;
}

/* State layer opacity validation (requires iui_state_t from iui.h) */
#ifdef IUI_H_
static inline md3_violation_t md3_check_state_alpha(uint8_t alpha,
                                                    iui_state_t state)
{
    uint8_t expected;
    switch (state) {
    case IUI_STATE_HOVERED:
        expected = IUI_STATE_HOVER_ALPHA;
        break;
    case IUI_STATE_PRESSED:
        expected = IUI_STATE_PRESS_ALPHA;
        break;
    case IUI_STATE_FOCUSED:
        expected = IUI_STATE_FOCUS_ALPHA;
        break;
    case IUI_STATE_DRAGGED:
        expected = IUI_STATE_DRAG_ALPHA;
        break;
    case IUI_STATE_DISABLED:
        expected = IUI_STATE_DISABLE_ALPHA;
        break;
    default:
        return MD3_OK; /* Default/none state has no required alpha */
    }
    return (alpha != expected) ? MD3_STATE_OPACITY : MD3_OK;
}
#endif

/* Debug reporting - enable with MD3_VALIDATION_VERBOSE for test diagnostics */
#ifdef MD3_VALIDATION_VERBOSE
#include <stdio.h>
static inline void md3_report(const char *component, md3_violation_t v)
{
    if (v == MD3_OK)
        return;
    fprintf(stderr, "[MD3] %s:", component);
    if (v & MD3_HEIGHT_LOW)
        fprintf(stderr, " HEIGHT_LOW");
    if (v & MD3_SIZE_MISMATCH)
        fprintf(stderr, " SIZE_MISMATCH");
    if (v & MD3_TOUCH_TARGET)
        fprintf(stderr, " TOUCH_TARGET");
    if (v & MD3_GRID_ALIGN)
        fprintf(stderr, " GRID_ALIGN");
    if (v & MD3_STATE_OPACITY)
        fprintf(stderr, " STATE_OPACITY");
    if (v & MD3_CORNER_RADIUS)
        fprintf(stderr, " CORNER_RADIUS");
    if (v & MD3_THUMB_SIZE)
        fprintf(stderr, " THUMB_SIZE");
    if (v & MD3_ICON_SIZE)
        fprintf(stderr, " ICON_SIZE");
    if (v & MD3_PADDING)
        fprintf(stderr, " PADDING");
    if (v & MD3_GAP)
        fprintf(stderr, " GAP");
    if (v & MD3_INDICATOR)
        fprintf(stderr, " INDICATOR");
    if (v & MD3_WIDTH_LOW)
        fprintf(stderr, " WIDTH_LOW");
    fprintf(stderr, "\n");
}
#else
#define md3_report(component, v) ((void) 0)
#endif

/* Include extended validators generated from DSL */
#include "md3-validate-gen.inc"

#endif /* IUI_MD3_VALIDATE_H */
