#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Build Melodee on macOS (see BUILDING.md).
#   ./build.sh [--release X.Y[.Z]] [--board fm1|smk37] [--model pro|elite|mke]
#   BOARD            target board: fm1 (default) or smk37 (SMK-37 -> build/melodee-smk37.fwsc)
#   MODEL            smk37: pro (default), elite or mke: the model name the keyboard reports
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK)
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
PY="${PYTHON:-python3}"
# Pillow loads Raqm's FriBidi at run time; macOS strips DYLD_* from the environment of /bin/sh, so it is set here
[ "$(uname -s)" = Darwin ] && export DYLD_FALLBACK_LIBRARY_PATH="${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib:/usr/local/lib:/usr/lib}"

"$PY" -c 'import PIL' 2>/dev/null || { echo "build.sh: $PY has no Pillow (pip3 install Pillow)"; exit 1; }
"$PY" -c 'from PIL import features; assert features.check("raqm")' 2>/dev/null ||
    { echo "build.sh: $PY's Pillow has no Raqm (the UI font needs OpenType features: brew install fribidi)"; exit 1; }
[ -x "$JIELI_TOOLCHAIN/pi32v2/bin/clang" ] || { echo "build.sh: no toolchain in $JIELI_TOOLCHAIN (run tools/get_toolchain.sh)"; exit 1; }
[ -f "$AC79_SDK/cpu/wl82/tools/uboot.boot" ] || { echo "build.sh: no JieLi AC79 SDK in $AC79_SDK"; exit 1; }
if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
    docker info >/dev/null 2>&1 || { echo "build.sh: Docker is not running"; exit 1; }
fi
export BOARD="${BOARD:-fm1}"     # read by tools/build.py (--board overrides it)
exec "$PY" tools/build.py "$@"
