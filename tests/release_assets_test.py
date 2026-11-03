#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Release aggregation refuses mismatched firmware, stale native builds and damaged ZIPs."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import prepare_release as A
from fm1_recover import APP_LO, Package, RecoveryError, sha256
from fm1pkg_make import BOARDS, MODELS, release_product, sfc, ufw
from recovery_test import make_package

MAJOR, MINOR, PATCH = A.VERSION.split("-", 1)[0].split(".")
PRODUCT = f"FM-1_9{MAJOR}{int(MINOR):02d}{PATCH}"


class ReleaseTests(unittest.TestCase):
    def test_release_and_prerelease_identities(self):
        for version, product in (("1.3.2", "FM-1_91032"), ("1.4.0-rc.1", "FM-1_91040"),
                                 ("1.4.0", "FM-1_91040"), ("1.5.0-rc.1", "FM-1_91050"), ("1.5.0-rc.2", "FM-1_91050"), ("1.5.0-rc.3", "FM-1_91050"), ("1.5.0-rc.4", "FM-1_91050"), ("0.11.1-audio3", "FM-1_90111"),
                                 ("0.10", "FM-1_9010")):
            with self.subTest(version=version):
                self.assertEqual(release_product(version), product)
        for version in ("1.4.0-", "1.4.0-rc..1", "1.4.0/rc.1", "10.4.0", "1.4.10"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                release_product(version)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name)
        self.commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        package = self.folder / f"melodee-{A.VERSION}.fwsc"
        package.write_bytes(make_package())
        package.write_bytes(ufw(Package(package).flash, b"loader", PRODUCT))
        obj = Package(package)
        decoded = bytearray(obj.flash[APP_LO:])
        sfc(decoded, 0, len(decoded), 0, obj.key)
        (self.folder / f"melodee-{A.VERSION}-app.bin").write_bytes(decoded[0x120:0x121])
        smk_flash = bytes(obj.flash) + bytes(BOARDS["smk37"][0] - len(obj.flash))
        for model, stem in A.SMK_STEMS.items():
            (self.folder / f"{stem}-{A.VERSION}.fwsc").write_bytes(ufw(smk_flash, b"loader", release_product(A.VERSION, MODELS[model])))
            (self.folder / f"{stem}-{A.VERSION}-app.bin").write_bytes(decoded[0x120:0x121])
        for target in A.TARGETS:
            self.write_archive(target)

    def tearDown(self):
        self.temp.cleanup()

    def write_archive(self, target, commit=None, damaged=False):
        name = f"fm1-recover-{A.VERSION}-{target}"
        info = {"commit": commit or self.commit, "version": A.VERSION, "target": target, "smoke_tests_passed": True}
        blob = json.dumps(info).encode()
        with zipfile.ZipFile(self.folder / (name + ".zip"), "w") as archive:
            archive.writestr(name + "/BUILD_INFO.json", blob)
            archive.writestr(name + "/SHA256SUMS", f"{sha256(blob) if not damaged else '0' * 64}  BUILD_INFO.json\n")

    def prepare(self):
        with patch.object(sys, "argv", ["prepare_release.py", A.VERSION, str(self.folder)]):
            A.main()

    def test_complete_release_and_padded_firmware_identity(self):
        self.prepare()
        info = json.loads((self.folder / "BUILD_INFO.json").read_text())
        self.assertEqual(info["product"], PRODUCT)
        self.assertEqual(info["commit"], self.commit)
        self.assertEqual(len(info["recovery_builds"]), 4)
        self.assertEqual(set(info["core_builds"]), {f"melodee-{A.VERSION}.fwsc"})
        self.assertEqual(info["core_builds"][f"melodee-{A.VERSION}.fwsc"]["MELODEE_DUAL_CORE"], 1)
        self.assertEqual(set(info["smk_builds"]), {f"{stem}-{A.VERSION}.fwsc" for stem in A.SMK_STEMS.values()})
        for line in (self.folder / "SHA256SUMS").read_text().splitlines():
            digest, name = line.split("  ", 1)
            self.assertEqual(sha256((self.folder / name).read_bytes()), digest)

    def test_stale_single_core_assets_are_rejected(self):
        (self.folder / f"melodee-{A.VERSION}-1core.fwsc").write_bytes(b"stale")
        with self.assertRaisesRegex(RuntimeError, "Single-core release assets are retired"):
            self.prepare()

    def test_application_mismatch_is_rejected(self):
        (self.folder / f"melodee-{A.VERSION}-app.bin").write_bytes(b"wrong app")
        with self.assertRaisesRegex(RuntimeError, "differs from package"):
            self.prepare()

    def test_smk_package_missing_is_rejected(self):
        (self.folder / f"melodee-smk37-elite-{A.VERSION}.fwsc").unlink()
        with self.assertRaises(FileNotFoundError):
            self.prepare()

    def test_smk_package_of_another_model_is_rejected(self):
        pro = (self.folder / f"melodee-smk37-{A.VERSION}.fwsc").read_bytes()
        (self.folder / f"melodee-smk37-mke-{A.VERSION}.fwsc").write_bytes(pro)
        with self.assertRaisesRegex(RecoveryError, "not for an MKE-P37"):
            self.prepare()

    def test_smk_application_mismatch_is_rejected(self):
        (self.folder / f"melodee-smk37-{A.VERSION}-app.bin").write_bytes(b"wrong app")
        with self.assertRaisesRegex(RuntimeError, "differs from package"):
            self.prepare()

    def test_stale_native_commit_is_rejected(self):
        self.write_archive("windows-x64", commit="0" * 40)
        with self.assertRaisesRegex(RuntimeError, "Wrong or untested"):
            self.prepare()

    def test_corrupted_archive_is_rejected(self):
        self.write_archive("macos-arm64", damaged=True)
        with self.assertRaisesRegex(RuntimeError, "checksum mismatch"):
            self.prepare()


if __name__ == "__main__":
    unittest.main()
