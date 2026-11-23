#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Write a build's synth modules (ABI 3: firmware/modules/shipped.h's) into the connected FM-1's module slots
through USB editor command 84, as the web installer does (web/fm1mods.js): pool records, first come first served like
projects and preset banks. A slot holding the very file stays; one holding a module of the same synth (its ID: another
firmware's, damaged) is emptied and written again. tools/fm1_install.py runs this after the resources when
melodee-mods.bin sits beside the package.
  fm1_modules.py build/melodee-mods.bin [--port NAME]"""
import argparse
import struct
import sys
from pathlib import Path

COMMAND = 84
FILE_MAGIC = 0x31444D4D
RC = {1: "bad request", 2: "storage full: delete a project or kit", 3: "flash error", 4: "busy: stop playback",
      5: "not a module for this firmware", 6: "no handle left for its synth", 7: "no room in RAM (stored)"}
PLAYING = (1, 2, 4, 6)                               # module_store.c MS_LOADED, MS_STORED, MS_NORAM, MS_SKIPPED
BAD = 3


def pack7(data):
    out = []
    for i in range(0, len(data), 7):
        group = data[i:i + 7]
        out += [sum((x >> 7) << j for j, x in enumerate(group))] + [x & 127 for x in group]
    return out


def u35(n):
    return [(n >> s) & 127 for s in (0, 7, 14, 21, 28)]


def r35(b, at):
    return sum(b[at + i] << 7 * i for i in range(5)) & 0xFFFFFFFF


def r70(b, at):
    """a synth's ID (u70: 10 x 7 bits, low first) as 16 hex digits"""
    return "%016x" % (sum(b[at + j] << 7 * j for j in range(10)) & 0xFFFFFFFFFFFFFFFF)


def file_id(b):
    """a file's ID from its IDNT chunk (version 2) as 16 hex digits, "" none (web/fm1mods.js mod_id)"""
    if not b[7] & 1:
        return ""
    at = 60 + struct.unpack_from("<I", b, 12)[0] + 4 * struct.unpack_from("<I", b, 20)[0]
    while at + 8 <= len(b):
        tag, n = struct.unpack_from("<II", b, at)
        if tag == 0x544E4449 and n == 96 and b[at + 16] == 2:
            return "%016x" % struct.unpack_from("<Q", b, at + 8)[0]
        at += 8 + ((n + 3) & ~3)
    return ""


# Melodee's own synths a later build no longer ships (2.0: DRUM split into the kit synths): an update empties their slots
RETIRED = {"241aacc57d998ffe"}                           # DRUM


def same(s, f):
    """a slot holding a file's synth: by their IDs"""
    return bool(s["id"]) and s["id"] == f["id"]          # (2.0: engine numbers are the device's handles)


def bundle(data):
    """the .mmod files of a melodee-mods.bin: [{engine, name, abi, crc, key, bytes}]"""
    out, at = [], 0
    while at < len(data):
        if len(data) - at < 56:
            raise RuntimeError("the synth module file is cut short")
        magic, abi, engine, _, total = struct.unpack_from("<IHBBI", data, at)
        if magic != FILE_MAGIC or total < 56 or at + total > len(data):
            raise RuntimeError("not a Melodee synth module file")
        b = data[at:at + total]
        out.append(dict(engine=engine, abi=abi, name=b[32:48].split(b"\0")[0].decode("ascii", "replace"),
                        crc=struct.unpack_from("<I", b, 28)[0], key=struct.unpack_from("<I", b, 52)[0] if abi >= 2 else 0,
                        id=file_id(b), bytes=b))
        at += total
    return out


def slots(device):
    """the device's slots (op 0, each file through op 5) and its own layout key"""
    a = device.request(COMMAND, [0], 5)
    if len(a) < 3 or a[0]:
        raise RuntimeError("this firmware has no synth module command (84)")
    out, p, own = [], 3, None
    for k in range(a[2]):
        out.append(dict(slot=k, state=a[p], engine=a[p + 1], len=r35(a, p + 4), crc=0, key=0, id=""))
        p += 9
        while a[p]:
            p += 1
        p += 1
    for s in out + ([] if any(s["len"] for s in out) else [dict(slot=0, len=1, probe=True)]):
        if not s["len"]:
            continue
        b = device.request(COMMAND, [5, s["slot"]], 5)
        if len(b) not in (19, 30) or b[0]:              # 30: since its synths have IDs (ID u70, signer)
            raise RuntimeError("this firmware cannot report its synth modules")
        own = r35(b, 14)
        if not s.get("probe"):
            s["crc"], s["key"], s["id"] = r35(b, 4), r35(b, 9), r70(b, 19) if len(b) == 30 else ""
    return out, own


def push(device, data, say=print):
    """'current' when the FM-1 has every module already, 'written' after the missing ones were written"""
    files = bundle(data)
    if not files:
        return "current"
    lst, own = slots(device)
    if any(f["abi"] >= 2 and f["key"] and f["key"] != own for f in files):
        raise RuntimeError("these synth modules belong to another firmware build than the FM-1 runs")
    removed = 0
    for s in lst:                                        # (a retired synth of Melodee's: its slot emptied)
        if s["len"] and s["id"] in RETIRED:
            rc = device.request(COMMAND, [4, s["slot"]], 10)[0]
            if rc:
                raise RuntimeError(f"slot {s['slot'] + 1}: delete refused: {RC.get(rc, rc)}")
            s.update(len=0, engine=0, id="", state=0)
            removed += 1
    todo = [f for f in files if not any(same(s, f) and s["state"] in PLAYING and s["crc"] == f["crc"]
                                        and s["key"] == f["key"] for s in lst)]
    if not todo:
        return "written" if removed else "current"
    for f in todo:
        s = (next((x for x in lst if x["len"] and same(x, f)), None) or
             next((x for x in lst if x["len"] and x["state"] == BAD), None) or next((x for x in lst if not x["len"]), None))
        if s is None:
            raise RuntimeError("every synth module slot is in use: delete one in the editor (Settings)")
        k = s["slot"]
        if s["len"]:
            rc = device.request(COMMAND, [4, k], 10)[0]
            if rc:
                raise RuntimeError(f"slot {k + 1}: delete refused: {RC.get(rc, rc)}")
            s.update(len=0, engine=0, id="", state=0)
        b = f["bytes"]
        rc = device.request(COMMAND, [1, k] + u35(len(b)), 10)[0]
        for off in range(0, len(b), 256):
            if rc:
                break
            rc = device.request(COMMAND, [2, k, off & 127, off >> 7 & 127, off >> 14 & 127] + pack7(b[off:off + 256]), 10)[0]
            say(f"\rsynth module {f['name']} {min(len(b), off + 256)}/{len(b)}", end="", flush=True)
        say("")
        if rc:
            raise RuntimeError(f"{f['name']}: write refused: {RC.get(rc, rc)}")
        c = device.request(COMMAND, [3, k], 30)
        if c[0] and c[0] != 7:
            raise RuntimeError(f"{f['name']}: commit refused: {RC.get(c[0], c[0])}")
        s.update(len=len(b), engine=f["engine"], id=f["id"], state=c[1])
    return "written"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("modules", type=Path, help="build/melodee-mods.bin")
    ap.add_argument("--port", metavar="NAME", help="MIDI port (part of its name; default Melodee)")
    a = ap.parse_args(argv)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from prophet_device import Device
    from fm1_resources import find_port
    device = Device(find_port(a.port))
    try:
        print(f"synth modules: {push(device, a.modules.read_bytes())}")
    except RuntimeError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    finally:
        device.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
