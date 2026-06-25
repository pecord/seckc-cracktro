/*
 * sphere.h : the environment-mapped chrome ball.
 */
#ifndef SPHERE_H
#define SPHERE_H

#include <stdint.h>

void sphere_init(void);              /* build the mesh + baked env map */

/* Two-pass reflection (PERF_SPHERE_DYNAMIC): after the scene has been drawn into
 * the back buffer WITHOUT the ball, copy it into the env map so the ball mirrors
 * the live scene. No-op when dynamic reflection is disabled. */
void sphere_capture_reflection(void);

void sphere_render(uint32_t frame);  /* draw the ball sampling the env map */

#endif /* SPHERE_H */
