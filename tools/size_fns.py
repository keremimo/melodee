#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build the main-loop code for size (build.py): the functions defined in SIZE_FILES get LLVM's
minsize (about -Oz) while everything else (the sound, render and ISR code, the flash and OTA code,
the main loop) keeps -Os. The JieLi clang 4 has no '#pragma clang attribute' and its minsize
attribute is an error on variables, so build.py compiles melodee.c to LLVM IR without the
optimizer, this script adds 'minsize' to those definitions, and the IR is then compiled at -Os
(without the edit, that round trip is byte-identical to compiling melodee.c directly).
  size_fns.py IN.ll OUT.ll"""
import re
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[1]
SRC = _ROOT / "firmware" / \
    "src" if (_ROOT / "firmware" / "src").is_dir() else _ROOT / \
    "src"   # either layout
# the UI, the stores, the editor and the console: main loop only, never called by sound code
SIZE_FILES = ["ui.c", "gfx.c", "favorites.c", "icons.c", "ui_graph.c", "ui_draw.c", "ui_menu.c", "ui_input.c",
              "editor_native.c", "scale_picker.c", "seq_edit.c", "seq_notes.c", "seq_undo.c", "prophet_patch.c", "native_presets.c", "prophet_user.c", "note_tools.c", "sysex_library.c", "fm6_store.c", "cz_store.c", "editor_prophet.c", "ui_browser.c", "ui_designer.c", "ui_designer_input.c", "ui_layer.c", "ui_list.c", "ui_motion.c", "ui_name.c",
              "ui_boot.c", "ui_view.c", "ui_midi_knobs.c", "editor_library.c", "shared_presets.c", "tracks.c",
              "ui_new.c", "ui_pages.c", "ui_patterns.c", "ui_popup.c", "ui_scales.c", "ui_sections.c", "ui_slots.c", "ui_stage.c",
              "storage.c", "upreset.c", "project.c", "settings_persist.c",
              "editor.c", "editor_cz.c", "cz_bank.c", "cz_patch.h", "pattern_store.c", "editor_fm6.c", "editor_preferences.c", "editor_backup.c", "console.c",
              # main-loop only too: the boot and the main loop (main.c, not its ISR), parameters and their text, the
              # browser, the screen saver, calibration, the CZ / Prophet editing helpers, the update entry
              "main.c", "params.c", "browse.c", "screen.c", "panel.c", "cz_edit.h", "prophet_edit.h", "ota.c", "favorites.c",
              "cz_panel.h",
              # the song, punch, section sound and Bluetooth screens and the editor's radio command
              "ui_song.c", "ui_song_punch.c", "ui_song_sound.c", "ui_ble.c", "editor_ble.c"]
DEF = re.compile(
    r"^(?:static|void|int|uint\w*|int\w*|const)\b[^;=(]*?\b([A-Za-z_]\w*)\s*\(", re.M)
# Stopped note transforms are infrequent main-loop work.
# Keep their callers' common input/render paths compact as well as the helpers.
COLD_NAMES = {"note_convert", "note_transform", "ui_confirm_accept"}
# Share event copies and keep confirmed writes out of the input dispatcher.
NOINLINE_NAMES = {"ui_confirm_accept", "note_assign",
                  # Large foreground transactions must not keep boot/import
                  # bank snapshots live in the permanent main-loop frame.
                  "persist_boot", "shared_boot", "shared_import",
                  # Reused controls: share the target's identical note setup.
                  "d9_biquad_set", "d9_shape_prep", "d9_smp_rate",
                  "step_note_resize", "note_line"}
IR_DEF = re.compile(r"^(define [^\n]*?@\"?([\w.]+)\"?\([^\n]*\)(?: unnamed_addr| local_unnamed_addr)?)( #\d+[^\n]*\{)$",
                    re.M)


def definitions(text):
    """names of the functions text defines (a signature at column 0 whose ')' is followed by '{')"""
    names = []
    # comments (none hold code)
    text = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)
    for m in DEF.finditer(text):
        i, depth = m.end() - 1, 0
        while i < len(text):
            depth += {"(": 1, ")": -1}.get(text[i], 0)
            i += 1
            if depth == 0:
                break
        rest = re.sub(r"^(__attribute__\s*\(\(.*?\)\)\s*)+",
                      "", text[i:i + 200].lstrip())
        if rest.startswith("{"):
            names.append(m.group(1))
    return names


def size_names():
    # These recording helpers only restore, copy or reindex editor/persistence data.
    # Playback, live capture and erase functions retain the normal optimization.
    names = {"recording_restore_note", "recording_copy", "recording_reindex",
             "drum909_init", "drum909_set", "drum_note_on", "voice_start",
             "audio_batch_push", "mix_prepare_batch", "fm6_sync", "cz_native_block"}
    # Resident audio code that runs once per note, MIDI message, drum hit, recorded take, pattern switch or SysEx byte,
    # never per sample: its SRAM is the audio arena's (the MIDI-only tracks' banks took some)
    names |= {"d9_set_panel", "d9_rim_retune", "d9_smp_rate", "d9_bt_trigger", "drum909_trigger", "d9_biquad_set",
              "d9_shape_prep", "dsk_load",
              "rec_note", "rec_finish", "rec_set", "rec_release", "recording_quantize", "recording_sync",
              "recording_present", "recording_undo_capture", "recording_undo_signature", "recording_insert",
              "recording_zero", "recording_snapshot", "recording_stop", "recording_pitch_add",
              "recording_queue_remove", "recording_queue_refresh",
              "trk_note_on_engine", "trk_note_on_raw", "input_on", "input_off", "key_off", "mono_play", "voice_victim",
              "midi_event", "midi_note_event", "midi_parameter", "midi_release", "midi_forget_track", "midi_clock_mode",
              "midi_insert_release", "midi_insert_controls", "midi_track_note_on", "midi_track_controller",
              "midi_track_flush", "midi_track_all_off",
              "chord_make", "chord_strum_begin", "lcz_sx_encode", "cz_ed_decode", "cz_ed_put", "sysex_byte",
              "fm6_sx_byte", "seq_erase_rebuild", "seq_erase_pass", "seq_stop", "seq_start", "pattern_encode",
              "pattern_apply", "fm6_note_on", "fm6_legato", "fm6_keyed", "fm6_alloc", "p5_note_on", "sid_note_on",
              "cz_native_note_on", "motion_native_edited", "motion_set_event", "motion_native_ok", "motion_capture",
              "motion_restore", "audition_stop"}
    for f in SIZE_FILES:
        names.update(n for n in definitions((SRC / f).read_text())
                     if not n.endswith("_irq"))   # (ISRs stay as they are)
    return names


def outlined_names(ir, names):
    """Outline substantial cold/control work; preserve explicit hot inlining.

    A single-use boot transaction can otherwise keep its largest local bank
    alive in the whole main loop. Tiny accessors remain eligible for inlining.
    Sample loops never enter this set, even when their IR body is large.
    """
    groups = {int(n): attrs for n, attrs in re.findall(
        r'^attributes #(\d+) = (.*)$', ir, re.M)}
    outlined = set()
    definitions = re.finditer(r'^define [^\n]*?@([\w.]+)\([^\n]*\)[^\n]* #(\d+)[^\n]*\{\n(.*?)^}',
                              ir, re.M | re.S)
    for function in definitions:
        name, group, body = function.groups()
        if name in names and len(body.splitlines()) >= 40 and 'alwaysinline' not in groups[int(group)]:
            outlined.add(name)
    return outlined


def main(src, dst):
    names, hit = size_names(), set()
    original = Path(src).read_text()
    outlined = outlined_names(original, names)

    def mark(m):
        if m.group(2) not in names:
            return m.group(0)
        hit.add(m.group(2))
        return m.group(1) + " minsize" + (" cold" if m.group(2) in COLD_NAMES else "") + (" noinline" if m.group(2) in NOINLINE_NAMES | outlined else "") + m.group(3)
    ir = IR_DEF.sub(mark, original)
    attrs = {int(n): a for n, a in re.findall(
        r"^attributes #(\d+) = (.*)$", ir, re.M)}
    nextattr = max(attrs)+1
    hotattrs = {}

    def hot(m):
        nonlocal nextattr
        num = int(m.group(2))
        if num not in hotattrs:
            hotattrs[num] = nextattr
            nextattr += 1
        return m.group(1) + "#" + str(hotattrs[num]) + m.group(3)
    ir = re.sub(
        r'^(define [^\n]+ )#(\d+)( section ".dsp_text"[^\n]*\{)$', hot, ir, flags=re.M)
    for n, new in hotattrs.items():
        ir += "\nattributes #"+str(new)+" = " + \
            attrs[n].replace("optsize ", "")+"\n"
    Path(dst).write_text(ir)
    print(
        f"size: {len(hit)} main-loop functions built for size ({len(names) - len(hit)} not in this build)")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:3]))
