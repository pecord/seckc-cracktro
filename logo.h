/*
 * logo.h : the SecKC ASCII-skull slab and the bouncing DVD logo.
 */
#ifndef LOGO_H
#define LOGO_H

#include <stdint.h>

void logo_init(void);                          /* upload the skull + DVD textures */

void logo_skull_render(uint32_t frame, int env);     /* spinning extruded slab */
void logo_boot_skull_render(uint32_t f, int fade);   /* boot-splash variant     */

void dvd_update(void);                         /* advance the bounce            */
void dvd_render(void);                          /* emit the logo quad           */

#endif /* LOGO_H */
