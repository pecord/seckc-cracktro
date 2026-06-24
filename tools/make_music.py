#!/usr/bin/env python3
"""Original synthwave track (no samples) for the disc's CD-DA audio.

16 bars @ 150 BPM, A minor, Am-F-C-G. Detuned-saw pad, driving 8th-note octave
bass, an arp with echo, a lead hook (comes and goes), and a drum machine with
fills/open-hats/claps/crash. A kick-triggered SIDECHAIN ducks the pad/bass/arp
for the signature pump. Seamless loop, baked a few times so the CD rarely
re-seeks. Seeded for reproducibility."""
import numpy as np
import struct

SR = 44100
BPM = 150
np.random.seed(0x5ecc)

beat = int(SR * 60 / BPM)
bar  = beat * 4
BARS = 16
N    = bar * BARS
PAD_TAIL = SR                                   # render room past the loop

def buf():
    return np.zeros(N + PAD_TAIL)

pad_l, pad_r = buf(), buf()
bass = buf()
arp  = buf()
lead_l, lead_r = buf(), buf()
drums_l, drums_r = buf(), buf()
duck = np.ones(N + PAD_TAIL)

def f(m):
    return 440.0 * 2.0 ** ((m - 69) / 12.0)

def adsr(n, a, d, sus, rel):
    e = np.zeros(n)
    ai = min(int(a * SR), n)
    di = min(int(d * SR), n - ai)
    ri = min(int(rel * SR), n - ai - di)
    si = n - ai - di - ri
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

def sq(freq, n, duty=0.5):
    ph = (np.arange(n) * freq / SR) % 1.0
    return np.where(ph < duty, 1.0, -1.0)

def lp(x, w):
    k = np.hanning(w * 2 + 1); k /= k.sum()
    return np.convolve(x, k, mode='same')

def put(b, sig, start):
    e = min(len(b), start + len(sig))
    if e > start: b[start:e] += sig[:e - start]

def put2(bl, br, sig, start, pan=0.0):
    put(bl, sig * (0.5 - 0.5 * pan), start)
    put(br, sig * (0.5 + 0.5 * pan), start)

# Am F C G  (triads + bass roots)
CH   = [[57, 60, 64], [53, 57, 60], [60, 64, 67], [55, 59, 62]]
BASS = [33, 29, 36, 31]
prog = [0, 1, 2, 3] * 4

# --- pad: detuned saws, slow attack, warm, wide ---
for b in range(BARS):
    ch = CH[prog[b]]
    n = bar + int(0.5 * SR)
    e = adsr(n, 0.25, 0.15, 0.62, 0.45)
    for j, m in enumerate(ch):
        for cents in (-7, 0, 7):
            v = saw(f(m), n, cents) * e * 0.045
            put2(pad_l, pad_r, v, b * bar, (cents / 7.0) * 0.6)
        put2(pad_l, pad_r, saw(f(m + 12), n) * e * 0.016, b * bar, (j - 1) * 0.4)

