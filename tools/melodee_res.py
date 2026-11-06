#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""The resource region (firmware/src/res.c) for the fm1-base build: tools/build.py's resources(), called by the base's
build after its link (tools/base_overlay.py): .mres as installed after an update, platform/build/melodee-res.bin =
the 32-byte header + the payload; this build's length and CRC-32 patched into res_want in the image."""
import struct
import zlib

BOARD_RES = {"fm1": (0xCB000, 21), "smk37": (0x9F000, 16)}   # the region per board (hal/fm1_flash_map.h FL_RES)


def patch(img, syms, out, app_xip, board="fm1"):
    import re

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    base, sectors = BOARD_RES[board]
    payload = (out / "mres.bin").read_bytes() if (out / "mres.bin").exists() else b""
    crc = zlib.crc32(payload)
    at = symv("res_want") - app_xip
    if img[at:at + 4] != struct.pack("<I", 0x544E5752):
        raise SystemExit("res_want: no RWNT marker in the image")
    img[at + 4:at + 12] = struct.pack("<II", len(payload), crc)
    head = struct.pack("<7I", 0x3153524D, len(payload), crc, 0, 0, 0, 0)
    region = head + struct.pack("<I", zlib.crc32(head)) + payload
    if len(region) > sectors * 4096:
        raise SystemExit(f"resources: {len(region)} B over the {sectors * 4096} B region")
    # the SPL and the update loader act on a sector whose last 256 bytes are an update record (ota.c UPDATA_PARM)
    for s in range(0, len(region), 4096):
        tail = region[s + 0xF00:s + 0xF08]
        if len(tail) == 8 and tail[2:8] == struct.pack("<3H", 0x5A0D, 0x5A01, 0x5441):
            raise SystemExit(f"resources: sector {base + s:#x}'s tail reads as an update record")
    (out / "melodee-res.bin").write_bytes(region)
    print(f"resources: {len(payload)} B at {base + 32:#x} ({-(-len(region) // 4096)} of {sectors} sectors), "
          f"crc {crc:08x} -> {out / 'melodee-res.bin'}")
