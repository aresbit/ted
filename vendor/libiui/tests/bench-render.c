/*
 * Rendering Benchmark
 *
 * Profiles the rendering pipeline under realistic application workloads.
 * Uses a simulated backend with per-call costs that model real hardware:
 *
 *   draw_box   : pixel fill proportional to area
 *   draw_text  : glyph rasterization (area per character)
 *   text_width : font shaping with glyph-advance + kerning table lookups
 *   set_clip   : GPU scissor state change
 *
 * Scenes mirror real application screens (480x800 mobile form factor):
 *   Settings : scrollable list with toggles, radios, section headers
 *   Dashboard: metric cards, chip filters, progress bars, action buttons
 *   Form     : sliders, checkboxes, buttons with varied label strings
 */

#include "common.h"

#include <time.h>

/* high-resolution timer */
static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

/* benchmark parameters */
#define SCREEN_W 480
#define SCREEN_H 800
#define FRAMES 5000
#define WARMUP 300
#define RUNS 3

/* simulated backend */
static volatile uint64_t g_pixels;
static volatile int g_draws;
static volatile int g_clips;
static volatile int g_tmeas;

static void sim_draw_box(iui_rect_t r, float radius, uint32_t color, void *user)
{
    (void) radius;
    (void) color;
    (void) user;
    uint64_t area = (uint64_t) (r.width > 0 ? r.width : 0) *
                    (uint64_t) (r.height > 0 ? r.height : 0);
    g_pixels += area;
    g_draws++;
}

static void sim_set_clip(uint16_t x0,
                         uint16_t y0,
                         uint16_t x1,
                         uint16_t y1,
                         void *user)
{
    (void) x0;
    (void) y0;
    (void) x1;
    (void) y1;
    (void) user;
    g_clips++;
}

static void sim_draw_text(float x,
                          float y,
                          const char *text,
                          uint32_t color,
                          void *user)
{
    (void) x;
    (void) y;
    (void) color;
    (void) user;
    g_pixels += (uint64_t) strlen(text) * 80;
    g_draws++;
}

/* Font shaping simulation.
 *
 * Models per-glyph work that a real font backend performs:
 * glyph-advance table lookup + kerning pair probe. The volatile tables prevent
 * the compiler from constant-folding the result, making each call cost ~2
 * cache-line touches per character.
 */
static volatile float g_glyph_advances[256];
static volatile uint8_t g_kern_table[256];

static void init_font_tables(void)
{
    for (int i = 0; i < 256; i++) {
        g_glyph_advances[i] = 6.f + (float) (i % 5);
        g_kern_table[i] = (uint8_t) (i * 31 + 17);
    }
}

static float sim_text_width(const char *text, void *user)
{
    (void) user;
    g_tmeas++;
    float w = 0.f;
    uint8_t prev = 0;
    for (const unsigned char *p = (const unsigned char *) text; *p; p++) {
        w += g_glyph_advances[*p];
        uint8_t pair = (uint8_t) (prev ^ *p);
        w += (float) g_kern_table[pair] * 0.01f;
        prev = *p;
    }
    return w;
}

/* scenes */

typedef void (*scene_fn)(iui_context *);

/* Scene A: Settings - heavy on text labels and toggles */
static void scene_settings(iui_context *ctx)
{
    static bool toggles[16];
    static int radio_val = 1;

    iui_begin_frame(ctx, 0.016f);
    iui_begin_window(ctx, "settings", 0, 0, SCREEN_W, SCREEN_H, 0);

    iui_text(ctx, IUI_ALIGN_LEFT, "General Settings");
    iui_divider(ctx);
    for (int i = 0; i < 8; i++) {
        char label[48];
        snprintf(label, sizeof(label), "Enable feature %d (recommended)",
                 i + 1);
        iui_checkbox(ctx, label, &toggles[i]);
    }

    iui_text(ctx, IUI_ALIGN_LEFT, "Display");
    iui_divider(ctx);
    for (int i = 8; i < 16; i++) {
        char label[48];
        snprintf(label, sizeof(label), "Display option %d", i - 7);
        iui_checkbox(ctx, label, &toggles[i]);
    }

    iui_text(ctx, IUI_ALIGN_LEFT, "Theme");
    iui_divider(ctx);
    static const char *themes[] = {"Light", "Dark", "System default",
                                   "High contrast"};
    for (int i = 0; i < 4; i++)
        iui_radio(ctx, themes[i], &radio_val, i);

    iui_text(ctx, IUI_ALIGN_LEFT, "About this application");
    iui_divider(ctx);
    iui_text(ctx, IUI_ALIGN_LEFT, "Version 1.4.2 (build 2891)");
    iui_text(ctx, IUI_ALIGN_LEFT,
             "Copyright 2024-2026 Example Corporation. All rights reserved.");

    iui_button(ctx, "Check for updates", IUI_ALIGN_CENTER);
    iui_button(ctx, "Reset all settings", IUI_ALIGN_CENTER);

    iui_end_window(ctx);
    iui_end_frame(ctx);
}

