#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Recover an FM-1 already in WL82 ROM UBOOT over direct USB.

  fm1_recover.py --info
  fm1_recover.py PACKAGE.fwsc --check
  fm1_recover.py PACKAGE.fwsc --loader wl82loader.bin
  fm1_recover.py PACKAGE.fwsc --loader wl82loader.bin --write

Without --write, the connected-device operation only loads a temporary RAM
loader and backs up flash. Source runs require PyUSB and libusb; standalone
executables bundle them. The RAM helper is included; --check needs no USB.
The USB BOT/LoaderV2 protocol is documented by FM-1-transporter:
https://github.com/kurogedelic/FM-1-transporter
"""
import argparse
import binascii
import ctypes.util
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
import time

from fm1_install import STOCK_V15_SHA256, logical_image, product_of
from fm1pkg_make import chipkey_decode, crc16, enc, sfc

APP_LO, APP_HI, FLASH_SIZE, SECTOR = 0x4000, 0x93000, 0x100000, 4096
VERSION = "1.3.1"
LOADER_SHA256 = "d9de9f1c81a3bb52998e5bced678f0ca851223014a7d43965b03f0719a44eb55"
LOADER_ADDRESS = 0x1C02000


def bundled_file(name):
    root = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
    path = root / name
    return path if path.is_file() else None


def default_loader():
    return bundled_file("wl82loader.bin") or Path(__file__).resolve().parents[1] / "assets/recovery/wl82loader.bin"


class RecoveryError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise RecoveryError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


class Package:
    def __init__(self, path):
        self.path = Path(path).resolve()
        raw = self.path.read_bytes()
        self.product = product_of(raw)
        require(re.fullmatch(r"FM-1_\d+", self.product), "Package is not for an FM-1")
        self.digest = sha256(raw)
        self.stock_v15 = self.product == "FM-1_015"
        if self.stock_v15:
            require(self.digest == STOCK_V15_SHA256,
                    "Official V15 package differs from the verified M-VAVE FM-1.fwsc; download the unmodified original")
        image = logical_image(raw)
        header = bytearray(image[:64])
        enc(header, 0, 64)
        require(crc16(header[2:]) == struct.unpack_from("<H", header)[0], "Bad package header checksum")
        total, count = struct.unpack_from("<IH", header, 4)
        require(total == len(image) and 1 <= count <= 8 and header[16:22] == b"AC791N",
                "Wrong chip or invalid package extent")
        entries = image[64:64 + count * 80]
        require(len(entries) == count * 80 and crc16(entries) == struct.unpack_from("<H", header, 2)[0],
                "Bad package entry-list checksum")
        self.flash = None
        ranges = []
        for i in range(count):
            entry = bytearray(entries[i * 80:(i + 1) * 80])
            enc(entry, 0, 80)
            kind, _, check, _, off, size, padded = struct.unpack_from("<HHHHIII", entry)
            # Genuine V15 includes an empty info.log and auxiliary vendor entries
            # whose checksum fields are not plain-data CRCs. Its pinned whole-file
            # SHA-256 authenticates those bytes; Melodee keeps the strict CRC path.
            require(off >= 0x400 and (size > 0 or self.stock_v15) and size <= padded and
                    off + padded <= ((total + 31) & ~31),
                    "Invalid package entry extent")
            require(off + size <= total and all(off + padded <= lo or off >= hi for lo, hi in ranges),
                    "Overlapping or truncated package entries")
            ranges.append((off, off + padded))
            data = image[off:off + size]
            if not self.stock_v15 or kind in (0, 100):
                require(crc16(data) == check, "Bad package data checksum")
            if kind == 0:
                require(self.flash is None and size == APP_HI and entry[64:].split(b"\0", 1)[0] == b"flash.bin",
                        "Expected one flash.bin with application boundary 0x93000")
                self.flash = data
        require(self.flash is not None, "Package has no flash.bin")
        self.key = None
        for off in range(32, 0x400, 32):
            entry = bytearray(self.flash[off:off + 32])
            enc(entry, 0, 32)
            require(crc16(entry[2:]) == struct.unpack_from("<H", entry)[0], "Bad flash-head entry checksum")
            if entry[16:].split(b"\0", 1)[0] == b"isd_config.ini":
                address, size = struct.unpack_from("<II", entry, 4)
                require(size >= 34 and address >= 0x400 and address + size <= APP_LO,
                        "Invalid chip-key configuration extent")
                blob = self.flash[address:address + size]
                require(crc16(blob) == struct.unpack_from("<H", entry, 2)[0] and
                        crc16(blob[:32]) == struct.unpack_from("<H", blob, 32)[0], "Bad chip-key configuration checksum")
                self.key = chipkey_decode(blob[:32])
                break
            if struct.unpack_from("<H", entry, 14)[0]:
                break
        require(self.key is not None, "Package has no chip-key configuration")
        app = bytearray(self.flash[APP_LO:APP_LO + 32])
        sfc(app, 0, 32, 0, self.key)
        require(crc16(app[2:]) == struct.unpack_from("<H", app)[0], "Application header does not match package chip key")


def cipher(data):
    result = bytearray(data)
    magic = bytes.fromhex("c3cfc0e8ced2b0aec4e3a3acd3f1c1d6")
    state = binascii.crc_hqx(b"\xff\xff", 0xFFFF)
    for i in range(len(result)):
        state = binascii.crc_hqx(magic[i % 16:i % 16 + 1], state)
        result[i] ^= state & 255
    return bytes(result)


def read_loader(path):
    blob = Path(path).read_bytes()
    require(len(blob) == 30720 and sha256(blob) == LOADER_SHA256,
            "Unsupported wl82loader.bin; use the unmodified AC79 SDK recovery loader documented in BUILDING.md")
    return blob


def durable_write(path, data):
    # Exclusive creation avoids replacing any previous backup. Sync before erasing.
    with Path(path).open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    require(Path(path).read_bytes() == data, f"Backup read-back failed: {path}")


class Link:
    def __init__(self, wait=15, library=None, bus=None, address=None):
        try:
            import usb.core
            import usb.util
            import usb.backend.libusb1
        except ImportError as error:
            raise RecoveryError("Install PyUSB: python -m pip install pyusb") from error
        self.util = usb.util
        if not library:
            name = "libusb-1.0.dll" if sys.platform == "win32" else "libusb-1.0.dylib" if sys.platform == "darwin" else "libusb-1.0.so.0"
            bundled = bundled_file(name)
            library = str(bundled) if bundled else ctypes.util.find_library("usb-1.0")
            if not library and sys.platform == "darwin":
                library = next((str(p) for p in (Path("/opt/homebrew/lib/libusb-1.0.dylib"),
                                               Path("/usr/local/lib/libusb-1.0.dylib")) if p.exists()), None)
        backend = usb.backend.libusb1.get_backend(find_library=lambda _: library)
        require(backend is not None, "libusb not found; install libusb or supply --libusb PATH (see BUILDING.md)")
        deadline = time.monotonic() + wait
        print("Waiting for WL82 ROM UBOOT USB (4c4a:8057)...", flush=True)
        while True:
            devices = [d for d in usb.core.find(find_all=True, idVendor=0x4C4A, idProduct=0x8057, backend=backend)
                       if (bus is None or d.bus == bus) and (address is None or d.address == address)]
            require(len(devices) <= 1, "Multiple ROM devices found; connect one FM-1 or use --bus and --address")
            if devices:
                self.dev = devices[0]
                break
            require(time.monotonic() < deadline,
                    "ROM UBOOT device not found. This tool needs 4c4a:8057, not the Melodee Update MIDI port")
            time.sleep(0.25)
        self.tag = 0
        try:
            try:
                active = self.dev.is_kernel_driver_active(0)
            except NotImplementedError:  # WinUSB has no kernel-detach operation.
                active = False
            if active:
                self.dev.detach_kernel_driver(0)
            usb.util.claim_interface(self.dev, 0)
        except Exception:
            self.close()
            raise

    def transfer(self, cdb, incoming=0, outgoing=None):
        require(len(cdb) <= 16 and not (incoming and outgoing is not None), "Invalid USB transfer")
        self.tag = self.tag % 0xFFFFFFFF + 1
        size = incoming if outgoing is None else len(outgoing)
        cbw = struct.pack("<IIIBBB", 0x43425355, self.tag, size, 0x80 if incoming else 0, 0, len(cdb))
        cbw += cdb + bytes(16 - len(cdb))
        require(self.dev.write(1, cbw, timeout=5000) == 31, "Short USB command write")
        if outgoing is not None:
            require(self.dev.write(1, outgoing, timeout=5000) == len(outgoing), "Short USB data write")
        data = bytes(self.dev.read(0x81, incoming, timeout=5000)) if incoming else b""
        csw = bytes(self.dev.read(0x81, 13, timeout=5000))
        require(len(csw) == 13, "Short USB status response")
        signature, tag, residue, status = struct.unpack("<IIIB", csw)
        require(signature == 0x53425355 and tag == self.tag and not status and not residue,
                f"USB command {cdb[:2].hex()} failed: status={status}, residue={residue}, tag={tag}")
        require(len(data) == incoming, "Short USB data response")
        return data

    def command(self, cmd, args=b"", incoming=None, outgoing=None):
        cdb = cmd.to_bytes(2, "big") + args
        require(len(cdb) <= 16, "Invalid USB command arguments")
        cdb += b"\xff" * (16 - len(cdb))
        if outgoing is not None:
            return self.transfer(cdb, outgoing=outgoing)
        if incoming is not None:
            return self.transfer(cdb, incoming=incoming)
        data = self.transfer(cdb, incoming=16)
        require(data[:2] == cmd.to_bytes(2, "big"), "Wrong USB command echo")
        return data[2:]

    def identify(self):
        inquiry = self.transfer(bytes([0x12, 0, 0, 0, 36, 0]), incoming=36)
        require(inquiry[8:16].strip() == b"WL82" and inquiry[16:32].strip() == b"UBOOT1.00",
                "Unexpected ROM identity; refusing recovery")
        return {"vendor": "WL82", "product": "UBOOT1.00"}

    def load(self, blob):
        for off in range(0, len(blob), 512):
            chunk = blob[off:off + 512]
            params = struct.pack(">IH", LOADER_ADDRESS + off, len(chunk)) + b"\0" + struct.pack("<H", crc16(chunk))
            self.command(0xFB06, params, outgoing=chunk)
        self.command(0xFB08, struct.pack(">IH", LOADER_ADDRESS, 1))
        key_data = self.command(0xFC09, struct.pack(">I", 0xAC6900))
        key = int.from_bytes(cipher(key_data[4:6][::-1]), "little")
        info = self.command(0xFC0A)
        require(info[0] == 3 and int.from_bytes(info[2:6], "little") == 0x856014,
                "Unexpected flash type or capacity; refusing recovery")
        return key

    def read_flash(self, address, length):
        require(0 <= address <= FLASH_SIZE and 0 <= length <= FLASH_SIZE - address, "Flash read out of bounds")
        data = bytearray()
        for off in range(0, length, 512):
            n = min(512, length - off)
            data += self.command(0xFD05, struct.pack(">IH", address + off, n), incoming=n)
            if length == FLASH_SIZE and off % 0x20000 == 0:
                print(f"Reading flash: {off * 100 // length}%", flush=True)
        return bytes(data)

    def write_sector(self, address, data):
        require(APP_LO <= address < APP_HI and address % SECTOR == 0 and len(data) == SECTOR,
                "Refusing write outside the application region")
        self.command(0xFB01, struct.pack(">I", address))
        for off in range(0, SECTOR, 512):
            chunk = data[off:off + 512]
            params = struct.pack(">IH", address + off, len(chunk)) + b"\0" + struct.pack("<H", crc16(chunk))
            self.command(0xFB04, params, outgoing=chunk)
        require(self.read_flash(address, SECTOR) == data, f"Sector verification failed at {address:#x}")

    def close(self):
        self.util.dispose_resources(self.dev)


def recover(link, package, loader, output, write=False):
    identity = link.identify()
    print("Connected to WL82 / UBOOT1.00. Loading recovery program into RAM...", flush=True)
    crash = cipher(link.command(0xFD07, struct.pack(">IH", 0x1C7C000, 512), incoming=512))
    durable_write(output / "retained-ram.bin", crash)
    key = link.load(loader)
    require(key == package.key, f"Chip-key mismatch: device {key:#06x}, package {package.key:#06x}; no flash writes")
    before = link.read_flash(0, FLASH_SIZE)
    require(len(before) == FLASH_SIZE, "Incomplete flash backup; refusing recovery")
    backup = output / "full-flash-before.bin"
    durable_write(backup, before)
    print(f"Full flash backup verified on disk: {backup}", flush=True)
    expected = before[:APP_LO] + package.flash[APP_LO:] + before[APP_HI:]
    sectors = [a for a in range(APP_LO, APP_HI, SECTOR) if before[a:a + SECTOR] != expected[a:a + SECTOR]]
    report = {"package": str(package.path), "product": package.product, "package_sha256": package.digest,
              "device": identity, "chip_key": f"{key:#06x}", "full_backup": str(backup.resolve()),
              "backup_sha256": sha256(before), "application_sectors_differ": len(sectors), "write_requested": write}
    durable_write(output / "backup.json", (json.dumps(report, indent=2) + "\n").encode())
    print(f"{len(sectors)} application sectors differ; protected regions will be retained.", flush=True)
    if not write:
        print("Backup complete. No flash changes. Power-cycle before using USB again; add --write to recover.")
        return report
    for index, address in enumerate(sectors):
        link.write_sector(address, expected[address:address + SECTOR])
        print(f"Restored and verified {index + 1}/{len(sectors)} sectors ({address:#x}).", flush=True)
    final = link.read_flash(0, FLASH_SIZE)
    require(final == expected, "Final full-flash verification failed; keep the backup and retry recovery")
    report.update(restored_sectors=len(sectors), full_flash_verified=True,
                  bootloader_unchanged=final[:APP_LO] == before[:APP_LO],
                  saved_data_unchanged=final[APP_HI:] == before[APP_HI:])
    durable_write(output / "recovery-result.json", (json.dumps(report, indent=2) + "\n").encode())
    print("RESTORE VERIFIED. Bootloader and saved data unchanged. Power-cycle the FM-1 to start it.")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", action="version", version=f"FM-1 Recovery {VERSION}")
    parser.add_argument("package", nargs="?", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--info", action="store_true", help="identify ROM USB only; no RAM loader or flash writes")
    mode.add_argument("--check", action="store_true", help="validate inputs offline; no USB access")
    mode.add_argument("--write", action="store_true", help="back up, restore application, and verify the entire flash")
    parser.add_argument("--loader", type=Path, help="override the bundled AC79 SDK RAM recovery loader")
    parser.add_argument("--backup-dir", type=Path, default=Path("fm1-recovery-backups"), help="parent for a new session directory")
    parser.add_argument("--libusb", help="explicit libusb shared-library path")
    parser.add_argument("--bus", type=int, help="USB bus when multiple devices are connected")
    parser.add_argument("--address", type=int, help="USB address when multiple devices are connected")
    parser.add_argument("--wait", type=float, default=15, help="seconds to wait for ROM USB (default 15)")
    args = parser.parse_args(argv)
    if not args.info and args.package is None:
        parser.error("PACKAGE.fwsc is required unless using --info")
    if args.info and (args.package or args.loader):
        parser.error("--info does not take a package or loader")
    if not 0 <= args.wait <= 60:
        parser.error("--wait must be between 0 and 60 seconds")
    link = None
    try:
        package = Package(args.package) if args.package else None
        if package:
            print(f"Package: {package.product}; SHA-256 {package.digest}")
            if package.stock_v15:
                print("Verified official M-VAVE V15. Recovery preserves saved-data bytes; stock firmware may change them after boot.")
        loader_path = args.loader
        if not args.info and not loader_path and os.environ.get("AC79_SDK"):
            loader_path = Path(os.environ["AC79_SDK"]) / "cpu/wl82/tools/wl82loader.bin"
        if not args.info and not loader_path:
            loader_path = default_loader()
        loader = read_loader(loader_path) if loader_path else None
        if args.check:
            print("Package checksums, chip key and application boundary verified." +
                  (" Recovery loader checksum verified." if loader else ""))
            return 0
        require(args.info or loader is not None, "Supply --loader wl82loader.bin or set AC79_SDK (see BUILDING.md)")
        link = Link(args.wait, args.libusb, args.bus, args.address)
        if args.info:
            print(json.dumps(link.identify(), indent=2))
            return 0
        args.backup_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-")
        output = Path(tempfile.mkdtemp(prefix=stamp, dir=args.backup_dir)).resolve()
        print(f"Recovery session: {output}", flush=True)
        recover(link, package, loader, output, args.write)
        return 0
    except (Exception, KeyboardInterrupt) as error:
        print(f"Recovery stopped: {error}", file=sys.stderr)
        print("No further writes attempted. Keep any backup; if flash writing began, leave the device in UBOOT and retry."
              " USB access errors may need OS permissions/drivers; see BUILDING.md.", file=sys.stderr)
        return 1
    finally:
        if link is not None:
            try:
                link.close()
            except Exception as error:
                print(f"USB cleanup: {error}", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
