/*
 * rave-psx : a SecKC demoscene cracktro for the MiSTer PSX core.
 *
 *   - synthwave/vaporwave backdrop: scrolling neon grid, banded sun, and a
 *     wireframe heightmap canyon that rushes toward the camera
 *   - the SecKC ASCII skull as a spinning extruded 3D slab
 *   - flying Malort bottles tumbling through the scene
 *   - a 3D perspective sine-scroller of greetz, and a boot splash
 *   - original 150 BPM synthwave track on CD-DA (the visuals pulse to it)
 *   - (toggle VAPORWAVE 0 for the fixed-point raytracer backdrop)
 *
 * Built with PSn00bSDK. NTSC 320x240, double buffered.
 */

#include <stdint.h>
#include <stdlib.h>
#include <psxgpu.h>
#include <psxgte.h>
#include <psxcd.h>
#include <psxspu.h>
#include <inline_c.h>
#include "vecfont.h"

/* ------------------------------------------------------------------ config */

#define OT_LEN      1024
#define PACKET_LEN  49152

#define SCREEN_XRES 320
#define SCREEN_YRES 240
#define CENTERX     (SCREEN_XRES >> 1)
#define CENTERY     (SCREEN_YRES >> 1)
#define PROJ        CENTERX                 /* GTE geom screen distance */

#define BPM         150                     /* matches the CD-DA music track */
#define FPB         ((60 * 60) / BPM)       /* frames/beat @ NTSC 60fps */

/* --- perf bisect toggles (override with -DPERF_*=0) --- */
#ifndef PERF_CDDA
#define PERF_CDDA       1   /* CD-DA music playback (CdlPlay) */
#endif
#ifndef PERF_VU_CAPTURE
#define PERF_VU_CAPTURE 0   /* per-frame SpuRead pins pcsx_rearmed to its slow SPU
                            * path (~1fps in browser); synthetic pulse instead.
                            * Build -DPERF_VU_CAPTURE=1 for live capture on HW. */
#endif
#ifndef PERF_BG
#define PERF_BG         1   /* full-screen backdrop overdraw + additive blend */
#endif

/* Plasma grid: 16x12 cells of 20px gouraud quads -> 17x13 vertices. */
#define PCOLS       16
#define PROWS       12
#define PCW         (SCREEN_XRES / PCOLS)
#define PCH         (SCREEN_YRES / PROWS)

/* Starfield */
#define NSTARS      96
#define STAR_FAR    2048
#define STAR_SPEED  34
#define STAR_SPREAD 1100

/* Logo */
#define LOGO_Z      300
#define LOGO_DEPTH  20
#define LOGO_GS_BIG 20
#define LOGO_GS_SM  14
#define MAXLSEG     40

/* Accent cubes */
#define RING_CUBES  6

/* Scroller */
#define SCR_SCALE   4
#define SCR_ADV     (VF_ADV * SCR_SCALE)
#define SCR_BASE    216
#define SCR_AMP     16

/* ----------------------------------------------------------- double buffer */

typedef struct {
	DISPENV  disp;
	DRAWENV  draw;
	uint32_t ot[OT_LEN];
	uint8_t  p[PACKET_LEN];
} DB;

static DB    db[2];
static int   db_active = 0;
static uint8_t *db_nextpri;

/* ---------------------------------------------------------------- geometry */

static const SVECTOR cube_verts[8] = {
	{ -100, -100, -100, 0 }, {  100, -100, -100, 0 },
	{ -100,  100, -100, 0 }, {  100,  100, -100, 0 },
	{  100, -100,  100, 0 }, { -100, -100,  100, 0 },
	{  100,  100,  100, 0 }, { -100,  100,  100, 0 }
};

/* 12 edges of the cube (vertex index pairs) */
static const uint8_t cube_edges[12][2] = {
	{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },   /* front */
	{ 5, 4 }, { 4, 6 }, { 6, 7 }, { 7, 5 },   /* back  */
	{ 0, 5 }, { 1, 4 }, { 3, 6 }, { 2, 7 }    /* sides */
};

/* SecKC ASCII-skull texture, baked into a TIM and embedded via incbin. */
extern const uint32_t seckc_tim[];
static uint16_t tex_tpage, tex_clut;

/* Flying Malort bottle texture (transparent background). */
extern const uint32_t bottle_tim[];
static uint16_t bottle_tpage, bottle_clut;

/* Bouncing DVD logo texture (white/tintable, transparent background). */
extern const uint32_t dvd_tim[];
static uint16_t dvd_tpage, dvd_clut;

/* The SPU continuously captures the live CD-DA audio into SPU RAM (left channel
 * at 0x000). We DMA a slice out and take its peak as a VU level so the skull
 * reacts to the music.
 *
 * IMPORTANT: never busy-wait the transfer. A per-frame
 * SpuIsTransferCompleted(SPU_TRANSFER_WAIT) is microseconds on real hardware
 * but pcsx_rearmed (the browser core) models it slowly enough to drop the demo
 * to ~1 fps. Instead we kick the read off on one frame and harvest it the next
 * with a NON-BLOCKING poll. And if the host doesn't emulate SPU capture at all
 * (peak stays pinned near zero), we fall back to a synthetic tempo pulse so the
 * skull still moves. */
static uint32_t spu_cap[128];          /* 512 bytes = 256 samples */
static int      g_vu = 0;

static int audio_level(uint32_t frame) {
#if PERF_VU_CAPTURE
	static int pending = 0, have_real = 0, last = 0;
	const int16_t *s = (const int16_t *)spu_cap;

	if (pending && SpuIsTransferCompleted(SPU_TRANSFER_PEEK)) {
		int i, peak = 0, n = (int)(sizeof(spu_cap) / 2);
		for (i = 0; i < n; i++) {
			int a = s[i];
			if (a < 0) a = -a;
			if (a > peak) peak = a;
		}
		pending = 0;
		last = peak;
		if (peak > 64) have_real = 1;     /* genuine capture is working */
	}
	if (!pending) {                        /* kick the next read, don't wait */
		SpuSetTransferStartAddr(0x0000);
		SpuRead(spu_cap, sizeof(spu_cap));
		pending = 1;
	}

	if (have_real)
		return last;                       /* 0..32767, real audio peak */
#endif

	/* fallback: a punchy four-on-the-floor-ish pulse (~150 BPM at 60 fps).
	 * Also the only path when PERF_AUDIO is bisected off. */
	{
		int ph = (int)(frame % 24);
		return (ph < 7) ? (32000 - ph * 4000) : 4500;
	}
}

/* ----------------------------------------------------------------- helpers */

