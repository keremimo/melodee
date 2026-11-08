#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""A synth module's factory presets as the PRST chunk of its .mmod file (firmware/src/module_presets.h): the browser's
index (names, categories) and every preset's bytes (the 16-byte common block, then the synth's own patch), each entry
packed on its own for dsp.c lz_unpack against one dictionary placed before it.

    tools/mod_presets.py --dump ENGINE         (the presets of a built-in engine, as the host firmware makes them)
    tools/mod_presets.py --check file.mmod     (a module's PRST chunk: CRC, index, every entry unpacked)

tools/mod_build.py builds the chunk: it compiles tools/mod_presets.c with the host's C compiler against the host
firmware (the engines built in, their preset makers: what the firmware made before the presets left it), runs it for
the module's engine number and packs what it prints."""
import os
import struct
import subprocess
import sys
import zlib
from collections import deque
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent
TAG = 0x54535250                                # "PRST"
VERSION = 1
HEAD = struct.Struct("<HHHHHHI")                # version, count, init, dict, ulen, rsv, crc (16 bytes)
REC = struct.Struct("<IHBB")                    # entry offset, name offset, category, flags (8 bytes)
COMMON = 16                                     # e[8], env[4], fenv, mono, 0, 0
BUF = 640                                       # module_presets.h MOD_PST_BUF: the dictionary, one entry unpacked and
                                                # its packed bytes
WINDOW = 256                                    # MOD_PST_WIN: an entry's packed bytes, at most
NAME_MAX = 20
NONE = 0xFFFF


def lz_pack(t, start=0):
    """t[start:] in dsp.c lz_unpack's format (c < 0x80: c + 1 literals follow; c >= 0x80: (c & 0x7f) + 3 bytes copied
    from d + 1 back, d in the next two bytes), t[:start] a dictionary already in place before the output. The
    shortest coding (every literal run and every match length considered), not a greedy one"""
    n = len(t)
    heads = {}
    longest = [0] * (n + 1)
    dist = [0] * (n + 1)
    for i in range(n - 2):
        key = t[i:i + 3]
        if i >= start:
            best = 0
            for s in reversed(heads.get(key, ())):
                if i - s > 65536:
                    break
                k = 3
                while k < 130 and i + k < n and t[s + k] == t[i + k]:
                    k += 1
                if k > best:
                    best, dist[i] = k, i - s
                    if k == 130:
                        break
            longest[i] = best
        heads.setdefault(key, []).append(i)
    INF = 1 << 30
    cost = [INF] * (n + 1)
    how = [None] * (n + 1)
    cost[start] = 0
    win = deque()                                       # places i < j, cost[i] - i increasing (a run's start)
    for j in range(start, n + 1):
        while win and win[0] < j - 128:
            win.popleft()
        if win and cost[win[0]] - win[0] + j + 1 < cost[j]:   # a run of literals from win[0] to j
            i = win[0]
            cost[j], how[j] = cost[i] - i + j + 1, (i, 0, 0)
        c = cost[j]
        if j == n or c == INF:
            continue
        for k in range(3, longest[j] + 1):              # a copy of k bytes
            if c + 3 < cost[j + k]:
                cost[j + k], how[j + k] = c + 3, (j, k, dist[j])
        while win and cost[win[-1]] - win[-1] >= c - j:
            win.pop()
        win.append(j)
    parts, j = [], n
    while j > start:
        i, k, d = how[j]
        parts.append(bytes([0x80 | (k - 3), (d - 1) & 0xFF, (d - 1) >> 8]) if k else bytes([j - i - 1]) + t[i:j])
        j = i
    return b"".join(reversed(parts))


def lz_unpack(b, n, dictionary=b""):
    """dsp.c lz_unpack: n bytes after the dictionary; ValueError on a stream that reads or reaches outside"""
    o, i = bytearray(dictionary), 0
    while len(o) < len(dictionary) + n:
        if i >= len(b):
            raise ValueError("stream ends early")
        c = b[i]
        if c < 0x80:
            if i + c + 2 > len(b):
                raise ValueError("literals past the stream")
            o += b[i + 1:i + c + 2]
            i += c + 2
        else:
            if i + 3 > len(b):
                raise ValueError("copy past the stream")
            d = (b[i + 1] | b[i + 2] << 8) + 1
            if d > len(o):
                raise ValueError("copy before the dictionary")
            for _ in range((c & 0x7F) + 3):
                o.append(o[-d])
            i += 3
    if len(o) != len(dictionary) + n or i != len(b):
        raise ValueError("stream and length differ")
    return bytes(o[len(dictionary):])


def pick_dictionary(entries, room):
    """a dictionary for every entry: the bytes most entries have at each place (one entry long), then real entries
    while they fit in room and make the chunk smaller"""
    if not entries or not entries[0]:
        return b""
    ulen = len(entries[0])
    mode = bytes(max(range(256), key=lambda v: (sum(e[p] == v for e in entries), -v)) for p in range(ulen))
    best = mode if ulen <= room else b""
    sample = entries[::max(1, len(entries) // 48)]      # (judged on a sample: a few seconds for 200 presets)
    size = lambda d: len(d) * len(sample) // len(entries) + sum(len(lz_pack(d + e, len(d))) for e in sample)
    cost = size(best)
    while len(best) + ulen <= room:
        found = None
        for e in entries[::max(1, len(entries) // 16)]:
            c = size(best + e)
            if c < cost and (found is None or c < found[0]):
                found = (c, e)
        if not found:
            break
        cost, best = found[0], best + found[1]
    return best


def build(presets, init):
    """presets: [(name, category, common16, patch)]; init: the engine's INIT preset (NONE: none) -> the chunk's payload"""
    count = len(presets)
    if count > 255:
        raise SystemExit("mod_presets: at most 255 presets (mod_file_t.npresets)")
    entries = [c + p for _, _, c, p in presets]
    ulen = len(entries[0]) if entries else COMMON
    for (name, cat, c, p), e in zip(presets, entries):
        if len(c) != COMMON or len(e) != ulen:
            raise SystemExit(f"mod_presets: preset {name!r}: every entry {ulen} B (16 common + the patch)")
        if len(name) > NAME_MAX or any(ch < 32 or ch > 126 for ch in name) or not 0 <= cat < 16:
            raise SystemExit(f"mod_presets: preset {name!r}: name 0..20 ASCII characters, category 0..15")
    if ulen > BUF:
        raise SystemExit(f"mod_presets: an entry of {ulen} B, more than the {BUF} B the firmware unpacks into")
    room = BUF - ulen
    while True:                                 # (the firmware unpacks into one buffer: dictionary, entry, packed)
        d = pick_dictionary(entries, room)
        packed = [lz_pack(d + e, len(d)) for e in entries]
        most = max(map(len, packed), default=0)
        if len(d) + ulen + most <= BUF or not d:
            break
        room = min(room - 1, BUF - ulen - most)
    for k, p in enumerate(packed):
        if len(p) > WINDOW or len(d) + ulen + len(p) > BUF:
            raise SystemExit(f"mod_presets: preset {k} packs to {len(p)} B: more than {WINDOW}, or with the dictionary "
                             f"and the entry more than {BUF}")
        assert lz_unpack(p, ulen, d) == entries[k]
    names = b"".join(n for n, *_ in presets)
    at = HEAD.size + REC.size * (count + 1)
    names_at = at
    d_at = names_at + len(names)
    e_at = d_at + len(d)
    if len(names) > 0xFFFF:
        raise SystemExit("mod_presets: the names take more than 64 KB")
    recs, eo, no = b"", e_at, 0
    for (name, cat, _, _), p in zip(presets, packed):
        recs += REC.pack(eo, no, cat, 0)
        eo += len(p)
        no += len(name)
    recs += REC.pack(eo, no, 0, 0)
    body = recs + names + d + b"".join(packed)
    if HEAD.size + len(body) > 0xFFFFFF:
        raise SystemExit("mod_presets: the chunk is too long")
    return HEAD.pack(VERSION, count, init, len(d), ulen, 0, zlib.crc32(body)) + body


def parse(payload):
    """a PRST payload -> (init, [(name, category, entry bytes)]); SystemExit on anything the firmware would refuse"""
    if len(payload) < HEAD.size:
        raise SystemExit("PRST: shorter than its head")
    ver, count, init, dlen, ulen, _, crc = HEAD.unpack_from(payload)
    if ver != VERSION or zlib.crc32(payload[HEAD.size:]) != crc or ulen < COMMON or dlen + ulen > BUF:
        raise SystemExit("PRST: version, CRC or sizes")
    recs = [REC.unpack_from(payload, HEAD.size + REC.size * k) for k in range(count + 1)]
    names_at = HEAD.size + REC.size * (count + 1)
    d_at = names_at + recs[count][1]
    d = payload[d_at:d_at + dlen]
    out = []
    for k in range(count):
        (eo, no, cat, _), (eo1, no1, _, _) = recs[k], recs[k + 1]
        if not (d_at + dlen <= eo <= eo1 <= len(payload) and no <= no1 and no1 - no <= NAME_MAX and eo1 - eo <= WINDOW):
            raise SystemExit(f"PRST: preset {k}'s index")
        out.append((payload[names_at + no:names_at + no1].decode("ascii"), cat, lz_unpack(payload[eo:eo1], ulen, d)))
    return init, out


def chunks(b, at):
    """the chunks after a module file's address words: {tag: payload}"""
    out = {}
    while at < len(b):
        tag, n = struct.unpack_from("<II", b, at)
        out[tag] = b[at + 8:at + 8 + n]
        at += 8 + ((n + 3) & ~3)
    if at != len(b):
        raise SystemExit("module file: a chunk runs past the file")
    return out


def chunk(tag, payload):
    return struct.pack("<II", tag, len(payload)) + payload + bytes(-len(payload) % 4)


def host_tool(out):
    """tools/mod_presets.c, built for the host against the host firmware (the engines and their makers built in)"""
    cc = os.environ.get("HOST_CC", os.environ.get("CC", "cc")).split()
    out.parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run([*cc, "-O0", "-w", "-ffp-contract=off", "-Ibuild/gen", "-Ifirmware/src", "-Itests",
                        str(SRC / "tools" / "mod_presets.c"), "-o", str(out), "-lm"], cwd=SRC, capture_output=True,
                       text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("mod_presets: the host C compiler failed on tools/mod_presets.c (HOST_CC)")
    return out


def dump(tool, engine):
    """engine's presets as the host firmware makes them: (init, [(name, category, common16, patch)])"""
    r = subprocess.run([str(tool), str(engine)], cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"mod_presets: tools/mod_presets.c failed for engine {engine}")
    init, presets = NONE, []
    for line in r.stdout.splitlines():
        f = line.split()
        if f[0] == "N":
            init = int(f[2])
        elif f[0] == "P":
            h = lambda s: b"" if s == "-" else bytes.fromhex(s)
            presets.append((h(f[3]), int(f[2]), h(f[4]), h(f[5])))
    return init, presets


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "--dump":
        init, presets = dump(host_tool(SRC / "build" / "modules" / "mod_presets"), int(a[1]))
        for k, (n, c, com, p) in enumerate(presets):
            print(k, c, n.decode(), com.hex(), p.hex())
        print("init", init)
    elif len(a) == 2 and a[0] == "--check":
        b = Path(a[1]).read_bytes()
        image, nrel = struct.unpack_from("<II", b, 12)[0], struct.unpack_from("<I", b, 20)[0]
        head = 56 if struct.unpack_from("<H", b, 4)[0] == 2 else 52
        c = chunks(b, head + image + 4 * nrel)
        if TAG not in c:
            raise SystemExit(f"{a[1]}: no PRST chunk")
        init, out = parse(c[TAG])
        print(f"{a[1]}: PRST {len(c[TAG])} B, {len(out)} presets, INIT {init}: ok")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
