/*
 * audio.c : original 150 BPM synthwave track on CD-DA (track 2), plus a VU level
 * the visuals pulse to.
 *
 * Browser-core quirks this works around (all harmless on real hardware):
 *  - pcsx_rearmed routes SPU register writes only when addressed via KUSEG
 *    (0x1f80_xxxx); the SDK writes them via KSEG1 (0xbf80_xxxx) and they're
 *    dropped -- so we re-assert the key SPU volumes/enables through KUSEG.
 *  - The CD controller's own mixer (CdMix) defaults to 0 there, muting CD audio.
 *  - CdlModeRept doesn't loop a single track seamlessly, so we watch playback
 *    status and re-issue Play, muting across each seek so the end-of-track tail
 *    (track 2 is last, head parks near the disc end) is never heard.
 */
#include <psxcd.h>
#include <psxspu.h>
#include "config.h"
#include "audio.h"

void audio_init(void) {
	CdInit();
	SpuInit();
	SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
	SpuSetCommonMasterVolume(0x1fff, 0x1fff);   /* ~50% of 0x3fff */
	SpuSetCommonCDVolume(0x3fff, 0x3fff);       /* CD-DA -> SPU input (defaults to 0!) */

	{
		CdlATV mix = { 0x80, 0x00, 0x80, 0x00 };   /* LtoL, LtoR, RtoR, RtoL = unity */
		CdMix(&mix);
	}
	CdControl(CdlDemute, 0, 0);

	/* KUSEG re-assert for the browser core (see file header) */
	*(volatile unsigned short *)0x1f801d80 = 0x1fff;  /* SPU master vol L (~50%) */
	*(volatile unsigned short *)0x1f801d82 = 0x1fff;  /* SPU master vol R (~50%) */
	*(volatile unsigned short *)0x1f801db0 = 0x3fff;  /* SPU CD input vol L */
	*(volatile unsigned short *)0x1f801db2 = 0x3fff;  /* SPU CD input vol R */
	*(volatile unsigned short *)0x1f801daa = 0xc001;  /* SPU on + unmute + CD audio enable */
}

/* ------------------------------------------------------------------- VU level */
#if PERF_VU_CAPTURE
/* Live metering: the SPU continuously captures CD-DA into SPU RAM; we DMA a slice
 * out and take its peak. NEVER busy-wait the transfer -- a blocking poll is fine
 * on hardware but drops the browser core to ~1fps. Kick on one frame, harvest the
 * next with a non-blocking poll; fall back to a tempo pulse if capture is absent. */
static uint32_t spu_cap[128];   /* 512 bytes = 256 samples */

int audio_level(uint32_t frame) {
	static int pending = 0, have_real = 0, last = 0;
	const int16_t *s = (const int16_t *)spu_cap;

	if (pending && SpuIsTransferCompleted(SPU_TRANSFER_PEEK)) {
		int i, peak = 0, n = (int)(sizeof(spu_cap) / 2);
		for (i = 0; i < n; i++) { int a = s[i]; if (a < 0) a = -a; if (a > peak) peak = a; }
		pending = 0; last = peak;
		if (peak > 64) have_real = 1;
	}
	if (!pending) {
		SpuSetTransferStartAddr(0x0000);
		SpuRead(spu_cap, sizeof(spu_cap));
		pending = 1;
	}
	if (have_real) return last >> 7;     /* 0..32767 peak -> 0..255 */
	{ int ph = (int)(frame % 24); return (ph < 7) ? (250 - ph * 30) : 40; }
}
#else
/* Default: index a build-time loudness envelope (one byte per CD sector, made by
 * tools/make_vu.py) using the live CD-DA play position from CdlGetlocP. Perfect
 * sync, no per-frame audio reads. The CD command is blocking, so we sample the
 * position only twice a second and advance the index between samples. */
#include "vu_env.h"

static inline int bcd2i(uint8_t b) { return (b >> 4) * 10 + (b & 0x0f); }

int audio_level(uint32_t frame) {
	static int base_idx = -1;
	static uint32_t base_frame = 0;
	if ((frame % 30) == 0) {
		CdlLOCINFOP loc;
		if (CdControl(CdlGetlocP, 0, (uint8_t *)&loc)) {
			int sec = bcd2i(loc.track_minute) * 60 + bcd2i(loc.track_second);
			base_idx   = sec * 75 + bcd2i(loc.track_sector);
			base_frame = frame;
		}
	}
	if (base_idx >= 0) {
		int idx = base_idx + ((int)(frame - base_frame) * 75) / 60;   /* 75 sectors/s */
		return vu_env[((unsigned)idx) % VU_ENV_LEN];
	}
	{ int ph = (int)(frame % 24); return (ph < 7) ? (250 - ph * 30) : 40; }
}
#endif

/* --------------------------------------------------------------- CD-DA loop */
void audio_update(uint32_t frame) {
#if PERF_CDDA
	/* 0 = wait for the loop to be running, then mute + Play(track 2)
	 * 1 = seeking (muted); once it settles, Demute -> playing
	 * 2 = playing; if playback drops (track ended), mute + re-seek back to 1 */
	static int cd_state = 0, cd_timer = 0;
	uint8_t track = 2;
	if (cd_state == 0) {
		if (frame >= 4) {
			uint8_t mode = CdlModeDA | CdlModeRept;
			CdControl(CdlMute, 0, 0);
			CdControl(CdlSetmode, &mode, 0);
			CdControl(CdlPlay, &track, 0);
			cd_state = 1; cd_timer = 0;
		}
	} else if (cd_state == 1) {
		if (++cd_timer >= 12) { CdControl(CdlDemute, 0, 0); cd_state = 2; }
	} else if ((frame & 1) == 0) {            /* poll every other frame */
		uint8_t res[16];
		CdControl(CdlNop, 0, res);
		if (!(res[0] & CdlStatPlay)) {        /* dropped -> clean muted re-seek */
			CdControl(CdlMute, 0, 0);
			CdControl(CdlPlay, &track, 0);
			cd_state = 1; cd_timer = 0;
		}
	}
#else
	(void)frame;
#endif
}
