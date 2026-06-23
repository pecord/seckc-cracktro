#!/bin/zsh
# Rebuild the demo and render a headless screenshot to /tmp/rave.png
set +e
export PATH="$HOME/src/psx-toolchain/bin:$PATH"
export PSN00BSDK_LIBS="$HOME/src/psn00bsdk/build/install/lib/libpsn00b"
PROJ="$HOME/src/rave-psx"
PCSX="$HOME/src/psx-toolchain/PCSX-Redux.app/Contents/MacOS/PCSX-Redux"
BIOS="$HOME/src/psx-toolchain/openbios/openbios.bin"
FRAME="${1:-150}"

cmake --build "$PROJ/build" >/tmp/build.log 2>&1 || { echo BUILD_FAIL; tail -20 /tmp/build.log; exit 1; }
rm -f /tmp/rave.raw /tmp/rave.png
SHOT_OUT=/tmp/rave.raw SHOT_FRAME=$FRAME nohup "$PCSX" -no-ui -run -interpreter \
  -bios "$BIOS" -exe "$PROJ/build/rave.exe" -dofile "$PROJ/tools/shot.lua" -stdout \
  >/tmp/pcsx.log 2>&1 &
PID=$!
# wait for the screenshot to land (or time out)
for i in {1..30}; do [ -s /tmp/rave.raw ] && break; sleep 0.4; done
sleep 0.5; kill $PID 2>/dev/null; pkill -f PCSX-Redux 2>/dev/null || true

python3 - <<'PY'
import struct,zlib
w,h=320,240
raw=open('/tmp/rave.raw','rb').read()
out=bytearray()
for i in range(w*h):
    v=raw[i*2]|(raw[i*2+1]<<8)
    out+=bytes(((v&0x1f)<<3,((v>>5)&0x1f)<<3,((v>>10)&0x1f)<<3))
def chunk(t,d):
    c=t+d; return struct.pack('>I',len(d))+c+struct.pack('>I',zlib.crc32(c)&0xffffffff)
rows=b''.join(b'\x00'+bytes(out[y*w*3:(y+1)*w*3]) for y in range(h))
open('/tmp/rave.png','wb').write(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows,9))+chunk(b'IEND',b''))
print('OK frame rendered')
PY
