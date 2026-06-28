/*
 * sphere.c : a GTE chrome ball that reflects the scene.
 *
 * The reflection is a "matcap": each vertex's UV into an environment map comes
 * from its SCREEN position relative to the ball centre, not its spun normal -- so
 * the reflected image stays anchored to the world while the surface spins under
 * it, exactly like real chrome.
 *
 * The env map is a baked synthwave panorama by default. PERF_SPHERE_DYNAMIC=1
 * switches to a patched previous-frame copy: it reflects the scene with one
 * frame of latency, then covers the previous sphere area with the baked map so
 * the ball does not recursively reflect itself. PERF_SPHERE_REFLECT_FULL=1 uses
 * a slower exact hidden pass instead.
 *
 * VRAM: working env map at (640,0), baked patch source at (768,0), both
 * 128x128 16bpp and clear of the framebuffers/logo/DVD textures.
 */
#include <psxgpu.h>
#include "gpu.h"
#include "sphere.h"

#if PERF_SPHERE
#define SP_SLICES  PERF_SPHERE_SLICES
#define SP_STACKS  PERF_SPHERE_STACKS
#define SP_RAD     105                          /* model radius (a small accent) */
#define SP_NV      ((SP_SLICES + 1) * (SP_STACKS + 1))
#define ENV_W      PERF_SPHERE_ENV_SIZE
#define ENV_H      PERF_SPHERE_ENV_SIZE

static SVECTOR  sp_pos[SP_NV];                  /* vertex positions (model units) */
static uint16_t chrome_env[ENV_W * ENV_H];      /* baked env, built once */
static uint16_t env_tpage;                       /* working map the ball samples (640,0) */
static uint16_t env_base_tpage;                  /* baked map used to patch self-capture */
static int      sp_last_cx, sp_last_cy, sp_last_rad;

static inline float fabsf_(float x) { return x < 0 ? -x : x; }

/* Build the baked reflection map: a little synthwave world keyed by the surface
 * normal -- neon sun + slits on the horizon, a magenta perspective grid below,
 * stars above. Used directly when dynamic reflection is off. (Boot-time
 * soft-float is OK.) */
static void chrome_env_init(void) {
	int u, v;
	const float lx = -0.45f, ly = 0.55f, lz = 0.70f;   /* specular light dir */
	for (v = 0; v < ENV_H; v++) {
		for (u = 0; u < ENV_W; u++) {
			float nx = (u - ENV_W / 2) / (float)(ENV_W / 2);
			float ny = (ENV_H / 2 - v) / (float)(ENV_H / 2);
			float r2 = nx * nx + ny * ny;
			float nz, ry, e, r, g, b;
			if (r2 > 1.0f) r2 = 1.0f;
			nz = 1.0f - r2;
			nz = nz > 0 ? nz : 0;
			{
				float x = nz, last = 0.0f;
				int it;
				for (it = 0; it < 8 && x != last; it++) {
					last = x;
					x = 0.5f * (x + (nz > 0 ? nz / x : 0));
				}
				nz = x;
			}
			ry = 2.0f * nz * ny;
			e  = ry;
			if (e >= 0.0f) {                       /* sky: purple + scan streaks + stars */
				r = 5 + e * 3; g = 1 + e * 2; b = 11 + e * 12;
				if (((int)(e * 70) & 7) == 0) { r += 3; b += 5; }
				{ unsigned h = ((unsigned)u * 73856093u) ^ ((unsigned)v * 19349663u);
				  if ((h & 1023u) < 5u) { r = 28; g = 28; b = 31; } }
			} else {                               /* floor: magenta perspective grid */
				float fe = -e, depth = 1.0f / (fe + 0.05f);
				r = 8; g = 2; b = 12;
				if (depth * 0.6f - (int)(depth * 0.6f) < 0.14f) {
					r += 18; g += 3; b += 22;
				}
				{
					float gx = nx * depth * 0.7f;
					float frac = gx - (int)gx;
					if (frac < 0.11f && frac >= 0.0f) { r += 14; g += 2; b += 18; }
				}
			}
			{ float glow = 1.0f - fabsf_(e) * 3.0f; /* neon sun on the horizon */
			  if (glow > 0.0f) {
				float sr = 31, sg = 6 + e * 40, sb = 20 - e * 30;
				float slit = (fabsf_(e) < 0.18f && (((int)(fabsf_(e) * 90) & 1))) ? 0.25f : 1.0f;
				float k = glow * glow * slit;
				r = r * (1 - k) + sr * k;
				g = g * (1 - k) + (sg < 0 ? 0 : sg) * k;
				b = b * (1 - k) + (sb < 0 ? 0 : sb) * k;
			  } }
			{ float d = nx * lx + ny * ly + nz * lz; /* specular hotspot */
			  if (d > 0.9f) { float s = (d - 0.9f) / 0.1f; float w = s * s * 31; r += w; g += w; b += w; } }
			chrome_env[v * ENV_W + u] = rgb5((int)r, (int)g, (int)b);
		}
	}
}

