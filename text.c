/*
 * text.c : the stroke (vector) font rendered as filled quads -- used for the boot
 * splash labels and the scrolling greetz.
 */
#include "gpu.h"
#include "vecfont.h"
#include "text.h"

void draw_text2d(const char *str, int cx, int y, int sc,
                 uint8_t r, uint8_t g, uint8_t b, int otz) {
	int len = 0, i, x;
	while (str[len]) len++;
	x = cx - (len * VF_ADV * sc) / 2;
	for (i = 0; i < len; i++) {
		const VGlyph *gl = vglyph(str[i]);
		int s;
		for (s = 0; s < gl->nseg; s++) {
			const signed char *sg = gl->seg + s * 4;
			DVECTOR a = { (short)(x + sg[0] * sc), (short)(y - sg[1] * sc) };
			DVECTOR c = { (short)(x + sg[2] * sc), (short)(y - sg[3] * sc) };
			add_fillstroke(a, c, sc > 2 ? 2 : 1, r, g, b, otz);
		}
		x += VF_ADV * sc;
	}
}

/* ------------------------------------------------------------------ scroller */

static const char *GREETZ =
	"SECKC // PSX  -  A PLAYSTATION DEMO FOR THE KANSAS CITY HACKER SCENE    *    "
	"DESTROY NO DATA  -  MAINTAIN NO PERSISTENCE  -  ABOVE ALL ELSE DO NO HARM    *    "
	"WE ARE SECKC  -  KANSAS CITYS HACKERS  -  LIVE EVERY MONTH AT KNUCKLEHEADS    *    "
	"GREETZ TO DC816  COWTOWN COMPUTER CONGRESS  BSIDESKC  HAMMERSPACE    *    "
	"AND EVERY HACKER MAKER AND BREAKER IN THE 816    *    "
	"SETEC ASTRONOMY  /  SHALL WE PLAY A GAME  /  FIGHT FOR THE USER    *    "
	"STAY CURIOUS  -  KEEP HACKING  -  150 BPM ON THE PSX    *    ";

/* Each glyph is a flat billboard in world space whose depth depends on its
 * on-screen position (not time): a fixed crest at the reading centre. Letters
 * glide forward as they scroll to the middle and recede as they leave, so the
 * word being read is always nearest, biggest and brightest -- no time-jitter to
 * fight while reading. */
#define S3_ADV    34     /* world units between glyph origins */
#define S3_SCALE  6      /* world units per font grid unit    */
#define S3_BASEZ  340    /* base depth                        */
#define S3_WAVE   55     /* depth wave amplitude              */
#define S3_YWAVE  18     /* vertical wave amplitude           */
#define S3_Y      110    /* baseline world Y (+down)          */

void scroller_render(uint32_t frame) {
	MATRIX  m;
	SVECTOR rot = { 0, 0, 0, 0 };
	VECTOR  tr  = { 0, 0, 0 };
	int     len = 0, total, eff, i;
	const char *p = GREETZ;
	while (*p++) len++;
	total = len * S3_ADV;
	eff   = (int)((frame * 2) % total);

	RotMatrix(&rot, &m);           /* identity: vertices are already in view space */
	TransMatrix(&m, &tr);
	gte_SetRotMatrix(&m);
	gte_SetTransMatrix(&m);

	for (i = 0; i < len; i++) {
		const VGlyph *gl = vglyph(GREETZ[i]);
		int pass;
		if (gl->nseg == 0) continue;
		for (pass = 0; pass < 2; pass++) {
			int wx = i * S3_ADV - eff + (pass ? total : 0);
			int wz, wy, s, d, recede;
			if (wx < -440 || wx > 440) continue;
			d = wx < 0 ? -wx : wx;
			if (d > 420) d = 420;
			recede = (d * d) / 420;                 /* 0 at centre .. 420 at edge */
			wz = (S3_BASEZ - S3_WAVE) + (2 * S3_WAVE * recede) / 420;
			wy = S3_Y + (S3_YWAVE * recede) / 420;  /* gentle hill: dips at sides */
			{
				int g = 255 - ((wz - (S3_BASEZ - S3_WAVE)) * 90) / (2 * S3_WAVE);
				uint8_t gg = (uint8_t)(g < 130 ? 130 : g);
				int wd = (2 * S3_BASEZ) / wz;          /* perspective stroke width */
				int ed = (S3_BASEZ - wz) / 4;          /* extrude depth, near only */
				if (wd < 1) wd = 1;
				if (wd > 4) wd = 4;
				if (ed > 18) ed = 18;
				for (s = 0; s < gl->nseg; s++) {
					const signed char *sg = gl->seg + s * 4;
					short vax = (short)(wx + sg[0] * S3_SCALE);
					short vay = (short)(wy - sg[1] * S3_SCALE);
					short vbx = (short)(wx + sg[2] * S3_SCALE);
					short vby = (short)(wy - sg[3] * S3_SCALE);
					SVECTOR fa = { vax, vay, (short)wz, 0 };
					SVECTOR fb = { vbx, vby, (short)wz, 0 };
					DVECTOR pfa, pfb;
					project(&fa, &pfa); project(&fb, &pfb);
					if (ed > 1) {                       /* extruded back face, near only */
						SVECTOR ba = { vax, vay, (short)(wz + ed), 0 };
						SVECTOR bb = { vbx, vby, (short)(wz + ed), 0 };
						DVECTOR pba, pbb;
						project(&ba, &pba); project(&bb, &pbb);
						add_fillstroke(pba, pbb, wd, (uint8_t)(gg / 3), 16, (uint8_t)(gg / 4), 3);
					}
					add_fillstroke(pfa, pfb, wd, (uint8_t)gg, 46, (uint8_t)(gg * 3 / 4), 2);
				}
			}
		}
	}
}
