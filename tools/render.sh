#!/bin/zsh
# Rebuild the demo and capture the DuckStation render window to /tmp/rave.png.
set -e

export PATH="$HOME/src/psx-toolchain/bin:$PATH"
export PSN00BSDK_LIBS="$HOME/src/psn00bsdk/build/install/lib/libpsn00b"

PROJ="$HOME/src/rave-psx"
BUILD_DIR="${BUILD_DIR:-$PROJ/build}"
DUCK="/Applications/DuckStation.app/Contents/MacOS/DuckStation"
FRAME="${1:-180}"
WAIT="${RENDER_WAIT:-}"
PNG="${RENDER_OUT:-/tmp/rave.png}"
LOG="/tmp/duckstation-render.log"
BUILD_LOG="/tmp/rave_build.log"
PID=""

cleanup() {
  if [[ -n "$PID" ]]; then
    kill "$PID" 2>/dev/null || true
  fi
  pkill -f DuckStation 2>/dev/null || true
}

trap cleanup EXIT

if [[ ! -x "$DUCK" ]]; then
  echo "DuckStation not found at $DUCK"
  exit 1
fi

if [[ -z "$WAIT" ]]; then
  WAIT=$((FRAME / 60 + 9))
  if (( WAIT < 12 )); then
    WAIT=12
  fi
fi

cmake --build "$BUILD_DIR" >"$BUILD_LOG" 2>&1 || {
  echo BUILD_FAIL
  tail -20 "$BUILD_LOG"
  exit 1
}

rm -f "$PNG" "$LOG"
pkill -f DuckStation 2>/dev/null || true

"$DUCK" -fastboot "$BUILD_DIR/rave.cue" >"$LOG" 2>&1 &
PID=$!

sleep "$WAIT"

bounds=$(osascript <<'OSA'
tell application "System Events"
  tell process "DuckStation"
    set frontmost to true
    perform action "AXRaise" of window 1
    delay 0.2
    set p to position of window 1
    set s to size of window 1
    set px to item 1 of p as text
    set py to item 2 of p as text
    set sx to item 1 of s as text
    set sy to item 2 of s as text
    return px & "," & py & "," & sx & "," & sy
  end tell
end tell
OSA
)

if [[ -z "$bounds" ]]; then
  echo "Could not read DuckStation window bounds. Log:"
  tail -80 "$LOG"
  exit 1
fi

screencapture -x -R "$bounds" "$PNG"

if [[ ! -s "$PNG" ]]; then
  echo "Capture failed. Log:"
  tail -80 "$LOG"
  exit 1
fi

echo "$PNG"
