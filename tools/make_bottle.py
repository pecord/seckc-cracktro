#!/usr/bin/env python3
"""Draw a stylized Jeppson's Malort bottle and bake it to a 64x128 4bpp TIM
with a transparent background (CLUT index 0 = 0x0000)."""
import struct
from PIL import Image, ImageDraw, ImageFont

W, H = 64, 128
PAL = [
    (255, 0, 255),    # 0 transparent sentinel
    (232, 170, 44),   # 1 amber body
    (252, 214, 96),   # 2 light amber / glass shine
    (176, 120, 28),   # 3 dark amber / glass shadow
    (206, 164, 52),   # 4 gold cap
    (140, 100, 24),   # 5 dark gold
    (24, 18, 12),     # 6 black (neck label)
    (196, 198, 80),   # 7 label green-yellow
    (120, 132, 44),   # 8 label edge
    (206, 44, 32),    # 9 red (crest / MALORT)
    (130, 24, 16),    # 10 dark red
    (238, 238, 224),  # 11 white
    (255, 236, 150),  # 12 pale highlight
    (90, 70, 20),     # 13 amber deep
    (70, 80, 26),     # 14 olive
    (0, 0, 0),        # 15 spare
]

img = Image.new("RGB", (W, H), PAL[0])
d = ImageDraw.Draw(img)

def A(i):
    return PAL[i]

# cap
d.rectangle([27, 8, 37, 16], fill=A(4))
d.rectangle([27, 15, 37, 17], fill=A(5))
# neck
d.rectangle([28, 16, 36, 36], fill=A(1))
d.rectangle([28, 22, 36, 35], fill=A(6))          # black neck label
# shoulder
d.polygon([(28, 34), (36, 34), (45, 47), (19, 47)], fill=A(1))
# body
d.rectangle([19, 47, 45, 116], fill=A(1))
d.ellipse([19, 108, 45, 123], fill=A(1))          # rounded bottom
d.rectangle([22, 50, 25, 112], fill=A(2))         # left shine
d.rectangle([41, 50, 43, 112], fill=A(3))         # right shadow
# body label
d.rectangle([20, 74, 44, 114], fill=A(7))
d.rectangle([20, 74, 44, 76], fill=A(8))
d.rectangle([20, 112, 44, 114], fill=A(8))
# red crest shield
d.polygon([(29, 78), (35, 78), (35, 84), (32, 88), (29, 84)], fill=A(9))
d.rectangle([31, 80, 33, 86], fill=A(11))
# text
try:
    f1 = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 9)
    d.text((21, 91), "MALORT", fill=A(9), font=f1)
    f2 = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 6)
    d.text((25, 103), "LIQUEUR", fill=A(10), font=f2)
except Exception:
    d.rectangle([23, 93, 41, 98], fill=A(9))

px = img.load()

def nearest(rgb):
    best, bi = 1 << 30, 0
    for i, (pr, pg, pb) in enumerate(PAL):
        dd = (pr - rgb[0])**2 + (pg - rgb[1])**2 + (pb - rgb[2])**2
        if dd < best:
            best, bi = dd, i
    return bi

idx = bytearray(W * H)
for y in range(H):
    for x in range(W):
        idx[y * W + x] = nearest(px[x, y])

def col15(i):
    if i == 0:
        return 0x0000                       # transparent
    r, g, b = PAL[i]
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)

clut = [col15(i) for i in range(16)]
packed = bytearray(W * H // 2)
for i in range(0, W * H, 2):
    packed[i // 2] = idx[i] | (idx[i + 1] << 4)

CLUT_X, CLUT_Y = 464, 0
IMG_X, IMG_Y = 448, 0
out = bytearray()
out += struct.pack("<II", 0x10, 0x08)
out += struct.pack("<I", 12 + 16 * 2)
out += struct.pack("<HHHH", CLUT_X, CLUT_Y, 16, 1)
for c in clut:
    out += struct.pack("<H", c)
out += struct.pack("<I", 12 + (W // 4) * H * 2)
out += struct.pack("<HHHH", IMG_X, IMG_Y, W // 4, H)
out += packed
open("bottle.tim", "wb").write(out)

prev = Image.new("RGB", (W, H))
pp = prev.load()
for y in range(H):
    for x in range(W):
        i = idx[y * W + x]
        pp[x, y] = (40, 10, 40) if i == 0 else PAL[i]
prev.resize((W * 4, H * 4), Image.NEAREST).save("/tmp/bottle_preview.png")
print(f"wrote bottle.tim ({len(out)} bytes)")
