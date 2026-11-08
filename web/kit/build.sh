#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# The web kit maker's engine (web/kit/melodee_kit.c) as WebAssembly: the firmware's own Record kit processing,
# encoder and decoder. emcc 3.1.74: a local one, else the emscripten/emsdk:3.1.74 Docker image (as web/core). With cc
# and the generated headers (./build.sh first) it also builds the .mel fixture natively and has it record its kits
# through Record kit, keeping the takes it fed them: web/kit/check.mjs makes the same kits from those in WebAssembly.
#   web/kit/build.sh  ->  build/kit/{melodee_kit.wasm, mel_fixture, mel/}
set -e
cd "$(dirname "$0")/../.."
EMSDK_IMAGE="${EMSDK_IMAGE:-emscripten/emsdk:3.1.74}"
KT_ON=$(sed -n 's/^#define KT_ON \([0-9][0-9]*\).*/\1/p' firmware/src/usb_audio_stream.c)   # (a take's trigger)
[ -n "$KT_ON" ] || { echo "kit: no KT_ON in firmware/src/usb_audio_stream.c"; exit 1; }
mkdir -p build/kit
CFLAGS="-O2 -ffp-contract=off -std=gnu99 -Wall -Wno-unused-function -DKT_ON=$KT_ON -Ifirmware/src"
EXPORTS=_kit_max,_kit_begin,_kit_name,_kit_pad,_kit_build,_kit_report,_kit_decode,_malloc,_free
WFLAGS="--no-entry -sSTANDALONE_WASM -sINITIAL_MEMORY=4194304 -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0"
WFLAGS="$WFLAGS -sEXPORTED_FUNCTIONS=$EXPORTS"
if command -v emcc >/dev/null 2>&1; then
    emcc $CFLAGS $WFLAGS -o build/kit/melodee_kit.wasm web/kit/melodee_kit.c
else
    command -v docker >/dev/null 2>&1 || { echo "kit: no emcc and no docker"; exit 1; }
    docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src -u "$(id -u):$(id -g)" \
        "$EMSDK_IMAGE" emcc $CFLAGS $WFLAGS -o build/kit/melodee_kit.wasm web/kit/melodee_kit.c
fi
echo "kit: build/kit/melodee_kit.wasm ($(wc -c < build/kit/melodee_kit.wasm | tr -d ' ') B)"
if [ -f build/gen/melodee_tables.h ] && command -v cc >/dev/null 2>&1; then   # (tests/run_tests.sh's flags)
    cc -O1 -ffp-contract=off -Wall -Wno-unused-function -DMELODEE_USB_AUDIO=1 -w -Ibuild/gen -Ifirmware/src \
        -o build/kit/mel_fixture tests/mel_fixture.c -lm
    build/kit/mel_fixture emit build/kit/mel > /dev/null
    echo "kit: build/kit/mel (the fixture's kits and takes)"
else
    echo "kit: no build/gen (./build.sh) or no cc: the fixture's kits not made"
fi