/* Scene B: Dashboard - cards, chips, progress, mixed widgets */
static void scene_dashboard(iui_context *ctx)
{
    static bool filters[6] = {true, false, true, false, true, false};

    iui_begin_frame(ctx, 0.016f);
    iui_begin_window(ctx, "dash", 0, 0, SCREEN_W, SCREEN_H, 0);

    /* Filter chip bar */
    iui_chip_filter(ctx, "All items", &filters[0]);
    iui_chip_filter(ctx, "Active", &filters[1]);
    iui_chip_filter(ctx, "Pending review", &filters[2]);
    iui_chip_filter(ctx, "Archived", &filters[3]);
    iui_chip_filter(ctx, "Starred", &filters[4]);
    iui_chip_filter(ctx, "Flagged", &filters[5]);

    /* Metric cards */
    for (int i = 0; i < 6; i++) {
        float cx = (float) (i % 2) * (SCREEN_W / 2);
        float cy = 80.f + (float) (i / 2) * 150.f;
        iui_card_begin(ctx, cx, cy, SCREEN_W / 2 - 8, 140, IUI_CARD_ELEVATED);

        char title[32];
        snprintf(title, sizeof(title), "Metric %c", 'A' + i);
        iui_text(ctx, IUI_ALIGN_LEFT, title);

        float pct = 0.15f * (float) (i + 1);
        iui_progress_linear(ctx, pct, 1.f, false);

        char detail[64];
        snprintf(detail, sizeof(detail), "%.0f%% complete - %d of %d items",
                 pct * 100, (int) (pct * 120), 120);
        iui_text(ctx, IUI_ALIGN_LEFT, detail);

        iui_card_end(ctx);
    }

    /* Action row */
    iui_button(ctx, "Export Report", IUI_ALIGN_CENTER);
    iui_button(ctx, "Refresh Data", IUI_ALIGN_CENTER);
    iui_button(ctx, "Share Dashboard", IUI_ALIGN_CENTER);
    iui_button(ctx, "Print Summary", IUI_ALIGN_CENTER);

    iui_end_window(ctx);
    iui_end_frame(ctx);
}

/* Scene C: Form - sliders, checkboxes, buttons with varied labels */
static void scene_form(iui_context *ctx)
{
    static float sliders[8] = {0.3f, 0.5f, 0.7f, 0.1f, 0.9f, 0.4f, 0.6f, 0.2f};
    static bool checks[8];

    iui_begin_frame(ctx, 0.016f);
    iui_begin_window(ctx, "form", 0, 0, SCREEN_W, SCREEN_H, 0);

    iui_text(ctx, IUI_ALIGN_LEFT, "Image Processing Configuration");
    iui_divider(ctx);

    static const char *slider_labels[] = {
        "Brightness",    "Contrast",    "Saturation", "Sharpness",
        "Exposure bias", "Gamma curve", "Highlights", "Shadows",
    };
    for (int i = 0; i < 8; i++)
        iui_slider(ctx, slider_labels[i], 0.f, 1.f, 0.01f, &sliders[i], NULL);

    iui_text(ctx, IUI_ALIGN_LEFT, "Processing Options");
    iui_divider(ctx);

    static const char *check_labels[] = {
        "Auto white balance",       "Noise reduction (luminance)",
        "HDR tone mapping",         "Lens distortion correction",
        "Chromatic aberration fix", "Vignette removal",
        "Perspective correction",   "Auto-crop to content",
    };
    for (int i = 0; i < 8; i++)
        iui_checkbox(ctx, check_labels[i], &checks[i]);

    iui_text(ctx, IUI_ALIGN_LEFT, "Output");
    iui_divider(ctx);
    iui_text(ctx, IUI_ALIGN_LEFT,
             "Processed images will be saved to the output directory.");

    iui_button(ctx, "Apply Changes", IUI_ALIGN_CENTER);
    iui_button(ctx, "Reset to Defaults", IUI_ALIGN_CENTER);
    iui_button(ctx, "Preview Result", IUI_ALIGN_CENTER);
    iui_button(ctx, "Cancel", IUI_ALIGN_CENTER);

    iui_end_window(ctx);
    iui_end_frame(ctx);
}

