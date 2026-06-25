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
#ifndef PERF_SPHERE
#define PERF_SPHERE         1   /* the environment-mapped chrome sphere */
#endif
#ifndef PERF_SPHERE_DYNAMIC
#define PERF_SPHERE_DYNAMIC 1   /* 1 = ball reflects the live scene (prev frame);
                                 * 0 = reflect only the baked synthwave map */
#endif

#endif /* CONFIG_H */
