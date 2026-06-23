#!/bin/zsh
# Build the SecKC cracktro and launch it in DuckStation.
set -e
export PATH="$HOME/src/psx-toolchain/bin:$PATH"
export PSN00BSDK_LIBS="$HOME/src/psn00bsdk/build/install/lib/libpsn00b"
PROJ="$HOME/src/rave-psx"

echo "Building..."
cmake --build "$PROJ/build" >/tmp/rave_build.log 2>&1 || {
	echo "BUILD FAILED:"; tail -20 /tmp/rave_build.log; exit 1
}

echo "Launching DuckStation..."
pkill -9 -f DuckStation 2>/dev/null || true
sleep 1
open -a /Applications/DuckStation.app "$PROJ/build/rave.cue"
echo "Running rave.cue. Alt+Enter = fullscreen, Esc = quit."
