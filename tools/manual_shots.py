#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Relyt29 (@relyt29), Signals Intelligence Sound (https://sigintsound.com/)
"""Curate screenshots for a versioned web manual: copy a fixed allowlist of NIGHT UI
renders (tests/ui_render.c -> tests/ui_render.py, under build/ui_new/NIGHT) into
web/manual/vX.Y.Z/, which that version's index.html embeds and web/make_site.py publishes
under webapp/manual/vX.Y.Z/.

build/ is gitignored, so these PNGs must be committed copies. Rerun this after
the UI tests regenerate build/ui_new to refresh them:

  tools/manual_shots.py                      # → latest web/manual/v*/
  tools/manual_shots.py --version v1.1.0     # → a specific version folder
  tools/manual_shots.py --check              # list missing / stale, write nothing

Only NIGHT is used (the manual's palette). Add a name here, reference it from
web/manual/vX.Y.Z/index.html as <img src="name.png">, and commit the PNG in that folder.
"""
import argparse
import filecmp
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "build" / "ui_new" / "NIGHT"
MANUAL = ROOT / "web" / "manual"
VER_RE = re.compile(r"^v\d+(?:\.\d+)*$")

# one clear figure per page family; the device's own NIGHT renders (240 x 240).
SHOTS = [
    # Stage
    "home.png", "home_note.png", "stage_p5.png", "stage_cz.png", "stage_drum.png", "stage_filter.png",
    # Sounds / names / sheets
    "presets.png", "presets_cat.png", "user.png", "name_typing.png", "ref_sheet_sound.png",
    # Projects
    "project.png", "project_new.png", "new_key.png", "new_roles.png",
    # Envelopes and modulation
    "env.png", "env_dest.png", "lfo.png", "mod.png",
    # Effects: track inserts and the shared return
    "fx_phaser.png", "fx_echo.png", "fx_reverb.png", "fx_send.png", "fx_bus1.png",
    # Hold layers
    "layer_glo_active.png", "layer_scl_active.png", "layer_edit_active.png",
    # Scales and chords
    "scl.png", "scale_picker_favorites.png", "chord.png",
    # Arpeggiator
    "arp.png",
    # Sequencer
    "notes_mixed.png", "step.png", "pattern.png", "tempo.png", "patterns.png", "patterns_sheet.png",
    "song.png", "ref_sheet_song.png", "motion.png", "drum.png", "ref_sheet_note.png",
    # Mixer / global
    "mixer.png", "global.png", "system.png",
    # Menu
    "menu.png", "menu_hold.png", "menu_autosave_on.png", "menu_usb_stereo.png", "menu_clock.png",
    # Engines
    "ref_map_p5.png", "ref_edit_osc.png", "fm6_alg_01.png", "fm6_eg.png", "fm6_freq.png",
    "fm6_store.png", "ref_map_fm6.png", "cz1_env.png", "voice.png", "drum_sound_808.png", "drum_mix_909.png",
    # Sound designer
    "designer_start.png",
]


def version_dirs():
    return sorted(
        (p for p in MANUAL.iterdir() if p.is_dir() and VER_RE.match(p.name)),
        key=lambda p: tuple(int(x) for x in p.name[1:].split(".")),
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="report only; write nothing")
    ap.add_argument("--version", help="manual version folder (default: latest web/manual/v*)")
    a = ap.parse_args()
    if not SRC.is_dir():
        sys.exit(f"{SRC} is missing: run the UI renders first (tests/run_tests.sh)")
    if a.version:
        dst = MANUAL / a.version
        if not VER_RE.match(a.version):
            sys.exit(f"--version must look like v1.1.0, got {a.version!r}")
    else:
        vers = version_dirs()
        if not vers:
            sys.exit(f"no version folders in {MANUAL}: add web/manual/vX.Y.Z/ or pass --version")
        dst = vers[-1]
    dst.mkdir(parents=True, exist_ok=True)
    missing, copied, same = [], [], 0
    for name in SHOTS:
        src = SRC / name
        if not src.is_file():
            missing.append(name)
            continue
        out = dst / name
        if out.is_file() and filecmp.cmp(src, out, shallow=False):
            same += 1
            continue
        if not a.check:
            shutil.copy(src, out)
        copied.append(name)
    known = set(SHOTS)
    extra = sorted(p.name for p in dst.glob("*.png") if p.name not in known)
    if missing:
        print(f"missing from {SRC}: {', '.join(missing)}", file=sys.stderr)
    if extra:
        print(f"not in the allowlist (left in place): {', '.join(extra)}", file=sys.stderr)
    verb = "stale" if a.check else "copied"
    print(f"manual_shots: {dst.relative_to(ROOT)}: {len(SHOTS)} figures, {same} up to date, "
          f"{len(copied)} {verb}, {len(missing)} missing")
    if missing:
        sys.exit(1)


if __name__ == "__main__":
    main()
