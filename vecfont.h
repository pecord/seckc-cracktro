/*
 * vecfont.h : a tiny stroke (vector) font for rave-psx.
 *
 * Each glyph is a list of line segments on a 4-wide x 6-tall grid with the
 * origin at the bottom-left (y up). A segment is 4 signed bytes: x0,y0,x1,y1.
 * Used both for the rotating 3D "SecKC" wireframe logo and the 2D
 * sine-scroller greetz. Uppercase A-Z, digits, and a few symbols; lowercase
 * maps to uppercase.
 */
#ifndef VECFONT_H
#define VECFONT_H

#include <stdint.h>

#define VF_W   4    /* glyph cell width  */
#define VF_H   6    /* glyph cell height */
#define VF_ADV 6    /* horizontal advance (cell + 2 spacing) */

typedef struct { uint8_t nseg; const signed char *seg; } VGlyph;

#define SEG static const signed char

SEG g_A[] = { 0,0,0,4, 0,4,2,6, 2,6,4,4, 4,4,4,0, 0,3,4,3 };
SEG g_B[] = { 0,0,0,6, 0,6,3,6, 3,6,4,5, 4,5,3,3, 3,3,0,3, 3,3,4,2, 4,2,3,0, 3,0,0,0 };
SEG g_C[] = { 4,6,0,6, 0,6,0,0, 0,0,4,0 };
SEG g_D[] = { 0,0,0,6, 0,6,2,6, 2,6,4,4, 4,4,4,2, 4,2,2,0, 2,0,0,0 };
SEG g_E[] = { 4,6,0,6, 0,6,0,0, 0,0,4,0, 0,3,3,3 };
SEG g_F[] = { 4,6,0,6, 0,6,0,0, 0,3,3,3 };
SEG g_G[] = { 4,6,0,6, 0,6,0,0, 0,0,4,0, 4,0,4,3, 4,3,2,3 };
SEG g_H[] = { 0,0,0,6, 4,0,4,6, 0,3,4,3 };
SEG g_I[] = { 2,0,2,6, 1,6,3,6, 1,0,3,0 };
SEG g_J[] = { 4,6,4,1, 4,1,3,0, 3,0,1,0, 1,0,0,1 };
SEG g_K[] = { 0,0,0,6, 0,3,4,6, 0,3,4,0 };
SEG g_L[] = { 0,6,0,0, 0,0,4,0 };
SEG g_M[] = { 0,0,0,6, 0,6,2,3, 2,3,4,6, 4,6,4,0 };
SEG g_N[] = { 0,0,0,6, 0,6,4,0, 4,0,4,6 };
SEG g_O[] = { 0,1,0,5, 0,5,2,6, 2,6,4,5, 4,5,4,1, 4,1,2,0, 2,0,0,1 };
SEG g_P[] = { 0,0,0,6, 0,6,3,6, 3,6,4,5, 4,5,3,3, 3,3,0,3 };
SEG g_Q[] = { 0,1,0,5, 0,5,2,6, 2,6,4,5, 4,5,4,1, 4,1,2,0, 2,0,0,1, 2,2,4,0 };
SEG g_R[] = { 0,0,0,6, 0,6,3,6, 3,6,4,5, 4,5,3,3, 3,3,0,3, 1,3,4,0 };
SEG g_S[] = { 4,6,0,6, 0,6,0,3, 0,3,4,3, 4,3,4,0, 4,0,0,0 };
SEG g_T[] = { 0,6,4,6, 2,6,2,0 };
SEG g_U[] = { 0,6,0,0, 0,0,4,0, 4,0,4,6 };
SEG g_V[] = { 0,6,2,0, 2,0,4,6 };
SEG g_W[] = { 0,6,1,0, 1,0,2,3, 2,3,3,0, 3,0,4,6 };
SEG g_X[] = { 0,0,4,6, 0,6,4,0 };
SEG g_Y[] = { 0,6,2,3, 4,6,2,3, 2,3,2,0 };
SEG g_Z[] = { 0,6,4,6, 4,6,0,0, 0,0,4,0 };

