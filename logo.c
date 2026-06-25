/*
 * logo.c : the SecKC identity.
 *
 * Skull: the ASCII-art skull is baked into a TIM texture; we stack several
 * additively-blended copies of the textured quad along the depth axis (black
 * texels are transparent) so the green shape gains real thickness and reads as a
 * solid slab when it tumbles.
 *
 * DVD: the screensaver bouncer, recolouring on every wall hit.
 */
#include "gpu.h"
#include "logo.h"

/* Textures embedded via incbin (see CMakeLists.txt). Each TIM carries its own
 * VRAM coordinates, so LoadImage drops it where the TIM says. */
extern const uint32_t seckc_tim[];
extern const uint32_t dvd_tim[];

static uint16_t tex_tpage, tex_clut;     /* skull */
static uint16_t dvd_tpage, dvd_clut;

#define TEX_SLICES  8
#define TEX_DEPTH   20

void logo_init(void) {
	TIM_IMAGE tim;

	GetTimInfo(seckc_tim, &tim);
	LoadImage(tim.crect, tim.caddr);
	LoadImage(tim.prect, tim.paddr);
	DrawSync(0);
	tex_tpage = getTPage(tim.mode & 0x3, 1, tim.prect->x, tim.prect->y);
	tex_clut  = getClut(tim.crect->x, tim.crect->y);

	GetTimInfo(dvd_tim, &tim);
	LoadImage(tim.crect, tim.caddr);
	LoadImage(tim.prect, tim.paddr);
	DrawSync(0);
	dvd_tpage = getTPage(tim.mode & 0x3, 0, tim.prect->x, tim.prect->y);
	dvd_clut  = getClut(tim.crect->x, tim.crect->y);
}

/* Shared slab renderer: TEX_SLICES textured quads stacked in depth, back to
 * front, each at brightness mod[k]. */
static void skull_slices(const MATRIX *m, const int *mod) {
	int k;
	(void)m;
	for (k = 0; k < TEX_SLICES; k++) {
		short sz = (short)(-TEX_DEPTH + (2 * TEX_DEPTH * k) / (TEX_SLICES - 1));
		SVECTOR q[4] = {
			{ -120, -120, sz, 0 }, {  120, -120, sz, 0 },
			{ -120,  120, sz, 0 }, {  120,  120, sz, 0 }
		};
		uint8_t c = (uint8_t)mod[k];
		POLY_FT4 *pol = (POLY_FT4 *)db_nextpri;
		int otz;
		setPolyFT4(pol);
		setSemiTrans(pol, 1);
		setRGB0(pol, c, c, c);
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
		addPrim(OT_AT(otz), pol);
		db_nextpri = (uint8_t *)(pol + 1);
	}
}

void logo_skull_render(uint32_t frame, int env) {
	MATRIX  m;
	SVECTOR rot;
	VECTOR  pos = { 0, 0, 360 };
	int     fp  = isin((frame * 80) & 4095);            /* fast breathing pulse */
	int32_t scl = ONE + (env << 2) + ((fp < 0 ? -fp : fp) >> 4);
	VECTOR  sv  = { scl, scl, scl };
	int     mod[TEX_SLICES], k;

	rot.vx = (short)(isin(frame * 10) >> 5);
	rot.vy = (short)(frame * 17);
	rot.vz = (short)(isin(frame * 7) >> 7);
	rot.pad = 0;
	RotMatrix(&rot, &m);
	ScaleMatrix(&m, &sv);
	TransMatrix(&m, &pos);
	gte_SetRotMatrix(&m);
	gte_SetTransMatrix(&m);

	for (k = 0; k < TEX_SLICES; k++)
		mod[k] = 20 + (26 * k) / (TEX_SLICES - 1) + (env >> 3);
	skull_slices(&m, mod);
}

void logo_boot_skull_render(uint32_t f, int fade) {
	MATRIX  m;
	SVECTOR rot = { 0, 0, 0, 0 };
	VECTOR  pos = { 0, 0, 300 };
	VECTOR  sv  = { ONE, ONE, ONE };
	int     mod[TEX_SLICES], k;

	rot.vx = (short)(isin(f * 7) >> 6);
	rot.vy = (short)(f * 9);
	RotMatrix(&rot, &m);
	ScaleMatrix(&m, &sv);
	TransMatrix(&m, &pos);
	gte_SetRotMatrix(&m);
	gte_SetTransMatrix(&m);

	for (k = 0; k < TEX_SLICES; k++)
		mod[k] = ((20 + (26 * k) / (TEX_SLICES - 1)) * fade) / 255;
	skull_slices(&m, mod);
}

/* ----------------------------------------------------------------------- DVD */
#define DVD_W 64
#define DVD_H 32

static int dvd_x = 40, dvd_y = 70, dvd_vx = 2, dvd_vy = 1, dvd_ci = 0;

void dvd_update(void) {
	dvd_x += dvd_vx; dvd_y += dvd_vy;
	if (dvd_x <= 0)                  { dvd_x = 0; dvd_vx = -dvd_vx; dvd_ci = (dvd_ci + 1) % 6; }
	if (dvd_x >= SCREEN_XRES - DVD_W){ dvd_x = SCREEN_XRES - DVD_W; dvd_vx = -dvd_vx; dvd_ci = (dvd_ci + 1) % 6; }
	if (dvd_y <= 0)                  { dvd_y = 0; dvd_vy = -dvd_vy; dvd_ci = (dvd_ci + 1) % 6; }
	if (dvd_y >= SCREEN_YRES - DVD_H){ dvd_y = SCREEN_YRES - DVD_H; dvd_vy = -dvd_vy; dvd_ci = (dvd_ci + 1) % 6; }
}

void dvd_render(void) {
	static const uint8_t cols[6][3] = {
		{  40, 240, 255 }, { 255,  60, 200 }, { 150,  80, 255 },
		{ 255, 100, 150 }, { 255, 220,  90 }, {  90, 255, 160 }
	};
	POLY_FT4 *p = (POLY_FT4 *)db_nextpri;
	setPolyFT4(p);
	setRGB0(p, cols[dvd_ci][0], cols[dvd_ci][1], cols[dvd_ci][2]);   /* tint the white logo */
	setXY4(p, dvd_x, dvd_y, dvd_x + DVD_W, dvd_y, dvd_x, dvd_y + DVD_H, dvd_x + DVD_W, dvd_y + DVD_H);
	setUV4(p, 0, 0, DVD_W - 1, 0, 0, DVD_H - 1, DVD_W - 1, DVD_H - 1);
	p->tpage = dvd_tpage;
	p->clut  = dvd_clut;
	addPrim(OT_AT(OT_LEN - 5), p);   /* background layer */
	db_nextpri = (uint8_t *)(p + 1);
}
