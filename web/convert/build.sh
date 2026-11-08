#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The Melodee 1.x -> 2.0 converter (web/convert/legacy.c): the frozen Melodee 1.x tree (web/convert/LEGACY_REF, from
# this repository's history) built as its own virtual FM-1 with legacy.c in it. Needs what ./build.sh needs (the 1.x
# tree's device build makes its generated headers and its synths' files), cc for the native converter, and emcc
# 3.1.74 (else the emscripten/emsdk:3.1.74 Docker image) for the WebAssembly one. NATIVE=1: the native one only.
#   web/convert/build.sh  ->  build/convert/{legacy_native, melodee_legacy.wasm, modules/*.mmod}
set -e
cd "$(dirname "$0")/../.."
ROOT=$PWD
PY="${PYTHON:-python3}"
EMSDK_IMAGE="${EMSDK_IMAGE:-emscripten/emsdk:3.1.74}"
REF=$(tr -d ' \n' < web/convert/LEGACY_REF)
L=build/legacy/$REF/src
[ "$(uname -s)" = Darwin ] && export DYLD_FALLBACK_LIBRARY_PATH="${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib:/usr/local/lib:/usr/lib}"
if [ ! -f "$L/build/modules/drum.mmod" ]; then              # (the 1.x tree, once per ref)
    rm -rf "$L" && mkdir -p "$L"
    git archive "$REF" | tar -x -C "$L"
    (cd "$L" && PYTHON="$PY" ./build.sh > ../build.log 2>&1) || { echo "convert: the 1.x build failed (build/legacy/$REF/build.log)"; exit 1; }
fi
VERSION=$(sed -n 's/^#define MELODEE_VERSION "\([^"]*\)".*/\1/p' "$L/firmware/src/melodee.c" | head -n 1)
MODS="sid drum cz fm6 prophet"
mkdir -p build/convert/mods build/convert/modules
for m in $MODS; do cp "$L/build/modules/$m.mmod" "build/convert/modules/$m.mmod"; done
CFLAGS="-O2 -ffp-contract=off -std=gnu99 -w -DMELODEE_RAM_AUDIO=1 -DMELODEE_VERSION=\"$VERSION\" -I$L/build/gen -I$L/firmware/src -I$L/tests -I$L/web/core -DCORE_MODULES=1"
MFLAGS="-O2 -ffp-contract=off -w -DMOD_IN_CORE=1 -I$L/firmware/modules/sdk -I$L/firmware/hal -I$L/build/gen -I$L/firmware/src"
for m in $MODS; do cc -c $MFLAGS "$L/firmware/modules/$m/$m.c" -o "build/convert/mods/$m.o"; done
cc $CFLAGS -DLEGACY_NATIVE_MAIN -o build/convert/legacy_native web/convert/legacy.c build/convert/mods/*.o -lm
KT_ON=$(sed -n 's/^#define KT_ON \([0-9][0-9]*\).*/\1/p' "$L/firmware/src/usb_audio_stream.c")   # (1.x's kit maker)
cc -O2 -ffp-contract=off -std=gnu99 -w -DKT_ON=$KT_ON -I$L/web/kit -I$L/firmware/modules/drum -I$L/firmware/src \
    -o build/convert/kit_fixture web/convert/kit_fixture.c -lm
echo "convert: build/convert/legacy_native, kit_fixture (1.x $REF, $VERSION)"
if [ "${CHECK:-0}" = 1 ]; then                             # (the tests' 2.0 side: this tree's core, ./build.sh first)
    V2=$(sed -n 's/^#define MELODEE_VERSION "\([^"]*\)".*/\1/p' firmware/src/melodee.c | head -n 1)
    C2="-O2 -ffp-contract=off -std=gnu99 -w -DMELODEE_RAM_AUDIO=1 -DMELODEE_VERSION=\"$V2\" -Ibuild/gen -Ifirmware/src -Itests -Iweb/core -DCORE_MODULES=1"
    M2="-O2 -ffp-contract=off -w -DMOD_IN_CORE=1 -Ifirmware/modules/sdk -Ifirmware/hal -Ibuild/gen -Ifirmware/src"
    mkdir -p build/convert/mods2
    for m in sid 808 909 sidkit cr78 fmkit sampler cz fm6 prophet; do cc -c $M2 "firmware/modules/$m/$m.c" -o "build/convert/mods2/$m.o"; done
    cc $C2 -o build/convert/check_native web/convert/check.c build/convert/mods2/*.o -lm
    echo "convert: build/convert/check_native (2.0, $V2)"
fi
[ "${NATIVE:-0}" = 1 ] && exit 0
WFLAGS="--no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=16777216 -sMAXIMUM_MEMORY=25165824"
WFLAGS="$WFLAGS -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 -Wl,--export=__heap_base"
WFLAGS="$WFLAGS -Wl,--export-table -Wl,--growable-table -Wl,--export=memcpy -Wl,--export=memset -Wl,--export=memcmp"
if command -v emcc >/dev/null 2>&1; then
    emcc $CFLAGS $WFLAGS -o build/convert/melodee_legacy.wasm web/convert/legacy.c
else
    command -v docker >/dev/null 2>&1 || { echo "convert: no emcc and no docker"; exit 1; }
    docker run --rm -v "$ROOT":/src -w /src -u "$(id -u):$(id -g)" \
        "$EMSDK_IMAGE" emcc $CFLAGS $WFLAGS -o build/convert/melodee_legacy.wasm web/convert/legacy.c
fi
for m in $MODS; do (cd "$L" && "$PY" tools/mod_build.py --wasm "build/modules/$m.mmod" >/dev/null) && cp "$L/build/modules/wasm/$m.mmod" "build/convert/modules/$m.wasm.mmod"; done
echo "convert: build/convert/melodee_legacy.wasm ($(wc -c < build/convert/melodee_legacy.wasm | tr -d ' ') B), build/convert/modules"
