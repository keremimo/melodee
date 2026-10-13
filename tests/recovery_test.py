#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""ROM recovery against modeled USB/NOR; no connected hardware or PyUSB needed."""
import binascii
import contextlib
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_recover as R
import fm1pkg_make as P


def make_package(key=0x980F):
    flash = bytearray(b"\xff" * R.APP_HI)
    blob = P.key_blob(key)
    flash[0x400:0x400 + len(blob)] = blob
    entry = bytearray(32)
    struct.pack_into("<HII", entry, 2, P.crc16(blob), 0x400, len(blob))
    struct.pack_into("<H", entry, 14, 1)
    entry[16:31] = b"isd_config.ini\0"
    struct.pack_into("<H", entry, 0, P.crc16(entry[2:]))
    P.enc(entry, 0, 32)
    flash[32:64] = entry
    app = bytearray(32)
    struct.pack_into("<H", app, 0, P.crc16(app[2:]))
    P.sfc(app, 0, 32, 0, key)
    flash[R.APP_LO:R.APP_LO + 32] = app
    return P.ufw(bytes(flash), b"FELUCCA-LOADER-1", "FM-1_91030")


def make_stock_fixture():
    """Synthetic V15 entry conventions, with no vendor firmware redistributed."""
    image = R.logical_image(make_package())
    header = bytearray(image[:64])
    P.enc(header, 0, 64)
    entries = bytearray(image[64:224])
    off = (len(image) + 31) & ~31
    empty = bytearray(80)
    struct.pack_into("<HHHHIII", empty, 0, 2, 2, 0, 0, off, 0, 0)
    empty[64:72] = b"info.log"
    P.enc(empty, 0, 80)
    entries += empty
    auxiliary = bytearray(80)
    # The vendor metadata has a checksum convention unlike a plain-data CRC.
    data = b"vendor metadata!"
    struct.pack_into("<HHHHIII", auxiliary, 0, 50, 3, P.crc16(data) ^ 1, 0, off, len(data), 32)
    auxiliary[64:67] = b"USR"
    P.enc(auxiliary, 0, 80)
    entries += auxiliary
    image = bytearray(image + bytes(off - len(image)) + data)
    struct.pack_into("<IH", header, 4, len(image), 4)
    struct.pack_into("<H", header, 2, P.crc16(entries))
    struct.pack_into("<H", header, 0, P.crc16(header[2:]))
    P.enc(header, 0, 64)
    image[:64] = header
    image[64:64 + len(entries)] = entries
    raw = bytearray()
    product = "FM-1_015"
    for i in range(20):
        raw += image[i * 47:(i + 1) * 47]
        raw.append(ord(product[i]) + i + 1 if i < len(product) else 0x7D)
    return bytes(raw + image[940:])