SEG g_0[] = { 0,1,0,5, 0,5,2,6, 2,6,4,5, 4,5,4,1, 4,1,2,0, 2,0,0,1, 0,0,4,6 };
SEG g_1[] = { 2,0,2,6, 1,5,2,6, 1,0,3,0 };
SEG g_2[] = { 0,5,2,6, 2,6,4,5, 4,5,0,0, 0,0,4,0 };
SEG g_3[] = { 0,6,4,6, 4,6,4,0, 4,0,0,0, 1,3,4,3 };
SEG g_4[] = { 3,0,3,6, 3,6,0,2, 0,2,4,2 };
SEG g_5[] = { 4,6,0,6, 0,6,0,3, 0,3,3,3, 3,3,4,2, 4,2,3,0, 3,0,0,0 };
SEG g_6[] = { 4,6,0,3, 0,3,0,0, 0,0,4,0, 4,0,4,3, 4,3,0,3 };
SEG g_7[] = { 0,6,4,6, 4,6,1,0 };
SEG g_8[] = { 0,0,0,6, 0,6,4,6, 4,6,4,0, 4,0,0,0, 0,3,4,3 };
SEG g_9[] = { 4,3,0,3, 0,3,0,6, 0,6,4,6, 4,6,4,0, 4,0,0,0 };

SEG g_slash[] = { 0,0,4,6 };
SEG g_dot[]   = { 1,0,2,0 };
SEG g_bang[]  = { 2,6,2,2, 2,1,2,0 };
SEG g_dash[]  = { 1,3,3,3 };
SEG g_colon[] = { 2,4,2,5, 2,1,2,2 };
SEG g_star[]  = { 0,3,4,3, 2,5,2,1, 0,5,4,1, 4,5,0,1 };
SEG g_plus[]  = { 0,3,4,3, 2,5,2,1 };
SEG g_comma[] = { 2,1,1,0 };

#define SZ(a) (uint8_t)(sizeof(a) / 4)

static const VGlyph vfont[96] = {
	[' ' - 32] = { 0, 0 },
	['!' - 32] = { SZ(g_bang),  g_bang },
	['*' - 32] = { SZ(g_star),  g_star },
	['+' - 32] = { SZ(g_plus),  g_plus },
	[',' - 32] = { SZ(g_comma), g_comma },
	['-' - 32] = { SZ(g_dash),  g_dash },
	['.' - 32] = { SZ(g_dot),   g_dot },
	['/' - 32] = { SZ(g_slash), g_slash },
	['0' - 32] = { SZ(g_0), g_0 }, ['1' - 32] = { SZ(g_1), g_1 },
	['2' - 32] = { SZ(g_2), g_2 }, ['3' - 32] = { SZ(g_3), g_3 },
	['4' - 32] = { SZ(g_4), g_4 }, ['5' - 32] = { SZ(g_5), g_5 },
	['6' - 32] = { SZ(g_6), g_6 }, ['7' - 32] = { SZ(g_7), g_7 },
	['8' - 32] = { SZ(g_8), g_8 }, ['9' - 32] = { SZ(g_9), g_9 },
	[':' - 32] = { SZ(g_colon), g_colon },
	['A' - 32] = { SZ(g_A), g_A }, ['B' - 32] = { SZ(g_B), g_B },
	['C' - 32] = { SZ(g_C), g_C }, ['D' - 32] = { SZ(g_D), g_D },
	['E' - 32] = { SZ(g_E), g_E }, ['F' - 32] = { SZ(g_F), g_F },
	['G' - 32] = { SZ(g_G), g_G }, ['H' - 32] = { SZ(g_H), g_H },
	['I' - 32] = { SZ(g_I), g_I }, ['J' - 32] = { SZ(g_J), g_J },
	['K' - 32] = { SZ(g_K), g_K }, ['L' - 32] = { SZ(g_L), g_L },
	['M' - 32] = { SZ(g_M), g_M }, ['N' - 32] = { SZ(g_N), g_N },
	['O' - 32] = { SZ(g_O), g_O }, ['P' - 32] = { SZ(g_P), g_P },
	['Q' - 32] = { SZ(g_Q), g_Q }, ['R' - 32] = { SZ(g_R), g_R },
	['S' - 32] = { SZ(g_S), g_S }, ['T' - 32] = { SZ(g_T), g_T },
	['U' - 32] = { SZ(g_U), g_U }, ['V' - 32] = { SZ(g_V), g_V },
	['W' - 32] = { SZ(g_W), g_W }, ['X' - 32] = { SZ(g_X), g_X },
	['Y' - 32] = { SZ(g_Y), g_Y }, ['Z' - 32] = { SZ(g_Z), g_Z },
};

/* Look up a glyph; lowercase folds to uppercase, unknown -> blank. */
static inline const VGlyph *vglyph(char c) {
	if (c >= 'a' && c <= 'z') c -= 32;
	if (c < 32 || c > 'Z')    c = ' ';
	return &vfont[c - 32];
}

#endif /* VECFONT_H */
