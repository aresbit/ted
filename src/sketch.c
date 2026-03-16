/**
 * sketch.c - gesture-to-shape canvas for TED
 *
 * The fitting core uses least-squares objectives in a visually corrected
 * coordinate space (terminal rows are scaled by cell aspect ratio).
 */

#include "ted.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SKETCH_MAX_POINTS 512
#define SKETCH_MAX_SHAPES 128
#define SKETCH_ASPECT_Y 2.0

typedef struct {
    double x;
    double y;
} sketch_point_t;

typedef struct {
    sketch_shape_kind_t kind;
    double score;
    double cx;
    double cy;
    double angle;
    double rx;
    double ry;
    double x1;
    double y1;
    double x2;
    double y2;
} sketch_shape_t;

typedef struct {
    bool enabled;
    bool stroke_active;
    sketch_shape_kind_t preferred_kind;
    sketch_point_t stroke[SKETCH_MAX_POINTS];
    u32 stroke_count;
    sketch_shape_t shapes[SKETCH_MAX_SHAPES];
    u32 shape_count;
    sketch_shape_t preview;
    bool has_preview;
} sketch_state_t;

static sketch_state_t G = {0};
static bool fit_with_recognizers(const sketch_point_t *pts, u32 n, sketch_shape_t *out);

static double sqr(double v) { return v * v; }

static double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

static const c8 *kind_name(sketch_shape_kind_t kind) {
    switch (kind) {
    case SKETCH_SHAPE_AUTO: return "auto";
    case SKETCH_SHAPE_LINE: return "line";
    case SKETCH_SHAPE_RECT: return "rect";
    case SKETCH_SHAPE_SQUARE: return "square";
    case SKETCH_SHAPE_ELLIPSE: return "ellipse";
    case SKETCH_SHAPE_CIRCLE: return "circle";
    default: return "none";
    }
}

static double point_dist(sketch_point_t a, sketch_point_t b) {
    return sqrt(sqr(a.x - b.x) + sqr(a.y - b.y));
}

static bool solve_2x2(double a00, double a01, double a10, double a11,
                      double b0, double b1, double *x0, double *x1) {
    double det = a00 * a11 - a01 * a10;
    if (fabs(det) < 1e-9) return false;
    *x0 = (b0 * a11 - a01 * b1) / det;
    *x1 = (a00 * b1 - b0 * a10) / det;
    return true;
}

static bool solve_3x3(double a[3][3], double b[3], double x[3]) {
    double m[3][4];
    for (u32 r = 0; r < 3; r++) {
        for (u32 c = 0; c < 3; c++) m[r][c] = a[r][c];
        m[r][3] = b[r];
    }

    for (u32 col = 0; col < 3; col++) {
        u32 pivot = col;
        for (u32 r = col + 1; r < 3; r++) {
            if (fabs(m[r][col]) > fabs(m[pivot][col])) pivot = r;
        }
        if (fabs(m[pivot][col]) < 1e-9) return false;
        if (pivot != col) {
            for (u32 c = col; c < 4; c++) {
                double tmp = m[col][c];
                m[col][c] = m[pivot][c];
                m[pivot][c] = tmp;
            }
        }
        double div = m[col][col];
        for (u32 c = col; c < 4; c++) m[col][c] /= div;
        for (u32 r = 0; r < 3; r++) {
            if (r == col) continue;
            double factor = m[r][col];
            for (u32 c = col; c < 4; c++) {
                m[r][c] -= factor * m[col][c];
            }
        }
    }

    for (u32 i = 0; i < 3; i++) x[i] = m[i][3];
    return true;
}

static bool json_find_number(sp_str_t json, const c8 *key, double *out) {
    if (!json.data || json.len == 0) return false;
    u32 key_len = (u32)strlen(key);
    if (key_len == 0 || key_len > json.len) return false;

    const char *p = NULL;
    for (u32 i = 0; i + key_len <= json.len; i++) {
        if (memcmp(json.data + i, key, key_len) == 0) {
            p = json.data + i;
            break;
        }
    }
    if (!p) return false;
    p += key_len;
    const char *json_end = json.data + json.len;
    while (p < json_end && (*p == ' ' || *p == ':')) p++;
    if (p >= json_end) return false;

    u32 rem = (u32)(json_end - p);
    c8 num_buf[64];
    u32 n = rem < sizeof(num_buf) - 1 ? rem : sizeof(num_buf) - 1;
    memcpy(num_buf, p, n);
    num_buf[n] = '\0';

    char *end = NULL;
    double v = strtod(num_buf, &end);
    if (end == num_buf) return false;
    *out = v;
    return true;
}