/* Scene D: Dialog - small overlay window (partial-screen update).
 * This is the primary use-case for ink-bounds: only the dialog region (roughly
 * 60% x 30% of screen) needs to be blitted to the display, saving ~80% of the
 * framebuffer transfer cost.
 */
static void scene_dialog(iui_context *ctx)
{
    iui_begin_frame(ctx, 0.016f);

    /* Dialog overlay: centered, 280x240 on 480x800 screen */
    float dw = 280, dh = 240;
    float dx = (SCREEN_W - dw) * 0.5f;
    float dy = (SCREEN_H - dh) * 0.5f;
    iui_begin_window(ctx, "dialog", dx, dy, dw, dh, 0);

    iui_text(ctx, IUI_ALIGN_LEFT, "Confirm Action");
    iui_divider(ctx);
    iui_text(ctx, IUI_ALIGN_LEFT, "Are you sure you want to proceed?");
    iui_text(ctx, IUI_ALIGN_LEFT, "This action cannot be undone.");

    iui_button(ctx, "Confirm", IUI_ALIGN_CENTER);
    iui_button(ctx, "Cancel", IUI_ALIGN_CENTER);

    iui_end_window(ctx);
    iui_end_frame(ctx);
}

/* bench harness */
typedef struct {
    double us_per_frame;
    int draws_per_frame;
    int clips_per_frame;
    int tmeas_per_frame;
} bench_result_t;

/* Run benchmark, return median of RUNS iterations */
static bench_result_t bench_run(iui_context *ctx, scene_fn fn)
{
    double results[RUNS];

    for (int run = 0; run < RUNS; run++) {
        for (int i = 0; i < WARMUP; i++)
            fn(ctx);

        g_pixels = 0;
        g_draws = 0;
        g_clips = 0;
        g_tmeas = 0;

        uint64_t start = now_ns();
        for (int i = 0; i < FRAMES; i++)
            fn(ctx);
        uint64_t elapsed = now_ns() - start;
        results[run] = (double) elapsed / (double) FRAMES / 1e3;
    }

    /* Simple sort for median */
    for (int i = 0; i < RUNS - 1; i++)
        for (int j = i + 1; j < RUNS; j++)
            if (results[j] < results[i]) {
                double tmp = results[i];
                results[i] = results[j];
                results[j] = tmp;
            }

    bench_result_t r;
    r.us_per_frame = results[RUNS / 2]; /* median */
    r.draws_per_frame = g_draws / FRAMES;
    r.clips_per_frame = g_clips / FRAMES;
    r.tmeas_per_frame = g_tmeas / FRAMES;
    return r;
}

static void print_header(const char *scene)
{
    printf("\n  %s\n  ", scene);
    for (int i = 0; scene[i]; i++)
        putchar('-');
    putchar('\n');
}

static void print_row(const char *label, bench_result_t r)
{
    printf("  %-36s %6.1f us/frame  %3d draws  %3d clips  %3d tmeas\n", label,
           r.us_per_frame, r.draws_per_frame, r.clips_per_frame,
           r.tmeas_per_frame);
}

