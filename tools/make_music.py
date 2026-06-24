#!/usr/bin/env python3
"""Synthesize an ORIGINAL moody synthwave loop (no samples, all generated) and
write it as a 44.1kHz/16-bit stereo WAV for the disc's CD-DA track.

8 bars @ 75 BPM in A minor, progression Am - F - C - G: detuned-saw pad, sub
bass, an arp with echo, and a slow drum machine. Seeded for reproducibility."""
import numpy as np
import struct

SR = 44100
BPM = 150
np.random.seed(0x5ecc)

beat = int(SR * 60 / BPM)
bar  = beat * 4
BARS = 8
N    = bar * BARS

L = np.zeros(N + SR)        # a little tail room
Rc = np.zeros(N + SR)

def f(m):
    return 440.0 * 2.0 ** ((m - 69) / 12.0)

def adsr(n, a, d, sus, rel):
    e = np.zeros(n)
    ai, di, ri = int(a * SR), int(d * SR), int(rel * SR)
    si = max(0, n - ai - di - ri)
    i = 0
    if ai: e[:ai] = np.linspace(0, 1, ai); i = ai
    if di: e[i:i+di] = np.linspace(1, sus, di); i += di
    if si: e[i:i+si] = sus; i += si
    if ri: e[i:i+ri] = np.linspace(sus, 0, ri)
    return e

def saw(freq, n, cents=0.0):
    fr = freq * 2.0 ** (cents / 1200.0)
    ph = (np.arange(n) * fr / SR) % 1.0
    return 2.0 * ph - 1.0

def lp(x, width):
    k = np.hanning(width * 2 + 1); k /= k.sum()
    return np.convolve(x, k, mode='same')

def add(buf_l, buf_r, sig, start, pan=0.0):
    n = len(sig)
    s = start
    e = min(len(buf_l), s + n)
    if e <= s: return
    sig = sig[:e - s]
    buf_l[s:e] += sig * (0.5 - 0.5 * pan)
    buf_r[s:e] += sig * (0.5 + 0.5 * pan)

# Am F C G  (triads, and bass roots one+ octave down)
CH   = [[57, 60, 64], [53, 57, 60], [60, 64, 67], [55, 59, 62]]
BASS = [45, 41, 48, 43]
prog = [0, 1, 2, 3, 0, 1, 2, 3]

# --- pad: 3 detuned saws per chord note, slow attack, warm lowpass, wide ---
for b in range(BARS):
    ch = CH[prog[b]]
    nlen = bar + int(0.5 * SR)
    e = adsr(nlen, 0.28, 0.15, 0.62, 0.45)
    for j, m in enumerate(ch):
        for di, cents in enumerate((-7, 0, 7)):
            v = saw(f(m), nlen, cents) * e * 0.05
            pan = (cents / 7.0) * 0.6
            add(L, Rc, v, b * bar, pan)
        # octave up, quieter, for shimmer
        add(L, Rc, saw(f(m + 12), nlen) * e * 0.018, b * bar, (j - 1) * 0.4)

# --- sub bass: plucked root on each beat ---
for b in range(BARS):
    root = BASS[prog[b]]
    for k in range(4):
        nlen = int(beat * 0.92)
        e = adsr(nlen, 0.006, 0.12, 0.55, 0.18)
        v = (saw(f(root), nlen) * 0.6 + np.sign(saw(f(root), nlen)) * 0.4) * e * 0.5
        add(L, Rc, v, b * bar + k * beat, 0.0)

# --- arp: 8th notes through the chord, bright, with an echo ---
arp_buf = np.zeros(N + SR)
zero = np.zeros(N + SR)
for b in range(BARS):
    ch = CH[prog[b]]
    pat = [ch[0] + 12, ch[1] + 12, ch[2] + 12, ch[1] + 12,
           ch[2] + 12, ch[1] + 12, ch[0] + 12, ch[2] + 12]
    for k in range(8):
        nlen = int(beat * 0.45)
        e = adsr(nlen, 0.004, 0.05, 0.3, 0.12)
        v = saw(f(pat[k]), nlen, 4) * e * 0.12
        add(arp_buf, zero, v, b * bar + k * (beat // 2), 0.0)
# echo (delay ~ dotted 8th)
dly = int(beat * 0.75)
echo = np.zeros_like(arp_buf)
echo[dly:] += arp_buf[:-dly] * 0.5
echo[2*dly:] += arp_buf[:-2*dly] * 0.22
L += (arp_buf * (0.5 - 0.18) + echo * 0.32)
Rc += (arp_buf * (0.5 + 0.18) + echo * 0.5)

# --- drums ---
def kick(start):
    n = int(0.16 * SR)
    t = np.arange(n) / SR
    fsweep = 110 * np.exp(-t * 28) + 44
    ph = np.cumsum(2 * np.pi * fsweep / SR)
    v = np.sin(ph) * np.exp(-t * 9) * 0.9
    add(L, Rc, v, start)

def snare(start):
    n = int(0.2 * SR)
    t = np.arange(n) / SR
    nz = np.random.randn(n) * np.exp(-t * 16)
    nz = nz - lp(nz, 40)                    # high-passish
    tone = np.sin(2 * np.pi * 185 * t) * np.exp(-t * 22)
    v = (nz * 0.5 + tone * 0.4) * 0.5
    add(L, Rc, v, start, -0.05)

def hat(start, amp):
    n = int(0.04 * SR)
    t = np.arange(n) / SR
    nz = np.random.randn(n)
    nz = nz - lp(nz, 8)
    v = nz * np.exp(-t * 90) * amp
    add(L, Rc, v, start, 0.2)

for b in range(BARS):
    base = b * bar
    for k in range(4):
        kick(base + k * beat)               # four-on-the-floor
    snare(base + 1 * beat)
    snare(base + 3 * beat)
    if b % 4 == 3:
        snare(base + 3 * beat + beat // 2)   # fill
    for k in range(8):
        hat(base + k * (beat // 2), 0.11 if k % 2 else 0.05)

# --- master ---
# wrap note tails that ran past the loop end back to the start, so the loop
# point is seamless (no click)
tail = SR // 2
L[:tail] += L[N:N + tail]
Rc[:tail] += Rc[N:N + tail]
L = L[:N]; Rc = Rc[:N]
L = lp(L, 3); Rc = lp(Rc, 3)
vinyl = np.random.randn(N) * 0.004
L += vinyl; Rc += vinyl
L = np.tanh(L * 1.1); Rc = np.tanh(Rc * 1.1)
peak = max(np.abs(L).max(), np.abs(Rc).max())
g = 0.92 / peak
L *= g; Rc *= g

# bake several seamless repeats so the CD only re-seeks (and gaps) rarely
REPS = 5
L = np.tile(L, REPS); Rc = np.tile(Rc, REPS)
NT = N * REPS

stereo = np.empty(NT * 2, dtype=np.int16)
stereo[0::2] = (L * 32767).astype(np.int16)
stereo[1::2] = (Rc * 32767).astype(np.int16)

with open("music.wav", "wb") as w:
    data = stereo.tobytes()
    w.write(b"RIFF"); w.write(struct.pack("<I", 36 + len(data))); w.write(b"WAVE")
    w.write(b"fmt "); w.write(struct.pack("<IHHIIHH", 16, 1, 2, SR, SR * 4, 4, 16))
    w.write(b"data"); w.write(struct.pack("<I", len(data))); w.write(data)

print(f"wrote music.wav  loop {N/SR:.1f}s x{REPS} = {NT/SR:.1f}s  "
      f"{len(data)//1024} KB  ({BPM} BPM)")
