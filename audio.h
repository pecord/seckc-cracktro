/*
 * audio.h : CD-DA soundtrack + the music-reactive VU level.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

/* Bring up the CD drive and SPU, set volumes, and route CD-DA to the output.
 * (Playback itself is started later, from the render loop -- see audio_update.) */
void audio_init(void);

/* 0..255 loudness for this frame, used to drive the skull's "VU" pulse. */
int  audio_level(uint32_t frame);

/* Per-frame CD-DA state machine: starts the music once rendering is up and keeps
 * it looping with a clean, blip-free seek. Call exactly once per displayed frame
 * (NOT once per render pass). */
void audio_update(uint32_t frame);

#endif /* AUDIO_H */