class FakeROM(R.Link):
    def __init__(self, nor, key=0x980F):
        self.nor = bytearray(nor)
        self.key = key
        self.erases = []
        self.programs = 0
        self.fail_program = None
        self.corrupt_verify = False
        self.corrupt_protected = False
        self.output = None
        self.calls = []

    def transfer(self, cdb, incoming=0, outgoing=None):
        inquiry = bytearray(36)
        inquiry[8:16] = b"WL82    "
        inquiry[16:32] = b"UBOOT1.00       "
        return bytes(inquiry)

    def command(self, cmd, args=b"", incoming=None, outgoing=None):
        self.calls.append(cmd)
        if cmd == 0xFD07:
            return bytes(incoming)
        if cmd in (0xFB06, 0xFB08):
            return bytes(14)
        if cmd == 0xFC09:
            data = bytearray(14)
            data[4:6] = R.cipher(self.key.to_bytes(2, "little"))[::-1]
            return bytes(data)
        if cmd == 0xFC0A:
            data = bytearray(14)
            data[0] = 3
            data[2:6] = (0x856014).to_bytes(4, "little")
            return bytes(data)
        address = struct.unpack_from(">I", args)[0]
        if cmd == 0xFD05:
            data = bytes(self.nor[address:address + incoming])
            if self.corrupt_verify and self.programs and incoming == 512:
                data = bytes([data[0] ^ 1]) + data[1:]
            return data
        if cmd == 0xFB01:
            # The complete backup AND its manifest must be durable before the first erase.
            if self.output:
                backup = self.output / "full-flash-before.bin"
                assert len(backup.read_bytes()) == R.FLASH_SIZE
                assert json.loads((self.output / "backup.json").read_text())["backup_sha256"] == R.sha256(backup.read_bytes())
            self.erases.append(address)
            self.nor[address:address + R.SECTOR] = b"\xff" * R.SECTOR
            return bytes(14)
        if cmd == 0xFB04:
            self.programs += 1
            if self.programs == self.fail_program:
                raise OSError("USB disconnected during programming")
            self.nor[address:address + len(outgoing)] = outgoing
            if self.corrupt_protected:
                self.nor[0] ^= 1
                self.corrupt_protected = False
            return bytes(14)
        raise AssertionError(f"Unexpected command {cmd:#x}")


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name)
        self.path = self.folder / "test.fwsc"
        self.path.write_bytes(make_package())
        self.package = R.Package(self.path)
        self.before = bytearray(b"\x5a" * R.FLASH_SIZE)
        self.before[R.APP_LO:R.APP_HI] = self.package.flash[R.APP_LO:]
        self.before[R.APP_LO + R.SECTOR] = 0x33  # one changed sector
        self.output = self.folder / "session"
        self.output.mkdir()
        self.link = FakeROM(self.before)
        self.link.output = self.output
        self.quiet = contextlib.redirect_stdout(io.StringIO())
        self.quiet.__enter__()

    def tearDown(self):
        self.quiet.__exit__(None, None, None)
        self.temp.cleanup()

    def test_offline_check_never_opens_usb(self):
        with patch.object(R, "Link", side_effect=AssertionError("USB opened")):
            self.assertEqual(R.main([str(self.path), "--check"]), 0)

    def test_info_ignores_sdk_and_never_loads_ram_helper(self):
        self.link.close = lambda: None
        with patch.dict(R.os.environ, {"AC79_SDK": "/nonexistent/sdk"}), patch.object(R, "Link", return_value=self.link):
            self.assertEqual(R.main(["--info"]), 0)
        self.assertEqual(self.link.calls, [])

    def test_cli_write_is_explicit_and_sessions_do_not_overwrite(self):
        self.link.close = lambda: None
        backup_parent = self.folder / "backups"
        with patch.object(R, "Link", return_value=self.link), patch.object(R, "read_loader", return_value=bytes(512)):
            arguments = [str(self.path), "--loader", "loader.bin", "--backup-dir", str(backup_parent)]
            self.link.output = None
            self.assertEqual(R.main(arguments), 0)
            self.assertEqual(self.link.erases, [])
            self.assertEqual(R.main(arguments + ["--write"]), 0)
        sessions = list(backup_parent.iterdir())
        self.assertEqual(len(sessions), 2)
        self.assertEqual(sum((p / "recovery-result.json").exists() for p in sessions), 1)

    def test_package_and_loader_rejection(self):
        for raw in (b"short", make_package()[:-1], make_package()[:1200] + b"x" + make_package()[1201:]):
            self.path.write_bytes(raw)
            with self.assertRaises(Exception):
                R.Package(self.path)
        self.path.write_bytes(b"x" * 30720)
        with self.assertRaisesRegex(R.RecoveryError, "Unsupported"):
            R.read_loader(self.path)

    def test_wrong_partition_with_valid_checksums(self):
        flash = self.package.flash[:-R.SECTOR]
        self.path.write_bytes(P.ufw(flash, b"loader", "FM-1_91030"))
        with self.assertRaisesRegex(R.RecoveryError, "boundary"):
            R.Package(self.path)

    def test_wrong_model(self):
        self.path.write_bytes(P.ufw(self.package.flash, b"loader", "OTHER_123"))
        with self.assertRaisesRegex(R.RecoveryError, "not for an FM-1"):
            R.Package(self.path)

    def test_read_only_and_backup_integrity(self):
        result = R.recover(self.link, self.package, bytes(512), self.output)
        self.assertFalse(result["write_requested"])
        self.assertEqual(self.link.nor, self.before)
        self.assertEqual(self.link.erases, [])
        self.assertEqual((self.output / "full-flash-before.bin").read_bytes(), self.before)
        self.assertFalse((self.output / "recovery-result.json").exists())

    def test_restore_only_changed_application_sector(self):
        result = R.recover(self.link, self.package, bytes(512), self.output, True)
        self.assertEqual(self.link.erases, [R.APP_LO + R.SECTOR])
        self.assertTrue(result["full_flash_verified"])
        self.assertTrue(result["bootloader_unchanged"] and result["saved_data_unchanged"])
        self.assertEqual(self.link.nor[:R.APP_LO], self.before[:R.APP_LO])
        self.assertEqual(self.link.nor[R.APP_HI:], self.before[R.APP_HI:])
        self.assertEqual(self.link.nor[R.APP_LO:R.APP_HI], self.package.flash[R.APP_LO:])

    def test_chip_key_mismatch_precedes_flash_access(self):
        self.link.key = 0x1234
        with self.assertRaisesRegex(R.RecoveryError, "Chip-key mismatch"):
            R.recover(self.link, self.package, bytes(512), self.output, True)
        self.assertNotIn(0xFD05, self.link.calls)
        self.assertEqual(self.link.erases, [])

    def test_backup_failure_prevents_erasure(self):
        original = R.durable_write
        def fail(path, data):
            if path.name == "full-flash-before.bin":
                raise OSError("Disk full")
            return original(path, data)
        with patch.object(R, "durable_write", side_effect=fail):
            with self.assertRaisesRegex(OSError, "Disk full"):
                R.recover(self.link, self.package, bytes(512), self.output, True)
        self.assertEqual(self.link.erases, [])

    def test_manifest_failure_prevents_erasure(self):
        (self.output / "backup.json").write_text("existing")
        with self.assertRaises(FileExistsError):
            R.recover(self.link, self.package, bytes(512), self.output, True)
        self.assertEqual(self.link.erases, [])

    def test_interrupted_write_retains_backup_and_can_resume(self):
        self.link.fail_program = 2
        with self.assertRaisesRegex(OSError, "disconnected"):
            R.recover(self.link, self.package, bytes(512), self.output, True)
        self.assertEqual((self.output / "full-flash-before.bin").read_bytes(), self.before)
        self.assertFalse((self.output / "recovery-result.json").exists())
        self.assertEqual(self.link.nor[:R.APP_LO], self.before[:R.APP_LO])
        self.assertEqual(self.link.nor[R.APP_HI:], self.before[R.APP_HI:])
        second = self.folder / "retry"
        second.mkdir()
        self.link.output = second
        self.link.fail_program = None
        result = R.recover(self.link, self.package, bytes(512), second, True)
        self.assertTrue(result["full_flash_verified"])

    def test_verification_failure_does_not_report_success(self):
        self.link.corrupt_verify = True
        with self.assertRaisesRegex(R.RecoveryError, "Sector verification"):
            R.recover(self.link, self.package, bytes(512), self.output, True)
        self.assertFalse((self.output / "recovery-result.json").exists())

    def test_full_verification_checks_protected_regions(self):
        self.link.corrupt_protected = True
        with self.assertRaisesRegex(R.RecoveryError, "Final full-flash verification"):
            R.recover(self.link, self.package, bytes(512), self.output, True)

    def test_legacy_prophet_data_is_ignored_and_current_banks_preserved(self):
        h = bytearray(32)
        struct.pack_into("<IHH", h, 0, 0x554C4546, 25, 0)
        struct.pack_into("<I", h, 28, binascii.crc32(h[:28]))
        self.link.nor[0x89000:0x89020] = h
        result = R.recover(self.link, self.package, bytes(512), self.output, True)
        self.assertTrue(result["full_flash_verified"])
        self.assertEqual(self.link.nor[0x89000:0x89020], self.package.flash[0x89000:0x89020])
        self.assertEqual(self.link.nor[R.APP_HI:], self.before[R.APP_HI:])
        self.assertEqual((self.output / "full-flash-before.bin").read_bytes()[0x89000:0x89020], h)

    def test_write_boundary_guard(self):
        for address in (0, R.APP_LO - R.SECTOR, R.APP_LO + 1, R.APP_HI, 0xFC000):
            with self.assertRaisesRegex(R.RecoveryError, "outside"):
                self.link.write_sector(address, bytes(R.SECTOR))
        self.assertEqual(self.link.erases, [])