static void print_pct(const char *label, double base_us, double test_us)
{
    double pct = (test_us - base_us) / base_us * 100.0;
    printf("    %-32s %+6.1f%%\n", label, pct);
}

/* per-scene benchmark */
static void bench_scene(const char *title, scene_fn fn, iui_context *ctx)
{
    print_header(title);

    iui_ink_bounds_enable(ctx, false);
    iui_text_cache_enable(ctx, false);
    bench_result_t base = bench_run(ctx, fn);
    print_row("baseline (all opts OFF)", base);

    iui_ink_bounds_enable(ctx, true);
    iui_text_cache_enable(ctx, false);
    bench_result_t ink = bench_run(ctx, fn);
    print_row("ink-bounds ON", ink);

    iui_ink_bounds_enable(ctx, false);
    iui_text_cache_enable(ctx, true);
    bench_result_t tc = bench_run(ctx, fn);
    print_row("text-cache ON", tc);

    iui_ink_bounds_enable(ctx, true);
    iui_text_cache_enable(ctx, true);
    bench_result_t both = bench_run(ctx, fn);
    print_row("all opts ON", both);

    printf("\n");
    print_pct("ink-bounds overhead:", base.us_per_frame, ink.us_per_frame);
    print_pct("text-cache effect:", base.us_per_frame, tc.us_per_frame);
    print_pct("combined:", base.us_per_frame, both.us_per_frame);
}

/* ink-bounds & text cache analysis */
static void analyze_ink_bounds(iui_context *ctx, scene_fn fn, const char *name)
{
    iui_ink_bounds_enable(ctx, true);
    fn(ctx);
    iui_rect_t b;
    float total = (float) SCREEN_W * (float) SCREEN_H;
    if (iui_ink_bounds_get(ctx, &b)) {
        float dirty = b.width * b.height;
        float pct = dirty / total * 100.f;
        printf("    %-18s (%.0f,%.0f) %.0fx%.0f = %5.1f%%", name, b.x, b.y,
               b.width, b.height, pct);
        if (pct < 99.f)
            printf(" -> %.1f%% blit saved", 100.f - pct);
        printf("\n");
    }
    iui_ink_bounds_enable(ctx, false);
}

static void analyze_text_cache(iui_context *ctx, scene_fn fn, const char *name)
{
    /* Measure WITHOUT cache: count raw text_width calls */
    iui_text_cache_enable(ctx, false);
    g_tmeas = 0;
    for (int i = 0; i < 100; i++)
        fn(ctx);
    int raw_calls = g_tmeas;

    /* Measure WITH cache: count backend calls that survive */
    iui_text_cache_enable(ctx, true);
    iui_text_cache_clear(ctx);
    g_tmeas = 0;
    for (int i = 0; i < 100; i++)
        fn(ctx);
    int cached_calls = g_tmeas;

    int hits, misses;
    iui_text_cache_stats(ctx, &hits, &misses);
    int total = hits + misses;
    float rate = total > 0 ? (float) hits / (float) total * 100.f : 0.f;

    int saved = raw_calls - cached_calls;
    float save_pct =
        raw_calls > 0 ? (float) saved / (float) raw_calls * 100.f : 0.f;

    printf(
        "    %-18s %4d calls/frame -> %d with cache (%.1f%% eliminated, "
        "%.1f%% hit rate)\n",
        name, raw_calls / 100, cached_calls / 100, save_pct, rate);
    iui_text_cache_enable(ctx, false);
}