/* cheap HSV->RGB, h/s/v in 0..255 */
static void hsv(uint8_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
	uint8_t region = h / 43;
	uint8_t rem    = (h - region * 43) * 6;
	uint8_t pp = (v * (255 - s)) / 255;
	uint8_t qq = (v * (255 - ((s * rem) / 255))) / 255;
	uint8_t tt = (v * (255 - ((s * (255 - rem)) / 255))) / 255;
	switch (region) {
		case 0:  *r = v; *g = tt; *b = pp; break;
		case 1:  *r = qq; *g = v; *b = pp; break;
		case 2:  *r = pp; *g = v; *b = tt; break;
		case 3:  *r = pp; *g = qq; *b = v; break;
		case 4:  *r = tt; *g = pp; *b = v; break;
		default: *r = v; *g = pp; *b = qq; break;
	}
}

/* add a flat additive line at OT depth otz */
static void add_line(int x0, int y0, int x1, int y1,
                     uint8_t r, uint8_t g, uint8_t b, int otz) {
	LINE_F2 *l = (LINE_F2 *)db_nextpri;
	setLineF2(l);
	setSemiTrans(l, 1);
	setRGB0(l, r, g, b);
	setXY2(l, x0, y0, x1, y1);
	addPrim(db[db_active].ot + otz, l);
	db_nextpri = (uint8_t *)(l + 1);
}

/* Thicken a projected segment (a,b) into a SOLID filled quad of half-width w.
 * This is how we get filled (not wireframe) text: each font stroke becomes a
 * filled POLY_F4 beam. */
static void add_fillstroke(DVECTOR a, DVECTOR b, int w,
                           uint8_t r, uint8_t g, uint8_t bl, int otz) {
	int sdx = b.vx - a.vx, sdy = b.vy - a.vy;
	int ax  = sdx < 0 ? -sdx : sdx;
	int ay  = sdy < 0 ? -sdy : sdy;
	int len = ax > ay ? ax + (ay >> 1) : ay + (ax >> 1);   /* cheap hypot */
	int px, py;
	POLY_F4 *p;
	if (len < 1) len = 1;
	px = (-sdy * w) / len;
	py = ( sdx * w) / len;
	/* drop strokes that project far off-screen (GPU would discard the oversized
	 * quad anyway, which made waving letters flicker out) */
	if (a.vx < -400 || a.vx > 720 || b.vx < -400 || b.vx > 720 ||
	    a.vy < -400 || a.vy > 640 || b.vy < -400 || b.vy > 640) return;
	p  = (POLY_F4 *)db_nextpri;
	setPolyF4(p);                  /* opaque: stays readable over bright floor */
	setRGB0(p, r, g, bl);
	setXY4(p, a.vx + px, a.vy + py, b.vx + px, b.vy + py,
	          a.vx - px, a.vy - py, b.vx - px, b.vy - py);
	addPrim(db[db_active].ot + otz, p);
	db_nextpri = (uint8_t *)(p + 1);
}

/* Draw a flat 2D string centred on cx at baseline y, using the vector font as
 * filled strokes. */
static void draw_text2d(const char *str, int cx, int y, int sc,
                        uint8_t r, uint8_t g, uint8_t b, int otz) {
	int len = 0, i, x;
	while (str[len]) len++;
	x = cx - (len * VF_ADV * sc) / 2;
	for (i = 0; i < len; i++) {
		const VGlyph *gl = vglyph(str[i]);
		int s;
		for (s = 0; s < gl->nseg; s++) {
			const signed char *sg = gl->seg + s * 4;
			DVECTOR a = { (short)(x + sg[0] * sc), (short)(y - sg[1] * sc) };
			DVECTOR c = { (short)(x + sg[2] * sc), (short)(y - sg[3] * sc) };
			add_fillstroke(a, c, sc > 2 ? 2 : 1, r, g, b, otz);
		}
		x += VF_ADV * sc;
	}
}

/* project a model vertex with the current GTE matrix -> screen DVECTOR */
static void project(const SVECTOR *v, DVECTOR *out) {
	gte_ldv0(v);
	gte_rtps();
	gte_stsxy(out);
}

static int onscreen(const DVECTOR *p) {
	return p->vx > -200 && p->vx < SCREEN_XRES + 200 &&
	       p->vy > -200 && p->vy < SCREEN_YRES + 200;
}

/* -------------------------------------------------------------- raytracer */
/* A genuine (if cheeky) fixed-point raytracer: analytic ray-sphere + a checker
 * ground plane, Lambert + specular, and a one-bounce reflection. Rendered at
 * 64x48 into a 16bpp VRAM texture, then upscaled to a full-screen quad. No FPU,
 * no GTE — just 16.16 fixed point and an integer sqrt. */
#define RM_W 128
#define RM_H 96

/* backdrop: 1 = synthwave/vaporwave grid+sun, 0 = fixed-point raytracer */
#define VAPORWAVE 1

typedef int32_t fx;
#define F(a)   ((fx)((a) * 65536.0))      /* constants only (compile-time) */
#define IF(n)  ((fx)(n) << 16)            /* runtime int -> fixed          */

static uint16_t rmbuf[RM_W * RM_H];
static uint16_t rt_tpage_l, rt_tpage_r;   /* 128-wide split across two 16bpp pages */

static inline fx fmul(fx a, fx b) { return (fx)(((int64_t)a * b) >> 16); }
static inline fx fdiv(fx a, fx b) { return b ? (fx)((((int64_t)a) << 16) / b) : 0; }

/* 32-bit integer sqrt (avoids slow 64-bit math on the R3000) */
static uint32_t isqrt32(uint32_t n) {
	uint32_t c = 0, d = 1U << 30;
	while (d > n) d >>= 2;
	while (d) {
		if (n >= c + d) { n -= c + d; c = (c >> 1) + d; }
		else c >>= 1;
		d >>= 2;
	}
	return c;
}
/* sqrt of a 16.16 value kept in 32-bit: sqrt(a) == sqrt(real)*256, so <<8 */
static inline fx fsqrt(fx a) {
	return a <= 0 ? 0 : (fx)(isqrt32((uint32_t)a) << 8);
}

/* Camera, floor and light are fixed, so the background is identical every
 * frame. We precompute per-pixel ray dirs, the floor hit distance, and the
 * static sky/floor colour once; per frame only the moving spheres do work, and
 * any pixel the spheres miss is a straight table copy (no sqrt, no divide). */
#define RT_ROY  F(2.0)
#define RT_ROZ  F(-4.0)
#define RT_LX   F(-0.5)
#define RT_LY   F(0.74)
#define RT_LZ   F(-0.45)
#define RT_R2   F(0.81)

static fx       rt_dir[RM_W * RM_H][3];
static fx       rt_planet[RM_W * RM_H];   /* floor hit t, or -1 */
static uint16_t rt_bg[RM_W * RM_H];
static fx       rt_invR;

