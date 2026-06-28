/*
 * gpu.c : double-buffer setup, the per-frame / per-pass plumbing, and the shared
 * primitive + colour helpers. See gpu.h for the immediate-mode model.
 */
#include "gpu.h"
#if PERF_HUD
#include <psxapi.h>      /* root counters, for the on-screen profiler */
#include "text.h"        /* draw_text2d, for the readout */
static int hud_work = 0, hud_total = 263, hud_peak = 0;
#endif

DB       db[2];
int      db_active  = 0;
uint8_t *db_nextpri;

/* VRAM layout (1024x512, 16bpp):
 *   (0,0)   320x240  framebuffer A   (0,256) 320x240 framebuffer B
 *   x>=320            textures + scratch (see logo.c / sphere.c)
 * db[0] displays A and draws into B; db[1] is the mirror image, so the buffer we
 * draw into is always the one NOT on screen.
 *
 * B starts at y=256, not y=240, so it can also be sampled as a texture page for
 * the sphere's live reflection capture. Texture-page Y addressing is 256-aligned. */
void gpu_init(void) {
	const int fb_a_y = 0;
	const int fb_b_y = 256;
	ResetGraph(0);

	SetDefDispEnv(&db[0].disp, 0, fb_a_y, SCREEN_XRES, SCREEN_YRES);
	SetDefDrawEnv(&db[0].draw, 0, fb_b_y, SCREEN_XRES, SCREEN_YRES);
	SetDefDispEnv(&db[1].disp, 0, fb_b_y, SCREEN_XRES, SCREEN_YRES);
	SetDefDrawEnv(&db[1].draw, 0, fb_a_y, SCREEN_XRES, SCREEN_YRES);

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

#if PERF_HUD
	/* Free-running hblank counter (~15.7kHz, ~263 ticks per NTSC field), polled.
	 * We read it around VSync to time real CPU+GPU work per frame. */
	SetRCnt(RCntCNT1, 0xffff, RCntMdNOINTR);
	StartRCnt(RCntCNT1);
	ResetRCnt(RCntCNT1);
#endif
}

void gpu_pass_begin(void) {
	db_nextpri = db[db_active].p;
	ClearOTagR(db[db_active].ot, OT_LEN);
}

void gpu_draw_pass(void) {
	DrawSync(0);
	DrawOTagEnv(db[db_active].ot + (OT_LEN - 1), &db[db_active].draw);
	DrawSync(0);
}

void gpu_prepare_frame(void) {
	DrawSync(0);                         /* GPU idle: this frame's drawing is done */
#if PERF_HUD
	hud_work = GetRCnt(RCntCNT1);        /* hblanks of CPU+GPU work since frame start */
#endif
	VSync(0);                            /* sleep off any field time we didn't use */
#if PERF_HUD
	hud_total = GetRCnt(RCntCNT1);       /* hblanks frame-start -> vblank = cadence */
	ResetRCnt(RCntCNT1);
	if (hud_work > hud_peak) hud_peak = hud_work;
	else hud_peak -= (hud_peak - hud_work) >> 5;   /* slow decay */
#endif
	PutDrawEnv(&db[db_active].draw);
	PutDispEnv(&db[db_active].disp);
	SetDispMask(1);
}

void gpu_finish_frame(void) {
	db_active ^= 1;
}

void gpu_present(void) {
	gpu_prepare_frame();
	DrawOTag(db[db_active].ot + (OT_LEN - 1));
	gpu_finish_frame();
}

void gpu_additive_blend(int otz) {
	DR_TPAGE *tp = (DR_TPAGE *)db_nextpri;
	setDrawTPage(tp, 0, 1, getTPage(0, 1, 0, 0));
	addPrim(OT_AT(otz), tp);
	db_nextpri = (uint8_t *)(tp + 1);
}

void gpu_feedback_trails(int otz) {
#if PERF_FEEDBACK_TRAILS
	const int srcy = db[db_active].disp.disp.y;
	const int shade = PERF_FEEDBACK_STRENGTH;
	POLY_FT4 *strip = (POLY_FT4 *)db_nextpri;
	int i;

	for (i = 0; i < 5; i++) {
		const int x0 = i << 6;
		const int x1 = x0 + 64;
		POLY_FT4 *q = strip + i;

		setPolyFT4(q);
		setSemiTrans(q, 1);
		setRGB0(q, shade, shade, shade);
		setXY4(q, x0, 0, x1, 0, x0, SCREEN_YRES, x1, SCREEN_YRES);
		setUV4(q, 0, 0, 63, 0, 0, SCREEN_YRES - 1, 63, SCREEN_YRES - 1);
		q->tpage = getTPage(2, PERF_FEEDBACK_ABR, x0, srcy);
		addPrim(OT_AT(otz), q);
	}

	db_nextpri = (uint8_t *)(strip + 5);
#else
	(void)otz;
#endif
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

#if PERF_HUD
/* On-screen profiler. The bar shows this-frame work as a fraction of the NTSC
 * field budget (~263 hblanks); a white tick marks 100% (= the 60fps deadline),
 * a yellow notch holds the recent peak. Bar turns red when work overruns the
 * field (you'll drop to 30fps on real hardware). The text reads work% and the
 * effective fps derived from the actual frame cadence. */
static char *hud_uitoa(unsigned v, char *end) {
	*--end = 0;
	do { *--end = (char)('0' + v % 10); v /= 10; } while (v);
	return end;
}

void gpu_hud_render(int otz) {
	const int budget = 263;
	const int bx = 14, by = 14, bw = 220, bh = 6;
	int fw   = hud_work * bw / budget;
	int pkx  = hud_peak * bw / budget;
	int over = hud_work > budget;
	int pct  = hud_work * 100 / budget;
	int fps  = hud_total > 0 ? (60 * budget) / hud_total : 0;
	char buf[24]; char tmp[8]; char *q; int i = 0;
	const char *lbl;
	TILE *t;
	if (fw  < 0) fw  = 0; if (fw  > bw + 24) fw  = bw + 24;
	if (pkx < 0) pkx = 0; if (pkx > bw + 24) pkx = bw + 24;

#define HUD_TILE(X,Y,W,H,R,G,B) \
	t=(TILE*)db_nextpri; setTile(t); setXY0(t,(X),(Y)); setWH(t,(W),(H)); \
	setRGB0(t,(R),(G),(B)); addPrim(OT_AT(otz),t); db_nextpri=(uint8_t*)(t+1)
	HUD_TILE(bx - 1, by - 1, bw + 26, bh + 2, 12, 12, 20);          /* track  */
	HUD_TILE(bx, by, fw, bh, over ? 255 : 50, over ? 50 : 230, over ? 50 : 120);
	HUD_TILE(bx + bw, by - 3, 2, bh + 6, 255, 255, 255);           /* 100% tick */
	HUD_TILE(bx + pkx, by, 1, bh, 255, 220, 90);                   /* peak hold */
#undef HUD_TILE

	lbl = "GPU ";       while (*lbl) buf[i++] = *lbl++;
	q = hud_uitoa((unsigned)pct, tmp + 8); while (*q) buf[i++] = *q++;
	lbl = "  FPS ";     while (*lbl) buf[i++] = *lbl++;
	q = hud_uitoa((unsigned)fps, tmp + 8); while (*q) buf[i++] = *q++;
	buf[i] = 0;
	draw_text2d(buf, CENTERX, by + 26, 2, 180, 255, 255, otz);
}
#endif /* PERF_HUD */
