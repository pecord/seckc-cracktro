/*
 * gpu.h : the rendering core -- double buffering, the per-frame ordering table,
 * and the small primitive/colour helpers every effect uses.
 *
 * The demo is immediate-mode: each frame every effect emits GPU primitives into
 * db[db_active].ot (the ordering table), linked at a depth slot. DrawOTag walks
 * that list back-to-front (painter's algorithm) -- that's the ONLY depth sorting;
 * there is no scene graph and no z-buffer.
 */
#ifndef GPU_H
#define GPU_H

#include <stdint.h>
#include <psxgpu.h>
#include <psxgte.h>
#include <inline_c.h>
#include "config.h"

/* One double-buffer half: its display + draw environments, ordering table, and
 * primitive scratch buffer. db[0] and db[1] flip every presented frame. */
typedef struct {
	DISPENV  disp;
	DRAWENV  draw;
	uint32_t ot[OT_LEN];
	uint8_t  p[PACKET_LEN];
} DB;

extern DB       db[2];
extern int      db_active;     /* index of the buffer we're drawing into now */
extern uint8_t *db_nextpri;    /* bump allocator into db[db_active].p        */

/* short-hand for the active buffer's OT slot otz */
#define OT_AT(otz) (db[db_active].ot + (otz))

void gpu_init(void);

/* A frame is: gpu_pass_begin();  <effects emit primitives>;  gpu_present(); */
void gpu_pass_begin(void);     /* clear the active OT, reset the prim allocator */
void gpu_present(void);        /* wait for vblank, show the active buffer, flip  */

/* Switch the rest of this OT slot to additive blending (semi-transparent prims
 * add their colour). Call once per pass after the opaque scene is emitted. */
void gpu_additive_blend(int otz);

/* --- primitive helpers ---------------------------------------------------- */

/* a flat line at OT depth otz */
void add_line(int x0, int y0, int x1, int y1,
              uint8_t r, uint8_t g, uint8_t b, int otz);

/* a stroke (a..b) thickened into a SOLID filled quad of half-width w -- this is
 * how the vector font draws filled (not wireframe) glyphs. */
void add_fillstroke(DVECTOR a, DVECTOR b, int w,
                    uint8_t r, uint8_t g, uint8_t bl, int otz);

/* project a model vertex through the current GTE matrix -> screen point */
void project(const SVECTOR *v, DVECTOR *out);
int  onscreen(const DVECTOR *p);

/* --- misc helpers --------------------------------------------------------- */

/* pack 5-bit-per-channel RGB into a PSX 16bpp texel (clamps each channel) */
uint16_t rgb5(int r, int g, int b);

/* 32-bit integer square root (no FPU on the R3000) */
uint32_t isqrt32(uint32_t n);

#endif /* GPU_H */