static bool json_find_string(sp_str_t json, const c8 *key, c8 *buf, u32 buf_size) {
    if (!json.data || json.len == 0 || buf_size == 0) return false;
    u32 key_len = (u32)strlen(key);
    if (key_len == 0 || key_len > json.len) return false;

    const char *p = NULL;
    for (u32 i = 0; i + key_len <= json.len; i++) {
        if (memcmp(json.data + i, key, key_len) == 0) {
            p = json.data + i;
            break;
        }
    }
    if (!p) return false;
    p += key_len;
    const char *json_end = json.data + json.len;
    while (p < json_end && *p != '"') p++;
    if (p >= json_end || *p != '"') return false;
    p++;
    u32 i = 0;
    while (p < json_end && *p != '"' && i + 1 < buf_size) {
        buf[i++] = *p++;
    }
    buf[i] = '\0';
    return i > 0;
}

static void compute_mean_cov(const sketch_point_t *pts, u32 n,
                             double *mx, double *my,
                             double *sxx, double *sxy, double *syy) {
    *mx = 0.0;
    *my = 0.0;
    for (u32 i = 0; i < n; i++) {
        *mx += pts[i].x;
        *my += pts[i].y;
    }
    *mx /= (double)n;
    *my /= (double)n;

    *sxx = 0.0;
    *sxy = 0.0;
    *syy = 0.0;
    for (u32 i = 0; i < n; i++) {
        double dx = pts[i].x - *mx;
        double dy = pts[i].y - *my;
        *sxx += dx * dx;
        *sxy += dx * dy;
        *syy += dy * dy;
    }
    *sxx /= (double)n;
    *sxy /= (double)n;
    *syy /= (double)n;
}

static double principal_angle(double sxx, double sxy, double syy) {
    return 0.5 * atan2(2.0 * sxy, sxx - syy);
}

static void rotate_into_frame(const sketch_point_t *pts, u32 n, double cx, double cy,
                              double angle, double *umin, double *umax,
                              double *vmin, double *vmax, double *mean_r) {
    double c = cos(angle);
    double s = sin(angle);
    *umin = DBL_MAX;
    *umax = -DBL_MAX;
    *vmin = DBL_MAX;
    *vmax = -DBL_MAX;
    *mean_r = 0.0;
    for (u32 i = 0; i < n; i++) {
        double dx = pts[i].x - cx;
        double dy = pts[i].y - cy;
        double u = c * dx + s * dy;
        double v = -s * dx + c * dy;
        if (u < *umin) *umin = u;
        if (u > *umax) *umax = u;
        if (v < *vmin) *vmin = v;
        if (v > *vmax) *vmax = v;
        *mean_r += sqrt(u * u + v * v);
    }
    *mean_r /= (double)n;
}

static double stroke_gap_penalty(const sketch_point_t *pts, u32 n, double span) {
    if (n < 2 || span < 1e-6) return 1.0;
    double gap = point_dist(pts[0], pts[n - 1]) / span;
    return clamp01(gap);
}

static bool fit_line(const sketch_point_t *pts, u32 n, sketch_shape_t *out) {
    double mx, my, sxx, sxy, syy;
    compute_mean_cov(pts, n, &mx, &my, &sxx, &sxy, &syy);
    double angle = principal_angle(sxx, sxy, syy);
    double c = cos(angle);
    double s = sin(angle);
    double tmin = DBL_MAX;
    double tmax = -DBL_MAX;
    double mse = 0.0;

    for (u32 i = 0; i < n; i++) {
        double dx = pts[i].x - mx;
        double dy = pts[i].y - my;
        double t = c * dx + s * dy;
        double o = -s * dx + c * dy;
        if (t < tmin) tmin = t;
        if (t > tmax) tmax = t;
        mse += o * o;
    }
    mse /= (double)n;

    out->kind = SKETCH_SHAPE_LINE;
    out->score = mse;
    out->cx = mx;
    out->cy = my;
    out->angle = angle;
    out->x1 = mx + c * tmin;
    out->y1 = my + s * tmin;
    out->x2 = mx + c * tmax;
    out->y2 = my + s * tmax;
    out->rx = fabs(tmax - tmin) * 0.5;
    out->ry = 0.0;
    return true;
}

