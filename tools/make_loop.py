#!/usr/bin/env python3
"""Trim music.wav down to a short, seamlessly-looping CD-DA track.

Red Book CD-DA is uncompressed (~10MB/min), so the full song makes a big disc.
The demo loops the track, so we only need a ~90s slice. We take the head of the
song and equal-power crossfade the tail back into the start so the loop point is
click-free even if it isn't beat-aligned.

  music.wav  ->  music_loop.wav   (44.1kHz/16-bit stereo, ~LEN seconds)
"""
import sys, wave
import numpy as np

SRC = "music.wav"
DST = "music_loop.wav"
LEN = float(sys.argv[1]) if len(sys.argv) > 1 else 90.0   # loop length, seconds
XF  = 1.5                                                  # crossfade seconds

w = wave.open(SRC, "rb")
ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
if sw != 2:
    sys.exit(f"{SRC}: need 16-bit PCM (got {sw*8}-bit)")
raw = w.readframes(n); w.close()
a = np.frombuffer(raw, dtype="<i2").astype(np.float32).reshape(-1, ch)

L  = int(LEN * sr)
xf = int(XF * sr)
if a.shape[0] < L + xf:
    L = a.shape[0] - xf                    # song shorter than requested loop
    print(f"note: song only {a.shape[0]/sr:.1f}s, looping {L/sr:.1f}s")

# output = head [0, L); crossfade the tail [L, L+xf) over the head [0, xf)
head = a[:L].copy()
tail = a[L:L + xf]
t = np.linspace(0.0, 1.0, xf, endpoint=False)[:, None]
fin, fout = np.sin(t * np.pi / 2), np.cos(t * np.pi / 2)   # equal-power
head[:xf] = head[:xf] * fin + tail * fout

out = np.clip(head, -32768, 32767).astype("<i2")
o = wave.open(DST, "wb")
o.setnchannels(ch); o.setsampwidth(2); o.setframerate(sr)
o.writeframes(out.tobytes()); o.close()
print(f"wrote {DST}: {L/sr:.1f}s {sr}Hz {ch}ch  (~{out.nbytes/1e6:.1f}MB raw)")
