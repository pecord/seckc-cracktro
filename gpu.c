/*
 * gpu.c : double-buffer setup, the per-frame / per-pass plumbing, and the shared
 * primitive + colour helpers. See gpu.h for the immediate-mode model.
 */
#include "gpu.h"

DB       db[2];
int      db_active  = 0;
uint8_t *db_nextpri;

/* VRAM layout (1024x512, 16bpp):
 *   (0,0)   320x240  framebuffer A   (0,240) 320x240 framebuffer B
 *   x>=320            textures + scratch (see logo.c / sphere.c)
 * db[0] displays A and draws into B; db[1] is the mirror image, so the buffer we
 * draw into is always the one NOT on screen. */
void gpu_init(void) {
	ResetGraph(0);

	SetDefDispEnv(&db[0].disp, 0, 0,           SCREEN_XRES, SCREEN_YRES);
	SetDefDrawEnv(&db[0].draw, 0, SCREEN_YRES,  SCREEN_XRES, SCREEN_YRES);
	SetDefDispEnv(&db[1].disp, 0, SCREEN_YRES,  SCREEN_XRES, SCREEN_YRES);
	SetDefDrawEnv(&db[1].draw, 0, 0,           SCREEN_XRES, SCREEN_YRES);

	db[0].draw.isbg = 1; db[0].draw.dtd = 1;   /* clear-to-bg + dither on */
	db[1].draw.isbg = 1; db[1].draw.dtd = 1;
	setRGB0(&db[0].draw, 0, 0, 6);
	setRGB0(&db[1].draw, 0, 0, 6);

	ClearOTagR(db[0].ot, OT_LEN);
	ClearOTagR(db[1].ot, OT_LEN);
	db_active  = 0;
	db_nextpri = db[0].p;

	InitGeom();
	gte_SetGeomOffset(CENTERX, CENTERY);
	gte_SetGeomScreen(PROJ);
}

void gpu_pass_begin(void) {
	db_nextpri = db[db_active].p;
	ClearOTagR(db[db_active].ot, OT_LEN);
}

void gpu_present(void) {
	DrawSync(0);
	VSync(0);
	PutDrawEnv(&db[db_active].draw);
	PutDispEnv(&db[db_active].disp);
	SetDispMask(1);
	DrawOTag(db[db_active].ot + (OT_LEN - 1));
	db_active ^= 1;
}

void gpu_additive_blend(int otz) {
	DR_TPAGE *tp = (DR_TPAGE *)db_nextpri;
	setDrawTPage(tp, 0, 1, getTPage(0, 1, 0, 0));
	addPrim(OT_AT(otz), tp);
	db_nextpri = (uint8_t *)(tp + 1);
}

void add_line(int x0, int y0, int x1, int y1,
              uint8_t r, uint8_t g, uint8_t b, int otz) {
	LINE_F2 *l = (LINE_F2 *)db_nextpri;
	setLineF2(l);
	setRGB0(l, r, g, b);
	setXY2(l, x0, y0, x1, y1);
	addPrim(OT_AT(otz), l);
	db_nextpri = (uint8_t *)(l + 1);
}

void add_fillstroke(DVECTOR a, DVECTOR b, int w,
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
	/* At small scales (thin strokes, w==1) integer truncation rounds the
	 * perpendicular of a DIAGONAL stroke to (0,0), collapsing the quad to a
	 * zero-width sliver -- so diagonals (A,K,N,S,R...) vanish while vertical and
	 * horizontal strokes survive. Force at least 1px in each needed axis. */
	if (px == 0 && sdy != 0) px = sdy > 0 ? -1 : 1;
	if (py == 0 && sdx != 0) py = sdx > 0 ? 1 : -1;
	/* drop strokes that project far off-screen (the GPU would discard the
	 * oversized quad anyway, which made waving letters flicker out) */
	if (a.vx < -400 || a.vx > 720 || b.vx < -400 || b.vx > 720 ||
	    a.vy < -400 || a.vy > 640 || b.vy < -400 || b.vy > 640) return;
	p = (POLY_F4 *)db_nextpri;
	setPolyF4(p);                  /* opaque: stays readable over the bright floor */
	setRGB0(p, r, g, bl);
	setXY4(p, a.vx + px, a.vy + py, b.vx + px, b.vy + py,
	          a.vx - px, a.vy - py, b.vx - px, b.vy - py);
	addPrim(OT_AT(otz), p);
	db_nextpri = (uint8_t *)(p + 1);
}

void project(const SVECTOR *v, DVECTOR *out) {
	gte_ldv0(v);
	gte_rtps();
	gte_stsxy(out);
}

int onscreen(const DVECTOR *p) {
	return p->vx > -200 && p->vx < SCREEN_XRES + 200 &&
	       p->vy > -200 && p->vy < SCREEN_YRES + 200;
}

uint16_t rgb5(int r, int g, int b) {
	if (r < 0) r = 0; if (r > 31) r = 31;
	if (g < 0) g = 0; if (g > 31) g = 31;
	if (b < 0) b = 0; if (b > 31) b = 31;
	return (uint16_t)(r | (g << 5) | (b << 10));
}

uint32_t isqrt32(uint32_t n) {
	uint32_t c = 0, d = 1U << 30;
	while (d > n) d >>= 2;
	while (d) {
		if (n >= c + d) { n -= c + d; c = (c >> 1) + d; }
		else c >>= 1;
		d >>= 2;
	}
	return c;
}