static void finalize_box_shape(sketch_shape_t *out, sketch_shape_kind_t kind,
                               double cx, double cy, double angle,
                               double hx, double hy, double mse) {
    out->kind = kind;
    out->score = mse;
    out->cx = cx;
    out->cy = cy;
    out->angle = angle;
    out->rx = hx;
    out->ry = hy;
    out->x1 = cx;
    out->y1 = cy;
    out->x2 = cx;
    out->y2 = cy;
}

static bool fit_rect_like(const sketch_point_t *pts, u32 n, bool square, sketch_shape_t *out) {
    double mx, my, sxx, sxy, syy;
    compute_mean_cov(pts, n, &mx, &my, &sxx, &sxy, &syy);
    double angle = principal_angle(sxx, sxy, syy);
    double umin, umax, vmin, vmax, mean_r;
    rotate_into_frame(pts, n, mx, my, angle, &umin, &umax, &vmin, &vmax, &mean_r);

    double hx = fmax(fabs(umin), fabs(umax));
    double hy = fmax(fabs(vmin), fabs(vmax));
    if (square) {
        double h = 0.5 * (hx + hy);
        hx = h;
        hy = h;
    }
    if (hx < 1e-6 || hy < 1e-6) return false;

    double c = cos(angle);
    double s = sin(angle);
    double mse = 0.0;
    for (u32 i = 0; i < n; i++) {
        double dx = pts[i].x - mx;
        double dy = pts[i].y - my;
        double u = c * dx + s * dy;
        double v = -s * dx + c * dy;
        double du = fabs(fabs(u) - hx);
        double dv = fabs(fabs(v) - hy);
        double edge = du < dv ? du : dv;
        mse += edge * edge;
    }
    mse /= (double)n;

    finalize_box_shape(out,
                       square ? SKETCH_SHAPE_SQUARE : SKETCH_SHAPE_RECT,
                       mx, my, angle, hx, hy, mse);
    return true;
}

