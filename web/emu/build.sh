#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Melodee in the browser: tests/ui_test.c on web/emu/melodee_web.c, compiled to WebAssembly with Emscripten,
# plus its page. Needs Python with Pillow and fontTools (the generated headers, as ./build.sh) and emcc.
#   web/emu/build.sh  ->  build/emu/{index.html, worklet.js, melodee.wasm}
#   cd build/emu && python3 -m http.server 8000      (then http://localhost:8000)
set -e
cd "$(dirname "$0")/../.."
PY="${PYTHON:-python3}"
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten)"; exit 1; }
"$PY" -c 'import sys; sys.path.insert(0, "tools"); sys.argv = ["build.py"]; import build; build.generate()' >/dev/null
"$PY" tools/gen_909_tables.py build/gen/x0x_drum_tables.h >/dev/null
VERSION=$(sed -n 's/^#define MELODEE_VERSION "\([^"]*\)".*/\1/p' firmware/src/melodee.c | head -n 1)
mkdir -p build/emu
emcc -O2 -ffp-contract=off -std=gnu99 -w -Ibuild/gen -Ifirmware/src -Itests \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 \
    -o build/emu/melodee.wasm web/emu/melodee_web.c
sed "s/@VERSION@/$VERSION/g" web/emu/index.html > build/emu/index.html
cp web/emu/worklet.js build/emu/
echo "emu: build/emu ($(wc -c < build/emu/melodee.wasm | tr -d ' ') B wasm, $VERSION)"
