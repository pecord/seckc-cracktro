/*
 * rave-psx  --  "SecKC // PSX"
 *
 * A small PlayStation 1 demo (PSn00bSDK, NTSC 320x240, double buffered) built as
 * a learning project. Each frame is drawn immediate-mode: every effect emits GPU
 * primitives into an ordering table that the GPU walks back-to-front. There is no
 * scene graph and no z-buffer -- depth is purely the ordering-table slot.
 *
 * The scene, back to front:
 *   backdrop.c  synthwave grid + banded sun + starfield
 *   logo.c      bouncing DVD logo; the SecKC ASCII-skull slab (the centrepiece)
 *   sphere.c    a chrome ball that reflects the live scene (env-mapped)
 *   text.c      a 3D perspective greetz scroller
 *   audio.c     150 BPM CD-DA soundtrack; a VU level the skull pulses to
 *
 * Reflection (PERF_SPHERE_DYNAMIC): each frame the previously-displayed frame is
 * copied into the ball's environment map, so the ball mirrors the live scene
 * (one frame of latency). See sphere.c.
 *
 * Module map: config.h (tunables) | gpu.c (render core) | audio.c | backdrop.c |
 * logo.c | text.c | sphere.c. See ARCHITECTURE.md for the full tour.
 */
#include <psxgpu.h>
#include <psxgte.h>
#include "config.h"
#include "gpu.h"
#include "audio.h"
#include "backdrop.h"
#include "logo.h"
#include "sphere.h"
#include "text.h"

/* Emit one full scene into the active ordering table, back to front. State-
 * advancing effects (starfield, DVD) are updated once per frame OUTSIDE this. */
static void scene_render(uint32_t frame, int env) {
#if PERF_BG
	vaporwave_render(frame);
#endif
	starfield_render();
	dvd_render();
#if PERF_SPHERE
	sphere_render(frame);
#endif
	logo_skull_render(frame, env);
	scroller_render(frame);
#if PERF_BG
	gpu_additive_blend(OT_LEN - 1);   /* semi-transparent prims add from here */
#endif
}

/* The SecKC skull spins up on black with a fading title before the demo proper. */
static void boot_splash(void) {
	uint32_t f;
	for (f = 0; f < 160; f++) {
		int env = (f < 28) ? (int)f * 9 : (f > 128 ? (160 - (int)f) * 8 : 255);
		uint8_t e;
		if (env > 255) env = 255;
		if (env < 0) env = 0;
		e = (uint8_t)env;

		gpu_pass_begin();
		logo_boot_skull_render(f, env);
		draw_text2d("SECKC", 160, 196, 4, e, (uint8_t)(e / 4), (uint8_t)(e * 3 / 4), 2);
		draw_text2d("KANSAS CITY HACKERS", 160, 220, 2,
		            (uint8_t)(e * 4 / 5), e, e, 2);
		gpu_additive_blend(OT_LEN - 1);
		gpu_present();
	}
}

int main(void) {
	uint32_t frame = 0;
	int g_vu = 0;

	gpu_init();
	audio_init();
	logo_init();
#if PERF_SPHERE
	sphere_init();
#endif
	backdrop_init();

	boot_splash();

	while (1) {
		/* VU envelope: fast attack, slow decay, so the skull punches on the beat */
		int target = audio_level(frame);
		int env;
		if (target > 255) target = 255;
		if (target > g_vu) g_vu = target;
		else g_vu -= (g_vu - target) >> 2;
		env = g_vu;

		audio_update(frame);
		starfield_update();
		dvd_update();

		gpu_pass_begin();
#if PERF_SPHERE && PERF_SPHERE_DYNAMIC
		/* mirror the previous frame into the ball's env map (1-frame delay) */
		sphere_capture_reflection();
#endif
		scene_render(frame, env);
		gpu_present();

		frame++;
	}
	return 0;
}