void sphere_init(void) {
	int j, i, k = 0;
	RECT er  = { 640, 0, ENV_W, ENV_H };
	RECT erb = { 768, 0, ENV_W, ENV_H };

	/* tessellate a unit sphere into model-space positions */
	for (j = 0; j <= SP_STACKS; j++) {
		int lat = -1024 + (2048 * j) / SP_STACKS;     /* -90..+90 deg (4096 = 360) */
		int cl  = isin(lat & 4095);                   /* y unit, -4096..4096 */
		int rl  = icos(lat & 4095);                   /* ring radius unit    */
		for (i = 0; i <= SP_SLICES; i++, k++) {
			int lon = (4096 * i) / SP_SLICES;
			int xu  = (rl * icos(lon & 4095)) >> 12;
			int zu  = (rl * isin(lon & 4095)) >> 12;
			sp_pos[k].vx = (short)((xu * SP_RAD) >> 12);
			sp_pos[k].vy = (short)((cl * SP_RAD) >> 12);
			sp_pos[k].vz = (short)((zu * SP_RAD) >> 12);
		}
	}

	chrome_env_init();
	LoadImage(&er,  (uint32_t *)chrome_env);
	LoadImage(&erb, (uint32_t *)chrome_env);
	DrawSync(0);
	env_tpage      = getTPage(2, 0, 640, 0);
	env_base_tpage = getTPage(2, 0, 768, 0);
}