class OfficialRecoveryTests(RecoveryTests):
    def setUp(self):
        super().setUp()
        raw = make_stock_fixture()
        self.stock_hash = patch.object(R, "STOCK_V15_SHA256", R.sha256(raw))
        self.stock_hash.start()
        self.addCleanup(self.stock_hash.stop)
        self.path.write_bytes(raw)
        self.package = R.Package(self.path)

    # The inherited recovery tests also exercise stock writes, failures and bounds.
    # Package-specific Melodee fixtures still use the strict non-stock branch.
    def test_stock_metadata_accepted_only_with_exact_whole_file_hash(self):
        self.assertTrue(self.package.stock_v15)
        self.assertEqual(self.package.product, "FM-1_015")
        with patch.object(R, "STOCK_V15_SHA256", "0" * 64):
            with self.assertRaisesRegex(R.RecoveryError, "unmodified original"):
                R.Package(self.path)

    def test_modified_stock_rejected_before_usb_access(self):
        original = self.path.read_bytes()
        for address in (10, 1200, R.APP_LO + 0x400 + 20, len(original) - 1):
            raw = bytearray(original)
            raw[address] ^= 1
            self.path.write_bytes(raw)
            with patch.object(R, "Link", side_effect=AssertionError("USB opened")), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(R.main([str(self.path), "--write"]), 1)
        self.assertEqual(self.link.erases, [])

    def test_metadata_exceptions_do_not_apply_to_melodee(self):
        image = R.logical_image(self.path.read_bytes())
        raw = bytearray()
        product = "FM-1_91031"
        for i in range(20):
            raw += image[i * 47:(i + 1) * 47]
            raw.append(ord(product[i]) + i + 1 if i < len(product) else 0x7D)
        self.path.write_bytes(raw + image[940:])
        with self.assertRaisesRegex(R.RecoveryError, "entry extent"):
            R.Package(self.path)


