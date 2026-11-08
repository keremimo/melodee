#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Host tests of the Melodee sources (no hardware). Run from the repo root after ./build.sh:
#   tests/run_tests.sh               everything
#   SOUND=0 tests/run_tests.sh       everything but the sound tests
#   SOUND=only tests/run_tests.sh    the sound tests alone
#   UI=renders tests/run_tests.sh    UI screenshots only (build/ui_new), for the web manual
# Sound tests: the regression suite, the DSP render and TRACKS, and every test of an engine's or an effect's rendered
#   signal (native fp32, Prophet, SPEAKER, MOD, SLICER, the FX layer, REVERB, DELAY, MOD / SPREAD, CZ, SID,
#   CZ-1 knobs, PHYS, DRUM, NOISE, FM6, the dual-core pairing). The rest (storage, UI, MIDI, USB, editor, web) always runs.
# Each test (its compile and its runs) is a job; PARALLEL=n jobs run at once (default: the CPU count; 1: one by one).
# A job's output goes to build/host/log/<n>.log and is printed in the order of this file as the jobs end; the
# failures are listed again at the end. The cost gates count instructions, which the load leaves alone; the ns
# figures printed beside them are noisier in parallel (PARALLEL=1 for quiet ones).
#
# Regression suite (tests/regress.c, tests/target_budget.py; details at the top of regress.c):
#   golden renders  every engine x preset, the GM kit (808 DRUM), voice modes, FX sends, a 4-track mix: one hash
#                   each in tests/golden.txt. A change of the sound fails with the list of renders.
#   health          clipping, DC, peak level, voices free after the release, silence at the end.
#   CPU             instructions / sample per preset and mix (tests/cpu_baseline.txt, +25 %), ns printed;
#                   target: loop instructions of the render functions in the pi32v2 disassembly
#                   (tests/target_budget.txt, +10 %; exact, static).
#   voices          the budget of 8, steal fades, MONO / LEGATO / UNISON keep their note, the VOICE cap,
#                   no hanging notes on any MIDI / key routing.
# UI renders (tests/ui_render.c): every screen in every palette from the real drawing code: the layout lint (no text
#                   off the screen, cut, hidden, overlapping or spilling out of its cell / card; only free text
#                   ellipsised), MONO gray, the draw cost, the text audit (build/ui_new/text_audit.tsv);
#                   PNGs of MONO GREEN PAPER in build/ui_new (tests/ui_render.py), the findings in build/ui_new/report.txt;
#                   FM6's 32 algorithm charts as drawn (no box overlapping, no route through a box or crossing another);
#                   every frame of the rolling digits (lint, MONO), their filmstrips in build/ui_slot.
# UI (tests/ui_test.c): the UI sources against stub display / buttons / knobs: sound loads keep the steps and
#                   the track's ARP / SCL / SLICER, the SEQ > PATTERNS loader and its REPLACE? dialog, the
#                   one-step undo of both (EDIT held), REC on TRACKS / SEQ / ARP, STEP and ARP while recording,
#                   MIDI IN ROUT, saves refused while playing and the OVERWRITE? dialog, MUTE on TRACKS KNOB 1,
#                   the DRUM grid (keys, knobs, LEDs, pages, live recording into it, BEAT from PATTERNS).
# Audio / persistence / editor: the real C paths against simulated DMA and NOR flash: bounded overload
#                   fades, shared-voice limits, deferred settings and retries, failed-save rollback,
#                   malformed transfers, transport-stop timeouts, MIDI and UART recovery.
# CHORD (tests/chord_test.c): the chord keys (src/chord.c): diatonic triads / sevenths of several scales and roots,
#                   the fixed shapes and voicings (at most 4 notes), names, MONO plays the root, a release ends
#                   exactly what its key / MIDI note started, recording, the ARP, MIDI IN, kits ignore CHRD.
# MOD (tests/mod_test.c): the modulation matrix: slots that do nothing are bit-identical, every source on each
#                   kind of destination, clamping, MIDI CC1 / CC11 / aftertouch routing, the cost of 4 active
#                   slots (PHYS at most +5%, mixed dry path at most 90 instructions/sample), demos in build/mod_demo/.
# LEGACY FX (tests/no_effects_test.c): legacy values/automation do not process audio or allocate,
#                   old notes survive, retired MIDI sends are inert, FX opens only the new knob page.
# MOD / SPREAD (tests/mod_spread_test.c): the matrix's S&H / SLEW sources and DEPTH destination, SPREAD's sides.
# LAYERS / SCREEN (tests/layer_screen_test.c): GLO double-tap lock, SCL tap navigation, SCREEN OFF, LFO 2.
# INPUT (tests/input_test.c): the key / button debounce of hal/fm1_input.h against the TIMER5 scan and bouncing
#                   contacts: a press within 2 scans (<= 2.3 ms), one note per bouncy press, no early or hanging
#                   release, stray samples ignored, fast repeats, the encoders' detents.
# USB audio (tests/usb_audio_*): the descriptors of every build (MIDI only, CDC, USB audio) as a host parses them
#                   (usb_audio_desc_test.py); the rings against clock drift, packet bounds and recovery
#                   (usb_audio_test.c); the endpoint driver against a model SIE: SET_INTERFACE, feedback, capture
#                   packets ready in time, Out / In left out and the replug (usb_audio_driver_test.c); the four
#                   track stems through the real mixer (usb_audio_tracks_test.c).
# web (web/test_web.mjs): the editor protocol against its mock device, whose tables must equal the
#                   firmware's (tests/descdump.c -> build/host/desc.json), the package builder, the updater.
# .mel (tests/mel_fixture.c, web/test_mel.mjs): the project file the web page writes and reads (web/mel_file.js; the
#                   firmware has no .mel code): the page's record layout == the firmware's, its compact form and
#                   coder, damaged files refused, a project's kits renamed in every place as the firmware's decoded
#                   state has them, export / import against a simulated device, then what the page sent through the
#                   device's own backup PUT; the sizes next to the stored records.
# Studio bundle (tools/studio_bundle.py -> build/studio-bundle): what Melodee Studio takes from the firmware (desc,
#                   layout + fixtures, protocol, constants, settings, scales, Prophet factory, palettes, licences), from
#                   the firmware's own sources (tests/protodump.c, constdump.c, settingsdump.c), checked against itself
#                   and its manifest's hashes; with web/core/build.sh's WebAssembly, the virtual FM-1 core
#                   (web/core/core_check.mjs): Studio's commands, a render, taps, events, kits, the NOR, WASM == native,
#                   the five synths' modules in WebAssembly (tools/mod_build.py --wasm) == the native core's.
# DRUM (tests/drum_test.c): the drum voices (src/drum_voice.c): pitch, decay, centroid and level against
#                   Melodee's targets, the controls' directions, no clipping, DC, retriggers, the hat choke, the
#                   kick on a small speaker; the DRUM engine (src/eng_drum.c): its key map, the 8 lanes together,
#                   one hit per lane, the choke between lanes; the cost per voice; demos in build/drum_demo/.
# DRUM SID (tests/drum_sid_test.c): KIT SID (src/drum_sid.c): one 6581 on one voice, its three voices taken by
#                   the hits, frame programs at PAL frames, DIGI on $D418 and its effect on the voices ringing,
#                   the filter for the routed drums, levels next to the 808; demos in build/sid_kit_demo/.
# DRUM KITS (tests/drum_kits_test.c): KIT CR78 (src/drum_cr78.c) and KIT FM (src/drum_fm.c) on the lanes of
#                   src/drum_lanes.c: every drum alone, one voice, a lane struck again without a step, the hat choke,
#                   a recorded length, kit switches, the knobs' directions, levels next to the 808, no wrapped
#                   sample, the corners; demos in build/drum_kits_demo/.
# FM6 (tests/fm6_test.c): the 6-operator FM engine (src/eng_fm6.c, src/fm6_core.c) against the DX7: the 32 algorithms
#                   against its diagrams, pitch, levels, envelopes, modulation; no DC / clipping over the factory
#                   patches, the macros (neutral at 0, their directions), factory presets, pack / unpack and the SysEx layouts,
#                   Dexed's 16 voices, the cost per voice; demos in build/fm6_demo/. tests/fm6_ams_test.c: the fp32 AMS share
#                   against Dexed's double reference. With DEXED_SRC (a Dexed checkout's Source/): tests/fm6_parity.sh
#                   --quick renders scores through Dexed's own code and FM6 and reports sample differences (strict parity predates the fp32 port).
# Change baseline entries only for reviewed, intentional differences in sound or cost;
# retain every unaffected golden / CPU / target entry. VERBOSE=1: every render.
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"                           # (as build.sh: the Python with Pillow, Raqm and the build tools)
# Pillow's Raqm (text spacing, renders): macOS strips DYLD_* on the way into /bin/sh (build.sh)
[ "$(uname -s)" = Darwin ] && export DYLD_FALLBACK_LIBRARY_PATH="${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib:/usr/local/lib:/usr/lib}"
OUT=build/host
LOG=$OUT/log
CC="${CC:-cc} -O1 -ffp-contract=off -Wall -Wno-unused-function"
FW="-w -Ibuild/gen -Ifirmware/src"
P5=build/prophet5/Prophet-5+10-Factory-Programs-ReadMe1.03
SOUND=${SOUND:-1}
PARALLEL=${PARALLEL:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
case $SOUND in 0 | 1 | only) ;; *) echo "SOUND=0, 1 or only"; exit 2 ;; esac
UI=${UI:-}
case $UI in "" | renders) ;; *) echo "UI=renders or empty"; exit 2 ;; esac
if [ "$UI" = renders ]; then
    "$PY" -c "import PIL" 2>/dev/null || { echo "UI renders need Pillow ($PY -m pip install Pillow)" >&2; exit 1; }
