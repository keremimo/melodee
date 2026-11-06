#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Melodee's fm1-base build in the host simulator: each platform/tests/*.txt script against platform/build/host/melodee_host
# (tools/build_base.py --host first). Screens and recordings land in build/base-tests/.
cd "$(dirname "$0")/.."
HOST="$PWD/platform/build/host/melodee_host"
[ -x "$HOST" ] || { echo "run_base_tests.sh: no $HOST (tools/build_base.py --host)"; exit 1; }
OUT="$PWD/build/base-tests"
mkdir -p "$OUT"
fail=0
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
