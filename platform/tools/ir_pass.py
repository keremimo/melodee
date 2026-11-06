#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Melodee's IR passes for the two base units: minsize for the instrument's main-loop code (size_fns.py), then the
checked resident-audio transformation (audio_ram.py)."""
import json
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import audio_ram
from jieli_toolchain import tc
src,dst=map(Path,sys.argv[1:3])
unit=sys.argv[3] if len(sys.argv)>3 else ''
optimized=src.with_suffix('.optimized.ll')
if unit:
    # Melodee's main-loop functions (UI, stores, editor) at minsize, as tools/build.py does for melodee.c
    sized=src.with_suffix('.sized.ll')
    subprocess.run([sys.executable,str(ROOT/'tools/size_fns.py'),str(src),str(sized)],check=True)
    src=sized
tc('cc','-mcpu=r3','-mfprev1','-ffp-contract=off','-fno-builtin','-O2' if unit else '-Os','-S','-emit-llvm',src,'-o',optimized)
roots=audio_ram.engine_roots(ROOT/'firmware/src')
low=()
if unit:
    roots|={'mlb_render','mlb_deadline','mlb_overload','mlb_sysex_byte'}   # (the base's audio and USB interrupts call these)
    required='mlb_render'
    # The drum kits' render in the low SRAM's DSP section (app.ld .ram_text: the second core's window), not the pool:
    # the engines' worst-case states keep their 48 KiB there (the user kits' player made the resident code 3.2 KB longer)
    low=('drk_render','drk_run','drum_render_into','drum_amp','drum_source')
else:
    roots|={'ml_ms','ml_ticks','ml_lock','ml_unlock','ml_watchdog','ml_midi_out'}
    required='fm1_alnk0_irq'
s,selected,far=audio_ram.relocate(optimized.read_text(),roots,required_root=required,low=low)
dst.write_text(s)
dst.with_suffix('.json').write_text(json.dumps({'resident':selected,'far_calls':far},indent=2))
print(f'{unit or "base"}: {len(selected)} resident functions, {len(far)} flash/RAM boundaries')