fi
[ "$PARALLEL" -ge 1 ] 2>/dev/null || { echo "PARALLEL=n, n >= 1"; exit 2; }
"$PY" tools/gen_scales.py --check || exit 1
"$PY" tools/audio_contract.py --check || exit 1
if [ "$UI" = renders ]; then
    [ -f build/gen/melodee_tables.h ] || { echo "run ./build.sh first"; exit 1; }
else
    [ -f build/melodee.fwsc ] || { echo "run ./build.sh first"; exit 1; }
fi
TABLES=0; [ -f build/gen/melodee_tables.h ] && TABLES=1
PIL=0; "$PY" -c "import PIL" 2>/dev/null && PIL=1
mkdir -p "$OUT" "$LOG"
rm -f "$LOG"/*
start=$(date +%s)

# ------------------------------------------------------------------- jobs ---
# job FN ARGS...: FN ARGS in the background once one of the PARALLEL slots is free (fd 3, a FIFO holding a line per
#   free slot), the output in $LOG/<n>.log; a failure is a line in $LOG/<n>.fail, $LOG/<n>.done marks the end.
# In a job: step TITLE CMD... (the title, then CMD; a failure is noted and the job goes on),
#   build OUT SRC FLAGS... (tests/SRC.c -> $OUT/OUT), fail WHAT.
# t TITLE OUT[:SRC] FLAGS... [-- CMD...]: the job building tests/SRC.c (SRC: OUT) and running CMD ($OUT/OUT).
n=0 shown=0 pids=
mkfifo "$LOG/slots" && exec 3<>"$LOG/slots" && rm -f "$LOG/slots" || exit 1
i=0; while [ $i -lt "$PARALLEL" ]; do echo; i=$((i + 1)); done >&3
# jobs ignore SIGINT (asynchronous lists): stop them and everything under them (cc -> clang -cc1, regress -> its renders)
tree() { echo "$1"; for c in $(pgrep -P "$1"); do tree "$c"; done; }
stop() { kill -TERM $(for p in $pids; do tree "$p"; done) 2>/dev/null; }
trap 'stop; exit 130' INT
trap 'stop; exit 143' TERM

fail() { echo "$1" >> "$LOG/$n.fail"; }
step() { echo "== $1"; _step=$1; shift; "$@" || fail "$_step"; }
build() {
    _out=$1 _src=$2; shift 2
    $CC "$@" -o "$OUT/$_out" "tests/$_src.c" -lm || { fail "compile tests/$_src.c ($_out)"; return 1; }
}
_t() {
    _title=$1 _out=${2%%:*} _src=${2#*:} _flags=; shift 2
    while [ $# -gt 0 ] && [ "$1" != -- ]; do _flags="$_flags $1"; shift; done
    [ $# -gt 0 ] && shift
    [ $# -gt 0 ] || set -- "$OUT/$_out"
    echo "== $_title"
    build "$_out" "$_src" $_flags || return
    "$@" || fail "$_title"
}
flush() {                                         # the logs of the jobs ended, in order
    while [ $shown -lt $n ] && [ -f "$LOG/$((shown + 1)).done" ]; do
        shown=$((shown + 1))
        cat "$LOG/$shown.log"
        [ -f "$LOG/$shown.fail" ] && cat "$LOG/$shown.fail" >> "$LOG/failed"
    done
}
job() {
    read -r slot <&3
    flush
    n=$((n + 1))
    ( "$@" 3>&- > "$LOG/$n.log" 2>&1; : > "$LOG/$n.done"; echo >&3 ) &
    pids="$pids $!"
}
t() { job _t "$@"; }
run() { job step "$@"; }
note() { job echo "$@"; }
core() { [ "$SOUND" != only ]; }
sound() { [ "$SOUND" != 0 ]; }

# ----------------------------------------------------------- multi-step jobs ---
web_tests() {                                     # the editor mock's tables must equal the firmware's (desc.json)
    if [ $TABLES = 1 ]; then
        build descdump descdump $FW &&
            step "parameter and engine tables as JSON (for the editor mock test)" desc_json
    fi
    if command -v node >/dev/null 2>&1; then
        step "web pages: editor protocol + the synths from their schemas, package builder, update protocol" node web/test_web.mjs
        step "installer UI: release selection, fetch races, cache and write lock" node web/test_installer.mjs
        step "installer safety: tab locks, wake/leave guards, throttled MIDI, preflight and interrupted resumes" node web/test_install_safety.mjs
        step "TransitFM1: verified package, backup, loader isolation and resume" node web/test_transit.mjs
        step "web backup: capture, validation before writes, restore order" node web/test_backup.mjs
        step "stock restore: the unmodified official V15 file is the image, CHECK HEAD range" node web/test_stock_restore.mjs
        if [ -f FM-1_v15.fwsc ]; then
            build v15_ldr ldr_test &&
                step "original V15 back to Melodee" "$OUT/v15_ldr" FM-1_v15.fwsc build/melodee.fwsc
        fi
    else
        echo "== skip web tests (no node)"
    fi
}
module_tests() {                                  # synth modules (ABI 3): built for the host ONCE, three firmwares
    _m="$OUT/modules" _o= _dev="-DMELODEE_SID_BUILTIN=0 -DMELODEE_DRUM_BUILTIN=0 -DMELODEE_CZ_BUILTIN=0 -DMELODEE_FM6_BUILTIN=0 -DMELODEE_P5_BUILTIN=0"
    mkdir -p "$_m/abi"
    for _s in sid drum cz fm6 prophet; do          # (the engine sources' modules; again for another firmware's layout)
        $CC -c -O2 -Ifirmware/modules/sdk -Ifirmware/hal $FW firmware/modules/$_s/$_s.c -o "$_m/$_s.o" &&
            $CC -c -O2 -DMELODEE_ABI_SHUFFLE=1 $_dev -Ifirmware/modules/sdk -Ifirmware/hal $FW \
                firmware/modules/$_s/$_s.c -o "$_m/abi/$_s.o" || { fail "compile firmware/modules/$_s/$_s.c"; return; }
        _o="$_o $_m/$_s.o"
    done
    for _s in trio wheel; do                       # (the contract alone)
        $CC -c -O2 -DMOD_HOSTED -Ifirmware/modules/sdk firmware/modules/$_s/$_s.c -o "$_m/$_s.o" ||
            { fail "compile firmware/modules/$_s/$_s.c"; return; }
        _o="$_o $_m/$_s.o"
    done
    step "synth module objects: the same against another firmware's private track_t / voice_t and its options (a module sees ABI 3's views only)" \
        sh -c "for s in sid drum cz fm6 prophet; do cmp -s '$_m/'\$s.o '$_m/abi/'\$s.o || { echo \$s.o differs; exit 1; }; done"
    (build module_test module_test -O2 -Ifirmware/modules/sdk -Ifirmware/hal $FW $_o &&
        step "synth modules: TRIO, WHEEL, SID, DRUM, CZ-1, FM6 and the Prophet (ABI 3) bit-exact, their presets from their files as the makers made them, files into slots, relocation, boot, refusals, capabilities (tools/mod_build.py)" \
            "$OUT/module_test") > "$_m/1.log" 2>&1 &
    (build module_test_device module_test -O2 $_dev -Ifirmware/modules/sdk -Ifirmware/hal $FW $_o &&
        step "synth modules as the device builds them: SID, DRUM, CZ-1, FM6 and the Prophet out of the firmware, the modules against the old renders, their presets (files only) against the old makers" \
            "$OUT/module_test_device") > "$_m/2.log" 2>&1 &
    (build module_test_abi module_test -O2 -DMELODEE_ABI_SHUFFLE=1 $_dev -Ifirmware/modules/sdk -Ifirmware/hal $FW $_o &&
        step "synth modules ABI stability: the same module objects in a firmware whose private track_t / voice_t fields are padded and reordered, bit-exact" \
            "$OUT/module_test_abi") > "$_m/3.log" 2>&1 &
    wait
    cat "$_m/1.log" "$_m/2.log" "$_m/3.log"
}
outside_author() {                                # the SDK alone: firmware/modules/sdk copied away with one module's source
    _d="$OUT/outside"
    rm -rf "$_d" && mkdir -p "$_d" && cp -R firmware/modules/sdk "$_d/sdk" && cp firmware/modules/trio/trio.c "$_d/trio.c" ||
        { fail "outside author: the folder"; return; }
    printf '%s\n' 028b4316505219da8b45b58781af5e22cd8e9771377068f9a1232d7e872b9f52 > "$_d/test.key"   # (the test key's seed)
    step "outside author: the SDK folder alone (no Melodee tree; the test key, as a tree without the release key) builds a module from its source" \
        sh -c "cd '$_d' && MELODEE_SIGN_KEY=test.key '$PY' sdk/mod_build.py trio.c -o out/trio.mmod >/dev/null" || return
    $CC -c -O2 -DMOD_HOSTED -I"$_d/sdk" "$_d/trio.c" -o "$_d/trio.o" || { fail "outside author: the host object"; return; }
    build outside_author_test outside_author_test -O1 $FW "$_d/trio.o" &&
        step "outside author: installed, listed, loaded and played in the host firmware as the reference plays it, Melodee's TRIO; the in-tree file (the release key's) the same module" \
            "$OUT/outside_author_test" "$_d/out/trio.mmod" build/modules/trio.mmod
}
outside_j106() {                                  # an outside synth: Saadet's JUNO-106 (outside/j106) and the SDK alone
    _d="$OUT/outside_j106"
    rm -rf "$_d" && mkdir -p "$_d" && cp -R firmware/modules/sdk "$_d/sdk" && cp -R outside/j106 "$_d/j106" ||
        { fail "outside JUNO-106: the folder"; return; }
    printf '%s\n' 5aade7a5e6c1b3d0a2e7b6f4c9d8e1f0a3b2c5d4e7f6a9b8c1d0e3f2a5b4c7d6 > "$_d/author.key"   # (a test author)
    step "outside JUNO-106: Saadet's synth built from the SDK folder alone (no Melodee tree; signed by a test author's key)" \
        sh -c "cd '$_d' && MELODEE_AUTHOR_KEY=author.key '$PY' sdk/mod_build.py j106/j106.c -o out/j106.mmod >/dev/null" || return
    $CC -c -O2 -DMOD_HOSTED -I"$_d/sdk" "$_d/j106/j106.c" -o "$_d/j106.o" || { fail "outside JUNO-106: the host object"; return; }
    build outside_j106_test outside_j106_test -O1 $FW "$_d/j106.o" || return
    step "outside JUNO-106: installed (the first free number, its author's), its presets from its file, plays in stereo, its pages, SysEx, a project saved, loaded, on another device" \
        "$OUT/outside_j106_test" "$_d/out/j106.mmod" "$_d/mel"
    if command -v node >/dev/null 2>&1; then
        step "outside JUNO-106: its project through a .mel file (web/mel_file.js: export, import)" \
            node tests/outside_j106_mel.mjs "$_d/mel" &&
            step "outside JUNO-106: what the .mel import sent goes in through the backup PUT, loads, plays the same" \
                "$OUT/outside_j106_test" --mel "$_d/out/j106.mmod" "$_d/mel"
    else
        echo "== skip the outside JUNO-106 .mel round trip (no node)"
    fi
}
outside_hydra() {                                 # an outside synth: Saadet's HYDRA (outside/hydra) and the SDK alone
    _d="$OUT/outside_hydra"
    rm -rf "$_d" && mkdir -p "$_d" && cp -R firmware/modules/sdk "$_d/sdk" && cp -R outside/hydra "$_d/hydra" ||
        { fail "outside HYDRA: the folder"; return; }
    printf '%s\n' 5aade7a5e6c1b3d0a2e7b6f4c9d8e1f0a3b2c5d4e7f6a9b8c1d0e3f2a5b4c7d6 > "$_d/author.key"   # (a test author)
    step "outside HYDRA: Saadet's synth built from the SDK folder alone (no Melodee tree; signed by a test author's key)" \
        sh -c "cd '$_d' && MELODEE_AUTHOR_KEY=author.key '$PY' sdk/mod_build.py hydra/hydra.c -o out/hydra.mmod >/dev/null" || return
    $CC -c -O2 -DMOD_HOSTED -I"$_d/sdk" "$_d/hydra/hydra.c" -o "$_d/hydra.o" || { fail "outside HYDRA: the host object"; return; }
    build outside_hydra_test outside_hydra_test -O1 $FW "$_d/hydra.o" || return
    step "outside HYDRA: installed (the first free number, its author's), its presets from its file, plays (CPU1 queue too), its part memory by the patch, macros, its pages, SysEx, a project saved, loaded, on another device" \
        "$OUT/outside_hydra_test" "$_d/out/hydra.mmod" "$_d/mel"
}
outside_ppg() {                                   # an outside synth: Saadet's PPG (outside/ppg; its waves PPG's, from local images)
    "$PY" outside/ppg/gen_ppg_data.py >/dev/null 2>&1 || [ -f outside/ppg/ppg_rom.h ] ||
        { echo "== skip the outside PPG (no PPG Wave 2.2 images: outside/ppg/gen_ppg_data.py)"; return 0; }
    _d="$OUT/outside_ppg"
    rm -rf "$_d" && mkdir -p "$_d" && cp -R firmware/modules/sdk "$_d/sdk" && cp -R outside/ppg "$_d/ppg" ||
        { fail "outside PPG: the folder"; return; }
    printf '%s\n' 5aade7a5e6c1b3d0a2e7b6f4c9d8e1f0a3b2c5d4e7f6a9b8c1d0e3f2a5b4c7d6 > "$_d/author.key"   # (a test author)
    step "outside PPG: Saadet's synth built from the SDK folder alone (no Melodee tree; signed by a test author's key)" \
        sh -c "cd '$_d' && MELODEE_AUTHOR_KEY=author.key '$PY' sdk/mod_build.py ppg/ppg.c -o out/ppg.mmod >/dev/null" || return
    $CC -c -O2 -DMOD_HOSTED -I"$_d/sdk" "$_d/ppg/ppg.c" -o "$_d/ppg.o" || { fail "outside PPG: the host object"; return; }
    _smp=
    grep -q "PPG_HAVE_SAMPLE 1" outside/ppg/ppg_rom.h && _smp=-DPPG_TEST_SAMPLE
    build outside_ppg_test outside_ppg_test -O1 $FW $_smp "$_d/ppg.o" || return
    step "outside PPG: installed (the first free number), its presets from its file, plays, KEYB's voice dealing, its pages, a project saved, loaded, on another device" \
        "$OUT/outside_ppg_test" "$_d/out/ppg.mmod" "$_d/dir"
}
desc_json() { "$OUT/descdump" > "$OUT/desc.json"; }
mel_tests() {                                     # the .mel project file: the page's coding against the firmware's records
    build mel_fixture mel_fixture -DMELODEE_USB_AUDIO=1 $FW || return
    step ".mel project file: the firmware's records and kits for the page (tests/mel_fixture.c emit)" \
        "$OUT/mel_fixture" emit "$OUT/mel"
    if command -v node >/dev/null 2>&1; then
        step ".mel project file: layout == firmware, coder, refused files, kits renamed in every place, export / import" \
            node web/test_mel.mjs
        step ".mel project file: what the page's import sent goes in through the backup PUT, loads, its kits renamed" \
            "$OUT/mel_fixture" verify "$OUT/mel"
    else
        echo "== skip the .mel page test (no node)"
    fi
}
studio_bundle() {                                 # the firmware's data for Melodee Studio, its own dumpers in $OUT/studio
    rm -rf build/studio-bundle                    # (a failed build leaves none: nothing stale is verified)
    step "Studio bundle: tables, records, protocol, constants, menu, scales, palettes -> build/studio-bundle" \
        "$PY" tools/studio_bundle.py --work "$OUT/studio"
    [ -f build/studio-bundle/manifest.json ] &&
        step "Studio bundle: every file matches the manifest" "$PY" tools/studio_bundle.py --verify build/studio-bundle
    # the virtual FM-1 (web/core/build.sh makes the WebAssembly, emcc in Docker): its replies and its sound against
    # the same core built here from this tree, byte for byte (a WebAssembly core older than the tree shows here)
    if [ -f build/core/melodee_core.wasm ] && command -v node >/dev/null 2>&1; then
        _ver=$(sed -n 's/^#define MELODEE_VERSION "\([^"]*\)".*/\1/p' firmware/src/melodee.c | head -n 1)
        mkdir -p "$OUT/core_mods"                 # (the synths as modules: their objects beside the native core)
        for _s in sid drum cz fm6 prophet; do
            $CC -c -O2 -DMOD_IN_CORE=1 -Ifirmware/modules/sdk -Ifirmware/hal $FW firmware/modules/$_s/$_s.c -o "$OUT/core_mods/$_s.o" ||
                { fail "compile firmware/modules/$_s/$_s.c (core)"; return 1; }
        done
        $CC -O2 -std=gnu99 $FW -Itests -DMELODEE_RAM_AUDIO=1 -DMELODEE_VERSION="\"$_ver\"" -DCORE_MODULES=1 -DCORE_NATIVE_MAIN \
            -o "$OUT/core_native" web/core/melodee_core.c "$OUT"/core_mods/*.o -lm || { fail "compile web/core/melodee_core.c"; return 1; }
        step "virtual FM-1 core: Studio's commands, a render, taps, MIDI events, kits, the NOR kept, the five synths as WebAssembly modules (WASM == native)" \
            node web/core/core_check.mjs --native "$OUT/core_native" --mel "$OUT/studio/mel"
    else
        echo "== skip the virtual FM-1 core check (no build/core/melodee_core.wasm: web/core/build.sh; or no node)"
    fi
}
old_to_new() {                                    # a package of another app (this one cut short) -> this build
    head -c 200000 build/melodee.bin > "$OUT/old_app.bin" &&
        "$PY" tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null &&
        "$OUT/ldr_test" "$OUT/old.fwsc" build/melodee.fwsc
}
smk_packages() {                                  # SMK-37 layout packages of this build's binaries (BOARD_SMK37 tests)
    # (a resource region the SMK-37's 16 sectors hold: the FM-1's, cut short, its header remade with the window 9
    # (FL_RES_RESERVE, both boards: build.py's offset 12); NEW with an app
    # area tail from 0x93000 of 0xA000 - 100 pseudo-random bytes: firmware/src/smkx.c)
    "$PY" -c 'import struct,sys,zlib; d=open(sys.argv[1],"rb").read()[32:60032]; h=struct.pack("<7I",0x3153524D,len(d),zlib.crc32(d),9,0,0,0); open(sys.argv[2],"wb").write(h+struct.pack("<I",zlib.crc32(h))+d)' build/melodee-res.bin "$OUT/smk_res.bin" &&
    "$PY" -c "import random, sys; r = random.Random(37); sys.stdout.buffer.write(bytes(r.randrange(256) for _ in range(0xA000 - 100)))" \
        > "$OUT/smk_tail.bin" &&
        "$PY" tools/fm1pkg_make.py --board smk37 --res "$OUT/smk_res.bin" --tail "$OUT/smk_tail.bin" build/melodee.bin \
            build/loader/ota.bin "$OUT/smk_new.fwsc" >/dev/null &&
        head -c 200000 build/melodee.bin > "$OUT/smk_old_app.bin" &&
        "$PY" tools/fm1pkg_make.py --board smk37 "$OUT/smk_old_app.bin" build/loader/ota.bin "$OUT/smk_old.fwsc" >/dev/null
}
smk_tests() {                                     # the SMK-37 (SMK37.md): flash map and store, package, update entry, loader
    build ui_test_smk ui_test -DMELODEE_USB_AUDIO=1 -DBOARD_SMK37=1 $FW &&
        step "SMK-37 UI: the keys only play (SEQ held, PATTERNS, the dialog); the pads switch, arm, resize, copy, delete; lights" \
            "$OUT/ui_test_smk"
    build microtonal_test_smk microtonal_test -O1 -DMELODEE_USB_AUDIO=1 -DBOARD_SMK37=1 $FW &&
        step "SMK-37 microtonal MIDI OUT: nearest semitones, releases, shared semitones, the outer keys" \
            "$OUT/microtonal_test_smk"
    build smk_keys_test smk_keys_test &&
        step "SMK-37 velocity keybed: two contacts, bounces, strikes before a full lift, fast repeats, the outer keys' queue, pads, the factory curves, VELOCITY" \
            "$OUT/smk_keys_test"
    build smk_midi_out_test smk_midi_out_test &&
        step "SMK-37 TRS MIDI OUT: USB-MIDI packets to bytes, the ring, one byte a tick, OFF, no UART: no stall" \
            "$OUT/smk_midi_out_test"
    build smk_midi_track_test smk_midi_track_test -DMELODEE_USB_AUDIO=1 -DBOARD_SMK37=1 $FW &&
        step "SMK-37 MIDI Track at the TRS jack: no computer, USB gone or full: a note ends once there, USB's release still owed" \
            "$OUT/smk_midi_track_test"
    build smk_midi_track_ble_test smk_midi_track_test -DTRS_BLE=1 -DMELODEE_USB_AUDIO=1 -DBOARD_SMK37=1 -Itests/ble_fake $FW &&
        step "SMK-37 MIDI Track at the TRS jack, with BLE: BLE gone or full: a note ends once there, BLE's release still owed" \
            "$OUT/smk_midi_track_ble_test"
    build smk_flash_test smk_flash_test -DBOARD_SMK37=1 -Ifirmware/hal &&
        step "SMK-37 flash map and store: never the head, BTIF/USRTRIM, USR, key_mac; the stock marker only released" \
            "$OUT/smk_flash_test"
    smk_packages || { fail "SMK-37 packages (fm1pkg_make.py --board smk37)"; return; }
    step "SMK-37 package layout (fm1pkg_make --board smk37): PRCT 0x9D000, stock region table, the app area tail" \
        "$PY" tests/smk_pkg_test.py "$OUT/smk_new.fwsc" --tail "$OUT/smk_tail.bin"
    for m in 0 1 2; do                            # the pro, elite and mke builds of ota.c
        build ota_test_smk$m ota_test -DBOARD_SMK37=1 -DT_MODEL=$m &&
            step "M-UPGRADE entry, SMK-37 build $m (staging 0xBB000, the model's parm identity, the board check)" \
                "$OUT/ota_test_smk$m" "$OUT/smk_new.fwsc"
    done
    build ldr_test_smk ldr_test -DBOARD_SMK37=1 &&
        step "update loader, SMK-37: app area 0x4000-0x9CFFF (the tail before the marker), the store and the protected ranges untouched" \
            "$OUT/ldr_test_smk" "$OUT/smk_old.fwsc" "$OUT/smk_new.fwsc"
    build boot_stub_test_smk boot_stub_test -O2 -DBOARD_SMK37=1 &&
        step "boot stub, SMK-37 build: staged loader at 0xBB000, the model's record" "$OUT/boot_stub_test_smk"
    for f in build/melodee-smk37.fwsc build/melodee-smk37-elite.fwsc build/melodee-smk37-mke.fwsc; do
        [ -f "$f" ] || continue                   # the packages BOARD=smk37 MODEL=... ./build.sh made
        id="SMK-37 Pro_900"
        case $f in *-elite.fwsc) id="SMK-37 Elite_900" ;; *-mke.fwsc) id="MKE-P37_900" ;; esac
        step "SMK-37 package $f: identity $id, stock region table, the tail its app wants" "$PY" tests/smk_pkg_test.py "$f" "$id"
    done
}
powercut_tests() {
    "$PY" tests/boot_fixtures.py &&
        "$OUT/loader_powercut_test" build/host/boot-fixtures/legacy.fwsc build/melodee.fwsc \
        build/host/boot-fixtures/same-stub.fwsc build/host/boot-fixtures/other-legacy.fwsc \
        build/host/boot-fixtures/old-stub.fwsc
}
ui_renders() {
    build ui_render ui_render -O1 -w -DMELODEE_USB_AUDIO=1 -DMELODEE_USB_HOST=1 -Ibuild/gen -Ifirmware/src -Itests || return
    mkdir -p build/ui_new/ppm build/ui_slot
    step "UI renders: layout lint (every screen and palette, every page, engine and column value), MONO gray, draw cost" \
        "$OUT/ui_render" build/ui_new build/ui_slot
    build ui_render_whole ui_render -O1 -w -DMELODEE_USB_AUDIO=1 -DMELODEE_USB_HOST=1 -DMELODEE_CANVAS_ROWS=124 \
        -Ibuild/gen -Ifirmware/src -Itests || return
    # Fresh reference images avoid stale names from earlier versions; ui_new
    # also contains generated manual assets which this check need not remove.
    rm -rf build/ui_band_reference build/ui_band_slot
    mkdir -p build/ui_band_reference/ppm build/ui_band_slot
    step "UI canvas: whole-canvas reference" "$OUT/ui_render_whole" build/ui_band_reference build/ui_band_slot
    step "UI canvas: exact band replay pixels" "$PY" tests/canvas_bands_compare.py build/ui_new build/ui_band_reference
    step "UI canvas: exact rolling frames" "$PY" tests/canvas_bands_compare.py build/ui_slot build/ui_band_slot
    if [ $PIL = 1 ]; then
        "$PY" tests/ui_render.py build/ui_new build/ui_slot || fail "tests/ui_render.py"
        "$PY" tools/ui_mockcmp.py build/ui_new build/ui_mock || fail "tools/ui_mockcmp.py" # redesign parity (docs/design): a report, no gate yet
    fi
}
if [ "$UI" = renders ]; then
    ui_renders || true
    if [ -s "$LOG/$n.fail" ]; then
        echo "UI RENDERS FAILED"
        cat "$LOG/$n.fail"
        exit 1
    fi
    if [ ! -f build/ui_new/NIGHT/home.png ]; then
        echo "UI renders produced no NIGHT screenshots" >&2
        exit 1
    fi
    echo "UI RENDERS PASSED in $(($(date +%s) - start)) s"
    exit 0
fi
dsp_tracks() {
    build hostsim hostsim -O2 $FW || return
    step "DSP render (CZ-1 preset 0)" "$OUT/hostsim" 15 0 1 "$OUT/render.wav"
    mkdir -p build/tracks_demo
    step "TRACKS: 4-track pattern, live recording (lengths, swing), voice budget, engine switch, cost" \
        env TRACKS=build/tracks_demo "$OUT/hostsim" 15 0 1 "$OUT/tracks.wav"
}
fx_dormancy() {
    build fx_dormant_test fx_dormant_test -O2 $FW -UMELODEE_FX_DORMANT -DMELODEE_FX_DORMANT=1 || return
    step "Chorus dormancy: independent kernel PCM/state/ring parity and cost" "$OUT/fx_dormant_test"
    step "FX layout: frozen PCM/allocation trace across inserts, CHAIN/SPLIT and stereo changes" "$OUT/fx_dormant_test" --audio-trace
    build fx_dormant_reference fx_dormant_test -O2 $FW -UMELODEE_FX_DORMANT -DMELODEE_FX_DORMANT=0 || return
    "$OUT/fx_dormant_reference" --trace > "$OUT/fx-dormant-reference.trace" || fail "FX reference route trace"
    "$OUT/fx_dormant_test" --trace > "$OUT/fx-dormant-candidate.trace" || fail "FX candidate route trace"
    step "FX dormant/active: exact insert/CHAIN/SPLIT traces with all other effects, tails, edits and wakeups" \
        cmp "$OUT/fx-dormant-reference.trace" "$OUT/fx-dormant-candidate.trace"
}
prophet() {
    build prophet_test prophet_test -O2 $FW || return
    step "Prophet native frames, five voices, filters, sync and Poly-Mod" "$OUT/prophet_test"
    if [ -d "$P5" ]; then
        step "Prophet factory-file playback and exact round trips" "$OUT/prophet_test" \
            "$P5/P5_Factory_Programs_FACTORY_v1.03.syx" "$P5/P5_Factory_Programs_USER_v1.03.syx"
    fi
}

# ------------------------------------------------------------------ tests ---
# the longest first: they set the length of the run
sound && t "FX performance: all effects, four-track mixer, shared send bus and ordered three-effect chains" fx_performance -O2 $FW -- "$OUT/fx_performance" full
sound && t "FX: remaining inserts, smooth Tape, track ownership, dry bypass and retired-effects RAM reclamation" fx_test -O1 $FW
core && t "Retired Echo: reserved ID and reclaimed tanks; SLOOP Reverb and master punch controls" retired_fx_test -O1 $FW
sound && t "Master audio punch: 27 effects, history, freeze, transitions and key ownership" punch_master_test -O1 $FW
sound && t "SLOOP FX: compact stereo reverb, eight inserts, shared returns and controls" sloop_fx_test -O1 $FW
core && t "MIDI inserts: native release tails, timed note repeats, ownership, UI and persistence" midi_insert_test -O1 $FW
core && t "MIDI Track: USB notes, TRS routing, song/recording, cleanup, silent mixer and persistence" midi_track_test -O1 $FW
core && t "MIDI Track: host output, reconnect cleanup and USB input loss" midi_track_host_test:midi_track_test -O1 -DMELODEE_USB_HOST=1 $FW
core && t "EQ accelerator: register layout, isolated histories and bounded timeout" eq_hw_test
core && job web_tests
core && [ $TABLES = 1 ] && job mel_tests
core && [ $TABLES = 1 ] && job studio_bundle
sound && [ $TABLES = 1 ] && job dsp_tracks

if core; then
    t "shared undo RAM: eight levels, audio priority and exhaustion" undo_memory_test -O1 $FW
    t "flash storage (A/B, torn writes)" storage_test
    t "streamed banks: unpublished copies, page/header power cuts and readback" storage_stream_test
    t "streamed projects: exact codec bytes, dirty hashes, rename, legacy import and track removal" project_stream_test -O1 $FW
    t "native FM6/CZ user presets, scrolling, migration and interrupted saves" fm6_preset_test -O1 $FW
    t "Prophet native slots, atomic saves, projects, undo and performance" prophet_integration_test -O1 $FW
    t "resource lifetimes, cache eviction, tails and capacity" resources_test -O2 $FW
    t "aligned libc: copy/clear bounds, ordering and float aliasing" libc_word_test -O2 -fstrict-aliasing
    t "wire fields: byte order, cross-byte bounds and preserved surrounding bits" wire_bits_test -O2
    t "audio worker: stalled jobs and wrapping deadlines" audio_worker_test
    t "CPU1 fallback disabled: failures halt, healthy work stays dual-core" core1_fallback_test
    t "CPU1 stop: delayed bus errors, guard and lock restoration" core1_stop_test
    t "cache RAM startup, memory faults and safe fallback" cache_ram_test
    t "system clock: read only, never written" clock_test -O1
    t "stock V15 supply startup, timeouts and read-back faults" power_test -O1
    job step "nightly and synth-only site preserve release and editor" "$PY" tests/nightly_site_test.py
    run "audio diagnostics schema compatibility" "$PY" tests/usb_audio_stats_test.py
    t "audio profiling: histogram bounds and timer wrap" audio_profile_test
    t "batch profiling: per-core accounting and failed-worker drops" audio_batch_profile_test
    run "CPU baseline tool: bounded RAM restore and diagnostic decoding" "$PY" tests/audio_cpu_device_test.py
    run "Normal firmware stress: continuous ten-minute polling, faults, wrap and native driver stderr" "$PY" tests/audio_cpu_stress_test.py
    run "CPU USB capture: stereo/stems, actual frames and final driver errors" "$PY" tests/audio_cpu_usb_test.py
    run "CPU sequence runner: memory fixtures, external-clock timing and guarded restoration" "$PY" tests/audio_cpu_sequence_test.py
    run "CPU live takes: native replay comparison, timed input, failure cleanup and guarded restoration" "$PY" tests/audio_cpu_take_test.py
    run "target budget: natural loops and FPU decoding" "$PY" tests/target_budget_test.py
    t "synth identity: 2.0 handles, a synth not on this device lent a handle, its project and user preset kept" engine_retirement_test -O2 $FW
    job module_tests
    job outside_author
    job outside_j106
    job outside_hydra
    job outside_ppg
    t "identity: the engine map (UIDs, numbers given, kept), stores carrying their entries and patches, record owners, crash blame, signatures" \
        identity_test -O1 $FW
    t "capture gating, derived caches and silent-track clocks" performance_test -O2 $FW
    t "canvas drawing overlaps DMA without changing pending pixels" lcd_overlap_test -O1 -w -Ibuild/gen
    t "user presets (UP_PUT parser, bank round trip, versions, PHYS DRUM -> DRUM, grid records)" upreset_test
    t "keys and buttons: fast press, long release, bouncy contacts (one note each), glitches, encoders, fast spins" input_test
    t "LED hardware PWM: pin ownership, duty per scan, overlapping planes, blanking" led_pwm_test
    t "Panel fixed column words: full scan and GPIO/PWM fallback" led_pwm_fast:led_pwm_test -DMELODEE_PANEL_FAST=1
    t "Rejected panel brightness cache: exact masks/edits/fallback and sparse/changing costs" panel_prepare_bench -O2
    t "TRS MIDI parser" midi_uart_test
    t "USB audio: routing, clock drift and stream recovery" usb_audio_test
    t "USB audio: endpoint lifecycle and packet ownership" usb_audio_driver_test
    t "USB MIDI host: duplex I/O, enumeration, ACK ownership, back-pressure and recovery" usb_host_test -O1 -fsanitize=address,undefined
    t "USB MIDI device: the clock ahead of notes, a SysEx never split" usb_host_ota:usb_host_test -DMELODEE_OTA=1 -O1 -fsanitize=address,undefined
    run "USB descriptors: MIDI, CDC and audio configurations" "$PY" tests/usb_audio_desc_test.py
    run "boot audio: packed image, decoder bounds, malformed streams and XIP dependencies" "$PY" tests/boot_audio_test.py
    run "BLE MIDI: protocol, encryption, pairing, radio driver and Melodee integration" "$PY" tests/ble_tests.py
    run "BLE HCI size probe: shared events preserved and SDK drift rejected" "$PY" tests/ble_hci_probe_test.py
    run "size attributes: preserve hot loops and outline foreground transactions" "$PY" tests/size_fns_test.py
    run "stack budgets: callbacks, re-entry, nested IRQs and overflow rejection" "$PY" tests/stack_budget_test.py
    run "resident audio: call graph and flash/RAM ABI boundaries" "$PY" tests/audio_ram_test.py
    run "performance defaults: local/compiler agreement and explicit reference overrides" "$PY" tests/performance_defaults_test.py
    t "removed effects: old songs and motion, MIDI, physical input and dry output" no_effects_test -O1 $FW
    t "resident audio: four-track resource budget and removed buses" ram_audio_test -O2 $FW
    t "resident/cache undo: validated eight levels and bounded fallback" ui_resident_cache:ui_test -DMELODEE_RAM_AUDIO=1 -DMELODEE_CACHE_RAM=1 $FW
    t "resident audio: expanded voice budget and CPU1 fallback" ram_voice_budget_test -O2 $FW
    t "audio: calibrated reservations, release tails and overlapping hits" audio_admission_test -O2 $FW
    t "USB audio: four isolated track stems through the real mixer" usb_audio_tracks_test -O2 $FW
    t "boot stub: CRCs, rescue keys, damaged staging and retry fallback" boot_stub_test -O2
    run "boot packages: constant entry sector and valid directory CRCs" "$PY" tests/boot_stub_pkg_test.py
    t "power cuts: every erase/page, resume and unchanged boot sector" loader_powercut_test -O2 -- powercut_tests
    t "M-UPGRADE entry (own loader and pinned boot installer)" ota_test -DOWN_PKG=1 -- "$OUT/ota_test" build/melodee.fwsc web/m-bootloader/fm1-uboot.fwsc
    t "installer crash report (read only)" installer_report_test
    t "update loader: other app -> this build" ldr_test -- old_to_new
    run "installer CLI (fm1_install.py) against a simulated FM-1" "$PY" tests/install_test.py
    run "synth modules after an update (tools/fm1_modules.py) against a simulated FM-1" "$PY" tests/fm1_modules_test.py
    job smk_tests
    run "ROM USB recovery: backup, protected regions, interrupted writes and transport failures" "$PY" tests/recovery_test.py
    run "recovery diagnostics: legacy and boot-protected retained RAM" "$PY" tests/retained_ram_test.py
    run "release assets: firmware identity, application bytes, native provenance and ZIP integrity" "$PY" tests/release_assets_test.py
    run "recovery correction: preserve signed firmware and verify public replacement assets" "$PY" tests/recovery_correction_test.py
fi

if core && [ $TABLES = 1 ]; then
    t "scales: white-key mapping and note lifecycle" scale_test -O2 $FW
    t "microtonal: catalogue, layouts, note ownership, recordings, rendered pitch and MIDI OUT" microtonal_test -O2 $FW
    t "scale picker: families, favorites, empty lists, shared settings and navigation" scale_picker_test -O1 $FW
    t "scales and tunings: the maqam case, old entries as they were, stored data, SCL's Tuning row" scale_tuning_test -O2 $FW
    t "chord keys: diatonic and fixed chords, voicings, MONO root, releases, recording, ARP, MIDI IN, kits" chord_test $FW
    t "AUTO chords: Western scales, melody, cadences, voice leading and frozen ownership" chord_auto_test $FW
    t "AUTO2: stable melody, harmony below, Western scales and immediate strum attack" chord_melody_test $FW
    t "project record (FUN16 round trip, the song chain, names, FM6 patches; older formats refused)" project_test $FW
    t "motion, whole-step chance, an older parameter count, song restore and ARP repeat" motion_test -O1 $FW
    t "64 pattern banks: chords, ties, independent loop switching, copy and persistence" pattern_test \
        -O1 -DMELODEE_USB_AUDIO=1 $FW
    t "song: 64 sections, rests, cursor playback, grid edits, phrases and undo" song_mode_test -O1 $FW
    t "song/pattern presets: priority, browser restoration, playback, undo and persistence" song_preset_test -O1 $FW
    t "song punch: timed master FX, repeats, live overrides, edits and persistence" song_punch_test -O1 $FW
    t "compressed pattern pool: dense capacity, prepared switches and state-machine oracle" pattern_pool_test -O1 $FW
    t "16 x 128 shared pool: old dense capacity, atomic exhaustion and switching" pattern_capacity_test -O1 $FW
    t "recording/performance tools and legacy migrations" performance_tools_test -O1 $FW
    t "momentary snapshots: parameter IDs and native patch union restore exactly" ui_momentary_test -O1 $FW
    t "USB/TRS clock, bend, sustain, ownership and panic recovery" midi_control_test -O1 $FW
    t "CLK INT as MIDI master: clock, Start and Stop on the output queues, locked to the steps" midi_clock_out_test -O1 $FW
    t "MIDI panel knobs: profiles, native edits, encoder modes and bounded foreground dispatch" midi_knob_test -O1 $FW
    t "synth schemas: field mappings, roles, patch blobs, Motion and learned knobs by id, mutations caught" schema_test \
        -O1 $FW
    t "MIDI timing: quantized TRS/USB recording, audio timeline, tempo changes and timer wrap" midi_timing_test -O1 $FW
    t "MIDI recording: raw events, quantization at take end, independent releases, banks and capacity" recording_test \
        -O1 $FW
    t "Recorded-note layout: legacy bytes, 64 owners and 128 steps without event growth" recorded_note_test -O2 $FW
    t "16 x 128 recording core: positions, release queues, copies and pitch cache" recording_capacity_test -O1 -DMELODEE_RECORD_INDEX=1 $FW
    t "Recording scheduler: reference-state parity, cache invalidation and live lists" recording_scheduler_test -O1 $FW
    t "Recording RAM: shared capture/deadline counters and mode handoffs" recording_runtime_test -O1 $FW
    run "Recording storage codec probe: lossless roundtrip and malformed data" "$PY" tools/recording_storage_probe.py
    t "Recording ownership: independent scanner, edits, duplicates, sparse/dense queries and rebuild cost" recording_index_test -O2 $FW
    t "Timestamped releases: rollover, retrigger, 1024 cancellations, edits and track isolation" recording_queue_test -O1 $FW
    t "Recording scheduler oracle against the countdown cursor" recording_scheduler_countdown:recording_scheduler_test -O1 $FW -DMELODEE_RECORD_QUEUE=0
    t "Recording scheduler oracle against the original scanner" recording_scheduler_reference:recording_scheduler_test -O1 $FW -DMELODEE_RECORD_CURSOR=0
    t "Recording undo: whole takes, motion, early stops, redo, isolation and memory" recording_undo_test -O1 $FW
    t "Recorded notes: individual selection/deletion, zoom, undo, playback ownership and persistence" seq_notes_test \
        -O1 $FW
    t "MIDI IN through the scale layouts (WHITE, ALL, MPC), shared SCL / QNT" midi_scale_test -O1 $FW
    t "MPC bank octaves: controls, held notes, persistence and compatibility" mpc_octave_test -O1 $FW
    t "Concert pitch: A4 frequency, engines, modulation, held notes and project-independent settings" tuning_test \
        -O1 -DMELODEE_USB_AUDIO=1 $FW
    t "themes: contrast, text blending and font metrics" theme_test -O1 $FW
    t "text: pens, kerning and pixels equal the reference renderer" text_ref_test -O1 $FW
    if [ $PIL = 1 ]; then
        run "text spacing: glyph gaps against the font's own (16x supersampled), S M <= 0.25 px, L <= 0.5 px" \
            "$PY" tests/text_spacing_test.py build/gen/ui_fonts.h
    fi
    t "settings: PER5 records, palette ids and preference preservation" settings_test -O1 -w -Ibuild/gen
    t "UI: sounds keep steps, undo, recording, MIDI overflow, pending saves, panel recovery, drum grid, song chain, MONO gray" \
        ui_test -DMELODEE_USB_AUDIO=1 $FW
    t "Prophet: all native tone knobs through the panel and sound engine" prophet_knob_test -O2 -DMELODEE_USB_AUDIO=1 $FW
    t "Sound designer: native recipes, dependencies, compare, discard and save" designer_test -O2 -DMELODEE_USB_AUDIO=1 $FW
    t "Sound designer from the synths' blocks: every answer as a894c6f7's designer gave it" designer_parity_test -O1 \
        -DMELODEE_USB_AUDIO=1 $FW
    t "Sequencer: tied length/move/delete, eight-level undo/redo, MIDI step entry and playing-step recording" seq_edit_test \
        -DMELODEE_USB_AUDIO=1 $FW
    t "Note/library gestures: quarter snapping, range moves/copies, undo and bulk preset erase" note_tools_test \
        -O1 -DMELODEE_USB_AUDIO=1 $FW
    job ui_renders
    t "boot audio: codec waits for settled silent DMA and shuts down before I2S" audio_boot_test -w
    t "audio: overload protection, bounded fades and DMA diagnostics" audio_test $FW
    t "GLO > WEIGHTS: per-engine budget points, SAFETY, device persistence and pages" voice_weights_test $FW
    t "audio: silent bench keeps synthesis running and silences physical output" audio_silent_test:audio_test \
        -DMELODEE_BENCH_SILENT=1 $FW
    t "384 shared presets: all-engine capacity, conversion, torn writes and high-slot identity" unified_presets_test $FW
    t "persistence: deferred settings, retry and failed-save rollback" persistence_test $FW
    t "flash JEDEC gate: accepted parts (Puya, UCUN) enable writes, unknown IDs stay RAM-only" flash_id_test $FW
    t "first boot: foreign settings, safe defaults, ownership and interrupted cleanup" settings_origin_test $FW
    t "first boot: reset unsupported preset banks, preserve good entries and retry cuts" preset_boot_reset_test $FW
    t "resource region: the installed header's window (none or torn: the floor)" res_boot_test $FW
    t "storage safety: protect unreadable records until confirmed replacement" storage_safety_test $FW
    t "full backup: CRC before writes, stale runtime, USB reset / timeout, malformed objects, older projects" backup_test $FW
    t "CPU laboratory: persistence guard and RAM-only preference restoration" backup_lab:backup_test -DMELODEE_AUDIO_PROFILE=1 $FW
    t "editor: real C protocol, malformed transfers and queue recovery" editor_test $FW
    t "synths' SysEx in and out as 88f4eb02 had them: DX7 banks, voices, functions, CZ-1 tones, Prophet programs, sends (tests/fixtures/sysex-88f4eb02.txt)" \
        sysex_parity_test $FW
    t "editor over Bluetooth (MELODEE_BLE_EDITOR, two links): SysEx across BLE packets, the command list, replies on the asking link" \
        editor_ble:editor_test -DMELODEE_BLE=1 -DMELODEE_BLE_EDITOR=1 -DBLE_LINKS=2 -Itests/ble_fake $FW
    t "editor over Bluetooth (MELODEE_BLE_EDITOR, one link)" editor_ble1:editor_test -DMELODEE_BLE=1 -DMELODEE_BLE_EDITOR=1 -Itests/ble_fake $FW
    t "CPU lab controls: native recording/transport, guarded protocol and exact restoration" audio_cpu_lab_test -O2 -DMELODEE_AUDIO_PROFILE=1 -DMELODEE_AUDIO_BENCH=1 $FW
    t "CPU lab controls absent in normal firmware" cpu_lab_off:audio_cpu_lab_test -O2 $FW
    t "CPU lab controls absent with profiling only" cpu_lab_profile:audio_cpu_lab_test -O2 -DMELODEE_AUDIO_PROFILE=1 $FW
    t "CPU lab controls absent with benchmark only" cpu_lab_bench:audio_cpu_lab_test -O2 -DMELODEE_AUDIO_BENCH=1 $FW
    t "DX7 librarian: USB voice/bank receive, dump replies and malformed frame recovery" fm6_sysex_test $FW
    t "SWING: track + global at most 100, sequencer step lengths, the SWG display" swing_test -O2 $FW
    t "GLO double-tap lock, SCL tap navigation, SCREEN OFF, LFO 2 SYNC / TRIG / POL" layer_screen_test \
        -O1 -DMELODEE_USB_AUDIO=1 $FW
    t "DRUM EDITOR: synthesized 909, sound controls, individual pitch/length, undo and storage" drum_editor_test -O2 $FW
    t "pads: DRUM's pads block read as any synth's == 70111e91's drum_map.c (every KIT), flags, records, the KIT packing, mutations caught" \
        pads_test $FW
    run "no synth code in firmware/src: every hit of a synth's name, number or symbol under a rule with its reason (tests/no_synth_code.py)" \
        "$PY" tests/no_synth_code.py
fi

if sound && [ $TABLES = 1 ]; then
    mkdir -p build/mod_demo \
        build/drum_demo build/fm6_demo
    t "regression: golden renders, health, voices, CPU budget" regress -O2 $FW -- \
        "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
    t "paired FM6/Prophet voices: serial/concurrent sample equality and eight-voice allocation" dual_core_test \
        -O2 $FW -pthread
    t "audio: 1296 ordered engine assignments, releases, controls and concurrent PCM parity" audio_matrix_test -O2 $FW -pthread
    t "CZ-1: every knob changes a full tone; the dim ones leave INIT and factory tones bit for bit" cz1_knob_test -O2 $FW
    t "native fp32: independent numerical references, exact pitch, quiet signals and fractional envelopes" synth_fpu_test \
        -O2 $FW
    job prophet
    t "modulation matrix: off = bit-identical, the math, MIDI CC1 / CC11 / aftertouch, cost, demos" mod_test \
        -O2 $FW -- "$OUT/mod_test" build/mod_demo
    t "matrix S&H / SLEW / DEPTH, SPREAD: off = bit-identical, stereo sides and routing" mod_spread_test -O2 $FW
    t "FM6 ROM symmetry: exact original operators and envelope wrap" fm6_table_test -O2 $FW
    t "synth kernels: exact Prophet/FM6 PCM and state, all modes and live edits" synth_kernel_test -O2 $FW
    t "synth kernels at 2x: exact Prophet PCM and state" synth_kernel_os2:synth_kernel_test -O2 -DP5_OVERSAMPLE=2 $FW -- "$OUT/synth_kernel_os2" parity
    t "modulation routing cache: independent reference parity and comparison cost" mod_route_test -O2 $FW
    t "CPU playback fixtures: exact full-bank serialization across densities/timing/engines" audio_cpu_fixture -O2 $FW
    t "CPU live takes: REC/overdub schedules, native simulation, jitter bounds, FBKK decoding" audio_cpu_take -O2 $FW
    t "capture allocation: full-state parity with the original scan, path coverage and cost" capture_alloc_test -O2 $FW
    t "capture allocation with the overview pitch index" capture_alloc_index:capture_alloc_test -O2 \
        -UMELODEE_RECORD_INDEX -DMELODEE_RECORD_INDEX=1 $FW -- "$OUT/capture_alloc_index" 400 nocost
    t "routing rebuild probes: full-state parity and rejected steady/changing costs" mod_churn_bench -O2 $FW
    t "SID filter output: exact full state across models/waves/stacks/modes and costs" sid_filter_test -O2 $FW
    t "808/909 metal preparation: exact pitches, states and steady/changing costs" drum_prepare_test -O2 $FW
    t "SID drum kernel: original PCM/state through digi, controls, steals and releases" drum_sid_fast_test -O2 $FW
    t "CZ native window: exact phase kernel and full renderer through live edits" cz_window_test -O2 $FW
    t "FM6 zero-level preparation: exact state and static/changing costs" fm6_prepare_test -O2 $FW
    t "Prophet zero-wheel: exact shared state and static/changing cost" prophet_fast_test -O2 $FW
    t "Prophet zero-wheel at 2x: exact shared state and static/changing cost" prophet_fast_os2:prophet_fast_test -O2 -DP5_OVERSAMPLE=2 $FW
    t "centered bend fast path: full raw/range oracle and control cost" control_prepare_test -O2 $FW
    job fx_dormancy
    t "mono mixer fast path: output/send/peak/stem reference parity and cost" mix_fast_test -O2 $FW
    t "master output: final volume, held notes/punch tails, exact ramps and dry-pass cost" master_output_test -O2 $FW
    t "CZ: oscillator boundaries, native rate/target envelopes and independent lines" phase_test \
        -O2 $FW
    t "SID: oscillator/LFSR/register semantics, revisions and 32 factory presets" sid_test -O2 $FW
    t "DRUM: 808 instruments, velocity, hat choke, release, eight lanes and shared voice budget" drum_test -O2 $FW -- \
        "$OUT/drum_test" build/drum_demo
    t "DRUM SID: one 6581 on one voice, frame programs, DIGI on \$D418, filter routing, levels" drum_sid_test -O2 $FW -- \
        "$OUT/drum_sid_test" build/sid_kit_demo
    t "DRUM CR78/FM: eight lanes on one voice, retriggers, choke, length, knobs, levels, corners" drum_kits_test -O2 $FW -- \
        "$OUT/drum_kits_test" build/drum_kits_demo
    t "USER KITS: pool record == one RAM block, sector edges, empty/rewritten/deleted silent, refused records, boot scan" \
        user_kit_test -O2 $FW
    t "RECORD KIT: USB take trigger/end, device encoder == tools/drum_codec.c, 22/44 kHz, cymbal loop, 11 pads fit, clear" \
        kit_take_test -O2 -DMELODEE_USB_AUDIO=1 $FW -- "$OUT/kit_take_test" build/kit_take_demo
    t "FM6: the DX7's algorithms, pitch, levels, envelopes, modulation; DC, clipping, macros, patches, voices, cost, demos" \
        fm6_test -O2 $FW -- "$OUT/fm6_test" build/fm6_demo
    t "FM6: native fp32 AMS against the double reference, every modulation" fm6_ams_test -O2 $FW
    if [ -n "${DEXED_SRC:-}" ]; then
        run "FM6 vs Dexed: informational fp32 render comparison (DEXED_SRC)" sh tests/fm6_parity.sh --quick --diagnostic
    else
        note "== FM6 vs Dexed: skipped (DEXED_SRC=<dexed>/Source to run tests/fm6_parity.sh)"
    fi
fi
if sound; then
    run "regression: target cost of the render loops (pi32v2 disassembly)" "$PY" tests/target_budget.py \
        build/melodee.dis tests/target_budget.txt
fi
[ $TABLES = 1 ] || note "== skip hostsim (run ./build.sh once)"

# every slot back: every job ended
i=0; while [ $i -lt "$PARALLEL" ]; do read -r slot <&3; flush; i=$((i + 1)); done
wait
flush
exec 3>&-
secs=$(($(date +%s) - start))
case $SOUND in
    1) what="ALL HOST TESTS" ;;
    0) what="HOST TESTS (sound tests skipped: SOUND=0)" ;;
    only) what="SOUND TESTS (SOUND=only)" ;;
esac
if [ -s "$LOG/failed" ]; then
    echo "$what FAILED in $secs s:"
    sed 's/^/  /' "$LOG/failed"
    exit 1
fi
echo "$what PASSED in $secs s"