# --- bass: driving 8th-note pattern, root with octave bounce ---
for b in range(BARS):
    root = BASS[prog[b]]
    for k in range(8):
        note = root + (12 if k % 4 == 3 else 0)
        n = int(beat * 0.46)
        e = adsr(n, 0.004, 0.08, 0.5, 0.1)
        v = (saw(f(note), n) * 0.6 + sq(f(note), n) * 0.4) * e * 0.5
        put(bass, lp(v, 2), b * bar + k * (beat // 2))

# --- arp: 16th-ish run through the chord, with echo ---
for b in range(BARS):
    ch = CH[prog[b]]
    pat = [ch[0]+12, ch[2]+12, ch[1]+12, ch[2]+12,
           ch[0]+24, ch[2]+12, ch[1]+12, ch[0]+12]
    for k in range(8):
        n = int(beat * 0.4)
        e = adsr(n, 0.003, 0.04, 0.25, 0.1)
        v = saw(f(pat[k]), n, 5) * e * 0.10
        put(arp, v, b * bar + k * (beat // 2))
dly = int(beat * 0.75)
echo = np.zeros_like(arp)
echo[dly:] += arp[:-dly] * 0.45
echo[2*dly:] += arp[:-2*dly] * 0.2
arp = arp + echo

# --- lead hook (only in bars 4-7 and 12-15) ---
PHRASE = [  # (bar_in_phrase, beat, midi, dur_beats)
    (0, 0.0, 69, 1.0), (0, 1.0, 72, 1.0), (0, 2.0, 76, 1.5), (0, 3.5, 74, 0.5),
    (1, 0.0, 72, 1.5), (1, 1.5, 69, 0.5), (1, 2.0, 77, 2.0),
    (2, 0.0, 76, 1.0), (2, 1.0, 79, 1.0), (2, 2.0, 76, 1.0), (2, 3.0, 72, 1.0),
    (3, 0.0, 74, 1.0), (3, 1.0, 71, 1.0), (3, 2.0, 67, 1.5), (3, 3.5, 71, 0.5),
]
def lead_note(m, dur, start):
    n = int(dur * beat)
    e = adsr(n, 0.02, 0.1, 0.7, 0.18)
    vib = 1.0 + 0.006 * np.sin(2*np.pi*5.5*np.arange(n)/SR)
    v = (saw(f(m), n, -6)*vib + saw(f(m), n, 6)*vib + sq(f(m), n)*0.5)
    v = v * e * 0.07
    put2(lead_l, lead_r, v, start, -0.1)
    ed = np.zeros(n + dly); ed[dly:] += v[:n] * 0.4
    put2(lead_r, lead_l, ed, start, 0.1)
for ph_start in (4, 12):
    for (pb, bt, m, dur) in PHRASE:
        put_at = (ph_start + pb) * bar + int(bt * beat)
        lead_note(m, dur, put_at)

# --- drums ---
def kick(start, amp=0.95):
    n = int(0.17 * SR); t = np.arange(n)/SR
    fsw = 115*np.exp(-t*30)+44
    v = np.sin(np.cumsum(2*np.pi*fsw/SR))*np.exp(-t*9)*amp
    put2(drums_l, drums_r, v, start)
    return start, int(0.28*SR)
def snare(start):
    n = int(0.2*SR); t = np.arange(n)/SR
    nz = np.random.randn(n)*np.exp(-t*15); nz = nz - lp(nz, 40)
    tone = np.sin(2*np.pi*185*t)*np.exp(-t*22)
    put2(drums_l, drums_r, (nz*0.5+tone*0.35)*0.5, start, -0.05)
def clap(start):
    n = int(0.13*SR); t = np.arange(n)/SR
    nz = np.random.randn(n); nz = nz - lp(nz, 20)
    env = np.exp(-((t-0.0)**2)/0.0006)+np.exp(-((t-0.012)**2)/0.0006)+np.exp(-t*40)
    put2(drums_l, drums_r, nz*env*0.18, start, 0.1)
def hat(start, amp, dec=90):
    n = int(0.05*SR); t = np.arange(n)/SR
    nz = np.random.randn(n); nz = nz - lp(nz, 8)
    put2(drums_l, drums_r, nz*np.exp(-t*dec)*amp, start, 0.2)
def crash(start):
    n = int(0.7*SR); t = np.arange(n)/SR
    nz = np.random.randn(n); nz = nz - lp(nz, 6)
    put2(drums_l, drums_r, nz*np.exp(-t*4)*0.22, start, 0.0)

kick_hits = []
for b in range(BARS):
    base = b * bar
    if b in (0, 8):
        crash(base)
    for k in range(4):
        kick_hits.append(kick(base + k * beat))
    snare(base + 1*beat); clap(base + 1*beat)
    snare(base + 3*beat); clap(base + 3*beat)
    for k in range(8):
        hat(base + k*(beat//2), 0.11 if k % 2 else 0.05,
            dec=45 if k == 7 else 90)          # open-ish hat on the last 8th
    if b % 4 == 3:                              # fill
        for r in range(4):
            snare(base + 3*beat + r*(beat//4))

# --- sidechain duck from the kicks ---
for (ks, kl) in kick_hits:
    seg = np.linspace(0.32, 1.0, kl)
    e = min(len(duck), ks + kl)
    duck[ks:e] = np.minimum(duck[ks:e], seg[:e - ks])

pad_l *= duck; pad_r *= duck
bass  *= duck
arp   *= duck

# --- mix ---
L = pad_l + bass + arp + lead_l + drums_l
R = pad_r + bass + arp + lead_r + drums_r

# wrap tails past the loop for a seamless loop point
tail = SR // 2
L[:tail] += L[N:N+tail]; R[:tail] += R[N:N+tail]
L = L[:N]; R = R[:N]
L = lp(L, 2); R = lp(R, 2)
vinyl = np.random.randn(N) * 0.004
L += vinyl; R += vinyl
L = np.tanh(L * 1.05); R = np.tanh(R * 1.05)
g = 0.94 / max(np.abs(L).max(), np.abs(R).max())
L *= g; R *= g

REPS = 3
L = np.tile(L, REPS); R = np.tile(R, REPS)
NT = N * REPS
stereo = np.empty(NT * 2, dtype=np.int16)
stereo[0::2] = (L * 32767).astype(np.int16)
stereo[1::2] = (R * 32767).astype(np.int16)
with open("music.wav", "wb") as w:
    data = stereo.tobytes()
    w.write(b"RIFF"); w.write(struct.pack("<I", 36 + len(data))); w.write(b"WAVE")
    w.write(b"fmt "); w.write(struct.pack("<IHHIIHH", 16, 1, 2, SR, SR*4, 4, 16))
    w.write(b"data"); w.write(struct.pack("<I", len(data))); w.write(data)
print(f"wrote music.wav  loop {N/SR:.1f}s x{REPS} = {NT/SR:.1f}s  "
      f"{len(data)//1024} KB  ({BPM} BPM)")