static bool fit_ellipse_like(const sketch_point_t *pts, u32 n, bool circle, sketch_shape_t *out) {
    double mx, my, sxx, sxy, syy;
    compute_mean_cov(pts, n, &mx, &my, &sxx, &sxy, &syy);
    double angle = principal_angle(sxx, sxy, syy);
    double c = cos(angle);
    double s = sin(angle);

    if (circle) {
        double ata[3][3] = {{0}};
        double atb[3] = {0};
        for (u32 i = 0; i < n; i++) {
            double x = pts[i].x;
            double y = pts[i].y;
            double row[3] = { x, y, 1.0 };
            double rhs = -(x * x + y * y);
            for (u32 r = 0; r < 3; r++) {
                atb[r] += row[r] * rhs;
                for (u32 col = 0; col < 3; col++) {
                    ata[r][col] += row[r] * row[col];
                }
            }
        }
        double sol[3];
        if (!solve_3x3(ata, atb, sol)) return false;
        double cx = -0.5 * sol[0];
        double cy = -0.5 * sol[1];
        double r2 = cx * cx + cy * cy - sol[2];
        if (r2 <= 1e-6) return false;
        double radius = sqrt(r2);
        double mse = 0.0;
        for (u32 i = 0; i < n; i++) {
            double d = point_dist((sketch_point_t){cx, cy}, pts[i]) - radius;
            mse += d * d;
        }
        mse /= (double)n;
        out->kind = SKETCH_SHAPE_CIRCLE;
        out->score = mse;
        out->cx = cx;
        out->cy = cy;
        out->angle = 0.0;
        out->rx = radius;
        out->ry = radius;
        out->x1 = cx;
        out->y1 = cy;
        out->x2 = cx;
        out->y2 = cy;
        return true;
    }

    double uu = 0.0, vv = 0.0, uv = 0.0, ub = 0.0, vb = 0.0;
    for (u32 i = 0; i < n; i++) {
        double dx = pts[i].x - mx;
        double dy = pts[i].y - my;
        double u = c * dx + s * dy;
        double v = -s * dx + c * dy;
        double u2 = u * u;
        double v2 = v * v;
        uu += u2 * u2;
        vv += v2 * v2;
        uv += u2 * v2;
        ub += u2;
        vb += v2;
    }

    double a = 0.0;
    double b = 0.0;
    if (!solve_2x2(uu, uv, uv, vv, ub, vb, &a, &b)) return false;
    if (a <= 1e-9 || b <= 1e-9) return false;

    double rx = sqrt(1.0 / a);
    double ry = sqrt(1.0 / b);
    if (rx < 1e-6 || ry < 1e-6) return false;

    double mse = 0.0;
    for (u32 i = 0; i < n; i++) {
        double dx = pts[i].x - mx;
        double dy = pts[i].y - my;
        double u = c * dx + s * dy;
        double v = -s * dx + c * dy;
        double residual = (u * u) / (rx * rx) + (v * v) / (ry * ry) - 1.0;
        mse += residual * residual;
    }
    mse /= (double)n;

    out->kind = SKETCH_SHAPE_ELLIPSE;
    out->score = mse;
    out->cx = mx;
    out->cy = my;
    out->angle = angle;
    out->rx = rx;
    out->ry = ry;
    out->x1 = mx;
    out->y1 = my;
    out->x2 = mx;
    out->y2 = my;
    return true;
}

static sketch_shape_t choose_best_fit(const sketch_point_t *pts, u32 n) {
    sketch_shape_t best = { .kind = SKETCH_SHAPE_NONE, .score = DBL_MAX };
    double mx, my, sxx, sxy, syy;
    compute_mean_cov(pts, n, &mx, &my, &sxx, &sxy, &syy);
    double span = sqrt(fmax(1e-9, sxx + syy)) * 4.0;
    double gap = stroke_gap_penalty(pts, n, span);

    sketch_shape_t cand[5];
    bool ok[5] = {
        fit_line(pts, n, &cand[0]),
        fit_rect_like(pts, n, false, &cand[1]),
        fit_rect_like(pts, n, true, &cand[2]),
        fit_ellipse_like(pts, n, false, &cand[3]),
        fit_ellipse_like(pts, n, true, &cand[4]),
    };

    for (u32 i = 0; i < 5; i++) {
        if (!ok[i]) continue;
        double normalized = cand[i].score / fmax(span * span, 1.0);
        double closure_penalty = (cand[i].kind == SKETCH_SHAPE_LINE) ? (0.30 * (1.0 - gap)) : (0.30 * gap);
        double complexity_penalty = 0.0;
        if (cand[i].kind == SKETCH_SHAPE_RECT || cand[i].kind == SKETCH_SHAPE_ELLIPSE) complexity_penalty = 0.01;
        if (cand[i].kind == SKETCH_SHAPE_SQUARE || cand[i].kind == SKETCH_SHAPE_CIRCLE) complexity_penalty = 0.02;
        cand[i].score = normalized + closure_penalty + complexity_penalty;
        if (cand[i].score < best.score) best = cand[i];
    }

    return best;
}

static sketch_shape_t fit_for_mode(const sketch_point_t *pts, u32 n, sketch_shape_kind_t kind) {
    sketch_shape_t shape = { .kind = SKETCH_SHAPE_NONE, .score = DBL_MAX };
    switch (kind) {
    case SKETCH_SHAPE_AUTO:
        if (fit_with_recognizers(pts, n, &shape)) {
            break;
        }
        shape = choose_best_fit(pts, n);
        break;
    case SKETCH_SHAPE_LINE:
        fit_line(pts, n, &shape);
        break;
    case SKETCH_SHAPE_RECT:
        fit_rect_like(pts, n, false, &shape);
        break;
    case SKETCH_SHAPE_SQUARE:
        fit_rect_like(pts, n, true, &shape);
        break;
    case SKETCH_SHAPE_ELLIPSE:
        fit_ellipse_like(pts, n, false, &shape);
        break;
    case SKETCH_SHAPE_CIRCLE:
        fit_ellipse_like(pts, n, true, &shape);
        break;
    default:
        break;
    }
    return shape;
}