static void ray_init(void) {
	int i, j;
	rt_invR = fdiv(F(1.0), F(0.9));
	for (j = 0; j < RM_H; j++) {
		fx v = fdiv(IF(RM_H / 2 - j), IF(RM_H / 2));
		for (i = 0; i < RM_W; i++) {
			int px = j * RM_W + i;
			fx dx = fmul(fdiv(IF(i - RM_W / 2), IF(RM_W / 2)), F(1.35));
			fx dy = v, dz = F(1.45);
			fx dl = fsqrt(fmul(dx, dx) + fmul(dy, dy) + fmul(dz, dz));
			dx = fdiv(dx, dl); dy = fdiv(dy, dl); dz = fdiv(dz, dl);
			rt_dir[px][0] = dx; rt_dir[px][1] = dy; rt_dir[px][2] = dz;

			if (dy < -F(0.001)) {
				fx tp = fdiv(-RT_ROY, dy);
				fx hx = fmul(dx, tp);
				fx hz = RT_ROZ + fmul(dz, tp);
				int chk = ((hx >> 16) + (hz >> 16)) & 1;
				int g = chk ? 12 : 4;           /* floor + baked lambert */
				rt_planet[px] = tp;
				rt_bg[px] = (uint16_t)((g >> 2) | (g << 5) | ((g / 3) << 10));
			} else {
				int sky = 5 + (int)((dy + F(0.3)) >> 13);
				if (sky < 2) sky = 2; if (sky > 13) sky = 13;
				rt_planet[px] = -1;
				rt_bg[px] = (uint16_t)((sky >> 1) | (sky << 5) | ((sky + 2) << 10));
			}
		}
	}
}

static void rt_render(uint32_t frame) {
	fx cx0, cy0, cz0, cc0, ox, oy, oz;
	fx Px, Py, Pz, invPz, u, v, du;
	int ic, jc, pri, prj, i0, i1, j0, j1, i, j;
	int parity = (int)(frame & 1);

	/* Interlaced: recompute only this frame's parity scanlines; the rest keep
	 * last frame. Floor/sky are static so kept rows stay identical -- only the
	 * moving sphere shows a faint 1-frame comb. Roughly halves rt_render.
	 * On each parity row, restore the baked background; the sphere bbox below
	 * then overpaints (also only on parity rows). */
	for (j = parity; j < RM_H; j += 2) {
		uint32_t *d = (uint32_t *)(rmbuf + j * RM_W);
		uint32_t *s = (uint32_t *)(rt_bg  + j * RM_W);
		int w = RM_W / 2;
		while (w--) *d++ = *s++;
	}

	cx0 = fmul((fx)isin((frame * 20) & 4095) << 4, F(1.7));
	cy0 = F(1.0) + fmul((fx)isin((frame * 40) & 4095) << 4, F(0.35));
	cz0 = F(1.6) + fmul((fx)icos((frame * 20) & 4095) << 4, F(0.9));
	ox = -cx0; oy = RT_ROY - cy0; oz = RT_ROZ - cz0;
	cc0 = fmul(ox, ox) + fmul(oy, oy) + fmul(oz, oz) - RT_R2;

	/* project sphere centre + radius to the RM grid (camera looks +z) */
	Px = cx0; Py = cy0 - RT_ROY; Pz = cz0 - RT_ROZ;
	if (Pz <= F(0.3)) return;
	invPz = fdiv(F(1.0), Pz);
	u  = fmul(F(1.45), fmul(Px, invPz));
	v  = fmul(F(1.45), fmul(Py, invPz));
	du = fmul(F(1.45), invPz);              /* (R=0.9 + margin) ~ 1.0 in u-units */
	ic = RM_W / 2 + (int)(fmul(fdiv(u, F(1.35)), IF(RM_W / 2)) >> 16);
	jc = RM_H / 2 - (int)(fmul(v, IF(RM_H / 2)) >> 16);
	pri = (int)(fmul(fdiv(du, F(1.35)), IF(RM_W / 2)) >> 16) + 2;
	prj = (int)(fmul(du, IF(RM_H / 2)) >> 16) + 2;
	i0 = ic - pri; i1 = ic + pri; j0 = jc - prj; j1 = jc + prj;
	if (i0 < 0) i0 = 0; if (i1 >= RM_W) i1 = RM_W - 1;
	if (j0 < 0) j0 = 0; if (j1 >= RM_H) j1 = RM_H - 1;
	if ((j0 & 1) != parity) j0++;          /* sphere only on parity rows */

	for (j = j0; j <= j1; j += 2) {
		for (i = i0; i <= i1; i++) {
			int px = j * RM_W + i;
			fx dx = rt_dir[px][0], dy = rt_dir[px][1], dz = rt_dir[px][2];
			fx floorT = rt_planet[px] >= 0 ? rt_planet[px] : F(1000);
			fx bb = fmul(ox, dx) + fmul(oy, dy) + fmul(oz, dz);
			fx disc = fmul(bb, bb) - cc0;
			fx th, hx, hy, hz, nx, ny, nz, diff, d2, ry;
			int g, r, b, di;
			if (disc <= 0) continue;
			th = -bb - fsqrt(disc);
			if (th <= 0 || th >= floorT) continue;
			hx = fmul(dx, th);
			hy = RT_ROY + fmul(dy, th);
			hz = RT_ROZ + fmul(dz, th);
			nx = fmul(hx - cx0, rt_invR);
			ny = fmul(hy - cy0, rt_invR);
			nz = fmul(hz - cz0, rt_invR);
			diff = fmul(nx, RT_LX) + fmul(ny, RT_LY) + fmul(nz, RT_LZ);
			if (diff < 0) diff = 0;
			di = (int)(diff >> 11);
			g = 7 + di; r = g >> 2; b = g / 3;
			d2 = fmul(dx, nx) + fmul(dy, ny) + fmul(dz, nz);
			ry = dy - fmul(F(2), fmul(d2, ny));
			if (ry < -F(0.04)) {
				fx rx = dx - fmul(F(2), fmul(d2, nx));
				fx rz = dz - fmul(F(2), fmul(d2, nz));
				fx tp = fdiv(-hy, ry);
				if (tp > 0 && ((((hx + fmul(rx, tp)) >> 16) +
				               ((hz + fmul(rz, tp)) >> 16)) & 1)) g += 6;
			}
			if (g > 31) g = 31; if (r > 31) r = 31; if (b > 31) b = 31;
			rmbuf[px] = (uint16_t)(r | (g << 5) | (b << 10));
		}
	}
}

