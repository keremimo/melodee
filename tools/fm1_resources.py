#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Write the resource region (firmware/src/res.c: Rubik, the factory sounds, the radio program) into the
connected FM-1 through USB editor command 82. tools/fm1_install.py runs this after an update when
melodee-res.bin sits beside the package.
  fm1_resources.py build/melodee-res.bin [--port NAME]"""
import argparse
import struct
import sys
from pathlib import Path

COMMAND = 82
RC = {1: "bad request", 2: "no flash", 3: "flash error", 4: "busy: stop playback and switch Bluetooth off", 7: "check failed"}


def pack7(data):
    out = []
    for i in range(0, len(data), 7):
        group = data[i:i + 7]
        out += [sum((x >> 7) << j for j, x in enumerate(group))] + [x & 127 for x in group]
    return out


def status(device, args, timeout=5):
    b = device.request(COMMAND, args, timeout)
    if len(b) != 12:
        raise RuntimeError("this firmware has no resource command (82)")
    r35 = lambda at: sum(b[at + i] << 7 * i for i in range(5)) & 0xFFFFFFFF
    return dict(rc=b[0], installed=bool(b[1]), length=r35(2), crc=r35(7))


def push(device, region, say=print):
    """'current' when the FM-1 has these resources already, 'written' after a verified write"""
    magic, length, crc = struct.unpack_from("<3I", region)
    if magic != 0x3153524D:
        raise RuntimeError("not a Melodee resource file")
    payload = region[32:32 + length]
    now = status(device, [0])
    if now["installed"] and now["crc"] == crc:
        return "current"
    if (now["length"], now["crc"]) != (length, crc):
        raise RuntimeError(f"these resources ({crc:08x}) belong to another build than the FM-1 runs ({now['crc']:08x})")
    r = status(device, [1], timeout=30)
    if r["rc"]:
        raise RuntimeError(f"begin refused: {RC.get(r['rc'], r['rc'])}")
    for off in range(0, length, 256):
        r = status(device, [2, off & 127, off >> 7 & 127, off >> 14 & 127] + pack7(payload[off:off + 256]))
        if r["rc"]:
            raise RuntimeError(f"write at {off} refused: {RC.get(r['rc'], r['rc'])}")
        say(f"\rresources {min(length, off + 256)}/{length}", end="", flush=True)
    say("")
    r = status(device, [3], timeout=10)
    if r["rc"] or not r["installed"]:
        raise RuntimeError(f"commit refused: {RC.get(r['rc'], r['rc'])}")
    return "written"


def find_port(part=None):
    import mido
    names = [n for n in mido.get_output_names() if (part or "Melodee").lower() in n.lower()]
    if not names:
        raise RuntimeError(f"no MIDI port matching {part or 'Melodee'}")
    return names[0]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("resources", type=Path, help="build/melodee-res.bin")
    ap.add_argument("--port", metavar="NAME", help="MIDI port (part of its name; default Melodee)")
    a = ap.parse_args(argv)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from prophet_device import Device
    device = Device(find_port(a.port))
    try:
        print(f"resources: {push(device, a.resources.read_bytes())}")
    except RuntimeError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    finally:
        device.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
