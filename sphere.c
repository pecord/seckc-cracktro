/*
 * sphere.c : a GTE chrome ball that reflects the scene.
 *
 * The reflection is a "matcap": each vertex's UV into an environment map comes
 * from its SCREEN position relative to the ball centre, not its spun normal -- so
 * the reflected image stays anchored to the world while the surface spins under
 * it, exactly like real chrome.
 *
 * The env map is either a baked synthwave panorama (PERF_SPHERE_DYNAMIC 0) or,
 * by default, a live downscaled snapshot of the scene rendered WITHOUT the ball
 * (the two-pass render in main.c). Because the snapshot never contains the ball,
 * there's no feedback loop -- the reflection is exact and single-depth.
 *
 * VRAM: working env map at (640,0), pristine baked copy at (768,0), both 128x128
 * 16bpp, clear of the framebuffers and the logo/DVD textures.
 */
#include <psxgpu.h>
#include "gpu.h"
#include "sphere.h"

#if PERF_SPHERE
#define SP_SLICES  20
#define SP_STACKS  12
#define SP_RAD     105                          /* model radius (a small accent) */
#define SP_NV      ((SP_SLICES + 1) * (SP_STACKS + 1))
#define ENV_W      128
#define ENV_H      128

static SVECTOR  sp_pos[SP_NV];                  /* vertex positions (model units) */
static uint16_t chrome_env[ENV_W * ENV_H];      /* baked env, built once */
static uint16_t env_tpage;                       /* working map the ball samples (640,0) */
static uint16_t env_base_tpage;                  /* pristine baked copy (768,0) */

static inline float fabsf_(float x) { return x < 0 ? -x : x; }

/* Build the baked reflection map: a little synthwave world keyed by the surface
 * normal -- neon sun + slits on the horizon, a magenta perspective grid below,
 * stars above. Used directly when dynamic reflection is off, and as a metallic
 * base the live scene is blended over when it's on. (Boot-time soft-float is OK.) */
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
			nz = 1.0f - r2; nz = nz > 0 ? nz : 0;
			{ float x = nz, last = 0; int it; for (it = 0; it < 8 && x != last; it++) { last = x; x = 0.5f * (x + (nz > 0 ? nz / x : 0)); } nz = x; }
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
				if (depth * 0.6f - (int)(depth * 0.6f) < 0.14f) { r += 18; g += 3; b += 22; }
				{ float gx = nx * depth * 0.7f; if (gx - (int)gx < 0.11f && gx - (int)gx >= 0.0f) { r += 14; g += 2; b += 18; } }
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
/* Copy the previous frame (the buffer currently on screen) into the env map so
 * the ball mirrors the live scene: reset to the baked metallic base, then blend
 * the scene over it at 50% (and mirror it in U, since a convex mirror flips
 * left-right). The 50% baked blend halves the ball's self-reflection each frame
 * so the Droste feedback decays in ~1 bounce instead of spiralling. (A two-pass
 * render that captures the scene WITHOUT the ball would remove feedback entirely;
 * see ARCHITECTURE.md -- left as a follow-up.) */
void sphere_capture_reflection(void) {
	static DRAWENV  refl_draw;
	static POLY_FT4 base, blit;
	int srcy = db[db_active].disp.disp.y;   /* the buffer currently displayed */
	DrawSync(0);
	SetDefDrawEnv(&refl_draw, 640, 0, ENV_W, ENV_H);
	refl_draw.isbg = 0; refl_draw.dtd = 1;
	PutDrawEnv(&refl_draw);

	setPolyFT4(&base);                              /* baked base (opaque) */
	setRGB0(&base, 128, 128, 128);
	setXY4(&base, 0, 0, ENV_W, 0, 0, ENV_H, ENV_W, ENV_H);
	setUV4(&base, 0, 0, ENV_W - 1, 0, 0, ENV_H - 1, ENV_W - 1, ENV_H - 1);
	base.tpage = env_base_tpage;
	DrawPrim(&base);

	setPolyFT4(&blit);                              /* live scene, 50% over base */
	setRGB0(&blit, 128, 128, 128);
	setSemiTrans(&blit, 1);
	setXY4(&blit, 0, 0, ENV_W, 0, 0, ENV_H, ENV_W, ENV_H);
	setUV4(&blit, 255, 0, 0, 0, 255, 239, 0, 239); /* 256x240 FB -> 128x128, L-R mirrored */
	blit.tpage = getTPage(2, 0, 0, srcy);          /* back buffer as a 16bpp texture, ABR=0 */
	DrawPrim(&blit);
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

	for (j = 0; j < SP_STACKS; j++) {
		for (i = 0; i < SP_SLICES; i++) {
			int a = j * (SP_SLICES + 1) + i;
			int b = a + 1;
			int c = a + (SP_SLICES + 1);
			int d = c + 1;
			POLY_FT4 *pol = (POLY_FT4 *)db_nextpri;
			int otz;
			uint8_t ua, va, ub, vb, uc, vc, ud, vd;

			setPolyFT4(pol);
			setRGB0(pol, 128, 128, 128);
			gte_ldv3(&sp_pos[a], &sp_pos[b], &sp_pos[c]);
			gte_rtpt();
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
