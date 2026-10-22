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
# LAYERS / SCREEN (tests/layer_screen_test.c): GLO/SCL double-tapped layers, SCREEN OFF, LFO 2.
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
# PHYS (tests/phys_test.c): stability over the whole parameter and pitch range, the worst-case cost against
#                   the heaviest factory preset, demos in build/phys_demo/; tests/phys_ref.cpp compares the
#                   fixed-point models with DaisySP's float originals when DaisySP is there (DAISYSP=path).
# DRUM (tests/drum_test.c): the drum voices (src/drum_voice.c): pitch, decay, centroid and level against
#                   Melodee's targets, the controls' directions, no clipping, DC, retriggers, the hat choke, the
#                   kick on a small speaker; the DRUM engine (src/eng_drum.c): its key map, the 8 lanes together,
#                   one hit per lane, the choke between lanes; the cost per voice; demos in build/drum_demo/.
# DRUM SID (tests/drum_sid_test.c): KIT SID (src/drum_sid.c): one 6581 on one voice, its three voices taken by
#                   the hits, frame programs at PAL frames, DIGI on $D418 and its effect on the voices ringing,
#                   the filter for the routed drums, levels next to the 808; demos in build/sid_kit_demo/.
# NOISE (tests/noise_test.c): the engine (src/eng_noise.c): COLR's slope (white, pink, brown), the filter and the
#                   register clock following the key, META periodic at the key, no DC, no clipping at the
#                   corners, a note from silence the same twice, the cost per voice; demos in build/noise_demo/.
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
    python3 -c "import PIL" 2>/dev/null || { echo "UI renders need Pillow (python3 -m pip install Pillow)" >&2; exit 1; }
fi
[ "$PARALLEL" -ge 1 ] 2>/dev/null || { echo "PARALLEL=n, n >= 1"; exit 2; }
python3 tools/gen_scales.py --check || exit 1
python3 tools/audio_contract.py --check || exit 1
if [ "$UI" = renders ]; then
    [ -f build/gen/melodee_tables.h ] || { echo "run ./build.sh first"; exit 1; }
else
    [ -f build/melodee.fwsc ] || { echo "run ./build.sh first"; exit 1; }
