#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Local builds and C consumers must select the same production performance paths."""
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build

OPTIONS = tuple("MELODEE_" + name for name in (
    "RAM_AUDIO", "CACHE_RAM", "MOD_CACHE", "CONTROL_FAST", "MIX_FAST",
    "FX_DORMANT", "RECORD_INDEX", "PROPHET_FAST", "PANEL_FAST", "FM6_FAST",
    "CZ_FAST", "DRUM_FAST", "SID_FAST", "CAPTURE_FAST", "RENDER_AHEAD"))


class PerformanceDefaultsTest(unittest.TestCase):
    def setUp(self):
        # Clearing Melodee options must not strip PATH or toolchain settings
        # from the compiler subprocess (hosted Linux compilers need them).
        self.compiler_env = os.environ.copy()

    def macros(self, overrides=()):
        result = subprocess.run([
            "cc", "-E", "-dM", "-x", "c", *overrides,
            "-include", str(ROOT / "firmware/src/performance_options.h"), "-"],
            input="", capture_output=True, text=True, env=self.compiler_env)
        self.assertEqual(result.returncode, 0, result.stderr)
        return dict(re.findall(r"^#define (MELODEE_\w+) ([01])$", result.stdout, re.M))

    def test_clean_local_build_enables_every_optimization(self):
        with patch.dict(os.environ, {}, clear=True):
            build.apply_performance_defaults()
            expected = dict.fromkeys(OPTIONS, "1")
            self.assertEqual(build.performance_defaults(), expected)
            self.assertEqual({k: os.environ[k] for k in OPTIONS}, expected)
            self.assertEqual(self.macros(), expected)

    def test_reference_overrides_match_compiler_and_build_placement(self):
        overrides = dict.fromkeys(OPTIONS, "0")
        with patch.dict(os.environ, overrides, clear=True):
            build.apply_performance_defaults()
            self.assertEqual({k: os.environ[k] for k in OPTIONS}, overrides)
            self.assertEqual(self.macros([f"-D{k}=0" for k in OPTIONS]), overrides)

    def test_mistyped_option_cannot_silently_disable_resident_build_steps(self):
        with patch.dict(os.environ, {"MELODEE_RAM_AUDIO": "yes"}, clear=True):
            with self.assertRaisesRegex(SystemExit, "MELODEE_RAM_AUDIO must be 0 or 1"):
                build.apply_performance_defaults()


if __name__ == "__main__":
    unittest.main()