static sp_str_t stroke_json(const sketch_point_t *pts, u32 n) {
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    sp_str_builder_append_cstr(&b, "{\"points\":[");
    for (u32 i = 0; i < n; i++) {
        if (i > 0) sp_str_builder_append_cstr(&b, ",");
        sp_str_builder_append_cstr(&b, "{\"x\":");
        sp_str_builder_append(&b, sp_format("{:.3}", SP_FMT_F64(pts[i].x)));
        sp_str_builder_append_cstr(&b, ",\"y\":");
        sp_str_builder_append(&b, sp_format("{:.3}", SP_FMT_F64(pts[i].y)));
        sp_str_builder_append_cstr(&b, "}");
    }
    sp_str_builder_append_cstr(&b, "]}");
    return sp_str_builder_to_str(&b);
}

static bool shape_from_json(sp_str_t json, sketch_shape_t *out) {
    c8 kind_buf[24];
    if (!json_find_string(json, "\"kind\"", kind_buf, sizeof(kind_buf))) return false;

    sp_memset(out, 0, sizeof(*out));
    if (strcmp(kind_buf, "line") == 0) out->kind = SKETCH_SHAPE_LINE;
    else if (strcmp(kind_buf, "rect") == 0) out->kind = SKETCH_SHAPE_RECT;
    else if (strcmp(kind_buf, "square") == 0) out->kind = SKETCH_SHAPE_SQUARE;
    else if (strcmp(kind_buf, "ellipse") == 0) out->kind = SKETCH_SHAPE_ELLIPSE;
    else if (strcmp(kind_buf, "circle") == 0) out->kind = SKETCH_SHAPE_CIRCLE;
    else return false;

    json_find_number(json, "\"score\"", &out->score);
    json_find_number(json, "\"cx\"", &out->cx);
    json_find_number(json, "\"cy\"", &out->cy);
    json_find_number(json, "\"angle\"", &out->angle);
    json_find_number(json, "\"rx\"", &out->rx);
    json_find_number(json, "\"ry\"", &out->ry);
    json_find_number(json, "\"x1\"", &out->x1);
    json_find_number(json, "\"y1\"", &out->y1);
    json_find_number(json, "\"x2\"", &out->x2);
    json_find_number(json, "\"y2\"", &out->y2);
    return true;
}

static bool fit_with_recognizers(const sketch_point_t *pts, u32 n, sketch_shape_t *out) {
    if (ext_recognizer_count() == 0) return false;

    sp_str_t stroke = stroke_json(pts, n);
    sp_str_t result = sp_str_lit("");
    sp_str_t err = sp_str_lit("");
    if (!ext_invoke_recognizer(stroke, &result, &err)) {
        if (err.len > 0) {
            editor_set_message("Recognizer error: %.*s", (int)err.len, err.data);
        }
        return false;
    }
    if (result.len == 0) return false;
    return shape_from_json(result, out);
}

static void stroke_reset(void) {
    G.stroke_active = false;
    G.stroke_count = 0;
    G.has_preview = false;
}

static void push_point(double x, double y) {
    if (G.stroke_count >= SKETCH_MAX_POINTS) return;
    if (G.stroke_count > 0) {
        sketch_point_t prev = G.stroke[G.stroke_count - 1];
        if (fabs(prev.x - x) < 0.2 && fabs(prev.y - y) < 0.2) return;
    }
    G.stroke[G.stroke_count++] = (sketch_point_t){ x, y };
}

static bool commit_shape(sketch_shape_t shape) {
    if (shape.kind == SKETCH_SHAPE_NONE) return false;
    if (G.shape_count >= SKETCH_MAX_SHAPES) return false;
    G.shapes[G.shape_count++] = shape;
    G.preview = shape;
    G.has_preview = true;
    return true;
}

