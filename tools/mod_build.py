#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""The synth module builder: the SDK's (firmware/modules/sdk/mod_build.py), run from Melodee's tree."""
import runpy
import sys
from pathlib import Path

SDK = Path(__file__).resolve().parent.parent / "firmware" / "modules" / "sdk"
sys.path.insert(0, str(SDK))
runpy.run_path(str(SDK / "mod_build.py"), run_name="__main__")
