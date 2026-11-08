#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Build a synth module (ABI 3: firmware/modules/sdk/mod_abi3.h) from one C file: build/modules/<name>.mmod.

    tools/mod_build.py firmware/modules/sid/sid.c [-DNAME=V ...] [-o out.mmod] [--version N]
    tools/mod_build.py --check file.mmod      (the file's head, CRC, address list and descriptor, its presets)

Two kinds of source, one file format:
  - an engine-source module (#include "mod_engine.h": SID, DRUM, CZ-1, FM6, the Prophet) is a synth written against
    the firmware's engine headers, built as tools/build.py builds the image's audio code (IR, tools/size_fns.py, -O2)
    with the image's -D flags (tools/build.py passes them: such a module is per board);
  - an SDK module (#include "mod_sdk.h": TRIO, WHEEL) sees only the contract, built -Os without firmware options.
Either links at address 0 (firmware/modules/sdk/module.ld, mod_rt.c) with --emit-relocs and again at another base. The
absolute words come from the relocations (R_PI32V2_ABS32: the word at r_offset; R_PI32V2_MOV_ABS32: a 6-byte
`rX = imm32`, the word at r_offset + 2); every other relocation must be PC-relative inside the image. The two links
must differ exactly in those words, by the base difference: a word the list misses (an absolute address in some other
form) fails the build instead of crashing the device.

A module's factory presets follow its address words as its PRST chunk (firmware/src/module_presets.h,
tools/mod_presets.py): the presets the host firmware's makers make for its engine (tools/mod_presets.c, built with the
host's C compiler, HOST_CC or CC or cc), packed; build/modules/<name>.prst keeps the last chunk for the same presets."""
import argparse
import hashlib
import os
import re
import platform
import shlex
import struct
import subprocess
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mod_presets                                  # noqa: E402  (the PRST chunk)

SRC = Path(__file__).resolve().parent.parent
SDK = SRC / "firmware" / "modules" / "sdk"
ABI_H = (SDK / "mod_abi3.h").read_text()
CFLAGS = ["-mcpu=r3", "-mfprev1", "-ffp-contract=off", "-Werror=double-promotion", "-Os", "-ffunction-sections",
          "-fdata-sections", "-fno-builtin", "-Wall", "-Werror", "-Wno-unused-function"]
NCFLAGS = ["-mcpu=r3", "-mfprev1", "-ffp-contract=off", "-Werror=double-promotion", "-Os", "-ffunction-sections",
           "-fdata-sections", "-fno-builtin", "-Wall", "-Wno-unused-function", "-Wno-unused-variable",
           "-Wno-unused-const-variable", "-DFM1_IRQ_TARGET=1"]
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
R_ABS32, R_MOV_ABS32 = 8, 5
BASE2 = 0x00100000                                    # the second link's base
HOST_CALL = 160                                       # a call through the host table (its deepest callee and below)
NENGINES = 30
# the numbers a module may play as (eng_module_main.c mod_engine_ok): retired engines' when it is that engine again,
# the shipped synths', 22..29
ENGINE_OK = {2, 5, 6, 7, 9, 11, 3, 10, 12, 15, 19} | set(range(22, NENGINES))


def abi_num(name):
    return int(re.search(rf"#define {name} (0x[0-9A-Fa-f]+|\d+)u", ABI_H)[1], 0)


FILE_MAGIC, MAGIC, ABI, HOST = abi_num("MOD3_FILE_MAGIC"), abi_num("MOD3_MAGIC"), abi_num("MOD3_ABI"), abi_num("MOD3_HOST")
STACK_MAX, IMAGE_MAX, BSS_MAX = abi_num("MOD3_STACK_MAX"), abi_num("MOD3_IMAGE_MAX"), abi_num("MOD3_BSS_MAX")
HEAD = struct.Struct("<IHBBIIIIII16sHBBIHH")        # mod_file3_t, 60 bytes
assert HEAD.size == abi_num("MOD3_HEAD")
DESC = struct.Struct("<IHHHHBBBBIIII")              # mod_desc_v3 on pi32v2, 32 bytes (eng_module.c asserts it)
ENG_NOTE_ON, ENG_RENDER = 188, 192                  # mod_engine_v3's note_on and render on pi32v2 (eng_module.c)


def toolchain():
    tc = Path(os.environ.get("JIELI_TOOLCHAIN", Path.home() / ".jieli" / "toolchain")).resolve()
    if not (tc / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit(f"mod_build: no JieLi toolchain in {tc} (tools/get_toolchain.sh)")
    return tc


def run_all(cmds):
    """toolchain commands [(tool, *args), ...] one after another (one container off Linux), cwd SRC; Path
    arguments relative to SRC"""
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    docker = os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"
    root = "/opt/jieli" if docker else str(toolchain())
    arg = lambda a: shlex.quote(str(Path(a).resolve().relative_to(SRC)) if isinstance(a, Path) else a)
    line = " && ".join(" ".join([f"{root}/{c[0]}", *map(arg, c[1:])]) for c in cmds)
    cmd = (["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{SRC}:/work", "-v",
            f"{toolchain()}:/opt/jieli:ro", "-w", "/work", DOCKER_IMAGE, "sh", "-c", line] if docker else
           ["sh", "-c", line])
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("mod_build: the toolchain failed")
    return r.stdout


def elf_sections(d):
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
    sh = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
    names = sh[shstrndx][4]
    out = {}
    for s in sh:
        nm = d[names + s[0]: d.index(b"\0", names + s[0])].decode()
        out[nm] = dict(type=s[1], addr=s[3], off=s[4], size=s[5])
    return out


def image_of(elf, base):
    d = elf.read_bytes()
    sec = elf_sections(d)
    im, bss = sec[".image"], sec.get(".bss")
    if im["addr"] != base:
        raise SystemExit(f"mod_build: .image at {im['addr']:#x}, not {base:#x}")
    img = d[im["off"]: im["off"] + im["size"]]
    nbss = bss["size"] if bss else 0
    if bss and nbss and 0 < bss["addr"] - (base + len(img)) < 64:
        img += bytes(bss["addr"] - (base + len(img)))   # (.bss aligned past the image's end: zeros up to it; the
                                                        # arena places the block at a word, the module needs no more)
    if bss and nbss and bss["addr"] != base + len(img):
        raise SystemExit("mod_build: .bss does not follow the image")
    words = set()
    for nm, s in sec.items():
        if s["type"] != 4:                              # SHT_RELA
            continue
        for k in range(s["size"] // 12):
            off, info, _ = struct.unpack_from("<IIi", d, s["off"] + 12 * k)
            typ = info & 0xFF
            if typ == R_ABS32:
                words.add(off - base)
            elif typ == R_MOV_ABS32:
                words.add(off - base + 2)
    for w in words:
        if w < 0 or w + 4 > len(img):
            raise SystemExit(f"mod_build: address word at {w:#x} outside the image")
    return img, nbss, sorted(words)


def check_pair(img0, img1, words):
    """the links at 0 and BASE2 differ exactly in the listed words, by BASE2"""
    if len(img0) != len(img1):
        raise SystemExit("mod_build: the two links differ in size")
    listed = set()
    for w in words:
        a, b = struct.unpack_from("<I", img0, w)[0], struct.unpack_from("<I", img1, w)[0]
        if (b - a) & 0xFFFFFFFF != BASE2:
            raise SystemExit(f"mod_build: listed word {w:#x} moved by {(b - a) & 0xFFFFFFFF:#x}, not {BASE2:#x}")
        listed.update(range(w, w + 4))
    for i, (a, b) in enumerate(zip(img0, img1)):
        if a != b and i not in listed:
            raise SystemExit(f"mod_build: byte {i:#x} depends on the base but no address word covers it "
                             "(an absolute address the loader would not move)")


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def cstr(img, at):
    if at >= len(img):
        raise SystemExit(f"mod_build: string at {at:#x} outside the image")
    return img[at: img.index(b"\0", at)].decode("ascii")


def stack_need(dis):
    """the deepest stack of any function's call path in the module's disassembly (frames as tools/stack_budget.py
    reads them), a call through a register (the host table) counted as HOST_CALL"""
    frames, calls, host = {}, {}, set()
    cur = None
    for line in dis.splitlines():
        m = re.match(r"^([A-Za-z_][\w.]*):$", line)
        if m and not m[1].startswith("."):
            cur = m[1]
            frames[cur], calls[cur] = 0, set()
            continue
        if not cur:
            continue
        m = re.search(r"sp \+= -(\d+)", line)
        if m:
            frames[cur] += int(m[1])
        m = re.search(r"\[--sp\] = \{([^}]+)\}", line)
        if m:
            for r in m[1].split(","):
                q = re.match(r"r(\d+)-r(\d+)", r.strip())
                frames[cur] += 4 * (abs(int(q[1]) - int(q[2])) + 1) if q else 4
        if re.search(r"sp (?:\+=|-=|=).*r\d", line):
            raise SystemExit(f"mod_build: {cur} has a dynamic stack frame")
        m = re.search(r"\bcall -?\d+ <([\w.]+) :", line)
        if m:
            calls[cur].add(m[1])
        elif re.search(r"\bcall r\d+", line):
            host.add(cur)
    memo, path = {}, []

    def depth(f):
        if f in path:
            raise SystemExit("mod_build: recursion: " + " -> ".join(path + [f]))
        if f not in memo:
            path.append(f)
            below = [depth(g) for g in calls.get(f, ()) if g in frames]
            memo[f] = frames[f] + max(below + [HOST_CALL if f in host else 0])
            path.pop()
        return memo[f]
    return max((depth(f) for f in frames), default=0)


def is_engine(src):
    return '#include "mod_engine.h"' in Path(src).read_text()


def describe(img, words, bss=0):
    """the descriptor (mod_desc_v3, the image's first bytes) as module_store.c mod_dcheck reads it, at link base 0:
    (engine, name, npresets, key, host)"""
    magic, abi, size, host, eng_size, engine, npre, flags, _, key, name, eng, bind = DESC.unpack_from(img, 0)
    if magic != MAGIC:
        raise SystemExit("mod_build: no ABI 3 descriptor first in the image (MOD3_EXPORT / MOD3_ENGINE_EXPORT)")
    if abi != ABI or size != DESC.size or host > HOST or flags:
        raise SystemExit(f"mod_build: descriptor ABI {abi} size {size} host {host}, want {ABI} / {DESC.size} / <= {HOST}")
    ws, end = set(words), len(img) + bss
    for at in (20, 24, 28):                               # name, eng, bind
        if at not in ws or u32(img, at) >= end:
            raise SystemExit(f"mod_build: descriptor word at {at} must be an address in the module")
    if eng + eng_size > len(img) or eng_size < ENG_RENDER + 4:
        raise SystemExit(f"mod_build: the engine (at {eng:#x}, {eng_size} B) is not in the image")
    for at in (ENG_NOTE_ON, ENG_RENDER):
        if eng + at not in ws or u32(img, eng + at) >= len(img):
            raise SystemExit("mod_build: the engine's note_on and render must be the module's own")
    if engine not in ENGINE_OK:
        raise SystemExit(f"mod_build: engine {engine}: a module plays as {sorted(ENGINE_OK)}")
    return engine, cstr(img, name), npre, key, host


def build(src, out, version, defs):
    stem, engine_src = src.stem, is_engine(src)
    obj, rt = SRC / "build" / "modules" / (stem + ".o"), SRC / "build" / "modules" / "mod_rt.o"
    e0, e1 = SRC / "build" / "modules" / (stem + ".elf"), SRC / "build" / "modules" / (stem + ".b2.elf")
    obj.parent.mkdir(parents=True, exist_ok=True)
    ld = ["--gc-sections", "--emit-relocs", "-T", SDK / "module.ld"]
    rtc = ("pi32v2/bin/clang", "-target", "pi32v2", *CFLAGS, "-c", SDK / "mod_rt.c", "-o", rt)
    links = [("pi32v2/bin/ld", *ld, "--defsym=MOD_BASE=0", obj, rt, "-o", e0),
             ("pi32v2/bin/ld", *ld, f"--defsym=MOD_BASE={BASE2:#x}", obj, rt, "-o", e1)]
    if engine_src:
        # as tools/build.py compiles the image's audio code: IR without the optimizer, the once-per-note functions
        # marked minsize (tools/size_fns.py), then the IR at -O2: the module's code is the code the engine had built in
        flags = ["-target", "pi32v2", *NCFLAGS, *defs, "-Ifirmware/src", "-Ifirmware/hal", "-Ibuild/gen",
                 "-Ifirmware/modules/sdk"]
        ll, lls = obj.with_suffix(".ll"), obj.with_name(stem + "_size.ll")
        run_all([("pi32v2/bin/clang", *flags, "-S", "-emit-llvm", "-Xclang", "-disable-llvm-optzns", "-c", src, "-o",
                  ll)])
        subprocess.run([sys.executable, SRC / "tools" / "size_fns.py", ll, lls], check=True, stdout=subprocess.DEVNULL)
        irflags = [("-O2" if f == "-Os" else f) for f in flags if not f.startswith(("-I", "-D", "-W"))]
        run_all([("pi32v2/bin/clang", *irflags, "-c", lls, "-o", obj), rtc, *links])
    else:                                            # (the contract alone: no firmware header, no firmware option)
        run_all([("pi32v2/bin/clang", "-target", "pi32v2", *CFLAGS, "-Ifirmware/modules/sdk", "-c", src, "-o", obj),
                 rtc, *links])
    dis = run_all([("pi32v2/bin/objdump", "-d", "-mcpu=r3", "-mattr=+fprev1", e0)])
    e0.with_suffix(".dis").write_text(dis)           # (tests/target_budget.py: the engine's render loops, as built)
    need = stack_need(dis)
    if need > STACK_MAX:
        raise SystemExit(f"mod_build: the module needs {need} B of stack, more than MOD3_STACK_MAX {STACK_MAX} B")
    img, nbss, words = image_of(e0, 0)
    img1, _, words1 = image_of(e1, BASE2)
    if words != words1:
        raise SystemExit("mod_build: the two links list different address words")
    check_pair(img, img1, words)
    if len(img) > IMAGE_MAX or nbss > BSS_MAX:
        raise SystemExit(f"mod_build: image {len(img)} B / bss {nbss} B, at most {IMAGE_MAX} / {BSS_MAX}")
    engine, name, npre, key, host = describe(img, words, nbss)
    if engine_src != (key != 0):
        raise SystemExit("mod_build: an engine-source module names the legacy key (MOD3_ENGINE_HEAD), an SDK module none")
    body = img + b"".join(struct.pack("<I", w) for w in words)
    prst = presets_chunk(stem, engine, npre) if npre else b""
    chunks = mod_presets.chunk(mod_presets.TAG, prst) if prst else b""
    head = HEAD.pack(FILE_MAGIC, ABI, engine, 1 if chunks else 0, HEAD.size + len(body) + len(chunks), len(img), nbss,
                     len(words), 0, zlib.crc32(body + chunks), name.encode()[:16].ljust(16, b"\0"), version, npre, 0,
                     key, host, 0)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(head + body + chunks)
    print(f"module {name}: ABI {ABI} host {host}, engine {engine}, " + (f"legacy key {key:08x}, " if key else "") +
          f"image {len(img)} B, bss {nbss} B, {len(words)} address words, stack {need} B, {npre} presets {len(prst)} B, "
          f"file {len(head) + len(body) + len(chunks)} B -> {out}")


def presets_chunk(stem, engine, npre):
    """engine's factory presets as the host firmware makes them, packed (tools/mod_presets.py): the PRST payload"""
    tool = mod_presets.host_tool(SRC / "build" / "modules" / (stem + "_presets"))
    init, presets = mod_presets.dump(tool, engine)
    if len(presets) != npre:
        raise SystemExit(f"mod_build: engine {engine}: {len(presets)} presets made, the descriptor says {npre}")
    key = hashlib.sha256(repr((mod_presets.VERSION, init, presets)).encode() +
                         Path(mod_presets.__file__).read_bytes()).hexdigest()
    cache = SRC / "build" / "modules" / (stem + ".prst")
    if cache.exists() and cache.with_suffix(".prst.key").exists() and \
            cache.with_suffix(".prst.key").read_text() == key:
        payload = cache.read_bytes()
    else:
        payload = mod_presets.build(presets, init)
        cache.write_bytes(payload)
        cache.with_suffix(".prst.key").write_text(key)
    _, out = mod_presets.parse(payload)                # (the firmware's reading of it: every entry as made)
    if [(n, c, e) for n, c, e in out] != [(n.decode(), c, com + p) for n, c, com, p in presets]:
        raise SystemExit(f"mod_build: engine {engine}: the PRST chunk does not read back as its presets")
    return payload


def check(path):
    b = path.read_bytes()
    magic, abi, engine, flags, total, image, bss, nrel, desc, crc, name, version, npre, _, key, host, _ = HEAD.unpack_from(b)
    end = HEAD.size + image + 4 * nrel
    if magic != FILE_MAGIC or abi != ABI or total != len(b) or desc or \
            (total != end if not flags else flags != 1 or total < end + 8):
        raise SystemExit(f"{path}: not an ABI {ABI} module file")
    if zlib.crc32(b[HEAD.size:]) != crc:
        raise SystemExit(f"{path}: CRC mismatch")
    img = b[HEAD.size: HEAD.size + image]
    words = [u32(b, HEAD.size + image + 4 * k) for k in range(nrel)]
    e, n, k, kk, hh = describe(img, words, bss)
    if e != engine or n.encode()[:16] != name.rstrip(b"\0") or k != npre or kk != key or hh != host:
        raise SystemExit(f"{path}: head and descriptor differ")
    chunks = mod_presets.chunks(b, end) if flags else {}
    if npre or mod_presets.TAG in chunks:
        if mod_presets.TAG not in chunks:
            raise SystemExit(f"{path}: {npre} presets, no PRST chunk")
        init, presets = mod_presets.parse(chunks[mod_presets.TAG])
        if len(presets) != npre:
            raise SystemExit(f"{path}: PRST holds {len(presets)} presets, the head says {npre}")
    print(f"{path}: {n} engine {engine}, ABI {abi} host {host}" + (f", legacy key {key:08x}" if key else "") +
          f", image {image} B, bss {bss} B, {nrel} address words, {npre} presets, version {version}: ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("src", type=Path)
    ap.add_argument("-o", "--out", type=Path)
    ap.add_argument("--version", type=int, default=1)
    ap.add_argument("--check", action="store_true", help="check a built .mmod file instead")
    a, rest = ap.parse_known_args()
    if a.check:
        return check(a.src)
    src = a.src.resolve()
    out = (a.out or SRC / "build" / "modules" / (src.stem + ".mmod")).resolve()
    if any(not d.startswith("-D") for d in rest):
        raise SystemExit(f"mod_build: unknown arguments {rest}")
    build(src, out, a.version, rest)


if __name__ == "__main__":
    main()