void sketch_init(void) {
    sp_memset(&G, 0, sizeof(G));
    G.preferred_kind = SKETCH_SHAPE_AUTO;
}

bool sketch_is_enabled(void) {
    return G.enabled;
}

bool sketch_set_enabled(bool enabled) {
    G.enabled = enabled;
    if (!enabled) {
        stroke_reset();
    }
    return true;
}

bool sketch_set_preferred_kind(sketch_shape_kind_t kind) {
    if (kind < SKETCH_SHAPE_AUTO || kind > SKETCH_SHAPE_CIRCLE) return false;
    G.preferred_kind = kind;
    return true;
}

sketch_shape_kind_t sketch_preferred_kind(void) {
    return G.preferred_kind;
}

bool sketch_set_mode_name(sp_str_t mode) {
    if (sp_str_equal(mode, sp_str_lit("auto"))) return sketch_set_preferred_kind(SKETCH_SHAPE_AUTO);
    if (sp_str_equal(mode, sp_str_lit("line"))) return sketch_set_preferred_kind(SKETCH_SHAPE_LINE);
    if (sp_str_equal(mode, sp_str_lit("rect")) || sp_str_equal(mode, sp_str_lit("rectangle"))) {
        return sketch_set_preferred_kind(SKETCH_SHAPE_RECT);
    }
    if (sp_str_equal(mode, sp_str_lit("square"))) return sketch_set_preferred_kind(SKETCH_SHAPE_SQUARE);
    if (sp_str_equal(mode, sp_str_lit("ellipse"))) return sketch_set_preferred_kind(SKETCH_SHAPE_ELLIPSE);
    if (sp_str_equal(mode, sp_str_lit("circle"))) return sketch_set_preferred_kind(SKETCH_SHAPE_CIRCLE);
    return false;
}

sp_str_t sketch_mode_name(void) {
    return sp_str_from_cstr(kind_name(G.preferred_kind));
}

sp_str_t sketch_status(void) {
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    sp_str_builder_append_cstr(&b, "sketch:");
    sp_str_builder_append_cstr(&b, G.enabled ? "on" : "off");
    sp_str_builder_append_cstr(&b, " mode:");
    sp_str_builder_append_cstr(&b, kind_name(G.preferred_kind));
    sp_str_builder_append_cstr(&b, " shapes:");
    sp_str_builder_append(&b, sp_format("{}", SP_FMT_U32(G.shape_count)));
    if (G.has_preview) {
        sp_str_builder_append_cstr(&b, " last:");
        sp_str_builder_append_cstr(&b, kind_name(G.preview.kind));
        sp_str_builder_append_cstr(&b, " score:");
        sp_str_builder_append(&b, sp_format("{:.4}", SP_FMT_F64(G.preview.score)));
    }
    return sp_str_builder_to_str(&b);
}

static void append_shape_json(sp_str_builder_t *b, const sketch_shape_t *shape, bool last) {
    sp_str_builder_append_cstr(b, "{");
    sp_str_builder_append_cstr(b, "\"kind\":\"");
    sp_str_builder_append_cstr(b, kind_name(shape->kind));
    sp_str_builder_append_cstr(b, "\",\"score\":");
    sp_str_builder_append(&b[0], sp_format("{:.6}", SP_FMT_F64(shape->score)));
    sp_str_builder_append_cstr(b, ",\"cx\":");
    sp_str_builder_append(&b[0], sp_format("{:.3}", SP_FMT_F64(shape->cx)));
    sp_str_builder_append_cstr(b, ",\"cy\":");
    sp_str_builder_append(&b[0], sp_format("{:.3}", SP_FMT_F64(shape->cy)));
    sp_str_builder_append_cstr(b, ",\"angle\":");
    sp_str_builder_append(&b[0], sp_format("{:.6}", SP_FMT_F64(shape->angle)));
    sp_str_builder_append_cstr(b, ",\"rx\":");
    sp_str_builder_append(&b[0], sp_format("{:.3}", SP_FMT_F64(shape->rx)));
    sp_str_builder_append_cstr(b, ",\"ry\":");
    sp_str_builder_append(&b[0], sp_format("{:.3}", SP_FMT_F64(shape->ry)));
    sp_str_builder_append_cstr(b, ",\"x1\":");
    sp_str_builder_append(&b[0], sp_format("{:.3}", SP_FMT_F64(shape->x1)));
    sp_str_builder_append_cstr(b, ",\"y1\":");
    sp_str_builder_append(&b[0], sp_format("{:.3}", SP_FMT_F64(shape->y1)));
    sp_str_builder_append_cstr(b, ",\"x2\":");
    sp_str_builder_append(&b[0], sp_format("{:.3}", SP_FMT_F64(shape->x2)));
    sp_str_builder_append_cstr(b, ",\"y2\":");
    sp_str_builder_append(&b[0], sp_format("{:.3}", SP_FMT_F64(shape->y2)));
    sp_str_builder_append_cstr(b, "}");
    if (!last) sp_str_builder_append_cstr(b, ",");
}