int main(void)
{
    init_font_tables();

    static union {
        uint8_t buf[65536];
        void *align;
    } mem;

    iui_renderer_t renderer = {
        .draw_box = sim_draw_box,
        .set_clip_rect = sim_set_clip,
        .text_width = sim_text_width,
        .draw_text = sim_draw_text,
    };
    iui_config_t cfg = iui_make_config(mem.buf, renderer, 14.f, NULL);
    iui_context *ctx = iui_init(&cfg);
    if (!ctx) {
        fprintf(stderr, "iui_init failed\n");
        return 1;
    }

    printf(
        "==================================================================\n");
    printf(" libiui Rendering Benchmark\n");
    printf(" Screen: %dx%d  Frames: %d  Runs: %d (median)\n", SCREEN_W,
           SCREEN_H, FRAMES, RUNS);
    printf(
        "==================================================================\n");

    bench_scene("Settings (16 toggles, 4 radios, labels)", scene_settings, ctx);
    bench_scene("Dashboard (6 cards, 6 chips, 4 buttons)", scene_dashboard,
                ctx);
    bench_scene("Form (8 sliders, 8 checks, 4 buttons)", scene_form, ctx);
    bench_scene("Dialog (confirm overlay, partial screen)", scene_dialog, ctx);

    /* Ink-bounds coverage */
    print_header("Ink-Bounds Coverage");
    analyze_ink_bounds(ctx, scene_settings, "Settings");
    analyze_ink_bounds(ctx, scene_dashboard, "Dashboard");
    analyze_ink_bounds(ctx, scene_form, "Form");
    analyze_ink_bounds(ctx, scene_dialog, "Dialog");

    /* Text cache effectiveness */
    print_header("Text Cache Effectiveness (100 frames)");
    analyze_text_cache(ctx, scene_settings, "Settings");
    analyze_text_cache(ctx, scene_dashboard, "Dashboard");
    analyze_text_cache(ctx, scene_form, "Form");
    analyze_text_cache(ctx, scene_dialog, "Dialog");

    /* Backend cost model: estimates real-world savings from ink-bounds
     * partial blit + text cache.  Based on measured pixel counts and
     * text_width call counts from the scenes above.
     */
    print_header("Estimated Backend Savings");
    {
        const double blit_us_per_pixel = 1.0 / 2.5; /* 0.4 us per pixel */
        const double tw_us_per_call = 5.0;          /* 5 us per text_width */
        double full_blit_us =
            (double) SCREEN_W * (double) SCREEN_H * blit_us_per_pixel;

        scene_fn scenes[] = {scene_settings, scene_dashboard, scene_form,
                             scene_dialog};
        const char *names[] = {"Settings", "Dashboard", "Form", "Dialog"};

        for (int s = 0; s < 4; s++) {
            /* Measure ink-bounds region */
            iui_ink_bounds_enable(ctx, true);
            iui_text_cache_enable(ctx, false);
            g_tmeas = 0;
            scenes[s](ctx);
            int raw_tw = g_tmeas;

            iui_rect_t bounds;
            double partial_blit_us = full_blit_us;
            float coverage = 100.f;
            if (iui_ink_bounds_get(ctx, &bounds)) {
                double dirty = (double) bounds.width * (double) bounds.height;
                partial_blit_us = dirty * blit_us_per_pixel;
                coverage =
                    (float) (dirty / ((double) SCREEN_W * (double) SCREEN_H) *
                             100.0);
            }

            /* Measure cached text_width calls */
            iui_text_cache_enable(ctx, true);
            iui_text_cache_clear(ctx);
            g_tmeas = 0;
            scenes[s](ctx); /* prime cache */
            g_tmeas = 0;
            scenes[s](ctx); /* steady state */
            int cached_tw = g_tmeas;
            iui_ink_bounds_enable(ctx, false);
            iui_text_cache_enable(ctx, false);

            double blit_save_us = full_blit_us - partial_blit_us;
            double tw_save_us = (double) (raw_tw - cached_tw) * tw_us_per_call;
            double total_save_us = blit_save_us + tw_save_us;
            double baseline_us =
                full_blit_us + (double) raw_tw * tw_us_per_call;
            double save_pct =
                baseline_us > 0 ? total_save_us / baseline_us * 100.0 : 0.0;

            printf("    %-18s blit: %.0f -> %.0f us (%.0f%% coverage)\n",
                   names[s], full_blit_us, partial_blit_us, (double) coverage);
            printf("    %-18s text_width: %d -> %d calls (%.0f us saved)\n", "",
                   raw_tw, cached_tw, tw_save_us);
            printf("    %-18s total: %.0f us saved/frame (%.1f%% of backend)\n",
                   "", total_save_us, save_pct);
            if (s < 3)
                printf("\n");
        }
    }

    printf(
        "\n=================================================================="
        "\n");
    return 0;
}
