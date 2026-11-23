#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Melodee's fm1-base build: felucca-bsp's update loader (the pinned one) installing each built FM-1 base package over
# Melodee's store (tests/base_ldr_test.c), then each platform/tests/*.txt script in the host simulator
# platform/build/host/melodee_host (tools/build_base.py --host first). Screens and recordings land in build/base-tests/.
cd "$(dirname "$0")/.."
OUT="$PWD/build/base-tests"
mkdir -p "$OUT"
fail=0
REV=$(sed -n 's/^REV="\([0-9a-f]\{12\}\).*/\1/p' tools/build_base.py)
BSP="${BASE_BSP:-build/deps/fm1-base-$REV/bsp}"
for pkg in platform/build/melodee.fwsc platform/build/melodee-usb.fwsc; do
    [ -f "$pkg" ] || continue
    name=base_ldr_$(basename "$pkg" .fwsc)
    [ -f "$BSP/loader/ldr_core.c" ] || { echo "FAIL $name (no $BSP: tools/build_base.py fetches it)"; fail=1; break; }
    [ -x "$OUT/base_ldr_test" ] ||
        "${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
            -fno-sanitize-recover=undefined -I"$BSP" tests/base_ldr_test.c -o "$OUT/base_ldr_test" || { fail=1; break; }
    old=
    [ -f build/melodee.fwsc ] && old=build/melodee.fwsc     # the classic build installed: the update users make
    if "$OUT/base_ldr_test" "$pkg" > "$OUT/$name.log" 2>&1 &&
        { [ -z "$old" ] || "$OUT/base_ldr_test" "$pkg" "$old" >> "$OUT/$name.log" 2>&1; }; then
        echo "ok   $name"
    else
        echo "FAIL $name (build/base-tests/$name.log)"
        grep -v ' ok$' "$OUT/$name.log" | tail -8
        fail=1
    fi
done
rm -f "$OUT/base_ldr_test"
HOST="$PWD/platform/build/host/melodee_host"
[ -x "$HOST" ] || { echo "run_base_tests.sh: no $HOST (tools/build_base.py --host)"; exit 1; }
for t in platform/tests/*.txt; do
    name=$(basename "$t" .txt)
    if (cd "$OUT" && "$HOST" "$OLDPWD/$t" > "$name.log" 2>&1); then
        echo "ok   $name"
    else
        echo "FAIL $name (build/base-tests/$name.log)"
        tail -5 "$OUT/$name.log"
        fail=1
    fi
done
exit $fail
