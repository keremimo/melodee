#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The 1.x -> 2.0 converter (web/convert) against both firmwares' own code, natively: Melodee 1.x's virtual FM-1 with
# legacy.c in it (legacy_native: the frozen tree, web/convert/LEGACY_REF) writes the 1.x stores and converts them;
# 2.0's (check_native: this tree's core) takes the converted ones through its backup PUT and plays them.
#   1. renders: the fixture projects (made by 1.x's code: legacy_native fixtures, kit_fixture's user kits) played 10 s in
#      the 1.x core and, converted, in the 2.0 core (web/convert/render_cmp.mjs):
#        synths, midi, userkit   every sample the same
#        kits                    the same at a shift of at most 64 samples (a kit that becomes another kit's synth
#                                starts at the next block in 2.0)
#        kitjump                 the same until 2.5 s (KIT moved by Motion to another kit's synth: 2.0 drops it)
#   2. the store (legacy_native store: user presets of every form in five banks, stars, a voice budget, a template):
#      converted, every object PUT into 2.0; each user preset loaded in both cores: the same synth, every parameter,
#      the same patch bytes; the stars by synth
#   3. a real 1.x backup (CONVERT_BACKUP, else Kerem's FM-1_900 one if this checkout has it): converted, PUT into 2.0
#   tests/convert_test.sh [OUT]   (needs the 1.x tree built once: web/convert/build.sh; node; run_tests.sh's job)
set -u
cd "$(dirname "$0")/.."
OUT=${1:-build/host/convert}
B=build/convert
failed=0
ok() { echo "convert: $1"; }
bad() { echo "convert: FAIL $1"; failed=$((failed + 1)); }
CHECK=1 NATIVE=1 sh web/convert/build.sh > "$OUT.build.log" 2>&1 || { echo "convert: FAIL the build ($OUT.build.log)"; cat "$OUT.build.log"; exit 1; }
rm -rf "$OUT" && mkdir -p "$OUT/fx" "$OUT/r" "$OUT/st" "$OUT/st2" "$OUT/bk"

# 1. the fixtures, played in both cores
"$B/kit_fixture" "$OUT/fx" > "$OUT/fx.log" && "$B/legacy_native" "$B/modules" fixtures "$OUT/fx" >> "$OUT/fx.log" 2>&1 ||
    { bad "the fixtures ($OUT/fx.log)"; cat "$OUT/fx.log"; }
for spec in synths:identical midi:identical userkit:identical kits:shifted:64 kitjump:until:2.5; do
    name=${spec%%:*} how=${spec#*:}
    kits=; [ "$name" = userkit ] && kits="$OUT/fx"
    "$B/legacy_native" "$B/modules" render "$OUT/fx/v1-$name.rec" 10 "$OUT/r/a-$name.f32" $kits > /dev/null &&
        "$B/check_native" build/modules render "$OUT/fx/v2-$name.rec" 10 "$OUT/r/b-$name.f32" $kits > /dev/null ||
        { bad "$name: not rendered"; continue; }
    if r=$(node web/convert/render_cmp.mjs "$OUT/r/a-$name.f32" "$OUT/r/b-$name.f32" $(echo "$how" | tr ':' ' ')); then
        ok "$name: $r"
    else bad "$name: $r"; fi
done

# 2. the store: PUT, presets, stars
"$B/legacy_native" "$B/modules" store "$OUT/st" > "$OUT/st.log" 2>&1 &&
    "$B/legacy_native" "$B/modules" convert "$OUT/st" "$OUT/st2" >> "$OUT/st.log" 2>&1 || { bad "the store not converted ($OUT/st.log)"; }
if "$B/check_native" build/modules put "$OUT/st2" > "$OUT/put.log" 2>&1; then
    ok "the store: $(grep -c '^put' "$OUT/put.log") objects PUT into 2.0, every one taken"
else bad "the store: 2.0 refused $(grep '^put' "$OUT/put.log" | grep -v 'rc 0$' | tr '\n' ' ')"; fi
"$B/legacy_native" "$B/modules" presets "$OUT/st" "$OUT/r/pa" > "$OUT/pa.txt" 2>&1
"$B/check_native" build/modules presets "$OUT/st2" "$OUT/r/pb" > "$OUT/pb.txt" 2>&1
n=$(grep -c '^preset' "$OUT/pa.txt")
if [ "$n" -ge 11 ] && [ "$(grep '^preset' "$OUT/pa.txt")" = "$(grep '^preset' "$OUT/pb.txt")" ]; then
    ok "the store: $n user presets loaded in both cores: the same synth, every parameter, the same patch bytes"
else bad "the store: user presets differ ($OUT/pa.txt, $OUT/pb.txt)"; diff "$OUT/pa.txt" "$OUT/pb.txt" | head -20; fi
stars=$(grep '^star' "$OUT/pb.txt" | tr '\n' ',')       # (the store's: Prophet 5 and 200, FM6 2, CZ-1 10, SID 1; DRUM's 1
want="star PROPHET 5,star PROPHET 200,star FM6 2,star CZ-1 10,star SID 1,star 909 0,star SID KIT 1,"   # and 3: their kits')
if [ "$stars" = "$want" ]; then ok "the store: the stars by synth ($(grep -c '^star' "$OUT/pb.txt"))"
else bad "the store: stars $stars (want $want)"; fi

# 3. a real 1.x backup
BK=${CONVERT_BACKUP:-}
[ -z "$BK" ] && for d in build/backups/fm1-900-1x-2026-11-19 "$(dirname "$(git rev-parse --git-common-dir 2>/dev/null)")/build/backups/fm1-900-1x-2026-11-19"; do
    [ -f "$d/1.bin" ] && BK=$d && break
done
if [ -n "$BK" ]; then
    if "$B/legacy_native" "$B/modules" convert "$BK" "$OUT/bk" > "$OUT/bk.log" 2>&1 &&
        "$B/check_native" build/modules put "$OUT/bk" > "$OUT/bkput.log" 2>&1; then
        ok "a real backup ($BK): converted, $(grep -c '^put' "$OUT/bkput.log") objects PUT into 2.0"
    else bad "a real backup ($BK): $OUT/bk.log, $OUT/bkput.log"; fi
else
    echo "convert: no real 1.x backup (CONVERT_BACKUP=DIR): skipped"
fi
[ $failed = 0 ] && echo "convert: all ok" || echo "convert: $failed FAILED"
exit $failed