@unittest.skipUnless(os.environ.get("FM1_STOCK_V15"), "set FM1_STOCK_V15 to test genuine official V15")
class GenuineV15Tests(unittest.TestCase):
    def test_genuine_package_check_and_restore_preserve_protected_flash(self):
        path = Path(os.environ["FM1_STOCK_V15"])
        package = R.Package(path)
        self.assertEqual(package.digest, R.STOCK_V15_SHA256)
        self.assertTrue(package.stock_v15)
        with patch.object(R, "Link", side_effect=AssertionError("USB opened")):
            self.assertEqual(R.main([str(path), "--check"]), 0)
        before = bytearray(b"\x5a" * R.FLASH_SIZE)
        link = FakeROM(before)
        with tempfile.TemporaryDirectory() as folder, contextlib.redirect_stdout(io.StringIO()):
            link.output = Path(folder)
            report = R.recover(link, package, R.read_loader(R.default_loader()), link.output, True)
            self.assertEqual((link.output / "full-flash-before.bin").read_bytes(), before)
        self.assertEqual(link.erases, list(range(R.APP_LO, R.APP_HI, R.SECTOR)))
        self.assertTrue(report["full_flash_verified"])
        self.assertEqual(link.nor[:R.APP_LO], before[:R.APP_LO])
        self.assertEqual(link.nor[R.APP_HI:], before[R.APP_HI:])
        self.assertEqual(link.nor[R.APP_LO:R.APP_HI], package.flash[R.APP_LO:])


class TransportTests(unittest.TestCase):
    def test_discovery_filters_devices_and_supports_winusb(self):
        class Device:
            bus, address = 2, 7
            def is_kernel_driver_active(self, interface):
                raise NotImplementedError("WinUSB")
        device = Device()
        core = types.ModuleType("usb.core")
        core.find = lambda **kwargs: devices
        util = types.ModuleType("usb.util")
        util.claim_interface = lambda dev, interface: claimed.append(dev)
        util.dispose_resources = lambda dev: None
        libusb = types.ModuleType("usb.backend.libusb1")
        libusb.get_backend = lambda **kwargs: object()
        backend = types.ModuleType("usb.backend")
        backend.libusb1 = libusb
        usb = types.ModuleType("usb")
        usb.core, usb.util, usb.backend = core, util, backend
        modules = {"usb": usb, "usb.core": core, "usb.util": util,
                   "usb.backend": backend, "usb.backend.libusb1": libusb}
        devices, claimed = [], []
        with patch.dict(sys.modules, modules):
            with self.assertRaisesRegex(R.RecoveryError, "not found"):
                R.Link(wait=0, library="libusb.dll")
            devices = [device, device]
            with self.assertRaisesRegex(R.RecoveryError, "Multiple"):
                R.Link(wait=0, library="libusb.dll")
            devices = [device]
            with self.assertRaisesRegex(R.RecoveryError, "not found"):
                R.Link(wait=0, library="libusb.dll", bus=3)
            link = R.Link(wait=0, library="libusb.dll", bus=2, address=7)
            self.assertEqual(claimed, [device])
            link.close()

    def test_bad_status_short_reads_and_wrong_echo(self):
        class Device:
            def __init__(self, status):
                self.responses = [bytes(16), status]
            def write(self, ep, data, timeout):
                return len(data)
            def read(self, ep, length, timeout):
                return self.responses.pop(0)
        cases = [b"short", struct.pack("<IIIB", 0, 1, 0, 0), struct.pack("<IIIB", 0x53425355, 2, 0, 0),
                 struct.pack("<IIIB", 0x53425355, 1, 1, 0), struct.pack("<IIIB", 0x53425355, 1, 0, 1)]
        for status in cases:
            link = R.Link.__new__(R.Link)
            link.tag, link.dev = 0, Device(status)
            with self.assertRaises(R.RecoveryError):
                link.transfer(b"x", incoming=16)
        link.tag, link.dev = 0, Device(struct.pack("<IIIB", 0x53425355, 1, 0, 0))
        with self.assertRaisesRegex(R.RecoveryError, "echo"):
            link.command(0xFC09)
        link.tag, link.dev = 0, Device(struct.pack("<IIIB", 0x53425355, 1, 0, 0))
        with self.assertRaisesRegex(R.RecoveryError, "Short USB data response"):
            link.transfer(b"x", incoming=32)


if __name__ == "__main__":
    unittest.main()
