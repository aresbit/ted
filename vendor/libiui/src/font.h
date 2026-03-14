/*
 * Built-in Vector Font Definitions
 *
 * Contains glyph data and accessors for the built-in vector font.
 * Reference: Based on classic bitmap font rendering techniques adapted for
 * vectors.
 */

#ifndef IUI_FONT_H
#define IUI_FONT_H

#include <stdint.h>

/* Glyph header accessors for the built-in vector font */
#define IUI_GLYPH_LEFT(g) ((g)[0])
#define IUI_GLYPH_RIGHT(g) ((g)[1])
#define IUI_GLYPH_ASCENT(g) ((g)[2])
#define IUI_GLYPH_DESCENT(g) ((g)[3])
#define IUI_GLYPH_N_SNAP_X(g) ((g)[4])
#define IUI_GLYPH_N_SNAP_Y(g) ((g)[5])
#define IUI_GLYPH_DRAW(g) \
    ((g) + 6 + IUI_GLYPH_N_SNAP_X(g) + IUI_GLYPH_N_SNAP_Y(g))

/* Built-in font data (printable ASCII 0x20-0x7E; index 0 is a box) */
extern const signed char iui_glyph_table[];
extern const uint16_t iui_glyph_offsets[128];

/* Lookup a glyph pointer; maps out-of-range codepoints to index 0 box */
const signed char *iui_get_glyph(unsigned char c);

#endif /* IUI_FONT_H */
