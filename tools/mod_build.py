#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Build a synth module (firmware/src/module_abi.h) from one C file: build/modules/<name>.mmod.

    tools/mod_build.py firmware/modules/trio/trio.c [-o out.mmod] [--version N]
    tools/mod_build.py firmware/modules/sid/sid.c [-DNAME=V ...]   (a native module: firmware/src/module_native.h,
                                              built with the image's -D flags; tools/build.py passes them)
    tools/mod_build.py --check file.mmod      (the file's head, CRC, address list and descriptor)

The module compiles with the firmware's flags, links at address 0 (firmware/modules/sdk/module.ld, mod_rt.c) with
--emit-relocs and again at another base. The absolute words come from the relocations (R_PI32V2_ABS32: the word at
r_offset; R_PI32V2_MOV_ABS32: a 6-byte `rX = imm32`, the word at r_offset + 2); every other relocation must be
PC-relative inside the image. The two links must differ exactly in those words, by the base difference: a word the
list misses (an absolute address in some other form) fails the build instead of crashing the device."""
import argparse
import os
import re
import platform
import shlex
import struct
import subprocess
import sys
import zlib
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent
SDK = SRC / "firmware" / "modules" / "sdk"
CFLAGS = ["-mcpu=r3", "-mfprev1", "-ffp-contract=off", "-Werror=double-promotion", "-Os", "-ffunction-sections",
          "-fdata-sections", "-fno-builtin", "-Wall", "-Werror", "-Wno-unused-function"]
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
R_ABS32, R_MOV_ABS32 = 8, 5
FILE_MAGIC, DESC_MAGIC, ABI = 0x31444D4D, 0x4353444D, 1
HEAD = struct.Struct("<IHBBIIIIII16sHBB")           # mod_file_t, 52 bytes
DESC_SIZE = 216                                       # sizeof(mod_desc_t) on pi32v2
BASE2 = 0x00100000                                    # the second link's base
STACK_MAX = int(re.search(r"#define MOD_STACK_MAX (\d+)u", (SRC / "firmware/src/module_abi.h").read_text())[1])
HOST_CALL = 160                                       # a call through the host table (mod_pitch_inc and below)
NENGINES = 30
ENGINE_OK = {2, 5, 6, 7, 9, 11} | set(range(22, NENGINES))   # eng_module.c mod_engine_ok
# native modules (firmware/src/module_native.h): the engine's own source against the firmware's headers, its flags
# (the audio's optimization: the firmware compiles its audio code at -O2), the 56-byte head with the layout key
ABI_NATIVE, NDESC_MAGIC, NDESC_SIZE = 2, 0x54414E4D, 28
HEADN = struct.Struct("<IHBBIIIIII16sHBBI")         # mod_file_t, then the key
NATIVE_H = (SRC / "firmware/src/module_native.h").read_text()
NSTACK_MAX = int(re.search(r"#define MOD_NSTACK_MAX (\d+)u", NATIVE_H)[1])
NIMAGE_MAX = int(re.search(r"#define MOD_NIMAGE_MAX (\d+)u", NATIVE_H)[1])
NBSS_MAX = int(re.search(r"#define MOD_NBSS_MAX (\d+)u", NATIVE_H)[1])
NCFLAGS = ["-mcpu=r3", "-mfprev1", "-ffp-contract=off", "-Werror=double-promotion", "-Os", "-ffunction-sections",
           "-fdata-sections", "-fno-builtin", "-Wall", "-Wno-unused-function", "-Wno-unused-variable",
           "-Wno-unused-const-variable", "-DFM1_IRQ_TARGET=1"]
ENGINE_OK_NATIVE = ENGINE_OK | {3, 10, 12, 15, 19}    # (+ SID, DRUM, FM6, CZ-1 and the Prophet: eng_module.c
                                                     # mod_engine_ok)


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


# mod_desc_t's address words on the target (word indices), as module_store.c mod_check reads them, and the ones that
# must be set (MOD_PTR_NEEDED): name, page titles, the 8 labels, host, note_on, render
MOD_PTRS = [2, 3, 4, 5, 8, 9, 10, 13, 14, 15, 18, 19, 20, 23, 24, 25, 28, 29, 30, 33, 34, 35, 38, 39, 40, 43, 44, 45,
            50, 51, 52, 53]
MOD_PTR_NEEDED = 0x7124924F


def describe(img, words, desc=0, bss=0):
    """the descriptor's checks (as module_store.c mod_check does them) at link base 0; its engine and name"""
    if u32(img, desc) != DESC_MAGIC:
        raise SystemExit("mod_build: no module descriptor first in the image (MOD_EXPORT)")
    abi, size = struct.unpack_from("<HH", img, desc + 4)
    if abi != ABI or size != DESC_SIZE:
        raise SystemExit(f"mod_build: descriptor ABI {abi} size {size}, want {ABI} / {DESC_SIZE}")
    ws, end = set(words), len(img) + bss
    for i, w in enumerate(MOD_PTRS):
        at = desc + 4 * w
        v = u32(img, at)
        if at in ws:
            if v >= end:
                raise SystemExit(f"mod_build: descriptor word {w} points outside the module")
        elif v or (MOD_PTR_NEEDED >> i) & 1:
            raise SystemExit(f"mod_build: descriptor word {w} must be an address in the module")
    name = cstr(img, u32(img, desc + 8))
    for k in range(8):
        cstr(img, u32(img, desc + 20 + 20 * k))           # every label a string in the image
    engine = img[desc + 191]
    if engine not in ENGINE_OK:
        raise SystemExit(f"mod_build: engine {engine}: a module plays as 2 5 6 7 9 11 or 22..{NENGINES - 1}")
    npre, pre = img[desc + 184], u32(img, desc + 180)
    for k in range(npre):
        if pre + 20 * k not in ws:
            raise SystemExit(f"mod_build: preset {k} has no name address")
        cstr(img, u32(img, pre + 20 * k))
    return engine, name, npre


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


def is_native(src):
    return '#include "mod_native.h"' in Path(src).read_text()


def describe_native(img, words, bss=0):
    """the native descriptor (module_native.h mod_native_t, first in the image) as module_store.c checks it"""
    if u32(img, 0) != NDESC_MAGIC:
        raise SystemExit("mod_build: no native descriptor first in the image (MOD_NATIVE_EXPORT)")
    abi, size = struct.unpack_from("<HH", img, 4)
    if abi != ABI_NATIVE or size != NDESC_SIZE:
        raise SystemExit(f"mod_build: native descriptor ABI {abi} size {size}, want {ABI_NATIVE} / {NDESC_SIZE}")
    key, engine, npre = u32(img, 8), img[12], img[13]
    ws = set(words)
    for at in (16, 20, 24):                               # name, engine, bind
        if at not in ws or u32(img, at) >= len(img) + bss:
            raise SystemExit(f"mod_build: native descriptor word at {at} must be an address in the module")
    if engine not in ENGINE_OK_NATIVE:
        raise SystemExit(f"mod_build: engine {engine}: a native module plays as {sorted(ENGINE_OK_NATIVE)}")
    return engine, cstr(img, u32(img, 16)), npre, key


def build_native(src, out, version, defs):
    stem = src.stem
    obj, rt = SRC / "build" / "modules" / (stem + ".o"), SRC / "build" / "modules" / "mod_rt.o"
    e0, e1 = SRC / "build" / "modules" / (stem + ".elf"), SRC / "build" / "modules" / (stem + ".b2.elf")
    obj.parent.mkdir(parents=True, exist_ok=True)
    flags = ["-target", "pi32v2", *NCFLAGS, *defs, "-Ifirmware/src", "-Ifirmware/hal", "-Ibuild/gen",
             "-Ifirmware/modules/sdk"]
    ld = ["--gc-sections", "--emit-relocs", "-T", SDK / "module.ld"]
    # as tools/build.py compiles the image's audio code: IR without the optimizer, the once-per-note functions marked
    # minsize (tools/size_fns.py), then the IR at -O2: the module's code is the code the engine had built in
    ll, lls = obj.with_suffix(".ll"), obj.with_name(stem + "_size.ll")
    run_all([("pi32v2/bin/clang", *flags, "-S", "-emit-llvm", "-Xclang", "-disable-llvm-optzns", "-c", src, "-o", ll)])
    subprocess.run([sys.executable, SRC / "tools" / "size_fns.py", ll, lls], check=True, stdout=subprocess.DEVNULL)
    irflags = [("-O2" if f == "-Os" else f) for f in flags if not f.startswith(("-I", "-D", "-W"))]
    run_all([("pi32v2/bin/clang", *irflags, "-c", lls, "-o", obj),
             ("pi32v2/bin/clang", "-target", "pi32v2", *CFLAGS, "-c", SDK / "mod_rt.c", "-o", rt),
             ("pi32v2/bin/ld", *ld, "--defsym=MOD_BASE=0", obj, rt, "-o", e0),
             ("pi32v2/bin/ld", *ld, f"--defsym=MOD_BASE={BASE2:#x}", obj, rt, "-o", e1)])
    dis = run_all([("pi32v2/bin/objdump", "-d", "-mcpu=r3", "-mattr=+fprev1", e0)])
    e0.with_suffix(".dis").write_text(dis)           # (tests/target_budget.py: the engine's render loops, as built)
    need = stack_need(dis)
    if need > NSTACK_MAX:
        raise SystemExit(f"mod_build: the module needs {need} B of stack, more than MOD_NSTACK_MAX {NSTACK_MAX} B")
    img, nbss, words = image_of(e0, 0)
    img1, _, words1 = image_of(e1, BASE2)
    if words != words1:
        raise SystemExit("mod_build: the two links list different address words")
    check_pair(img, img1, words)
    if len(img) > NIMAGE_MAX or nbss > NBSS_MAX:
        raise SystemExit(f"mod_build: image {len(img)} B / bss {nbss} B, at most {NIMAGE_MAX} / {NBSS_MAX}")
    engine, name, npre, key = describe_native(img, words, nbss)
    body = img + b"".join(struct.pack("<I", w) for w in words)
    head = HEADN.pack(FILE_MAGIC, ABI_NATIVE, engine, 0, HEADN.size + len(body), len(img), nbss, len(words), 0,
                      zlib.crc32(body), name.encode()[:16].ljust(16, b"\0"), version, npre, 0, key)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(head + body)
    print(f"module {name}: native, engine {engine}, key {key:08x}, image {len(img)} B, bss {nbss} B, {len(words)} "
          f"address words, stack {need} B, file {len(head) + len(body)} B -> {out}")


def build(src, out, version):
    obj = SRC / "build" / "modules" / (src.stem + ".o")
    rt = SRC / "build" / "modules" / "mod_rt.o"
    e0 = SRC / "build" / "modules" / (src.stem + ".elf")
    e1 = SRC / "build" / "modules" / (src.stem + ".b2.elf")
    obj.parent.mkdir(parents=True, exist_ok=True)
    flags = ["-target", "pi32v2", *CFLAGS, "-Ifirmware/src", "-Ifirmware/modules/sdk"]
    ld = ["--gc-sections", "--emit-relocs", "-T", SDK / "module.ld"]
    run_all([("pi32v2/bin/clang", *flags, "-c", src, "-o", obj),
             ("pi32v2/bin/clang", *flags, "-c", SDK / "mod_rt.c", "-o", rt),
             ("pi32v2/bin/ld", *ld, "--defsym=MOD_BASE=0", obj, rt, "-o", e0),
             ("pi32v2/bin/ld", *ld, f"--defsym=MOD_BASE={BASE2:#x}", obj, rt, "-o", e1)])
    need = stack_need(run_all([("pi32v2/bin/objdump", "-d", "-mcpu=r3", "-mattr=+fprev1", e0)]))
    if need > STACK_MAX:
        raise SystemExit(f"mod_build: the module needs {need} B of stack, more than MOD_STACK_MAX {STACK_MAX} B")
    img, nbss, words = image_of(e0, 0)
    img1, _, words1 = image_of(e1, BASE2)
    if words != words1:
        raise SystemExit("mod_build: the two links list different address words")
    check_pair(img, img1, words)
    engine, name, npre = describe(img, words, 0, nbss)
    body = img + b"".join(struct.pack("<I", w) for w in words)
    head = HEAD.pack(FILE_MAGIC, ABI, engine, 0, HEAD.size + len(body), len(img), nbss, len(words), 0,
                     zlib.crc32(body), name.encode()[:16].ljust(16, b"\0"), version, npre, 0)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(head + body)
    print(f"module {name}: engine {engine}, image {len(img)} B, bss {nbss} B, {len(words)} address words, "
          f"stack {need} B, file {len(head) + len(body)} B -> {out}")


def check(path):
    b = path.read_bytes()
    if struct.unpack_from("<H", b, 4)[0] == ABI_NATIVE:
        magic, abi, engine, flags, total, image, bss, nrel, desc, crc, name, version, npre, _, key = HEADN.unpack_from(b)
        if magic != FILE_MAGIC or total != len(b) or total != HEADN.size + image + 4 * nrel or desc:
            raise SystemExit(f"{path}: not a native module file")
        if zlib.crc32(b[HEADN.size:]) != crc:
            raise SystemExit(f"{path}: CRC mismatch")
        img = b[HEADN.size: HEADN.size + image]
        words = [u32(b, HEADN.size + image + 4 * k) for k in range(nrel)]
        e, n, k, kk = describe_native(img, words, bss)
        if e != engine or n.encode()[:16] != name.rstrip(b"\0") or k != npre or kk != key:
            raise SystemExit(f"{path}: head and native descriptor differ")
        print(f"{path}: {n} native engine {engine}, key {key:08x}, image {image} B, bss {bss} B, {nrel} address "
              f"words, version {version}: ok")
        return
    magic, abi, engine, flags, total, image, bss, nrel, desc, crc, name, version, npre, _ = HEAD.unpack_from(b)
    if magic != FILE_MAGIC or abi != ABI or total != len(b) or total != HEAD.size + image + 4 * nrel:
        raise SystemExit(f"{path}: not an ABI {ABI} module file")
    if zlib.crc32(b[HEAD.size:]) != crc:
        raise SystemExit(f"{path}: CRC mismatch")
    img = b[HEAD.size: HEAD.size + image]
    words = [u32(b, HEAD.size + image + 4 * k) for k in range(nrel)]
    e, n, k = describe(img, words, desc, bss)
    if e != engine or n.encode()[:16] != name.rstrip(b"\0") or k != npre:
        raise SystemExit(f"{path}: head ({engine}, {name}, {npre} presets) and descriptor ({e}, {n}, {k}) differ")
    print(f"{path}: {n} engine {engine}, image {image} B, bss {bss} B, {nrel} address words, version {version}: ok")


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
    if is_native(src):
        if any(not d.startswith("-D") for d in rest):
            raise SystemExit(f"mod_build: unknown arguments {rest}")
        return build_native(src, out, a.version, rest)
    if any(not d.startswith("-D") for d in rest):
        raise SystemExit(f"mod_build: unknown arguments {rest}")
    build(src, out, a.version)              # (ABI 1: the frozen contract, no firmware options)


if __name__ == "__main__":
    main()
