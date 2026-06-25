/*
 * backdrop.c : classic synthwave backdrop. All 2D primitives, basically free.
 *  - a purple sky / magenta-to-black ground split at the horizon
 *  - a banded semicircle "sun" with slits
 *  - canyon walls as a perspective wireframe heightmap + a scrolling floor grid
 *  - a parallax starfield streaking past
 */
#include <stdlib.h>
#include "gpu.h"
#include "backdrop.h"

/* ------------------------------------------------------------------ vaporwave */
#define VW_HORIZON 138
#define VW_SUNR    54

void vaporwave_render(uint32_t frame) {
	POLY_G4 *q;
	int i, k, fr;

	/* sky: dark purple (top) -> magenta (horizon) */
	q = (POLY_G4 *)db_nextpri;
	setPolyG4(q);
	setXY4(q, 0, 0, SCREEN_XRES, 0, 0, VW_HORIZON, SCREEN_XRES, VW_HORIZON);
	setRGB0(q, 26, 8, 48); setRGB1(q, 26, 8, 48);
	setRGB2(q, 122, 26, 112); setRGB3(q, 122, 26, 112);
	addPrim(OT_AT(OT_LEN - 1), q);
	db_nextpri = (uint8_t *)(q + 1);

	/* ground: magenta (horizon) -> near black */
	q = (POLY_G4 *)db_nextpri;
	setPolyG4(q);
	setXY4(q, 0, VW_HORIZON, SCREEN_XRES, VW_HORIZON, 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
	setRGB0(q, 58, 12, 70); setRGB1(q, 58, 12, 70);
	setRGB2(q, 8, 4, 20); setRGB3(q, 8, 4, 20);
	addPrim(OT_AT(OT_LEN - 1), q);
	db_nextpri = (uint8_t *)(q + 1);

	/* banded sun: stacked horizontal bars forming a semicircle, slits below */
	for (i = 3; i <= VW_SUNR; i += 3) {
		int y  = VW_HORIZON - VW_SUNR + i;
		int dy = VW_SUNR - i;
		int hw = (int)isqrt32((uint32_t)(VW_SUNR * VW_SUNR - dy * dy));
		int t  = (i * 255) / VW_SUNR;
		TILE *s;
		if (hw < 1) continue;
		if (i > VW_SUNR * 52 / 100 && ((i / 3) & 1)) continue;   /* slits */
		s = (TILE *)db_nextpri;
		setTile(s);
		setXY0(s, 160 - hw, y);
		setWH(s, hw * 2, 3);
		setRGB0(s, 255, 232 - (t * 150) / 255, 96 + (t * 110) / 255);
		addPrim(OT_AT(OT_LEN - 2), s);
		db_nextpri = (uint8_t *)(s + 1);
	}

	/* Canyon walls + floor grid share ONE scroll phase, so struts and grid lines
	 * rush toward the camera together: it reads as driving through. */
	fr = 255 - (int)((frame * 5) & 255);

	/* canyon walls as a perspective wireframe HEIGHTMAP: a dense grid mesh on
	 * each side climbing away from the road and receding to the horizon */
	{
		static const unsigned char nz[16] =
			{ 8, 30, 14, 38, 4, 26, 18, 34, 10, 40, 6, 28, 16, 36, 12, 22 };
		int sc = (int)((frame * 5) >> 8);
		int side, r, c;
		for (side = -1; side <= 1; side += 2) {
			int px[6], py[6], have = 0;
			for (r = 12; r >= 0; r--) {            /* far row -> near row */
				int denom = (r << 8) + fr;
				int cx[6], cy[6];
				if (denom < 150) { have = 0; continue; }   /* too near: clips */
				for (c = 0; c < 6; c++) {
					int worldX = (80 + c * 46) * side;
					int worldH = c * c * 9 + nz[(c * 5 + r + sc) & 15] * 2;
					cx[c] = 160 + (worldX * 256) / denom;
					cy[c] = VW_HORIZON + ((110 * 256) - worldH * 256) / denom;
				}
				for (c = 0; c < 5; c++) {          /* lateral lines (ridge brightest) */
					int top = (c == 4);
					add_line(cx[c], cy[c], cx[c + 1], cy[c + 1],
					         40, top ? 235 : 120, top ? 255 : 185, OT_LEN - 3);
				}
				if (have)                          /* depth lines into the screen */
					for (c = 0; c < 6; c++)
						add_line(px[c], py[c], cx[c], cy[c], 40, 150, 210, OT_LEN - 3);
				for (c = 0; c < 6; c++) { px[c] = cx[c]; py[c] = cy[c]; }
				have = 1;
			}
		}
	}

	/* road edges (static verticals to the vanishing point) */
	for (k = -6; k <= 6; k++)
		add_line(160 + k * 26, SCREEN_YRES, 160, VW_HORIZON, 235, 40, 205, OT_LEN - 4);

	/* floor grid: horizontals scrolling toward the viewer */
	for (i = 0; i <= 12; i++) {
		int denom = (i << 8) + fr;
		int y, b;
		if (denom < 1) continue;
		y = VW_HORIZON + (110 * 256) / denom;
		if (y <= VW_HORIZON || y >= SCREEN_YRES) continue;
		b = 80 + (y - VW_HORIZON) * 175 / (SCREEN_YRES - VW_HORIZON);
		add_line(0, y, SCREEN_XRES, y, (uint8_t)b, 38, (uint8_t)(b * 9 / 10), OT_LEN - 4);
	}

	/* highway centre line: scrolling yellow dashes */
	for (i = 0; i <= 12; i++) {
		int d1 = (i << 8) + fr;
		int d2 = d1 + 132;
		int y1, y2;
		if (d1 < 1) continue;
		y1 = VW_HORIZON + (110 * 256) / d1;
		y2 = VW_HORIZON + (110 * 256) / d2;
		if (y1 > SCREEN_YRES) y1 = SCREEN_YRES;
		if (y1 <= VW_HORIZON || y2 <= VW_HORIZON) continue;
		add_line(160, y2, 160, y1, 255, 232, 110, OT_LEN - 4);
	}
}

/* ------------------------------------------------------------------ starfield */
#define NSTARS      96
#define STAR_FAR    2048
#define STAR_SPEED  34
#define STAR_SPREAD 1100

static short star_x[NSTARS], star_y[NSTARS];
static int   star_z[NSTARS];

static void star_reset(int i) {
	star_x[i] = (short)(rand() % (2 * STAR_SPREAD) - STAR_SPREAD);
	star_y[i] = (short)(rand() % (2 * STAR_SPREAD) - STAR_SPREAD);
	star_z[i] = rand() % STAR_FAR + 64;
}

void backdrop_init(void) {
	int i;
	srand(0x5ecc);
	for (i = 0; i < NSTARS; i++) star_reset(i);
}

void starfield_update(void) {
	int i;
	for (i = 0; i < NSTARS; i++) {
		star_z[i] -= STAR_SPEED;
		if (star_z[i] < 24) { star_reset(i); star_z[i] = STAR_FAR; }
	}
}

void starfield_render(void) {
	int i;
	for (i = 0; i < NSTARS; i++) {
		int z  = star_z[i];
		int zp = z + STAR_SPEED * 3;
		int sx = CENTERX + (star_x[i] * PROJ) / z;
		int sy = CENTERY + (star_y[i] * PROJ) / z;
		int sxp, syp, bri;
		if (sx < 0 || sx >= SCREEN_XRES || sy < 0 || sy >= SCREEN_YRES) continue;
		sxp = CENTERX + (star_x[i] * PROJ) / zp;
		syp = CENTERY + (star_y[i] * PROJ) / zp;
		bri = 70 + (STAR_FAR - z) / 9;
		if (bri > 255) bri = 255;
		add_line(sxp, syp, sx, sy,
		         (uint8_t)(bri * 3 / 5), (uint8_t)bri, (uint8_t)bri, OT_LEN - 2);
	}
}