fi
TABLES=0; [ -f build/gen/melodee_tables.h ] && TABLES=1
PIL=0; python3 -c "import PIL" 2>/dev/null && PIL=1
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
        step "web pages: editor protocol + native CZ patches, package builder, update protocol" node web/test_web.mjs
        step "installer UI: release selection, fetch races, cache and write lock" node web/test_installer.mjs
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
desc_json() { "$OUT/descdump" > "$OUT/desc.json"; }
old_to_new() {                                    # a package of another app (this one cut short) -> this build
    head -c 200000 build/melodee.bin > "$OUT/old_app.bin" &&
        python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null &&
        "$OUT/ldr_test" "$OUT/old.fwsc" build/melodee.fwsc
}
smk_packages() {                                  # SMK-37 layout packages of this build's binaries (BOARD_SMK37 tests)
    python3 tools/fm1pkg_make.py --board smk37 build/melodee.bin build/loader/ota.bin "$OUT/smk_new.fwsc" >/dev/null &&
        head -c 200000 build/melodee.bin > "$OUT/smk_old_app.bin" &&
        python3 tools/fm1pkg_make.py --board smk37 "$OUT/smk_old_app.bin" build/loader/ota.bin "$OUT/smk_old.fwsc" >/dev/null
}
smk_tests() {                                     # the SMK-37 (SMK37.md): flash map and store, package, update entry, loader
    build smk_midi_out_test smk_midi_out_test &&
        step "SMK-37 TRS MIDI OUT: USB-MIDI packets to bytes, the ring, one byte a tick, OFF, no UART: no stall" \
            "$OUT/smk_midi_out_test"
    build smk_flash_test smk_flash_test -DBOARD_SMK37=1 -Ifirmware/hal &&
        step "SMK-37 flash map and store: never the head, BTIF/USRTRIM, USR, key_mac; the stock marker only released" \
            "$OUT/smk_flash_test"
    smk_packages || { fail "SMK-37 packages (fm1pkg_make.py --board smk37)"; return; }
    step "SMK-37 package layout (fm1pkg_make --board smk37): PRCT 0x9D000, stock region table" \
        python3 tests/smk_pkg_test.py "$OUT/smk_new.fwsc"
    for m in 0 1 2; do                            # the pro, elite and mke builds of ota.c
        build ota_test_smk$m ota_test -DBOARD_SMK37=1 -DT_MODEL=$m &&
            step "M-UPGRADE entry, SMK-37 build $m (staging 0xBB000, the model's parm identity, the board check)" \
                "$OUT/ota_test_smk$m" "$OUT/smk_new.fwsc"
    done
    build ldr_test_smk ldr_test -DBOARD_SMK37=1 &&
        step "update loader, SMK-37: app area 0x4000-0x9CFFF, the store and the protected ranges untouched" \
            "$OUT/ldr_test_smk" "$OUT/smk_old.fwsc" "$OUT/smk_new.fwsc"
    build boot_stub_test_smk boot_stub_test -O2 -DBOARD_SMK37=1 &&
        step "boot stub, SMK-37 build: staged loader at 0xBB000, the model's record" "$OUT/boot_stub_test_smk"
    for f in build/melodee-smk37.fwsc build/melodee-smk37-elite.fwsc build/melodee-smk37-mke.fwsc; do
        [ -f "$f" ] || continue                   # the packages BOARD=smk37 MODEL=... ./build.sh made
        id="SMK-37 Pro_900"
        case $f in *-elite.fwsc) id="SMK-37 Elite_900" ;; *-mke.fwsc) id="MKE-P37_900" ;; esac
        step "SMK-37 package $f: identity $id, stock region table" python3 tests/smk_pkg_test.py "$f" "$id"
    done
}
powercut_tests() {
    python3 tests/boot_fixtures.py &&
        "$OUT/loader_powercut_test" build/host/boot-fixtures/legacy.fwsc build/melodee.fwsc \
        build/host/boot-fixtures/same-stub.fwsc build/host/boot-fixtures/other-legacy.fwsc \
        build/host/boot-fixtures/old-stub.fwsc
}
ui_renders() {
    build ui_render ui_render -O1 -w -DMELODEE_USB_AUDIO=1 -Ibuild/gen -Ifirmware/src -Itests || return
    mkdir -p build/ui_new/ppm build/ui_slot
    step "UI renders: layout lint (every screen and palette, every page, engine and column value), MONO gray, draw cost" \
        "$OUT/ui_render" build/ui_new build/ui_slot
    if [ $PIL = 1 ]; then
        python3 tests/ui_render.py build/ui_new build/ui_slot || fail "tests/ui_render.py"
        python3 tools/ui_mockcmp.py build/ui_new build/ui_mock || fail "tools/ui_mockcmp.py" # redesign parity (docs/design): a report, no gate yet
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
phys_ref() {
    D=${DAISYSP:-vendor/DaisySP}/Source
    if [ ! -d "$D/PhysicalModeling" ] || ! command -v c++ >/dev/null 2>&1; then
        echo "== skip PHYS reference test (no DaisySP: set DAISYSP to a checkout)"
        return
    fi
    $CC -DMELODEE_LEGACY_EXTRAS=1 -O2 $FW -Itests -c -o "$OUT/phys_fixed.o" tests/phys_fixed.c &&
        c++ -O2 -std=c++14 -w -I"$D" -I"$D/Utility" -o "$OUT/phys_ref" tests/phys_ref.cpp \
            "$D/PhysicalModeling/modalvoice.cpp" "$D/PhysicalModeling/resonator.cpp" "$D/PhysicalModeling/stringvoice.cpp" \
            "$D/PhysicalModeling/KarplusString.cpp" "$D/Filters/svf.cpp" "$D/Utility/dcblock.cpp" \
            "$D/Dynamics/crossfade.cpp" "$OUT/phys_fixed.o" || { fail "compile tests/phys_ref.cpp"; return; }
    step "PHYS: the fixed-point models against DaisySP's float originals (mode frequencies, decays, Svf)" "$OUT/phys_ref"
}

# ------------------------------------------------------------------ tests ---
# the longest first: they set the length of the run
sound && t "FX performance: all effects, four-track mixer, shared send bus and ordered three-effect chains" fx_performance -O2 $FW -- "$OUT/fx_performance" full
sound && t "FX: three-slot chains, reverb decay/density/rate conversion, smooth Tape, track ownership, dry bypass and frozen legacy storage" fx_test -O1 $FW
core && t "EQ accelerator: register layout, isolated histories and bounded timeout" eq_hw_test
core && job web_tests
sound && [ $TABLES = 1 ] && job dsp_tracks

if core; then
    t "flash storage (A/B, torn writes)" storage_test
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
    job step "nightly and synth-only site preserve release and editor" python3 tests/nightly_site_test.py
    run "audio diagnostics schema compatibility" python3 tests/usb_audio_stats_test.py
    t "audio profiling: histogram bounds and timer wrap" audio_profile_test
    t "batch profiling: per-core accounting and failed-worker drops" audio_batch_profile_test
    run "CPU baseline tool: bounded RAM restore and diagnostic decoding" python3 tests/audio_cpu_device_test.py
    run "Normal firmware stress: continuous ten-minute polling, faults, wrap and native driver stderr" python3 tests/audio_cpu_stress_test.py
    run "CPU USB capture: stereo/stems, actual frames and final driver errors" python3 tests/audio_cpu_usb_test.py
    run "CPU sequence runner: memory fixtures, external-clock timing and guarded restoration" python3 tests/audio_cpu_sequence_test.py
    run "CPU live takes: native replay comparison, timed input, failure cleanup and guarded restoration" python3 tests/audio_cpu_take_test.py
    run "target budget: natural loops and FPU decoding" python3 tests/target_budget_test.py
    t "retired synth identities, project/user patch preservation and favorites" engine_retirement_test -O2 $FW
    t "capture gating, derived caches and silent-track clocks" performance_test -DMELODEE_LEGACY_EXTRAS=1 -O2 $FW
    t "canvas drawing overlaps DMA without changing pending pixels" lcd_overlap_test -O1 -w -Ibuild/gen
    t "user presets (UP_PUT parser, bank round trip, versions, PHYS DRUM -> DRUM, grid records)" upreset_test
    t "keys and buttons: fast press, long release, bouncy contacts (one note each), glitches, encoders, fast spins" input_test
    t "LED hardware PWM: pin ownership, duty per scan, overlapping planes, blanking" led_pwm_test
    t "Panel fixed column words: full scan and GPIO/PWM fallback" led_pwm_fast:led_pwm_test -DMELODEE_PANEL_FAST=1
    t "Rejected panel brightness cache: exact masks/edits/fallback and sparse/changing costs" panel_prepare_bench -O2
    t "TRS MIDI parser" midi_uart_test
    t "USB audio: routing, clock drift and stream recovery" usb_audio_test
    t "USB audio: endpoint lifecycle and packet ownership" usb_audio_driver_test
    run "USB descriptors: MIDI, CDC and audio configurations" python3 tests/usb_audio_desc_test.py
    run "resident audio: call graph and flash/RAM ABI boundaries" python3 tests/audio_ram_test.py
    t "removed effects: old songs and motion, MIDI, physical input and dry output" no_effects_test -O1 $FW
    t "resident audio: four-track resource budget and removed buses" ram_audio_test -O2 $FW
    t "resident/cache undo: validated eight levels and bounded fallback" ui_resident_cache:ui_test -DMELODEE_RAM_AUDIO=1 -DMELODEE_CACHE_RAM=1 $FW
    t "resident audio: expanded voice budget and CPU1 fallback" ram_voice_budget_test -O2 $FW
    t "audio: calibrated reservations, release tails and overlapping hits" audio_admission_test -O2 $FW
    t "USB audio: four isolated track stems through the real mixer" usb_audio_tracks_test -O2 $FW
    t "boot stub: CRCs, rescue keys, damaged staging and retry fallback" boot_stub_test -O2
    run "boot packages: constant entry sector and valid directory CRCs" python3 tests/boot_stub_pkg_test.py
    t "power cuts: every erase/page, resume and unchanged boot sector" loader_powercut_test -O2 -- powercut_tests
    if [ -f FM-1_v15.fwsc ]; then
        t "M-UPGRADE entry (Melodee and official V15 loaders)" ota_test -- "$OUT/ota_test" build/melodee.fwsc FM-1_v15.fwsc
    else
        t "M-UPGRADE entry (own loader)" ota_test -- "$OUT/ota_test" build/melodee.fwsc
    fi
    t "update loader: other app -> this build" ldr_test -- old_to_new
    run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py
    job smk_tests
    run "ROM USB recovery: backup, protected regions, interrupted writes and transport failures" python3 tests/recovery_test.py
    run "recovery diagnostics: legacy and boot-protected retained RAM" python3 tests/retained_ram_test.py
    run "release assets: firmware identity, application bytes, native provenance and ZIP integrity" python3 tests/release_assets_test.py
    run "recovery correction: preserve signed firmware and verify public replacement assets" python3 tests/recovery_correction_test.py
fi

if core && [ $TABLES = 1 ]; then
    t "scales: white-key mapping and note lifecycle" scale_test -O2 $FW
    t "microtonal: catalogue, layouts, note ownership, recordings and rendered pitch" microtonal_test -O2 $FW
    t "scale picker: families, favorites, empty lists, shared settings and navigation" scale_picker_test -O1 $FW
    t "chord keys: diatonic and fixed chords, voicings, MONO root, releases, recording, ARP, MIDI IN, kits" chord_test $FW
    t "project record (FUN16 round trip, the song chain, names, FM6 patches; older formats refused)" project_test $FW
    t "motion, whole-step chance, an older parameter count, song restore and ARP repeat" motion_test -O1 $FW
    t "32 pattern banks: chords, ties, independent loop switching, copy and persistence" pattern_test \
        -O1 -DMELODEE_USB_AUDIO=1 $FW
    t "compressed pattern pool: dense capacity, prepared switches and state-machine oracle" pattern_pool_test -O1 $FW
    t "recording/performance tools and legacy migrations" performance_tools_test -O1 $FW
    t "USB/TRS clock, bend, sustain, ownership and panic recovery" midi_control_test -O1 $FW
    t "MIDI timing: quantized TRS/USB recording, audio timeline, tempo changes and timer wrap" midi_timing_test -O1 $FW
    t "MIDI recording: raw events, quantization at take end, independent releases, banks and capacity" recording_test \
        -O1 $FW
    t "Recording scheduler: reference-state parity, cache invalidation and live lists" recording_scheduler_test -O1 $FW
    run "Recording storage codec probe: lossless roundtrip and malformed data" python3 tools/recording_storage_probe.py
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
        -DMELODEE_LEGACY_EXTRAS=1 -O1 -DMELODEE_USB_AUDIO=1 $FW
    t "themes: contrast, text blending and font metrics" theme_test -O1 $FW
    t "text: pens, kerning and pixels equal the reference renderer" text_ref_test -O1 $FW
    if [ $PIL = 1 ]; then
        run "text spacing: glyph gaps against the font's own (16x supersampled), S M <= 0.25 px, L <= 0.5 px" \
            python3 tests/text_spacing_test.py build/gen/ui_fonts.h
    fi
    t "settings: PER5 records, palette ids and preference preservation" settings_test -O1 -w -Ibuild/gen
    t "UI: sounds keep steps, undo, recording, MIDI overflow, pending saves, panel recovery, drum grid, song chain, MONO gray" \
        ui_test -DMELODEE_USB_AUDIO=1 $FW
    t "Prophet: all native tone knobs through the panel and sound engine" prophet_knob_test -O2 -DMELODEE_USB_AUDIO=1 $FW
    t "Sound designer: native recipes, dependencies, compare, discard and save" designer_test -O2 -DMELODEE_USB_AUDIO=1 $FW
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
    t "first boot: foreign settings, safe defaults, ownership and interrupted cleanup" settings_origin_test $FW
    t "first boot: reset unsupported preset banks, preserve good entries and retry cuts" preset_boot_reset_test $FW
    t "storage safety: protect unreadable records until confirmed replacement" storage_safety_test $FW
    t "full backup: CRC before writes, stale runtime, USB reset / timeout, malformed objects, older projects" backup_test $FW
    t "CPU laboratory: persistence guard and RAM-only preference restoration" backup_lab:backup_test -DMELODEE_AUDIO_PROFILE=1 $FW
    t "editor: real C protocol, malformed transfers and queue recovery" editor_test $FW
    t "CPU lab controls: native recording/transport, guarded protocol and exact restoration" audio_cpu_lab_test -O2 -DMELODEE_AUDIO_PROFILE=1 -DMELODEE_AUDIO_BENCH=1 $FW
    t "CPU lab controls absent in normal firmware" cpu_lab_off:audio_cpu_lab_test -O2 $FW
    t "CPU lab controls absent with profiling only" cpu_lab_profile:audio_cpu_lab_test -O2 -DMELODEE_AUDIO_PROFILE=1 $FW
    t "CPU lab controls absent with benchmark only" cpu_lab_bench:audio_cpu_lab_test -O2 -DMELODEE_AUDIO_BENCH=1 $FW
    t "DX7 librarian: USB voice/bank receive, dump replies and malformed frame recovery" fm6_sysex_test $FW
    t "SWING: track + global at most 100, sequencer step lengths, the SWG display" swing_test -O2 $FW
    t "double-tap layers, SCREEN OFF, LFO 2 SYNC / TRIG / POL" layer_screen_test \
        -O1 -DMELODEE_USB_AUDIO=1 $FW
    t "DRUM EDITOR: synthesized 909, sound controls, individual pitch/length, undo and storage" drum_editor_test -O2 $FW
fi

if sound && [ $TABLES = 1 ]; then
    mkdir -p build/mod_demo \
        build/phys_demo build/drum_demo build/noise_demo build/fm6_demo
    t "regression: golden renders, health, voices, CPU budget" regress -O2 $FW -- \
        "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
    t "PHYS: stability C-1..G9 over the parameter corners, worst-case cost against PHASE WIRE, demos" phys_test \
        -DMELODEE_LEGACY_EXTRAS=1 -O2 $FW -- "$OUT/phys_test" build/phys_demo
    t "paired FM6/Prophet voices: serial/concurrent sample equality and eight-voice allocation" dual_core_test \
        -O2 $FW -pthread
    t "audio: 1296 ordered engine assignments, releases, controls and concurrent PCM parity" audio_matrix_test -O2 $FW -pthread
    t "Reverb inserts: private tanks, fallback and failed-worker drops" reverb_parallel_test -O2 $FW -pthread
    t "CZ-1: every knob changes a full tone; the dim ones leave INIT and factory tones bit for bit" cz1_knob_test -O2 $FW
    t "native fp32: independent numerical references, exact pitch, quiet signals and fractional envelopes" synth_fpu_test \
        -O2 $FW
    job prophet
    t "modulation matrix: off = bit-identical, the math, MIDI CC1 / CC11 / aftertouch, cost, demos" mod_test \
        -DMELODEE_LEGACY_EXTRAS=1 -O2 $FW -- "$OUT/mod_test" build/mod_demo
    t "matrix S&H / SLEW / DEPTH, SPREAD: off = bit-identical, stereo sides and routing" mod_spread_test -O2 $FW
    t "modulation routing cache: independent reference parity and comparison cost" mod_route_test -O2 $FW
    t "CPU playback fixtures: exact full-bank serialization across densities/timing/engines" audio_cpu_fixture -O2 $FW
    t "CPU live takes: REC/overdub schedules, native simulation, jitter bounds, FBKK decoding" audio_cpu_take -O2 $FW
    t "capture allocation: full-state parity with the original scan, path coverage and cost" capture_alloc_test -O2 $FW
    t "capture allocation with the overview pitch index" capture_alloc_index:capture_alloc_test -O2 \
        -UMELODEE_RECORD_INDEX -DMELODEE_RECORD_INDEX=1 $FW -- "$OUT/capture_alloc_index" 400 nocost
    t "routing rebuild probes: full-state parity and rejected steady/changing costs" mod_churn_bench -O2 $FW
    t "SID filter output: exact full state across models/waves/stacks/modes and costs" sid_filter_test -O2 $FW
    t "808/909 metal preparation: exact pitches, states and steady/changing costs" drum_prepare_test -O2 $FW
    t "CZ native window: exact phase kernel and full renderer through live edits" cz_window_test -O2 $FW
    t "FM6 zero-level preparation: exact state and static/changing costs" fm6_prepare_test -O2 $FW
    t "Prophet zero-wheel: exact shared state and static/changing cost" prophet_fast_test -O2 $FW
    t "Prophet zero-wheel at 2x: exact shared state and static/changing cost" prophet_fast_os2:prophet_fast_test -O2 -DP5_OVERSAMPLE=2 $FW
    t "centered bend fast path: full raw/range oracle and control cost" control_prepare_test -O2 $FW
    job fx_dormancy
    t "mono mixer fast path: output/send/peak/stem reference parity and cost" mix_fast_test -O2 $FW
    t "CZ: oscillator boundaries, native rate/target envelopes and independent lines" phase_test \
        -DMELODEE_LEGACY_EXTRAS=1 -O2 $FW
    t "SID: oscillator/LFSR/register semantics, revisions and 32 factory presets" sid_test -O2 $FW
    job phys_ref
    t "DRUM: 808 instruments, velocity, hat choke, release, eight lanes and shared voice budget" drum_test -O2 $FW -- \
        "$OUT/drum_test" build/drum_demo
    t "DRUM SID: one 6581 on one voice, frame programs, DIGI on \$D418, filter routing, levels" drum_sid_test -O2 $FW -- \
        "$OUT/drum_sid_test" build/sid_kit_demo
    t "NOISE: colour slopes, key-tracked filter and clock, META period, DC, clipping, retrigger, cost, demos" noise_test \
        -DMELODEE_LEGACY_EXTRAS=1 -O2 $FW -- "$OUT/noise_test" build/noise_demo
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
    run "regression: target cost of the render loops (pi32v2 disassembly)" python3 tests/target_budget.py \
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
