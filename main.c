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
 *   sphere.c    a chrome ball with a baked synthwave env map by default
 *   text.c      a 3D perspective greetz scroller
 *   audio.c     150 BPM CD-DA soundtrack; a VU level the skull pulses to
 *
 * Reflection: the default baked env map keeps the demo at 60fps. Optional live
 * modes either copy and patch the previous frame, or use a slower ball-free
 * hidden pass; see PERF_SPHERE_DYNAMIC in config.h.
 * Feedback trails: the visible pass can overlay a dim previous framebuffer copy
 * after the scene, giving bright neon shapes a cheap afterimage.
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

/* Emit one scene into the active ordering table, back to front. State-advancing
 * effects (starfield, DVD) are updated once per frame OUTSIDE this. */
static void scene_render(uint32_t frame, int env, int with_sphere, int with_scroller) {
#if PERF_BG
	vaporwave_render(frame);
#endif
	starfield_render();
	dvd_render();
#if PERF_SPHERE
	if (with_sphere) sphere_render(frame);
#endif
	logo_skull_render(frame, env);
	if (with_scroller) scroller_render(frame);
#if PERF_BG
	gpu_additive_blend(OT_LEN - 1);   /* semi-transparent prims add from here */
#endif
}

static void scene_postprocess(void) {
#if PERF_HUD
	/* Added before the trails at the same slot, so it draws on top of everything
	 * (within an OT slot, the first-added primitive is drawn last). */
	gpu_hud_render(0);
#endif
#if PERF_FEEDBACK_TRAILS && \
	(!PERF_SPHERE || !PERF_SPHERE_DYNAMIC || PERF_FEEDBACK_WITH_DYNAMIC_REFLECTION)
	/* Draw last-ish: it keeps the current frame crisp while old neon lingers. */
	gpu_feedback_trails(0);
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
#if PERF_SPHERE && PERF_SPHERE_DYNAMIC && PERF_SPHERE_REFLECT_INTERVAL > 1
	int reflect_wait = 0;
#endif

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

#if PERF_SPHERE && PERF_SPHERE_DYNAMIC
#if PERF_SPHERE_REFLECT_INTERVAL > 1
		int update_reflection = (reflect_wait == 0);
		if (update_reflection) reflect_wait = PERF_SPHERE_REFLECT_INTERVAL - 1;
		else reflect_wait--;
#else
		int update_reflection = 1;
#endif
#if PERF_SPHERE_REFLECT_FULL
		if (update_reflection) {
			gpu_prepare_frame();
			/* Slow reference path: capture a full scene before the ball exists,
			 * then redraw the full visible scene with the ball. */
			gpu_pass_begin();
			scene_render(frame, env, 0, 1);
			gpu_draw_pass();
			sphere_capture_reflection();

			gpu_pass_begin();
			scene_render(frame, env, 1, 1);
			scene_postprocess();
			gpu_draw_pass();
			gpu_finish_frame();
		} else {
			gpu_pass_begin();
			scene_render(frame, env, 1, 1);
			scene_postprocess();
			gpu_present();
		}
#else
		if (update_reflection) sphere_capture_reflection();
		gpu_pass_begin();
		scene_render(frame, env, 1, 1);
		scene_postprocess();
		gpu_present();
#endif
#else
		gpu_pass_begin();
		scene_render(frame, env, 1, 1);
		scene_postprocess();
		gpu_present();
#endif

		frame++;
	}
	return 0;
}
