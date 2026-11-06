#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""An SMK-37 package from fm1pkg_make.py --board smk37: flash.bin is the stock PRCT size (0x9D000) and
its app area carries the stock v016 region table exactly, so the SDK
and a later stock rollback see the same VM / BTIF / USRTRIM / USRFLASH / USR regions. The app area's tail from
0x93000 (firmware/src/smkx.c): it decrypts to the app's smkx_want (length and CRC-32; --tail: to exactly that file),
nothing past it; the packager refuses a tail on the FM-1 and one over 0xA000 B.
  smk_pkg_test.py PACKAGE.fwsc [PRODUCT] [--tail TAIL.bin]"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402
import fm1pkg_make as M  # noqa: E402

failed = 0


def ok(cond, what):
    global failed
    print(f"smk pkg: {what:<70} {'ok' if cond else 'FAIL'}")
    failed += not cond


ap = argparse.ArgumentParser()
ap.add_argument("package", type=Path)
ap.add_argument("product", nargs="?", default="SMK-37 Pro_900")
ap.add_argument("--tail", type=Path, help="the tail the package was made with (a build of the FM-1's app)")
args = ap.parse_args()
raw = args.package.read_bytes()
want_product = args.product
image, n = I.deinterleave(raw)
ok(n == 20 and I.product_of(raw) == want_product and not I.package_damage(image),
   f"identity {I.product_of(raw)}, 20 blocks, CRCs ok")
hdr = I._dec(image[:0x40])
e0 = I._dec(image[0x40:0x90])
off, size = struct.unpack_from("<II", e0, 8)
ok(struct.unpack_from("<H", e0, 0)[0] == 0 and size == 0x9D000, f"flash.bin {size:#x} B (stock PRCT 0x9D000)")
flash = bytearray(image[off:off + size])
area = bytearray(flash[0x4000:0x4000 + 0x400])
M.sfc(area, 0, len(area), 0, M.KEY)
ents = []
for k in range(1, 12):
    e = bytes(area[k * 32:(k + 1) * 32])
    if M.crc16(e[2:]) != struct.unpack_from("<H", e)[0]:
        break
    dcrc, eoff, esize, flags, resvd, index = struct.unpack_from("<HIIBBH", e, 2)
    ents.append((eoff, esize, flags, resvd, index, e[16:32]))
want = [(0x9D000, 0x23000, 0x12, 0x81, 0, bytes.fromhex("564d00ffffffffffffffffff00800000")),
        (0, 0x9D000, 0x92, 0x82, 0, bytes.fromhex("5052435400ffffffffffffff434f4445")),
        (0xC0000, 0x1000, 0x92, 0x82, 0, bytes.fromhex("4254494600ffffffffffffff4155544f")),
        (0xC1000, 0x1000, 0x92, 0x81, 0, bytes.fromhex("5553525452494d00ffffffffffffffff")),
        (0xC2000, 0x29000, 0x92, 0x81, 0, bytes.fromhex("555352464c41534800ffffffffffffff")),
        (0xF4000, 0xA000, 0x92, 0x81, 1, b"USR\0" + b"\xff" * 12)]
ok([e[5][:7] for e in ents[:2]] == [b"app.bin", b"cfg_too"], "app.bin, cfg_tool.bin first")
ok(ents[2:] == want, "region table == stock v016 (VM, PRCT, BTIF, USRTRIM, USRFLASH, USR)")
ok(flash[:0x4000] != b"\xff" * 0x4000, "head present (never written by the loader, checked against the device)")

# the app area's tail, decrypted as XIP reads it (the SFC block key from the offset in the app area)
dec = bytearray(flash[0x4000:])
M.sfc(dec, 0, len(dec), 0, M.KEY)
app, tail_at = dec[0x120:0x120 + M.APP_SLOT], M.TAIL_LO - 0x4000
wants = M.tail_wants(bytes(app))
if args.tail:
    tail = args.tail.read_bytes()
    ok(0 < len(tail) and dec[tail_at:tail_at + len(tail)] == tail, f"tail: {len(tail)} B at {M.TAIL_LO:#x} decrypt to the file")
elif wants:
    tail = next((bytes(dec[tail_at:tail_at + n]) for n, c in wants
                 if 0 < n <= size - M.TAIL_LO and zlib.crc32(dec[tail_at:tail_at + n]) == c), None)
    ok(tail is not None, f"tail: {len(tail or b'')} B at {M.TAIL_LO:#x} decrypt to the app's smkx_want (length, CRC-32)")
else:
    tail = b""                                                  # (an app from before the tail: it wants none)
end = M.TAIL_LO + len(tail or b"")
ok(flash[end:size] == b"\xff" * (size - end), f"tail: blank from {end:#x} to the PRCT end")
for board, data, what in (("fm1", b"\0", "on the FM-1"), ("smk37", bytes(size - M.TAIL_LO + 1), "over 0xA000 B")):
    M.set_board(board)
    try:
        M.flash_image(bytes(64), M.KEY, tail=data)
        refused = False
    except SystemExit:
        refused = True
    ok(refused, f"fm1pkg_make: a tail {what} is refused")
M.set_board("smk37")
print("SMK PACKAGE TEST FAILED" if failed else "smk package test passed")
sys.exit(1 if failed else 0)
