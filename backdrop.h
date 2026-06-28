/*
 * backdrop.h : the synthwave world behind everything -- neon perspective grid,
 * banded sun, and a starfield.
 *
 * Effects with moving state are split into update() (advance once per frame) and
 * render() (emit primitives). That keeps extra render passes possible without
 * double-speeding the animation.
 */
#ifndef BACKDROP_H
#define BACKDROP_H

#include <stdint.h>

void backdrop_init(void);              /* seed the starfield */

void vaporwave_render(uint32_t frame); /* stateless: sky, sun, scrolling grid */

void starfield_update(void);           /* advance the stars toward the camera */
void starfield_render(void);           /* emit the star streaks */

#endif /* BACKDROP_H */
