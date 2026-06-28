/*
 * sphere.h : the environment-mapped chrome ball.
 */
#ifndef SPHERE_H
#define SPHERE_H

#include <stdint.h>

void sphere_init(void);              /* build the mesh + baked env map */

/* Dynamic reflection (PERF_SPHERE_DYNAMIC): update the working env map. The fast
 * mode copies the previous frame and patches out the old sphere; full mode uses
 * a ball-free hidden pass. No-op when dynamic reflection is off. */
void sphere_capture_reflection(void);

void sphere_render(uint32_t frame);  /* draw the ball sampling the env map */

#endif /* SPHERE_H */