static void draw_rt_backdrop(void) {
	int hw  = SCREEN_XRES / 2;   /* screen split at 160 */
	int huv = RM_W / 2 - 1;      /* 63: U range per 64-wide page */
	POLY_FT4 *p;

	p = (POLY_FT4 *)db_nextpri;
	setPolyFT4(p);
	setRGB0(p, 128, 128, 128);
	setXY4(p, 0, 0, hw, 0, 0, SCREEN_YRES, hw, SCREEN_YRES);
	setUV4(p, 0, 0, huv, 0, 0, RM_H - 1, huv, RM_H - 1);
	p->tpage = rt_tpage_l;
	addPrim(db[db_active].ot + (OT_LEN - 1), p);
	db_nextpri = (uint8_t *)(p + 1);

	p = (POLY_FT4 *)db_nextpri;
	setPolyFT4(p);
	setRGB0(p, 128, 128, 128);
	setXY4(p, hw, 0, SCREEN_XRES, 0, hw, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
	setUV4(p, 0, 0, huv, 0, 0, RM_H - 1, huv, RM_H - 1);
	p->tpage = rt_tpage_r;
	addPrim(db[db_active].ot + (OT_LEN - 1), p);
	db_nextpri = (uint8_t *)(p + 1);
}

/* --------------------------------------------------------------- vaporwave */
/* Classic synthwave backdrop: purple gradient sky, a banded semicircle sun on
 * the horizon, and a magenta neon perspective grid scrolling toward the camera.
 * All 2D primitives, basically free. */
#define VW_HORIZON 138
#define VW_SUNR    54

static void draw_vaporwave(uint32_t frame) {
	POLY_G4 *q;
	int i, k, fr;

	/* sky: dark purple (top) -> magenta (horizon) */
	q = (POLY_G4 *)db_nextpri;
	setPolyG4(q);
	setXY4(q, 0, 0, SCREEN_XRES, 0, 0, VW_HORIZON, SCREEN_XRES, VW_HORIZON);
	setRGB0(q, 26, 8, 48); setRGB1(q, 26, 8, 48);
	setRGB2(q, 122, 26, 112); setRGB3(q, 122, 26, 112);
	addPrim(db[db_active].ot + (OT_LEN - 1), q);
	db_nextpri = (uint8_t *)(q + 1);

	/* ground: magenta (horizon) -> near black */
	q = (POLY_G4 *)db_nextpri;
	setPolyG4(q);
	setXY4(q, 0, VW_HORIZON, SCREEN_XRES, VW_HORIZON, 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
	setRGB0(q, 58, 12, 70); setRGB1(q, 58, 12, 70);
	setRGB2(q, 8, 4, 20); setRGB3(q, 8, 4, 20);
	addPrim(db[db_active].ot + (OT_LEN - 1), q);
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
		addPrim(db[db_active].ot + (OT_LEN - 2), s);
		db_nextpri = (uint8_t *)(s + 1);
	}

	/* Canyon walls + floor grid share ONE scroll phase, so struts and grid
	 * lines rush toward the camera together: it reads as driving through. */
	fr = 255 - (int)((frame * 5) & 255);

	/* canyon walls as a perspective wireframe HEIGHTMAP: a dense grid mesh on
	 * each side that climbs steeply away from the road and recedes to the
	 * horizon (lateral lines = the slope, depth lines = into the screen). */
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
					int worldX = (80 + c * 46) * side;     /* out from road edge */
					int worldH = c * c * 9 + nz[(c * 5 + r + sc) & 15] * 2;
					cx[c] = 160 + (worldX * 256) / denom;
					cy[c] = VW_HORIZON + ((110 * 256) - worldH * 256) / denom;
				}
				/* lateral lines up the slope (top edge = ridge, brightest) */
				for (c = 0; c < 5; c++) {
					int top = (c == 4);
					add_line(cx[c], cy[c], cx[c + 1], cy[c + 1],
					         40, top ? 235 : 120, top ? 255 : 185, OT_LEN - 3);
				}
				/* depth lines connecting this row to the farther one */
				if (have)
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

	/* highway centre line: scrolling yellow dashes down the middle */
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

/* ------------------------------------------------------------------ plasma */

static uint8_t pr[PCOLS + 1][PROWS + 1];
static uint8_t pg[PCOLS + 1][PROWS + 1];
static uint8_t pb[PCOLS + 1][PROWS + 1];

static void draw_plasma(uint32_t frame, int env) {
	POLY_G4 *pol;
	int      gx, gy;
	int      t   = (int)frame;
	uint8_t  val = 64 + (env >> 2);          /* dim backdrop, lifts on beat */
	uint8_t  sat = 255 - (env >> 1);

	for (gx = 0; gx <= PCOLS; gx++) {
		for (gy = 0; gy <= PROWS; gy++) {
			int s = isin((gx * 256 + t * 8) & 4095)
			      + isin((gy * 320 + t * 11) & 4095)
			      + isin(((gx + gy) * 200 + t * 6) & 4095);
			/* bias hue into the cyan(128)..purple..magenta(212) band */
			uint8_t h = (uint8_t)(168 + (s >> 8));
			hsv(h, sat, val, &pr[gx][gy], &pg[gx][gy], &pb[gx][gy]);
		}
	}

	pol = (POLY_G4 *)db_nextpri;
	for (gx = 0; gx < PCOLS; gx++) {
		for (gy = 0; gy < PROWS; gy++) {
			int x = gx * PCW, y = gy * PCH;
			setPolyG4(pol);
			setXY4(pol, x, y, x + PCW, y, x, y + PCH, x + PCW, y + PCH);
			setRGB0(pol, pr[gx][gy],         pg[gx][gy],         pb[gx][gy]);
			setRGB1(pol, pr[gx + 1][gy],     pg[gx + 1][gy],     pb[gx + 1][gy]);
			setRGB2(pol, pr[gx][gy + 1],     pg[gx][gy + 1],     pb[gx][gy + 1]);
			setRGB3(pol, pr[gx + 1][gy + 1], pg[gx + 1][gy + 1], pb[gx + 1][gy + 1]);
			addPrim(db[db_active].ot + (OT_LEN - 1), pol);
			pol++;
		}
	}
	db_nextpri = (uint8_t *)pol;
}

/* --------------------------------------------------------------- starfield */

static short star_x[NSTARS], star_y[NSTARS];
static int   star_z[NSTARS];

static void star_reset(int i) {
	star_x[i] = (short)(rand() % (2 * STAR_SPREAD) - STAR_SPREAD);
	star_y[i] = (short)(rand() % (2 * STAR_SPREAD) - STAR_SPREAD);
	star_z[i] = rand() % STAR_FAR + 64;
}

static void draw_starfield(void) {
	int i;
	for (i = 0; i < NSTARS; i++) {
		int z, zp, sx, sy, sxp, syp, bri;
		star_z[i] -= STAR_SPEED;
		if (star_z[i] < 24) { star_reset(i); star_z[i] = STAR_FAR; }
		z  = star_z[i];
		zp = z + STAR_SPEED * 3;
		sx  = CENTERX + (star_x[i] * PROJ) / z;
		sy  = CENTERY + (star_y[i] * PROJ) / z;
		if (sx < 0 || sx >= SCREEN_XRES || sy < 0 || sy >= SCREEN_YRES) continue;
		sxp = CENTERX + (star_x[i] * PROJ) / zp;
		syp = CENTERY + (star_y[i] * PROJ) / zp;
		bri = 70 + (STAR_FAR - z) / 9;
		if (bri > 255) bri = 255;
		add_line(sxp, syp, sx, sy,
		         (uint8_t)(bri * 3 / 5), (uint8_t)bri, (uint8_t)bri,
		         OT_LEN - 2);
	}
}

/* ----------------------------------------------------------- wireframe cube */

static void draw_wirecube(VECTOR *pos, SVECTOR *rot, int32_t scale,
                          uint8_t r, uint8_t g, uint8_t b) {
	MATRIX  m;
	VECTOR  sv = { scale, scale, scale };
	DVECTOR sxy[8];
	int     i;

	RotMatrix(rot, &m);
	ScaleMatrix(&m, &sv);
	TransMatrix(&m, pos);
	gte_SetRotMatrix(&m);
	gte_SetTransMatrix(&m);

	for (i = 0; i < 8; i++) project(&cube_verts[i], &sxy[i]);

	for (i = 0; i < 12; i++) {
		DVECTOR *a = &sxy[cube_edges[i][0]];
		DVECTOR *c = &sxy[cube_edges[i][1]];
		if (!onscreen(a) || !onscreen(c)) continue;
		add_line(a->vx, a->vy, c->vx, c->vy, r, g, b, 300);
	}
}

/* A flying Windows flag: four solid panes (red/green/blue/yellow) in a 2x2
 * with a centre gap, tumbling in 3D like the old screensaver. */
static const SVECTOR flag_pane[4][4] = {
	{ { -58, -58, 0, 0 }, { -5, -58, 0, 0 }, { -58, -5, 0, 0 }, { -5, -5, 0, 0 } },
	{ {   5, -58, 0, 0 }, { 58, -58, 0, 0 }, {   5, -5, 0, 0 }, { 58, -5, 0, 0 } },
	{ { -58,   5, 0, 0 }, { -5,   5, 0, 0 }, { -58, 58, 0, 0 }, { -5, 58, 0, 0 } },
	{ {   5,   5, 0, 0 }, { 58,   5, 0, 0 }, {   5, 58, 0, 0 }, { 58, 58, 0, 0 } }
};
static const uint8_t flag_col[4][3] = {
	{ 255,  64,  64 },   /* red    */
	{  80, 220,  96 },   /* green  */
	{  72, 128, 255 },   /* blue   */
	{ 255, 208,  72 }    /* yellow */
};

static void draw_winflag(VECTOR *pos, SVECTOR *rot, int32_t scale, int wph, int amp) {
	MATRIX m;
	VECTOR sv = { scale, scale, scale };
	int    pane, otz, v;

	RotMatrix(rot, &m);
	ScaleMatrix(&m, &sv);
	TransMatrix(&m, pos);
	gte_SetRotMatrix(&m);
	gte_SetTransMatrix(&m);

	for (pane = 0; pane < 4; pane++) {
		SVECTOR  c[4];
		POLY_F4 *p = (POLY_F4 *)db_nextpri;
		/* ripple the corners in Z so the flag flutters like paper; amp is the
		 * gust strength passing this flag right now */
		for (v = 0; v < 4; v++) {
			c[v] = flag_pane[pane][v];
			c[v].vz = (short)((isin((c[v].vx * 14 + wph) & 4095) * amp) >> 12);
		}
		setPolyF4(p);
		setRGB0(p, flag_col[pane][0], flag_col[pane][1], flag_col[pane][2]);
		gte_ldv3(&c[0], &c[1], &c[2]);
		gte_rtpt();
		gte_stsxy0(&p->x0);
		gte_stsxy1(&p->x1);
		gte_stsxy2(&p->x2);
		gte_ldv0(&c[3]);
		gte_rtps();
		gte_stsxy(&p->x3);
		gte_avsz4();
		gte_stotz(&otz);
		if (otz <= 0 || otz >= OT_LEN) otz = OT_LEN / 2;
		addPrim(db[db_active].ot + otz, p);
		db_nextpri = (uint8_t *)(p + 1);
	}
}

/* A flying Malort bottle. Faked into 3D with CROSSED BILLBOARDS: the same
 * texture on two quads at 90 degrees, so as it spins around its vertical axis
 * one face is always toward the camera and it never collapses to a flat line.
 * (A bottle is ~rotationally symmetric, so the silhouette stays consistent.) */
static void draw_bottle(VECTOR *pos, SVECTOR *rot, int32_t scale) {
	static const SVECTOR quads[2][4] = {
		{ { -48, -96, 0, 0 }, { 48, -96, 0, 0 },        /* facing +Z */
		  { -48,  96, 0, 0 }, { 48,  96, 0, 0 } },
		{ { 0, -96, -48, 0 }, { 0, -96, 48, 0 },        /* facing +X (perp.) */
		  { 0,  96, -48, 0 }, { 0,  96, 48, 0 } }
	};
	MATRIX m;
	VECTOR sv = { scale, scale, scale };
	int    q, otz;

	RotMatrix(rot, &m);
	ScaleMatrix(&m, &sv);
	TransMatrix(&m, pos);
	gte_SetRotMatrix(&m);
	gte_SetTransMatrix(&m);

	for (q = 0; q < 2; q++) {
		const SVECTOR *v = quads[q];
		POLY_FT4 *p = (POLY_FT4 *)db_nextpri;
		setPolyFT4(p);
		setRGB0(p, 128, 128, 128);
		gte_ldv3(&v[0], &v[1], &v[2]);
		gte_rtpt();
		gte_stsxy0(&p->x0);
		gte_stsxy1(&p->x1);
		gte_stsxy2(&p->x2);
		gte_ldv0(&v[3]);
		gte_rtps();
		gte_stsxy(&p->x3);
		gte_avsz4();
		gte_stotz(&otz);
		setUV4(p, 0, 0, 63, 0, 0, 127, 63, 127);
		p->tpage = bottle_tpage;
		p->clut  = bottle_clut;
		if (otz <= 0 || otz >= OT_LEN) otz = OT_LEN / 2;
		addPrim(db[db_active].ot + otz, p);
		db_nextpri = (uint8_t *)(p + 1);
	}
}

/* The DVD screensaver logo, bouncing around the background and changing to a
 * new neon colour on every wall hit. */
#define DVD_W 64
#define DVD_H 32
static void draw_dvd(void) {
	static const uint8_t cols[6][3] = {
		{  40, 240, 255 },   /* cyan    */
		{ 255,  60, 200 },   /* magenta */
		{ 150,  80, 255 },   /* purple  */
		{ 255, 100, 150 },   /* hot pink*/
		{ 255, 220,  90 },   /* gold    */
		{  90, 255, 160 }    /* mint    */
	};
	static int x = 40, y = 70, vx = 2, vy = 1, ci = 0;
	POLY_FT4 *p;

	x += vx; y += vy;
	if (x <= 0)               { x = 0; vx = -vx; ci = (ci + 1) % 6; }
	if (x >= SCREEN_XRES - DVD_W) { x = SCREEN_XRES - DVD_W; vx = -vx; ci = (ci + 1) % 6; }
	if (y <= 0)               { y = 0; vy = -vy; ci = (ci + 1) % 6; }
	if (y >= SCREEN_YRES - DVD_H) { y = SCREEN_YRES - DVD_H; vy = -vy; ci = (ci + 1) % 6; }

	p = (POLY_FT4 *)db_nextpri;
	setPolyFT4(p);
	setRGB0(p, cols[ci][0], cols[ci][1], cols[ci][2]);   /* tint the white logo */
	setXY4(p, x, y, x + DVD_W, y, x, y + DVD_H, x + DVD_W, y + DVD_H);
	setUV4(p, 0, 0, DVD_W - 1, 0, 0, DVD_H - 1, DVD_W - 1, DVD_H - 1);
	p->tpage = dvd_tpage;
	p->clut  = dvd_clut;
	addPrim(db[db_active].ot + (OT_LEN - 5), p);   /* background layer */
	db_nextpri = (uint8_t *)(p + 1);
}

/* --------------------------------------------------------- SecKC ASCII hex */

/* The SecKC ASCII skull as a spinning, EXTRUDED 3D object. We stack several
 * additively-blended copies of the textured quad along the depth axis; black
 * texels are transparent, so the green ASCII shape gets real thickness and you
 * see it as a solid slab when it tumbles (classic PSX affine-warp wobble). */
#define TEX_SLICES  8
#define TEX_DEPTH   20

static void draw_seckc_tex(uint32_t frame, int env) {
	MATRIX  m;
	SVECTOR rot;
	VECTOR  pos = { 0, 0, 360 };
	int     fp  = isin((frame * 80) & 4095);  /* fast breathing pulse */
	int32_t scl = ONE + (env << 2) + ((fp < 0 ? -fp : fp) >> 4);
	VECTOR  sv  = { scl, scl, scl };
	int     k;

	rot.vx = (short)(isin(frame * 10) >> 5);  /* tumble */
	rot.vy = (short)(frame * 17);             /* faster spin */
	rot.vz = (short)(isin(frame * 7) >> 7);
	rot.pad = 0;

	RotMatrix(&rot, &m);
	ScaleMatrix(&m, &sv);
	TransMatrix(&m, &pos);
	gte_SetRotMatrix(&m);
	gte_SetTransMatrix(&m);

	/* back-to-front slices; darker at the back for depth shading */
	for (k = 0; k < TEX_SLICES; k++) {
		short sz = (short)(-TEX_DEPTH + (2 * TEX_DEPTH * k) / (TEX_SLICES - 1));
		SVECTOR q[4] = {
			{ -120, -120, sz, 0 }, {  120, -120, sz, 0 },
			{ -120,  120, sz, 0 }, {  120,  120, sz, 0 }
		};
		uint8_t mod = (uint8_t)(20 + (26 * k) / (TEX_SLICES - 1) + (env >> 3));
		POLY_FT4 *pol = (POLY_FT4 *)db_nextpri;
		int otz;

		setPolyFT4(pol);
		setSemiTrans(pol, 1);
		setRGB0(pol, mod, mod, mod);

		gte_ldv3(&q[0], &q[1], &q[2]);
		gte_rtpt();
		gte_stsxy0(&pol->x0);
		gte_stsxy1(&pol->x1);
		gte_stsxy2(&pol->x2);
		gte_ldv0(&q[3]);
		gte_rtps();
		gte_stsxy(&pol->x3);
		gte_avsz4();
		gte_stotz(&otz);

		setUV4(pol, 0, 0, 255, 0, 0, 255, 255, 255);
		pol->tpage = tex_tpage;
		pol->clut  = tex_clut;

		if (otz <= 0 || otz >= OT_LEN) otz = OT_LEN / 2;
		addPrim(db[db_active].ot + otz, pol);
		db_nextpri = (uint8_t *)(pol + 1);
	}
}

/* "SecKC" as a chunky EXTRUDED 3D text logo: every vector-font stroke becomes a
 * filled beam with a darker back face, gently swung so it stays readable. */
static void draw_seckc_logo(uint32_t frame, int env) {
	static const char *word = "SecKC";
	MATRIX  m;
	SVECTOR rot;
	VECTOR  pos = { 0, 0, 360 };
	int     fp  = isin((frame * 70) & 4095);
	int32_t scl = ONE + (env << 1) + ((fp < 0 ? -fp : fp) >> 6);
	VECTOR  sv  = { scl, scl, scl };
	int     gi, gsz[5], gw[5], total = 0, penx;
	const int GS = 13, DEPTH = 20;

	rot.vx = (short)(isin(frame * 8) >> 6);
	rot.vy = (short)(isin(frame * 11) >> 4);   /* gentle readable swing */
	rot.vz = (short)(isin(frame * 5) >> 8);
	rot.pad = 0;
	RotMatrix(&rot, &m);
	ScaleMatrix(&m, &sv);
	TransMatrix(&m, &pos);
	gte_SetRotMatrix(&m);
	gte_SetTransMatrix(&m);

	for (gi = 0; gi < 5; gi++) {
		gsz[gi] = (word[gi] >= 'a' && word[gi] <= 'z') ? (GS * 7 / 10) : GS;
		gw[gi]  = VF_W * gsz[gi] + gsz[gi];
		total  += gw[gi];
	}
	penx = -total / 2;

	for (gi = 0; gi < 5; gi++) {
		const VGlyph *gl = vglyph(word[gi]);
		int gs = gsz[gi], s;
		for (s = 0; s < gl->nseg; s++) {
			const signed char *sg = gl->seg + s * 4;
			short ax = (short)(penx + sg[0] * gs), ay = (short)(3 * GS - sg[1] * gs);
			short bx = (short)(penx + sg[2] * gs), by = (short)(3 * GS - sg[3] * gs);
			SVECTOR fa = { ax, ay, 0, 0 }, fb = { bx, by, 0, 0 };
			SVECTOR ba = { ax, ay, DEPTH, 0 }, bb = { bx, by, DEPTH, 0 };
			DVECTOR pfa, pfb, pba, pbb;
			project(&fa, &pfa); project(&fb, &pfb);
			project(&ba, &pba); project(&bb, &pbb);
			add_fillstroke(pba, pbb, 4, 120, 24, 96, 100);    /* extruded back (magenta) */
			add_fillstroke(pfa, pfb, 4, 70, 235, 255, 95);    /* bright front (cyan)     */
		}
		penx += gw[gi];
	}
	(void)env;
}

/* ---------------------------------------------------------------- scroller */

static const char *GREETZ =
	"SECKC PRESENTS A PLAYSTATION CRACKTRO FOR THE KANSAS CITY HACKER SCENE    *    "
	"DESTROY NO DATA  -  MAINTAIN NO PERSISTENCE  -  ABOVE ALL ELSE DO NO HARM    *    "
	"WE ARE SECKC  -  KANSAS CITYS HACKERS  -  LIVE EVERY MONTH AT KNUCKLEHEADS    *    "
	"GREETZ TO DC816  COWTOWN COMPUTER CONGRESS  BSIDESKC  HAMMERSPACE    *    "
	"AND EVERY HACKER MAKER AND BREAKER IN THE 816    *    "
	"SETEC ASTRONOMY  /  SHALL WE PLAY A GAME  /  FIGHT FOR THE USER    *    "
	"STAY CURIOUS  -  KEEP HACKING  -  110 BPM ON THE PSX    *    ";

/* 3D perspective sine-scroller: each glyph is a flat billboard placed in world
 * space at its own depth; a travelling sine sends the ribbon of text toward and
 * away from the camera, so perspective grows the near letters and shrinks the
 * far ones. */
#define S3_ADV    34     /* world units between glyph origins */
#define S3_SCALE  6      /* world units per font grid unit    */
#define S3_BASEZ  340    /* base depth                        */
#define S3_WAVE   55     /* depth wave amplitude              */
#define S3_YWAVE  18     /* vertical wave amplitude           */
#define S3_Y      110    /* baseline world Y (+down)          */

static void draw_scroller(uint32_t frame) {
	MATRIX  m;
	SVECTOR rot = { 0, 0, 0, 0 };
	VECTOR  tr  = { 0, 0, 0 };
	int     len = 0, total, eff, i;
	const char *p = GREETZ;
	while (*p++) len++;
	total = len * S3_ADV;
	eff   = (int)((frame * 2) % total);

	/* identity transform: vertices are already in view space */
	RotMatrix(&rot, &m);
	TransMatrix(&m, &tr);
	gte_SetRotMatrix(&m);
	gte_SetTransMatrix(&m);

	for (i = 0; i < len; i++) {
		const VGlyph *gl = vglyph(GREETZ[i]);
		int pass;
		if (gl->nseg == 0) continue;
		for (pass = 0; pass < 2; pass++) {
			int wx = i * S3_ADV - eff + (pass ? total : 0);
			int ph, wz, wy, s;
			if (wx < -440 || wx > 440) continue;
			ph = (wx * 8 + (int)frame * 40) & 4095;
			wz = S3_BASEZ + ((isin(ph) * S3_WAVE) >> 12);
			wy = S3_Y + ((isin((ph + 1024) & 4095) * S3_YWAVE) >> 12);
			/* brighter the closer it waves toward the camera */
			{
				int g = 255 - ((wz - (S3_BASEZ - S3_WAVE)) * 90) / (2 * S3_WAVE);
				uint8_t gg = (uint8_t)(g < 130 ? 130 : g);
				int wd = (2 * S3_BASEZ) / wz;          /* perspective stroke width */
				int ed = (S3_BASEZ - wz) / 4;          /* extrude depth, near only */
				if (wd < 1) wd = 1;
				if (wd > 4) wd = 4;
				if (ed > 18) ed = 18;                  /* far letters go flat (no ghost) */
				for (s = 0; s < gl->nseg; s++) {
					const signed char *sg = gl->seg + s * 4;
					short vax = (short)(wx + sg[0] * S3_SCALE);
					short vay = (short)(wy - sg[1] * S3_SCALE);
					short vbx = (short)(wx + sg[2] * S3_SCALE);
					short vby = (short)(wy - sg[3] * S3_SCALE);
					SVECTOR fa = { vax, vay, (short)wz, 0 };
					SVECTOR fb = { vbx, vby, (short)wz, 0 };
					DVECTOR pfa, pfb;
					project(&fa, &pfa); project(&fb, &pfb);
					/* extruded back face only when the letter is near enough that
					 * the depth reads as 3D rather than a doubled ghost */
					if (ed > 1) {
						SVECTOR ba = { vax, vay, (short)(wz + ed), 0 };
						SVECTOR bb = { vbx, vby, (short)(wz + ed), 0 };
						DVECTOR pba, pbb;
						project(&ba, &pba); project(&bb, &pbb);
						add_fillstroke(pba, pbb, wd, (uint8_t)(gg / 3), 16, (uint8_t)(gg / 4), 3);
					}
					add_fillstroke(pfa, pfb, wd, (uint8_t)gg, 46, (uint8_t)(gg * 3 / 4), 2);
				}
			}
		}
	}
}

static void display(void);

/* Custom boot splash: the SecKC ASCII skull spins up on black with a title,
 * fading in then out, before the demo proper begins. */
static void boot_splash(void) {
	uint32_t f;
	for (f = 0; f < 160; f++) {
		int env = (f < 28) ? (int)f * 9 : (f > 128 ? (160 - (int)f) * 8 : 255);
		MATRIX  m;
		SVECTOR rot = { 0, 0, 0, 0 };
		VECTOR  pos = { 0, 0, 300 };
		VECTOR  sv  = { ONE, ONE, ONE };
		int     k;
		uint8_t e;
		if (env > 255) env = 255;
		if (env < 0) env = 0;
		e = (uint8_t)env;

		setRGB0(&db[db_active].draw, 0, 0, 6);

		rot.vx = (short)(isin(f * 7) >> 6);
		rot.vy = (short)(f * 9);
		RotMatrix(&rot, &m); ScaleMatrix(&m, &sv); TransMatrix(&m, &pos);
		gte_SetRotMatrix(&m); gte_SetTransMatrix(&m);
		for (k = 0; k < TEX_SLICES; k++) {
			short sz = (short)(-TEX_DEPTH + (2 * TEX_DEPTH * k) / (TEX_SLICES - 1));
			SVECTOR q[4] = { { -120, -120, sz, 0 }, { 120, -120, sz, 0 },
			                 { -120, 120, sz, 0 }, { 120, 120, sz, 0 } };
			uint8_t mod = (uint8_t)(((20 + (26 * k) / (TEX_SLICES - 1)) * env) / 255);
			POLY_FT4 *pol = (POLY_FT4 *)db_nextpri;
			int otz;
			setPolyFT4(pol); setSemiTrans(pol, 1); setRGB0(pol, mod, mod, mod);
			gte_ldv3(&q[0], &q[1], &q[2]); gte_rtpt();
			gte_stsxy0(&pol->x0); gte_stsxy1(&pol->x1); gte_stsxy2(&pol->x2);
			gte_ldv0(&q[3]); gte_rtps(); gte_stsxy(&pol->x3);
			gte_avsz4(); gte_stotz(&otz);
			if (otz <= 0 || otz >= OT_LEN) otz = OT_LEN / 2;
			addPrim(db[db_active].ot + otz, pol);
			db_nextpri = (uint8_t *)(pol + 1);
		}

		draw_text2d("SECKC", 160, 198, 4, e, (uint8_t)(e / 4), (uint8_t)(e * 3 / 4), 2);
		draw_text2d("KANSAS CITY HACKERS", 160, 216, 1,
		            (uint8_t)(e * 4 / 5), e, e, 2);

		{
			DR_TPAGE *tp = (DR_TPAGE *)db_nextpri;
			setDrawTPage(tp, 0, 1, getTPage(0, 1, 0, 0));
			addPrim(db[db_active].ot + (OT_LEN - 1), tp);
			db_nextpri = (uint8_t *)(tp + 1);
		}
		display();
	}
}

/* --------------------------------------------------------------------- main */

static void init(void);
static void display(void);

int main(void) {
	uint32_t frame = 0;

	init();
	boot_splash();

	/* kick off the CD-DA music track (2), looping (CdlModeRept) */
#if PERF_CDDA
	{
		uint8_t mode = CdlModeDA | CdlModeRept, track = 2;
		CdControl(CdlSetmode, &mode, 0);
		CdControl(CdlPlay, &track, 0);
	}
#endif

	while (1) {
		/* VU meter: drive the pulse from the live audio peak (fast attack,
		 * slow decay) so the visuals react to whatever music is playing */
		int lvl = audio_level(frame);         /* 0..32767 */
		int target = lvl >> 6;                /* 0..255-ish */
		int env;
		uint8_t hue = (uint8_t)(frame * 3);
		if (target > 255) target = 255;
		if (target > g_vu) g_vu = target;             /* attack */
		else g_vu -= (g_vu - target) >> 2;            /* decay  */
		env = g_vu;

#if PERF_BG
#if VAPORWAVE
		draw_vaporwave(frame);
#else
		rt_render(frame);
		draw_rt_backdrop();
#endif
#endif
		draw_starfield();
		draw_dvd();

		/* flying Malort bottles tumbling through the scene */
		{
			int i;
			for (i = 0; i < RING_CUBES; i++) {
				int     a   = (frame * 14) + (i * 4096) / RING_CUBES;
				int32_t rad = 400;             /* constant orbit */
				VECTOR  pos = { (icos(a) * rad) >> 12,
				                (isin(a) * rad) >> 12, 640 };
				SVECTOR rot = { (short)(isin(frame * 9 + i * 500) >> 5),
				                (short)(frame * 8 + i * 700),
				                (short)(isin(frame * 6 + i * 300) >> 6), 0 };
				draw_bottle(&pos, &rot, ONE);
			}
		}

		draw_seckc_tex(frame, env);
		draw_scroller(frame);

		/* additive blend mode for every semi-transparent prim this frame */
#if PERF_BG
		{
			DR_TPAGE *tp = (DR_TPAGE *)db_nextpri;
			setDrawTPage(tp, 0, 1, getTPage(0, 1, 0, 0));
			addPrim(db[db_active].ot + (OT_LEN - 1), tp);
			db_nextpri = (uint8_t *)(tp + 1);
		}
#endif

		/* re-trigger the CD-DA track if repeat ever drops it (cheap status poll) */
#if PERF_CDDA
		if (frame > 90 && (frame % 60) == 0) {
			uint8_t res[16];
			CdControl(CdlNop, 0, res);
			if (!(res[0] & CdlStatPlay)) {
				uint8_t track = 2;
				CdControl(CdlPlay, &track, 0);
			}
		}
#endif

		(void)hue;
		display();
		frame++;
	}
	return 0;
}

/* --------------------------------------------------------------------- init */

static void init(void) {
	ResetGraph(0);

	SetDefDispEnv(&db[0].disp, 0, 0,          SCREEN_XRES, SCREEN_YRES);
	SetDefDrawEnv(&db[0].draw, 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
	SetDefDispEnv(&db[1].disp, 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
	SetDefDrawEnv(&db[1].draw, 0, 0,          SCREEN_XRES, SCREEN_YRES);

	db[0].draw.isbg = 1; db[0].draw.dtd = 1;
	db[1].draw.isbg = 1; db[1].draw.dtd = 1;
	setRGB0(&db[0].draw, 0, 0, 6);
	setRGB0(&db[1].draw, 0, 0, 6);

	ClearOTagR(db[0].ot, OT_LEN);
	ClearOTagR(db[1].ot, OT_LEN);
	db_active  = 0;
	db_nextpri = db[0].p;

	srand(0x5ecc);
	{ int i; for (i = 0; i < NSTARS; i++) star_reset(i); }

	CdInit();
	SpuInit();
	SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
	SpuSetCommonMasterVolume(0x3fff, 0x3fff);
	SpuSetCommonCDVolume(0x3fff, 0x3fff);   /* CD-DA -> SPU input (defaults to 0!) */
	CdControl(CdlDemute, 0, 0);             /* un-mute the CD audio output */

	InitGeom();
	gte_SetGeomOffset(CENTERX, CENTERY);
	gte_SetGeomScreen(PROJ);

	/* Upload the SecKC ASCII-skull texture + CLUT to VRAM (x>=320, clear of
	 * the two 320x240 framebuffers). */
	{
		TIM_IMAGE tim;
		GetTimInfo(seckc_tim, &tim);
		LoadImage(tim.crect, tim.caddr);
		LoadImage(tim.prect, tim.paddr);
		DrawSync(0);
		tex_tpage = getTPage(tim.mode & 0x3, 1, tim.prect->x, tim.prect->y);
		tex_clut  = getClut(tim.crect->x, tim.crect->y);
	}

	/* Malort bottle texture (VRAM x=448, clear of the skull at x320). */
	{
		TIM_IMAGE tim;
		GetTimInfo(bottle_tim, &tim);
		LoadImage(tim.crect, tim.caddr);
		LoadImage(tim.prect, tim.paddr);
		DrawSync(0);
		bottle_tpage = getTPage(tim.mode & 0x3, 0, tim.prect->x, tim.prect->y);
		bottle_clut  = getClut(tim.crect->x, tim.crect->y);
	}

	/* DVD logo texture (VRAM x=512, y=256 — its own page). */
	{
		TIM_IMAGE tim;
		GetTimInfo(dvd_tim, &tim);
		LoadImage(tim.crect, tim.caddr);
		LoadImage(tim.prect, tim.paddr);
		DrawSync(0);
		dvd_tpage = getTPage(tim.mode & 0x3, 0, tim.prect->x, tim.prect->y);
		dvd_clut  = getClut(tim.crect->x, tim.crect->y);
	}

	/* 16bpp scratch page for the raytracer output (VRAM x=512, clear of the
	 * framebuffers and the skull texture). */
	rt_tpage_l = getTPage(2, 0, 512, 0);   /* rmbuf cols 0..63   -> VRAM x512 */
	rt_tpage_r = getTPage(2, 0, 576, 0);   /* rmbuf cols 64..127 -> VRAM x576 */
	ray_init();
}

static void display(void) {
	DrawSync(0);
	VSync(0);

	/* upload this frame's raytraced image into the VRAM scratch page */
#if !VAPORWAVE
	{
		RECT r = { 512, 0, RM_W, RM_H };
		LoadImage(&r, (uint32_t *)rmbuf);
		DrawSync(0);
	}
#endif

	PutDrawEnv(&db[db_active].draw);
	PutDispEnv(&db[db_active].disp);
	SetDispMask(1);
	DrawOTag(db[db_active].ot + (OT_LEN - 1));

	db_active ^= 1;
	db_nextpri = db[db_active].p;
	ClearOTagR(db[db_active].ot, OT_LEN);
}
