#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The virtual FM-1 (web/core/melodee_core.c) as WebAssembly, for Melodee Studio's IN THIS BROWSER mode.
# Needs Python with Pillow and fontTools (the generated headers, as ./build.sh), and emcc 3.1.74: a local one, else
# the emscripten/emsdk:3.1.74 Docker image (as .github/workflows/emulator.yml pins). With cc it also builds the same
# core natively and the editor test's harness (its own flags, as tests/run_tests.sh) replaying the same frames
# (web/core/spike.mjs compares them).
#   web/core/build.sh  ->  build/core/{melodee_core.wasm, core_native, editor_replay}
set -e
cd "$(dirname "$0")/../.."
PY="${PYTHON:-python3}"
EMSDK_IMAGE="${EMSDK_IMAGE:-emscripten/emsdk:3.1.74}"
[ "$(uname -s)" = Darwin ] && export DYLD_FALLBACK_LIBRARY_PATH="${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib:/usr/local/lib:/usr/lib}"
"$PY" -c 'import sys; sys.path.insert(0, "tools"); sys.argv = ["build.py"]; import build; build.generate()' >/dev/null
"$PY" tools/gen_909_tables.py build/gen/x0x_drum_tables.h >/dev/null
VERSION=$(sed -n 's/^#define MELODEE_VERSION "\([^"]*\)".*/\1/p' firmware/src/melodee.c | head -n 1)
mkdir -p build/core
# release-matching where the host can: resident-audio voice budget (MELODEE_RAM_AUDIO=1); no USB audio (editor.c's
# AUDIO_STATS reads the hardware's counters), no second core (renders serially), no BLE, no resource region
CFLAGS="-O2 -ffp-contract=off -std=gnu99 -w -DMELODEE_RAM_AUDIO=1 -DMELODEE_VERSION=\"$VERSION\" -Ibuild/gen -Ifirmware/src -Itests"
WFLAGS="--no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=16777216 -sMAXIMUM_MEMORY=67108864"
WFLAGS="$WFLAGS -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 -Wl,--export=__heap_base"   # (the static footprint, for the page)
if command -v emcc >/dev/null 2>&1; then
    emcc $CFLAGS $WFLAGS -o build/core/melodee_core.wasm web/core/melodee_core.c
else
    command -v docker >/dev/null 2>&1 || { echo "core: no emcc and no docker"; exit 1; }
    docker run --rm -v "$PWD":/src -w /src -u "$(id -u):$(id -g)" \
        "$EMSDK_IMAGE" emcc $CFLAGS $WFLAGS -o build/core/melodee_core.wasm web/core/melodee_core.c
fi
echo "core: build/core/melodee_core.wasm ($(wc -c < build/core/melodee_core.wasm | tr -d ' ') B, $VERSION)"
if command -v cc >/dev/null 2>&1; then
    cc $CFLAGS -DCORE_NATIVE_MAIN -o build/core/core_native web/core/melodee_core.c -lm
    cc -O1 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -Itests \
        -o build/core/editor_replay web/core/editor_replay.c -lm
    echo "core: build/core/core_native, build/core/editor_replay"
fi
