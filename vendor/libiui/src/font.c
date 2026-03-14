/* Built-in vector font implementation */

#include "internal.h"

/* Vector font glyph bytecode table (printable ASCII 0x20-0x7E).
 * Format: [left, right, ascent, descent, n_snap_x, n_snap_y, snap_x[],
 * snap_y[], opcodes...]. Coords in 1/64 units; scale = font_height / 64.0f.
 */
const signed char iui_glyph_table[] = {
#include "glyphs-data.inc"
};

/* clang-format off */
const uint16_t iui_glyph_offsets[128] = {
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    28,   40,   90,   114,
    152,  224,  323,  390,  419,  441,  463,  494,  520,  556,  575,  604,
    622,  666,  691,  736,  780,  809,  860,  919,  944,  1004, 1063, 1109,
    1162, 1183, 1209, 1230, 1288, 1375, 1406, 1455, 1499, 1534, 1572, 1604,
    1655, 1686, 1703, 1731, 1761, 1785, 1821, 1851, 1895, 1931, 1981, 2023,
    2074, 2100, 2128, 2152, 2188, 2212, 2240, 2271, 2296, 2314, 2339, 2363,
    2381, 2417, 2467, 2517, 2561, 2611, 2659, 2693, 2758, 2790, 2826, 2870,
    2900, 2917, 2963, 2995, 3039, 3089, 3139, 3168, 3219, 3252, 3283, 3307,
    3343, 3367, 3399, 3430, 3474, 3491, 3535, 0,
};
/* clang-format on */

const signed char *iui_get_glyph(unsigned char c)
{
    /* Map non-printable ASCII (0-31) and out-of-range (>127) to box glyph.
     * Index 0 in glyph_offsets points to the replacement box glyph.
     * Valid printable ASCII range is 32 (space) to 126 (~). */
    if (c < 32 || c > 126)
        c = 0;
    return iui_glyph_table + iui_glyph_offsets[c];
}
