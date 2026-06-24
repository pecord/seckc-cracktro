#!/usr/bin/env python3
"""Draw a DVD-screensaver-style logo and bake it to a 64x32 4bpp TIM with a
transparent background. The logo is white/gray so the demo can TINT it a
different neon colour on every bounce."""
import struct
from PIL import Image, ImageDraw, ImageFont

W, H = 64, 32
img = Image.new("L", (W, H), 0)        # black bg -> transparent
d = ImageDraw.Draw(img)

try:
    f = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 21)
except Exception:
    f = ImageFont.load_default()

# "DVD" wordmark, faux-bold by overdrawing with small offsets
for ox in (0, 1, 2):
    for oy in (0, 1):
        d.text((9 + ox, -1 + oy), "DVD", fill=255, font=f)

# the oval underline with tiny VIDEO
d.ellipse([6, 22, 58, 30], outline=255, width=1)
try:
    fs = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 6)
    d.text((23, 22), "VIDEO", fill=200, font=fs)
except Exception:
    pass

px = img.load()
idx = bytearray(W * H)
for y in range(H):
    for x in range(W):
        idx[y * W + x] = round(px[x, y] / 255 * 15)

def col15(i):
    if i == 0:
        return 0x0000
    v = round(i / 15 * 31)
    return v | (v << 5) | (v << 10)         # white/gray ramp (tintable)

clut = [col15(i) for i in range(16)]
packed = bytearray(W * H // 2)
for i in range(0, W * H, 2):
    packed[i // 2] = idx[i] | (idx[i + 1] << 4)

CLUT_X, CLUT_Y = 528, 256
IMG_X, IMG_Y = 512, 256
out = bytearray()
out += struct.pack("<II", 0x10, 0x08)
out += struct.pack("<I", 12 + 16 * 2)
out += struct.pack("<HHHH", CLUT_X, CLUT_Y, 16, 1)
for c in clut:
    out += struct.pack("<H", c)
out += struct.pack("<I", 12 + (W // 4) * H * 2)
out += struct.pack("<HHHH", IMG_X, IMG_Y, W // 4, H)
out += packed
open("dvd.tim", "wb").write(out)

prev = Image.new("RGB", (W, H))
pp = prev.load()
for y in range(H):
    for x in range(W):
        v = idx[y * W + x] * 17
        pp[x, y] = (30, 8, 30) if v == 0 else (v, v, v)
prev.resize((W * 5, H * 5), Image.NEAREST).save("/tmp/dvd_preview.png")
print(f"wrote dvd.tim ({len(out)} bytes)")