sp_str_t sketch_shapes_json(void) {
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    sp_str_builder_append_cstr(&b, "{\"enabled\":");
    sp_str_builder_append_cstr(&b, G.enabled ? "true" : "false");
    sp_str_builder_append_cstr(&b, ",\"mode\":\"");
    sp_str_builder_append_cstr(&b, kind_name(G.preferred_kind));
    sp_str_builder_append_cstr(&b, "\",\"shapeCount\":");
    sp_str_builder_append(&b, sp_format("{}", SP_FMT_U32(G.shape_count)));
    sp_str_builder_append_cstr(&b, ",\"shapes\":[");
    for (u32 i = 0; i < G.shape_count; i++) {
        append_shape_json(&b, &G.shapes[i], i + 1 == G.shape_count);
    }
    sp_str_builder_append_cstr(&b, "]");
    if (G.has_preview) {
        sp_str_builder_append_cstr(&b, ",\"preview\":");
        append_shape_json(&b, &G.preview, true);
    }
    sp_str_builder_append_cstr(&b, "}");
    return sp_str_builder_to_str(&b);
}

u32 sketch_shape_count(void) {
    return G.shape_count;
}

u32 sketch_stroke_point_count(void) {
    return G.stroke_count;
}

bool sketch_has_preview_shape(void) {
    return G.has_preview;
}

sketch_shape_kind_t sketch_preview_kind(void) {
    return G.has_preview ? G.preview.kind : SKETCH_SHAPE_NONE;
}

double sketch_preview_score(void) {
    return G.has_preview ? G.preview.score : 0.0;
}

void sketch_clear(void) {
    G.shape_count = 0;
    G.has_preview = false;
    stroke_reset();
}

bool sketch_handle_mouse(u32 term_col_1b, u32 term_row_1b, bool pressed) {
    if (!G.enabled) return false;
    if (term_col_1b == 0 || term_col_1b > E.screen_cols) return false;
    if (term_row_1b == 0 || term_row_1b > E.screen_rows) return false;

    double x = (double)(term_col_1b - 1);
    double y = (double)(term_row_1b - 1) * SKETCH_ASPECT_Y;

    if (pressed) {
        if (!G.stroke_active) {
            stroke_reset();
            G.stroke_active = true;
        }
        push_point(x, y);
        if (G.stroke_count >= 8) {
            G.preview = fit_for_mode(G.stroke, G.stroke_count, G.preferred_kind);
            G.has_preview = (G.preview.kind != SKETCH_SHAPE_NONE);
        }
    } else if (G.stroke_active) {
        push_point(x, y);
        if (G.stroke_count >= 2) {
            sketch_shape_t shape = fit_for_mode(G.stroke, G.stroke_count, G.preferred_kind);
            if (commit_shape(shape)) {
                editor_set_message("Sketch: %s fit score %.4f",
                                   kind_name(shape.kind),
                                   shape.score);
            } else {
                editor_set_message("Sketch: fit failed");
            }
        }
        stroke_reset();
    }

    return true;
}

static double point_segment_distance(double px, double py, double x1, double y1, double x2, double y2) {
    double vx = x2 - x1;
    double vy = y2 - y1;
    double denom = vx * vx + vy * vy;
    double t = 0.0;
    if (denom > 1e-9) {
        t = ((px - x1) * vx + (py - y1) * vy) / denom;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
    }
    double qx = x1 + t * vx;
    double qy = y1 + t * vy;
    return sqrt(sqr(px - qx) + sqr(py - qy));
}

