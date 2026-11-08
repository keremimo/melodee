#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""tools/fm1_modules.py (what tools/fm1_install.py writes after an update) against a simulated FM-1's editor command 84
(firmware/src/editor_module.c, module_store.c): the build's modules into free slots, a current one left, another
firmware's or a damaged one replaced, a full storage, another build and full slots refused. With
build/melodee-mods.bin (tools/build.py) its real files are read as the device reads their heads."""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import fm1_modules as M  # noqa: E402

KEY = 0x13572468


def module(engine, name, n, key=KEY, seed=1):
    b = bytearray((i * 29 + seed) & 255 for i in range(60 + n))
    struct.pack_into("<IHBBI", b, 0, M.FILE_MAGIC, 3, engine, 0, len(b))
    struct.pack_into("<I", b, 28, 0x1000 * seed + engine)
    b[32:48] = name.encode().ljust(16, b"\0")
    struct.pack_into("<I", b, 52, key)
    return bytes(b)


class Device:
    """editor_module.c's ops on eight slots; commit takes an ABI 3 file of this firmware's legacy key (or none), room permitting"""
    def __init__(self, room=1 << 20):
        self.slot = [dict(b=None, state=0, up=None) for _ in range(8)]
        self.room, self.log = room, []

    def request(self, cmd, args, timeout=5):
        assert cmd == M.COMMAND
        a = list(args)
        u = lambda n: [(n >> s) & 127 for s in (0, 7, 14, 21, 28)]
        if a == [0]:
            out = [0, 0, 8]
            for s in self.slot:
                name = s["b"][32:48].split(b"\0")[0] if s["b"] else b""
                out += [s["state"], s["b"][6] if s["b"] else 0, 1, 0] + u(len(s["b"]) if s["b"] else 0) + list(name) + [0]
            return out
        s = self.slot[a[1]]
        if a[0] == 5:
            b = s["b"]
            abi = struct.unpack_from("<H", b, 4)[0] if b and len(b) >= 6 else 0
            return [0, s["state"], b[6] if b else 0, abi] + u(struct.unpack_from("<I", b, 28)[0] if b else 0) + \
                u(struct.unpack_from("<I", b, 52)[0] if abi >= 2 else 0) + u(KEY)
        if a[0] == 1:
            s["up"] = bytearray()
            s["want"] = sum(x << 7 * i for i, x in enumerate(a[2:7]))
            return [0]
        if a[0] == 2:
            data, p = bytearray(), a[5:]
            for i in range(0, len(p), 8):
                g = p[i:i + 8]
                data += bytes(x | ((g[0] >> j) & 1) << 7 for j, x in enumerate(g[1:]))
            s["up"] += data
            return [0]
        if a[0] == 3:
            b = bytes(s.pop("up"))
            if struct.unpack_from("<H", b, 4)[0] != 3 or struct.unpack_from("<I", b, 52)[0] not in (0, KEY):
                return [5, s["state"], 0]
            if sum(len(x["b"]) for x in self.slot if x["b"] and x is not s) + len(b) > self.room:
                return [2, s["state"], 0]
            s.update(b=b, state=1)
            self.log.append(f"{b[32:48].split(b'\0')[0].decode()} -> {a[1] + 1}")
            return [0, 1, b[6]]
        if a[0] == 4:
            s.update(b=None, state=0)
            return [0]
        return [1]


def main():
    bad = 0

    def check(what, ok):
        nonlocal bad
        print(f"{what:70s} {'ok' if ok else 'FAIL'}")
        bad += not ok

    quiet = lambda *a, **k: None
    mods = module(10, "DRUM", 3000) + module(3, "SID", 900)
    d = Device()
    check("bundle: the files and their heads", [(f["engine"], f["name"], f["abi"]) for f in M.bundle(mods)] ==
          [(10, "DRUM", 3), (3, "SID", 3)])
    check("an empty FM-1: both written into free slots", M.push(d, mods, quiet) == "written" and d.log == ["DRUM -> 1", "SID -> 2"])
    check("the very files there already: nothing written", M.push(d, mods, quiet) == "current" and len(d.log) == 2)
    d.slot[0].update(b=module(10, "DRUM", 2000, key=0x5555, seed=7), state=7)       # another firmware's DRUM
    d.slot[2].update(b=bytes(80), state=3)                                            # a damaged file
    check("another firmware's DRUM: replaced in its own slot, SID left", M.push(d, mods, quiet) == "written" and
          d.log[2:] == ["DRUM -> 1"])
    for k in (0, 1):
        d.slot[k].update(b=None, state=0)
    d.room = 3500
    try:
        M.push(d, mods, quiet)
        check("a full storage: refused", False)
    except RuntimeError as e:
        check("a full storage: refused (the damaged file's slot reused first)", "storage full" in str(e) and
              d.log[-1] == "DRUM -> 3")
    try:
        M.push(Device(), module(10, "DRUM", 100, key=0x2468), quiet)
        check("another build's modules: refused", False)
    except RuntimeError as e:
        check("another build's modules: refused before writing", "another firmware build" in str(e))
    full = Device()
    for s in full.slot:
        s.update(b=module(22, "X", 10), state=1)
    try:
        M.push(full, module(9, "P", 10), quiet)
        check("every slot taken: refused", False)
    except RuntimeError as e:
        check("every slot taken: refused", "every synth module slot" in str(e))
    real = ROOT / "build/melodee-mods.bin"
    if real.exists():
        files = M.bundle(real.read_bytes())
        check("build/melodee-mods.bin: the ABI 3 CZ-1, SID, DRUM, FM6 and PROPHET files (DRUM's pads key alone)",
              sorted(f["name"] for f in files) == ["CZ-1", "DRUM", "FM6", "PROPHET", "SID"] and
              all(f["abi"] == 3 for f in files) and [f["name"] for f in files if f["key"]] == ["DRUM"])
    print("fm1_modules tests", "FAILED" if bad else "passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
