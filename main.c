/*
 * rave-psx : a SecKC demoscene cracktro for the MiSTer PSX core.
 *
 *   - themed cyan/magenta gouraud table-plasma backdrop
 *   - warp starfield
 *   - a rotating neon WIREFRAME "SecKC" logo (extruded, chrome-ish)
 *   - additive-glow wireframe accent cubes
 *   - a sine-scroller greetz to the KC hacker scene
 *
 * Everything additively blended and synced to a 140 BPM clock.
 * Built with PSn00bSDK. NTSC 320x240, double buffered.
 */

#include <stdint.h>
#include <stdlib.h>
#include <psxgpu.h>
#include <psxgte.h>
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

#define BPM         110
#define FPB         ((60 * 60) / BPM)       /* frames/beat @ NTSC 60fps */

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
	uint32_t *sp = (uint32_t *)rt_bg, *dp = (uint32_t *)rmbuf;
	int n = RM_W * RM_H / 2;

	/* The floor + sky are static, so blit the baked background, then overpaint
	 * ONLY the sphere's screen-space bounding box. ~80% of pixels never run the
	 * intersection test. */
	while (n--) *dp++ = *sp++;

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

	for (j = j0; j <= j1; j++) {
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
		uint8_t mod = (uint8_t)(20 + (26 * k) / (TEX_SLICES - 1));
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
#define S3_BASEZ  320    /* base depth                        */
#define S3_WAVE   90     /* depth wave amplitude              */
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
				if (wd < 1) wd = 1;
				if (wd > 4) wd = 4;
				for (s = 0; s < gl->nseg; s++) {
					const signed char *sg = gl->seg + s * 4;
					short vax = (short)(wx + sg[0] * S3_SCALE);
					short vay = (short)(wy - sg[1] * S3_SCALE);
					short vbx = (short)(wx + sg[2] * S3_SCALE);
					short vby = (short)(wy - sg[3] * S3_SCALE);
					SVECTOR fa = { vax, vay, (short)wz, 0 };
					SVECTOR fb = { vbx, vby, (short)wz, 0 };
					SVECTOR ba = { vax, vay, (short)(wz + 14), 0 };
					SVECTOR bb = { vbx, vby, (short)(wz + 14), 0 };
					DVECTOR pfa, pfb, pba, pbb;
					project(&fa, &pfa); project(&fb, &pfb);
					project(&ba, &pba); project(&bb, &pbb);
					/* extruded back face (dim) then bright filled front face */
					add_fillstroke(pba, pbb, wd, 16, (uint8_t)(gg / 3), 50, 3);
					add_fillstroke(pfa, pfb, wd, 40, gg, 120, 2);
				}
			}
		}
	}
}

/* --------------------------------------------------------------------- main */

static void init(void);
static void display(void);

int main(void) {
	uint32_t frame = 0;

	init();

	while (1) {
		int beat_frame = frame % FPB;
		int env        = 255 - ((beat_frame * 255) / FPB);
		if (env < 0) env = 0;
		uint8_t hue = (uint8_t)(frame * 3);

		rt_render(frame);
		draw_rt_backdrop();
		draw_starfield();

		/* orbiting wireframe accent cubes */
		{
			int i;
			for (i = 0; i < RING_CUBES; i++) {
				int     a   = (frame * 14) + (i * 4096) / RING_CUBES;
				int32_t rad = 360 + (env >> 1);
				VECTOR  pos = { (icos(a) * rad) >> 12,
				                (isin(a) * rad) >> 12, 760 };
				SVECTOR rot = { (short)(-frame * 20), (short)(frame * 13),
				                (short)(frame * 8), 0 };
				uint8_t r, g, b;
				hsv((uint8_t)(150 + i * 18), 255, 255, &r, &g, &b);
				draw_wirecube(&pos, &rot, ONE / 3, r, g, b);
			}
		}

		draw_seckc_tex(frame, env);
		draw_scroller(frame);

		/* additive blend mode for every semi-transparent prim this frame */
		{
			DR_TPAGE *tp = (DR_TPAGE *)db_nextpri;
			setDrawTPage(tp, 0, 1, getTPage(0, 1, 0, 0));
			addPrim(db[db_active].ot + (OT_LEN - 1), tp);
			db_nextpri = (uint8_t *)(tp + 1);
		}

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
	{
		RECT r = { 512, 0, RM_W, RM_H };
		LoadImage(&r, (uint32_t *)rmbuf);
		DrawSync(0);
	}

	PutDrawEnv(&db[db_active].draw);
	PutDispEnv(&db[db_active].disp);
	SetDispMask(1);
	DrawOTag(db[db_active].ot + (OT_LEN - 1));

	db_active ^= 1;
	db_nextpri = db[db_active].p;
	ClearOTagR(db[db_active].ot, OT_LEN);
}
