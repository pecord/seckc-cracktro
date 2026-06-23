#!/usr/bin/env python3
"""Bake the SecKC ASCII skull into a 256x256 4bpp green-phosphor TIM."""
import struct
from PIL import Image, ImageDraw, ImageFont

ART = r"""
                          `:oDFo:`
                       ./ymM0dayMmy/.
                    -+dHJ5aGFyZGVyIQ==+-
                `:smo~~Destroy.No.Data~~s:`
             -+h2~~Maintain.No.Persistence~~h+-
         `:odNo2~~Above.All.Else.Do.No.Harm~~Ndo:`
      ./etc/shadow.0days-Data' OR 1=1--.No.0MN8'/.
   -++SecKCoin++e.AMd`       `.-://///+hbove.913.ElsMNh+-
  -~/.ssh/id_rsa.Des-                  `htN01UserWroteMe!-
  :dopeAW.No<nano>o                     :is:TRiKC.sudo-.A:
  :we're.all.alike'`                     The.PFYroy.No.D7:
  :PLACEDRINKHERE!:                      yxp_cmdshell.Ab0:
  :msf>exploit -j.                       :Ns.BOB&ALICEes7:
  :---srwxrwx:-.`                        `MS146.52.No.Per:
  :<script>.Ac816/                        sENbove3101.404:
  :NT_AUTHORITY.Do                        `T:/shSYSTEM-.N:
  :09.14.2011.raid                       /STFU|wall.No.Pr:
  :hevnsntSurb025N.                      dNVRGOING2GIVUUP:
  :#OUTHOUSE-  -s:                       /corykennedyData:
  :$nmap -oS                              SSo.6178306Ence:
  :Awsm.da:                            /shMTl#beats3o.No.:
  :Ring0:                             `dDestRoyREXKC3ta/M:
  :23d:                               sSETEC.ASTRONOMYist:
   /-                        /yo-    .ence.N:(){ :|: & };:
                             `:Shall.We.Play.A.Game?tron/
                             ```-ooy.if1ghtf0r+ehUser5`
                           ..th3.H1V3.U2VjRFNN.jMh+.`
                          `MjM~~WE.ARE.se~~MMjMs
                           +~KANSAS.CITY's~-`
                            J~HAKCERS~./.`
                            .esc:wq!:`
                             +++ATH`
"""

TEX = 256
lines = [ln for ln in ART.split("\n")]
font = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 18)

ch_w = font.getlength("M")
ch_h = 20
W = int(max(len(l) for l in lines) * ch_w) + 8
H = len(lines) * ch_h + 8
big = Image.new("L", (W, H), 0)
d = ImageDraw.Draw(big)
for i, ln in enumerate(lines):
    d.text((4, 4 + i * ch_h), ln, fill=255, font=font)

img = big.resize((TEX, TEX), Image.LANCZOS)
px = img.load()

def ramp(i):
    t = i / 15.0
    r = round(12 * t)
    g = round(31 * (0.35 + 0.65 * t))
    b = round(20 * t * t)
    return (r << 0) | (min(g, 31) << 5) | (min(b, 31) << 10) | 0x8000

clut = [0x0000] + [ramp(i) for i in range(1, 16)]

idx = bytearray(TEX * TEX)
for y in range(TEX):
    for x in range(TEX):
        idx[y * TEX + x] = round(px[x, y] / 255 * 15)

packed = bytearray(TEX * TEX // 2)
for i in range(0, TEX * TEX, 2):
    packed[i // 2] = idx[i] | (idx[i + 1] << 4)

CLUT_X, CLUT_Y = 384, 0
IMG_X, IMG_Y = 320, 0
out = bytearray()
out += struct.pack("<II", 0x10, 0x08)
out += struct.pack("<I", 12 + 16 * 2)
out += struct.pack("<HHHH", CLUT_X, CLUT_Y, 16, 1)
for c in clut:
    out += struct.pack("<H", c)
out += struct.pack("<I", 12 + (TEX // 4) * TEX * 2)
out += struct.pack("<HHHH", IMG_X, IMG_Y, TEX // 4, TEX)
out += packed
open("seckc.tim", "wb").write(out)

prev = Image.new("RGB", (TEX, TEX))
pp = prev.load()
for y in range(TEX):
    for x in range(TEX):
        c = clut[idx[y * TEX + x]]
        pp[x, y] = (((c) & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)
prev.resize((512, 512), Image.NEAREST).save("/tmp/seckc_preview.png")
print(f"wrote seckc.tim ({len(out)} bytes), preview /tmp/seckc_preview.png")
