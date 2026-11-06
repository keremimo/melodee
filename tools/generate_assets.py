#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Generate Melodee's shared assets for the fm1-base build (platform/): the same headers tools/build.py generates.

The generation step is tools/build.py's generate() and its 909 tables, without its compiler or packager, so both
builds read one build/gen.
"""
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
GEN = SRC / "build/gen"


def generate():
    """generated headers (UI fonts, icons, keycaps, palettes, tables, factory sounds)"""
    GEN.mkdir(parents=True, exist_ok=True)
    for old in ("melodee_font.h", "melodee_icons.h"):     # headers of the bitmap font and icon atlas
        (GEN / old).unlink(missing_ok=True)
    tools = SRC / "tools"
    subprocess.run([sys.executable, str(tools / "gen_scales.py"), "--check"], check=True)
    cmds = [[tools / "gen_aa_font.py", GEN / "ui_fonts.h", "--preset", "rubik"],
            [tools / "gen_icons.py", GEN / "ui_icons.h"],
            [tools / "gen_aa_keycaps.py", GEN / "ui_keycaps.h"],
            [tools / "gen_ui_palettes.py", GEN / "ui_palettes.h"],
            [tools / "gen_tables.py", GEN / "melodee_tables.h"],
            [tools / "gen_fm6_patches.py", GEN / "melodee_fm6.h"],
            [tools / "gen_cz1_factory.py", GEN / "melodee_cz1.h"],
            [tools / "gen_prophet_factory.py", GEN / "melodee_prophet_factory.h", "--web", "check"],
            [tools / "gen_909_tables.py", GEN / "x0x_drum_tables.h"]]
    procs = [subprocess.Popen([sys.executable, *map(str, c)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True) for c in cmds]
    failed = []
    for c, p in zip(cmds, procs):
        sys.stdout.write(p.communicate()[0])
        if p.returncode:
            failed.append(c[0].name)
    if failed:
        raise SystemExit(f"build: {', '.join(failed)} failed")


if __name__ == "__main__":
    generate()
