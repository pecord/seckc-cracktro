/*
 * config.h : compile-time tunables shared across the whole demo.
 *
 * Screen geometry, the ordering-table size, and the PERF_* feature toggles live
 * here so every module agrees on them. Effect-specific constants stay in their
 * own module (e.g. the starfield's star count is in backdrop.c).
 */
#ifndef CONFIG_H
#define CONFIG_H

/* NTSC 320x240, double buffered. */
#define SCREEN_XRES 320
#define SCREEN_YRES 240
#define CENTERX     (SCREEN_XRES >> 1)
#define CENTERY     (SCREEN_YRES >> 1)
#define PROJ        CENTERX            /* GTE projection distance (screen plane) */

/* Ordering table: one linked list of GPU primitives per frame, drawn back to
 * front. OT_LEN slots = depth resolution; PACKET_LEN bytes = primitive scratch. */
#define OT_LEN      1024
#define PACKET_LEN  49152

#define BPM         150               /* the CD-DA music track's tempo */
#define FPB         ((60 * 60) / BPM) /* frames per beat @ NTSC 60fps  */

/* --- feature toggles (override from the compiler with -DPERF_*=0) --- */
#ifndef PERF_CDDA
#define PERF_CDDA           1   /* CD-DA music playback */
#endif
#ifndef PERF_VU_CAPTURE
#define PERF_VU_CAPTURE     0   /* 1 = live SPU peak metering (real HW only; the
                                 * browser core models SpuRead too slowly). 0 =
                                 * drive the VU from a baked loudness envelope. */
#endif
#ifndef PERF_BG
#define PERF_BG             1   /* full-screen backdrop + additive blend */
#endif
#ifndef PERF_FEEDBACK_TRAILS
#define PERF_FEEDBACK_TRAILS 0   /* 1 = overlay a dim copy of the previous frame
                                  * for neon afterimages. */
#endif
#ifndef PERF_FEEDBACK_STRENGTH
#define PERF_FEEDBACK_STRENGTH 8  /* Texture shade before the semi-trans blend.
                                   * 128 = full texture colour. */
#endif
#ifndef PERF_FEEDBACK_ABR
#define PERF_FEEDBACK_ABR 3       /* PS1 semi-trans mode: back + front/4. */
#endif
#ifndef PERF_FEEDBACK_WITH_DYNAMIC_REFLECTION
#define PERF_FEEDBACK_WITH_DYNAMIC_REFLECTION 0
#endif
#ifndef PERF_SPHERE
#define PERF_SPHERE         1   /* the environment-mapped chrome sphere */
#endif
#ifndef PERF_SPHERE_DYNAMIC
#define PERF_SPHERE_DYNAMIC 0   /* 1 = live reflection experiment;
                                 * 0 = 60fps baked synthwave reflection */
#endif
#ifndef PERF_SPHERE_REFLECT_FULL
#define PERF_SPHERE_REFLECT_FULL 0   /* 1 = slow reference mode: render a full
                                      * hidden scene for reflection. 0 = copy the
                                      * previous frame and patch out the ball. */
#endif
#ifndef PERF_SPHERE_REFLECT_INTERVAL
#define PERF_SPHERE_REFLECT_INTERVAL 1   /* Dynamic reflection update cadence.
                                          * 1 = every frame, 2 = every other
                                          * frame, etc. Reused frames keep the
                                          * visible scene smoother. */
#endif
#ifndef PERF_SPHERE_ENV_SIZE
#define PERF_SPHERE_ENV_SIZE 128         /* Dynamic/baked env-map width+height.
                                          * Try 64 for chunkier experiments. */
#endif
#ifndef PERF_SPHERE_SLICES
#define PERF_SPHERE_SLICES 20
#endif
#ifndef PERF_SPHERE_STACKS
#define PERF_SPHERE_STACKS 12
#endif
#ifndef PERF_HUD
#define PERF_HUD            0   /* 1 = on-screen profiler: per-frame work as a
                                 * fraction of the NTSC field budget + effective
                                 * fps. Times real CPU+GPU work with a hardware
                                 * root counter, so it reflects PS1 silicon, not
                                 * the host emulator's speed. */
#endif

#endif /* CONFIG_H */
