/*
 * text.h : stroke-font text -- flat 2D labels and the 3D perspective scroller.
 * Both are stateless (a pure function of the frame), so they're render-only.
 */
#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>

/* Draw a filled-stroke string centred on cx at baseline y, scale sc, at OT depth
 * otz. Uses the vector font in vecfont.h. */
void draw_text2d(const char *str, int cx, int y, int sc,
                 uint8_t r, uint8_t g, uint8_t b, int otz);

/* The 3D perspective greetz scroller. */
void scroller_render(uint32_t frame);

#endif /* TEXT_H */