static double oriented_box_distance(const sketch_shape_t *shape, double px, double py) {
    double c = cos(shape->angle);
    double s = sin(shape->angle);
    double dx = px - shape->cx;
    double dy = py - shape->cy;
    double u = c * dx + s * dy;
    double v = -s * dx + c * dy;
    double du = fabs(fabs(u) - shape->rx);
    double dv = fabs(fabs(v) - shape->ry);
    return du < dv ? du : dv;
}

static double ellipse_distance(const sketch_shape_t *shape, double px, double py) {
    double c = cos(shape->angle);
    double s = sin(shape->angle);
    double dx = px - shape->cx;
    double dy = py - shape->cy;
    double u = c * dx + s * dy;
    double v = -s * dx + c * dy;
    double q = (u * u) / fmax(shape->rx * shape->rx, 1e-6) +
               (v * v) / fmax(shape->ry * shape->ry, 1e-6);
    double scale = 0.5 * (shape->rx + shape->ry);
    return fabs(q - 1.0) * scale;
}

static double shape_distance(const sketch_shape_t *shape, double px, double py) {
    switch (shape->kind) {
    case SKETCH_SHAPE_LINE:
        return point_segment_distance(px, py, shape->x1, shape->y1, shape->x2, shape->y2);
    case SKETCH_SHAPE_RECT:
    case SKETCH_SHAPE_SQUARE:
        return oriented_box_distance(shape, px, py);
    case SKETCH_SHAPE_ELLIPSE:
    case SKETCH_SHAPE_CIRCLE:
        return ellipse_distance(shape, px, py);
    default:
        return DBL_MAX;
    }
}

static c8 glyph_for_distance(double d) {
    if (d < 0.35) return '#';
    if (d < 0.75) return '*';
    if (d < 1.20) return '+';
    if (d < 1.60) return '.';
    return ' ';
}

static c8 grid_glyph(u32 col, u32 row) {
    if (col % 8 == 0 && row % 4 == 0) return '+';
    if (col % 8 == 0) return '|';
    if (row % 4 == 0) return '-';
    if (((col / 2) + row) % 2 == 0) return '.';
    return ' ';
}

void sketch_draw_canvas(sp_io_writer_t *out) {
    if (!out) return;
    for (u32 row = 0; row < E.screen_rows; row++) {
        display_set_cursor(row, 0);
        sp_io_write_cstr(out, "\033[K");
        for (u32 col = 0; col < E.screen_cols; col++) {
            double px = (double)col;
            double py = (double)row * SKETCH_ASPECT_Y;
            double best = DBL_MAX;
            bool preview_hit = false;
            for (u32 i = 0; i < G.shape_count; i++) {
                double d = shape_distance(&G.shapes[i], px, py);
                if (d < best) best = d;
            }
            if (G.has_preview) {
                double d = shape_distance(&G.preview, px, py);
                if (d < best) {
                    best = d;
                    preview_hit = true;
                }
            }
            if (G.stroke_active && G.stroke_count > 0) {
                for (u32 i = 0; i < G.stroke_count; i++) {
                    double d = point_dist((sketch_point_t){px, py}, G.stroke[i]);
                    if (d < best) best = d;
                    if (i == 0) continue;
                    d = point_segment_distance(
                        px, py,
                        G.stroke[i - 1].x, G.stroke[i - 1].y,
                        G.stroke[i].x, G.stroke[i].y);
                    if (d < best) best = d;
                }
            }
            c8 ch = (best < DBL_MAX / 2.0) ? glyph_for_distance(best) : grid_glyph(col, row);
            if (preview_hit && (ch == '.' || ch == '+')) ch = '*';
            if (row == 0 && col < 56) {
                const c8 *banner = " TED Sketch  drag mouse to draw  :sketch auto|line|rect|square|ellipse|circle ";
            if ((u32)strlen(banner) > col) ch = banner[col];
            }
            sp_io_write(out, &ch, 1);
        }
    }
}