#if PERF_SPHERE_DYNAMIC
#if !PERF_SPHERE_REFLECT_FULL
static int clampi(int v, int lo, int hi) {
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static void patch_previous_sphere(void) {
	static POLY_FT4 patch;
	int pad, sx0, sx1, sy0, sy1, ex0, ex1, ey0, ey1;

	if (sp_last_rad <= 0) return;

	pad = sp_last_rad >> 2;
	if (pad < 4) pad = 4;
	sx0 = clampi(sp_last_cx - sp_last_rad - pad, 0, SCREEN_XRES - 1);
	sx1 = clampi(sp_last_cx + sp_last_rad + pad, 0, SCREEN_XRES - 1);
	sy0 = clampi(sp_last_cy - sp_last_rad - pad, 0, SCREEN_YRES - 1);
	sy1 = clampi(sp_last_cy + sp_last_rad + pad, 0, SCREEN_YRES - 1);

	ex0 = ((SCREEN_XRES - 1 - sx1) * ENV_W) / SCREEN_XRES;
	ex1 = ((SCREEN_XRES - 1 - sx0) * ENV_W) / SCREEN_XRES;
	ey0 = (sy0 * ENV_H) / SCREEN_YRES;
	ey1 = (sy1 * ENV_H) / SCREEN_YRES;

	if (ex1 <= ex0 || ey1 <= ey0) return;

	setPolyFT4(&patch);
	setRGB0(&patch, 128, 128, 128);
	setXY4(&patch, ex0, ey0, ex1, ey0, ex0, ey1, ex1, ey1);
	setUV4(&patch, ex0, ey0, ex1, ey0, ex0, ey1, ex1, ey1);
	patch.tpage = env_base_tpage;
	DrawPrim(&patch);
}
#endif

/* Dynamic reflection. The exact mode copies a hidden ball-free render from the
 * back buffer. The faster default copies the previously displayed frame, then
 * patches over the previous sphere with the baked env map to suppress feedback.
 * U is mirrored because a convex mirror flips left-right. */
void sphere_capture_reflection(void) {
	static DRAWENV  refl_draw;
	static POLY_FT4 blit;
#if PERF_SPHERE_REFLECT_FULL
	int srcy = db[db_active].draw.clip.y;   /* the back buffer just rendered */
#else
	int srcy = db[db_active].disp.disp.y;   /* the completed frame on screen */
#endif
	DrawSync(0);
	SetDefDrawEnv(&refl_draw, 640, 0, ENV_W, ENV_H);
	refl_draw.isbg = 0; refl_draw.dtd = 1;
	PutDrawEnv(&refl_draw);

	setPolyFT4(&blit);
	setRGB0(&blit, 128, 128, 128);
	setXY4(&blit, 0, 0, ENV_W, 0, 0, ENV_H, ENV_W, ENV_H);
	setUV4(&blit, 255, 0, 0, 0, 255, 239, 0, 239); /* 256x240 FB -> 128x128, L-R mirrored */
	blit.tpage = getTPage(2, 0, 0, srcy);
	DrawPrim(&blit);
#if !PERF_SPHERE_REFLECT_FULL
	patch_previous_sphere();
#endif
	DrawSync(0);
}
#else
void sphere_capture_reflection(void) {}            /* static map: nothing to do */
#endif

/* Env-map UV from a vertex's SCREEN position relative to the ball centre. */
static inline void matcap_uv(int sx, int sy, int cx, int cy, int rad,
                             uint8_t *u, uint8_t *uv_v) {
	int iu = (ENV_W / 2) + ((sx - cx) * (ENV_W / 2)) / rad;
	int iv = (ENV_H / 2) + ((sy - cy) * (ENV_H / 2)) / rad;
	if (iu < 0) iu = 0; if (iu > ENV_W - 1) iu = ENV_W - 1;
	if (iv < 0) iv = 0; if (iv > ENV_H - 1) iv = ENV_H - 1;
	*u = (uint8_t)iu; *uv_v = (uint8_t)iv;
}

void sphere_render(uint32_t frame) {
	MATRIX  m;
	SVECTOR rot = { (short)(isin(frame * 11) >> 7), (short)(frame * 13), 0 };
	/* a lazy lissajous orbit, well off-centre and pushed back in z, so the SecKC
	 * logo stays the centrepiece and the ball weaves around behind it */
	VECTOR  pos = { (isin(frame * 7) * 250) >> 12,
	                ((icos(frame * 5) * 60) >> 12) - 35,
	                430 + ((isin(frame * 9) * 110) >> 12) };
	int     j, i, cx, cy, rad;

	RotMatrix(&rot, &m);
	TransMatrix(&m, &pos);
	gte_SetRotMatrix(&m);
	gte_SetTransMatrix(&m);

	cx  = CENTERX + (pos.vx * PROJ) / pos.vz;       /* ball centre + radius on screen */
	cy  = CENTERY + (pos.vy * PROJ) / pos.vz;
	rad = (SP_RAD  * PROJ) / pos.vz;
	if (rad < 1) rad = 1;
	sp_last_cx = cx;
	sp_last_cy = cy;
	sp_last_rad = rad;

	for (j = 0; j < SP_STACKS; j++) {
		for (i = 0; i < SP_SLICES; i++) {
			int a = j * (SP_SLICES + 1) + i;
			int b = a + 1;
			int c = a + (SP_SLICES + 1);
			int d = c + 1;
			POLY_FT4 *pol = (POLY_FT4 *)db_nextpri;
			int otz, nclip;
			uint8_t ua, va, ub, vb, uc, vc, ud, vd;

			setPolyFT4(pol);
			setRGB0(pol, 128, 128, 128);
			gte_ldv3(&sp_pos[a], &sp_pos[b], &sp_pos[c]);
			gte_rtpt();
			/* Backface cull: skip the rear hemisphere (it's hidden behind the
			 * front anyway). Halves this loop's quads, GTE work and matcap
			 * divides. gte_nclip gives the screen-space winding of the projected
			 * triangle; back-facing quads have the opposite sign. */
			gte_nclip();
			gte_stopz(&nclip);
			if (nclip <= 0) continue;
			gte_stsxy0(&pol->x0);
			gte_stsxy1(&pol->x1);
			gte_stsxy2(&pol->x2);
			gte_ldv0(&sp_pos[d]);
			gte_rtps();
			gte_stsxy(&pol->x3);
			gte_avsz4();
			gte_stotz(&otz);

			matcap_uv(pol->x0, pol->y0, cx, cy, rad, &ua, &va);
			matcap_uv(pol->x1, pol->y1, cx, cy, rad, &ub, &vb);
			matcap_uv(pol->x2, pol->y2, cx, cy, rad, &uc, &vc);
			matcap_uv(pol->x3, pol->y3, cx, cy, rad, &ud, &vd);
			setUV4(pol, ua, va, ub, vb, uc, vc, ud, vd);
			pol->tpage = env_tpage;

			if (otz <= 0 || otz >= OT_LEN) continue;
			addPrim(OT_AT(otz), pol);
			db_nextpri = (uint8_t *)(pol + 1);
		}
	}
}
#endif /* PERF_SPHERE */
