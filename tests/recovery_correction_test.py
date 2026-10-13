#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Recovery asset replacement keeps signed firmware provenance and checksums."""
import contextlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import prepare_recovery_correction as C


class CorrectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.published, self.staged, self.output = root / "published", root / "staged", root / "output"
        self.published.mkdir()
        self.staged.mkdir()
        self.old, self.new = "a" * 40, "b" * 40
        cores = {"melodee-1.3.1.fwsc": {"sha256": C.sha256(b"old signed firmware")}}
        self.original = {"version": C.VERSION, "commit": self.old, "core_builds": cores, "recovery_builds": {}}
        self.build = {"version": C.VERSION, "commit": self.new, "core_builds": {}, "recovery_builds": {}}
        (self.published / "melodee-1.3.1.fwsc").write_bytes(b"old signed firmware")
        (self.published / "melodee-1.3.1-app.bin").write_bytes(b"old signed application")
        for target in C.TARGETS:
            name = f"fm1-recover-{C.VERSION}-{target}.zip"
            info = {"commit": self.new, "version": C.VERSION, "target": target,
                    "smoke_tests_passed": True, "worktree_dirty": False,
                    "source_sha256": {name: C.sha256((ROOT / "tools" / name).read_bytes())
                                      for name in ("fm1_recover.py", "fm1_install.py", "fm1pkg_make.py", "build_recovery.py")},
                    "executable_sha256": C.sha256(b"tested executable")}
            self.build["recovery_builds"][name] = info
            blob = (json.dumps(info) + "\n").encode()
            prefix = name[:-4] + "/"
            executable = "fm1-recover.exe" if target == "windows-x64" else "fm1-recover"
            with zipfile.ZipFile(self.staged / name, "w") as z:
                z.writestr(prefix + "BUILD_INFO.json", blob)
                z.writestr(prefix + executable, b"tested executable")
                z.writestr(prefix + "SHA256SUMS", f"{C.sha256(blob)}  BUILD_INFO.json\n"
                           f"{C.sha256(b'tested executable')}  {executable}\n")
            (self.published / name).write_bytes(b"old recovery archive")
        self.write_info(self.published, self.original)
        self.write_info(self.staged, self.build)

    def write_info(self, folder, info):
        (folder / "BUILD_INFO.json").write_text(json.dumps(info))
        self.sums(folder)

    def sums(self, folder):
        (folder / "SHA256SUMS").write_text("".join(f"{C.sha256(p.read_bytes())}  {p.name}\n"
                                                 for p in sorted(folder.iterdir()) if p.name != "SHA256SUMS"))

    def prepare(self, verify=False):
        with contextlib.redirect_stdout(io.StringIO()):
            C.prepare(C.VERSION, self.published, self.staged, self.output, self.new, self.old, verify)

    def test_replacement_and_public_verification_preserve_firmware(self):
        self.prepare()
        result = json.loads((self.output / "BUILD_INFO.json").read_text())
        self.assertEqual(result["commit"], self.old)
        self.assertEqual(result["core_builds"], self.original["core_builds"])
        self.assertEqual(result["recovery_correction"]["commit"], self.new)
        self.assertEqual(len(list(self.output.iterdir())), 6)
        for p in self.output.iterdir():
            shutil.copyfile(p, self.published / p.name)
        self.prepare(verify=True)
        self.assertEqual((self.published / "melodee-1.3.1.fwsc").read_bytes(), b"old signed firmware")
        self.assertEqual((self.published / "melodee-1.3.1-app.bin").read_bytes(), b"old signed application")

    def test_bad_original_checksum_is_rejected(self):
        (self.published / "melodee-1.3.1.fwsc").write_bytes(b"damaged")
        with self.assertRaisesRegex(RuntimeError, "checksum mismatch"):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_original_firmware_must_match_signed_tag(self):
        self.original["commit"] = "c" * 40
        self.write_info(self.published, self.original)
        with self.assertRaisesRegex(RuntimeError, "signed tag"):
            self.prepare()

    def test_missing_platform_is_rejected(self):
        self.build["recovery_builds"].pop(next(iter(self.build["recovery_builds"])))
        self.write_info(self.staged, self.build)
        with self.assertRaisesRegex(RuntimeError, "all four"):
            self.prepare()

    def test_unpublished_correction_cannot_pass_verification(self):
        with self.assertRaisesRegex(RuntimeError, "differs from tested builds"):
            self.prepare(verify=True)

    def test_extra_unchecksummed_file_is_rejected(self):
        (self.staged / "unexpected").write_bytes(b"uncovered")
        with self.assertRaisesRegex(RuntimeError, "every asset"):
            self.prepare()

    def test_source_drift_after_build_is_rejected(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "tools").mkdir()
            for name in ("fm1_recover.py", "fm1_install.py", "fm1pkg_make.py", "build_recovery.py"):
                (root / "tools" / name).write_bytes(b"different source")
            with patch.object(C, "ROOT", root):
                with self.assertRaisesRegex(RuntimeError, "source hashes differ"):
                    self.prepare()

    def test_windows_checkout_preserves_exact_recovery_source_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            subprocess.run(["git", "init", "--quiet", str(root)], check=True)
            subprocess.run(["git", "-C", str(root), "config", "core.autocrlf", "true"], check=True)
            shutil.copyfile(ROOT / ".gitattributes", root / ".gitattributes")
            (root / "tools").mkdir()
            names = ("fm1_recover.py", "fm1_install.py", "fm1pkg_make.py", "build_recovery.py")
            for name in names:
                shutil.copyfile(ROOT / "tools" / name, root / "tools" / name)
            subprocess.run(["git", "-C", str(root), "add", "."], check=True, capture_output=True)
            subprocess.run(["git", "-C", str(root), "-c", "commit.gpgsign=false",
                            "-c", "user.name=Recovery Test", "-c", "user.email=recovery-test@example.invalid",
                            "commit", "--quiet", "-m", "fixture"], check=True)
            shutil.rmtree(root / "tools")
            subprocess.run(["git", "-C", str(root), "checkout", "--", "tools"], check=True)
            for name in names:
                self.assertEqual((root / "tools" / name).read_bytes(), (ROOT / "tools" / name).read_bytes())


if __name__ == "__main__":
    unittest.main()
