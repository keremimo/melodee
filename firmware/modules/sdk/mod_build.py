#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Build a synth module (ABI 3: mod_abi3.h) from one C file: a .mmod file. The SDK's builder: this folder alone builds
an SDK module (mod_sdk.h), anywhere; in Melodee's tree it builds Melodee's engine-source modules too.

    mod_build.py SRC.c [-o OUT.mmod] [--version N] [-DNAME=V ...]
    mod_build.py --check FILE.mmod     (its head, CRC, address list, descriptor, presets, schema, who it is, signature)
    mod_build.py --wasm FILE.mmod      (Melodee's tree: the file with its module built for WebAssembly, a WASM chunk)

Needs: Python 3, a host C compiler (cc, or HOST_CC: a module's schema is written by its own source, built natively),
and the JieLi pi32v2 toolchain (JIELI_TOOLCHAIN, else ~/.jieli/toolchain; off Linux x86-64 it runs in Docker,
JIELI_DOCKER_IMAGE). Signs the file when it has a key (MELODEE_SIGN_KEY, else ~/.config/melodee/module-sign.key):
a module without Melodee's signature installs with a one-line warning (mod_sign.py).

Two kinds of source, one file format:
  - an SDK module (#include "mod_sdk.h": TRIO, WHEEL, an outside author's synth) sees only the contract, built -Os
    without any firmware option. What it is comes from its source: MOD3_EXPORT (the descriptor), MOD3_IDENT (vendor,
    tag: its UID is CRC-32 of "vendor/name"), MOD3_PRESETS (its factory presets, the first one the INIT), MOD3_SCHEMA
    (its parameters and pages, mod_schema.h: written by mod_dump.c, the source built for the host);
  - an engine-source module (#include "../mod_engine.h": SID, DRUM, CZ-1, FM6, the Prophet, Melodee's tree only) is a
    synth written against the firmware's engine headers, built as tools/build.py builds the image's audio code (IR,
    tools/size_fns.py, -O2) with the image's -D flags; its presets and schema are the host firmware's (tools/mod_presets.c).
Either links at address 0 (module.ld, mod_rt.c) with --emit-relocs and again at another base. The absolute words come
from the relocations (R_PI32V2_ABS32: the word at r_offset; R_PI32V2_MOV_ABS32: a 6-byte `rX = imm32`, the word at
r_offset + 2); every other relocation must be PC-relative inside the image. The two links must differ exactly in those
words, by the base difference: a word the list misses (an absolute address in some other form) fails the build instead
of crashing the device.

The chunks after the address words: PRST (the presets, mod_presets.py), SCHM (the schema), IDNT (who it is), SIGN
(the signature, last).

--wasm: the same file with a WASM chunk last, the module's source built as a WebAssembly side module (emcc -fPIC
-sSIDE_MODULE, MOD_IN_CORE). Melodee Studio's virtual FM-1 (web/core: CORE_MODULES) links it into its own memory and
table and installs the file without the chunk: the device's file, byte for byte. emcc: a local one, else the emsdk
Docker image web/core/build.sh uses (EMSDK_IMAGE). Written to build/modules/wasm/<name>.mmod."""
import argparse
import hashlib
import os
import re
import platform
import shlex
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

SDK = Path(__file__).resolve().parent
sys.path.insert(0, str(SDK))
import mod_presets                                  # noqa: E402  (the PRST chunk)
import mod_sign                                     # noqa: E402  (the SIGN chunk: Ed25519)

ABI_H = (SDK / "mod_abi3.h").read_text()
# Melodee's tree (firmware/modules/sdk inside it): its engine-source modules, build/, tools/; else None (the SDK alone)
SRC = SDK.parent.parent.parent if (SDK.parent.parent / "src" / "core.h").is_file() else None
CFLAGS = ["-mcpu=r3", "-mfprev1", "-ffp-contract=off", "-Werror=double-promotion", "-Os", "-ffunction-sections",
          "-fdata-sections", "-fno-builtin", "-Wall", "-Werror", "-Wno-unused-function"]
NCFLAGS = ["-mcpu=r3", "-mfprev1", "-ffp-contract=off", "-Werror=double-promotion", "-Os", "-ffunction-sections",
           "-fdata-sections", "-fno-builtin", "-Wall", "-Wno-unused-function", "-Wno-unused-variable",
           "-Wno-unused-const-variable", "-DFM1_IRQ_TARGET=1"]
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
R_ABS32, R_MOV_ABS32 = 8, 5
BASE2 = 0x00100000                                    # the second link's base
HOST_CALL = 160                                       # a call through the host table (its deepest callee and below)
NENGINES, ENGI_MOD0 = 30, 22
# the synths Melodee makes (firmware/modules/shipped.h): their numbers and UIDs, the device's default engine map
SHIPPED = {int(m[1]): int(m[2], 16) for m in re.finditer(r"X\((\d+)u, 0x([0-9A-Fa-f]+)u,",
                                                          (SDK.parent / "shipped.h").read_text())} \
    if (SDK.parent / "shipped.h").is_file() else {}
IDNT_TAG = 0x544E4449                                # "IDNT": who the module is (mod_abi3.h mod_ident_v3)
SIGN_TAG = 0x4E474953                                # "SIGN": Ed25519 of the bytes from the head's end to it, the last


def sign_key():
    """the signing key's seed: MELODEE_SIGN_KEY (a file; "" none), else ~/.config/melodee/module-sign.key if there"""
    path = os.environ.get("MELODEE_SIGN_KEY", str(Path.home() / ".config" / "melodee" / "module-sign.key"))
    return mod_sign.load(path) if path and Path(path).is_file() else None
IDNT = struct.Struct("<IBBH4s16s12x")


def uid_of(vendor, name):
    """a module's UID: CRC-32 of "<vendor>/<name>" (never 0)"""
    return zlib.crc32(f"{vendor}/{name}".encode()) or 1


def ident(elf, name):
    """(vendor, tag) from the object's .mod_ident (MOD3_IDENT in the source)"""
    d = elf.read_bytes()
    sec = elf_sections(d).get(".mod_ident")
    if not sec or sec["size"] != 20:
        raise SystemExit("mod_build: no MOD3_IDENT(\"vendor\", \"TAG\") in the module's source (mod_abi3.h)")
    raw = d[sec["off"]: sec["off"] + 20]
    vendor, tag = raw[:16].rstrip(b"\0").decode("ascii"), raw[16:].rstrip(b"\0").decode("ascii")
    if not re.fullmatch(r"[a-z0-9-]{1,16}", vendor) or "\0" in tag or not all(32 <= ord(c) < 127 for c in tag):
        raise SystemExit(f"mod_build: MOD3_IDENT: vendor {vendor!r} (lowercase letters, digits, '-'), tag {tag!r}")
    return vendor, tag


def abi_num(name):
    return int(re.search(rf"#define {name} (0x[0-9A-Fa-f]+|\d+)u", ABI_H)[1], 0)


FILE_MAGIC, MAGIC, ABI, HOST = abi_num("MOD3_FILE_MAGIC"), abi_num("MOD3_MAGIC"), abi_num("MOD3_ABI"), abi_num("MOD3_HOST")
STACK_MAX, IMAGE_MAX, BSS_MAX = abi_num("MOD3_STACK_MAX"), abi_num("MOD3_IMAGE_MAX"), abi_num("MOD3_BSS_MAX")
HEAD = struct.Struct("<IHBBIIIIII16sHBBIHH")        # mod_file3_t, 60 bytes (its uid where ABI 3 had desc 0)
assert HEAD.size == abi_num("MOD3_HEAD")
DESC = struct.Struct("<IHHHHBBBBIIII")              # mod_desc_v3 on pi32v2, 32 bytes (eng_module.c asserts it)
ENG_NOTE_ON, ENG_RENDER = 188, 192                  # mod_engine_v3's note_on and render on pi32v2 (eng_module.c)


def toolchain():
    tc = Path(os.environ.get("JIELI_TOOLCHAIN", Path.home() / ".jieli" / "toolchain")).resolve()
    if not (tc / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit(f"mod_build: no JieLi toolchain in {tc} (tools/get_toolchain.sh)")
    return tc


ROOT = SRC                                           # the folder the toolchain sees (build(): every path inside it)


def run_all(cmds):
    """toolchain commands [(tool, *args), ...] one after another (one container off Linux), cwd ROOT; Path
    arguments relative to ROOT"""
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    docker = os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"
    root = "/opt/jieli" if docker else str(toolchain())
    arg = lambda a: shlex.quote(str(Path(a).resolve().relative_to(ROOT)) if isinstance(a, Path) else a)
    line = " && ".join(" ".join([f"{root}/{c[0]}", *map(arg, c[1:])]) for c in cmds)
    cmd = (["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{ROOT}:/work", "-v",
            f"{toolchain()}:/opt/jieli:ro", "-w", "/work", DOCKER_IMAGE, "sh", "-c", line] if docker else
           ["sh", "-c", line])
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
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
    return 'mod_engine.h"' in Path(src).read_text()


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
    if not (engine in SHIPPED or (not SHIPPED and engine < ENGI_MOD0)) and not ENGI_MOD0 <= engine < NENGINES:
        raise SystemExit(f"mod_build: engine {engine}: a module asks for {ENGI_MOD0}..{NENGINES - 1} (below: Melodee's "
                         f"own synths, firmware/modules/shipped.h {sorted(SHIPPED)})")
    if key:
        raise SystemExit("mod_build: a module names no firmware data key (host 4: its pads and records are the host "
                         "table's)")
    return engine, cstr(img, name), npre, key, host


def build(src, out, version, defs):
    global ROOT
    stem, engine_src = src.stem, is_engine(src)
    if engine_src and not SRC:
        raise SystemExit("mod_build: an engine-source module (mod_engine.h) builds in Melodee's tree only")
    inside = SRC and all(SRC in p.parents for p in (src, out, SDK))
    ROOT = SRC if inside else Path(os.path.commonpath([SDK, src, out.parent]))
    work = (SRC / "build" / "modules") if inside else out.parent / ".mod_build"
    obj, rt = work / (stem + ".o"), work / "mod_rt.o"
    e0, e1 = work / (stem + ".elf"), work / (stem + ".b2.elf")
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
        run_all([("pi32v2/bin/clang", "-target", "pi32v2", *CFLAGS, "-I" + str(SDK.relative_to(ROOT)), "-c", src, "-o",
                  obj), rtc, *links])
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
    if not engine_src and key:
        raise SystemExit("mod_build: an SDK module names no key (MOD3_SDK_HEAD)")
    vendor, tag = ident(e0, name)
    uid = uid_of(vendor, name)
    if engine < ENGI_MOD0 and not SHIPPED and vendor != "melodee":
        raise SystemExit(f"mod_build: engine {engine} is one of Melodee's own synths' numbers: ask for "
                         f"{ENGI_MOD0}..{NENGINES - 1} (a device gives a module the number it asks for when it is free)")
    if engine in SHIPPED and SHIPPED[engine] != uid:
        raise SystemExit(f"mod_build: engine {engine} is {SHIPPED[engine]:08x}'s (firmware/modules/shipped.h), this "
                         f"module is {vendor}/{name} {uid:08x}: ask for {ENGI_MOD0}..{NENGINES - 1}")
    body = img + b"".join(struct.pack("<I", w) for w in words)
    if engine_src:                                   # (the host firmware's makers and descriptions)
        prst = presets_chunk(stem, engine, npre) if npre else b""
        schm = schema_chunk(stem, engine)
    else:                                            # (the source's own: MOD3_PRESETS, MOD3_SCHEMA)
        prst = sdk_presets(e0, npre, work / (stem + ".prst"))
        schm = sdk_schema(src, work / (stem + "_dump"))
    idnt = IDNT.pack(uid, 1, 0, 0, tag.encode().ljust(4, b"\0"), vendor.encode().ljust(16, b"\0"))
    chunks = (mod_presets.chunk(mod_presets.TAG, prst) if prst else b"") + \
        (mod_presets.chunk(SCHM_TAG, schm) if schm else b"") + mod_presets.chunk(IDNT_TAG, idnt)
    seed = sign_key()
    if seed:
        chunks += mod_presets.chunk(SIGN_TAG, mod_sign.sign(seed, body + chunks))
    head = HEAD.pack(FILE_MAGIC, ABI, engine, 1, HEAD.size + len(body) + len(chunks), len(img), nbss,
                     len(words), uid, zlib.crc32(body + chunks), name.encode()[:16].ljust(16, b"\0"), version, npre, 0,
                     key, host, 0)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(head + body + chunks)
    print(f"module {name}: {vendor}/{name} uid {uid:08x} tag {tag!r}, ABI {ABI} host {host}, engine {engine}, " +
          (f"key {key:08x}, " if key else "") +
          f"image {len(img)} B, bss {nbss} B, {len(words)} address words, stack {need} B, {npre} presets {len(prst)} B, "
          f"schema {len(schm)} B, " + ("signed, " if seed else "unsigned, ") +
          f"file {len(head) + len(body) + len(chunks)} B -> {out}")


SCHM_TAG = 0x4D484353                          # "SCHM": the module's schema (firmware/modules/sdk/mod_schema.h)


def host_tool(out):
    """tools/mod_presets.c, built for the host against the host firmware (Melodee's tree: the engines and their makers
    built in)"""
    cc = os.environ.get("HOST_CC", os.environ.get("CC", "cc")).split()
    out.parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run([*cc, "-O0", "-w", "-ffp-contract=off", "-Ibuild/gen", "-Ifirmware/src", "-Itests",
                        str(SRC / "tools" / "mod_presets.c"), "-o", str(out), "-lm"], cwd=SRC, capture_output=True,
                       text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("mod_build: the host C compiler failed on tools/mod_presets.c (HOST_CC)")
    return out


def dump(tool, engine):
    """engine's presets as the host firmware makes them: (init, [(name, category, common16, patch)])"""
    r = subprocess.run([str(tool), str(engine)], cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"mod_build: tools/mod_presets.c failed for engine {engine}")
    init, presets = mod_presets.NONE, []
    for line in r.stdout.splitlines():
        f = line.split()
        if f[0] == "N":
            init = int(f[2])
        elif f[0] == "P":
            h = lambda s: b"" if s == "-" else bytes.fromhex(s)
            presets.append((h(f[3]), int(f[2]), h(f[4]), h(f[5])))
    return init, presets


def schema_chunk(stem, engine):
    """engine's schema as the host firmware has it built in (its module source's description, mod_sch_write): the
    SCHM payload, b"" none"""
    tool = host_tool(SRC / "build" / "modules" / (stem + "_presets"))
    r = subprocess.run([str(tool), "S", str(engine)], cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"mod_build: engine {engine}: its schema does not write (tools/mod_presets.c S)")
    f = r.stdout.split()
    if len(f) < 3 or f[0] != "S" or f[2] == "-":
        return b""
    payload = bytes.fromhex(f[2])
    if len(payload) != int(f[1]) or zlib.crc32(payload[52:]) != struct.unpack_from("<I", payload, 48)[0]:
        raise SystemExit(f"mod_build: engine {engine}: the schema's bytes do not check")
    return payload


PROW = struct.Struct("<21sB8b4BbB")                  # mod_sdk.h mod3_prow_t: name, category, e[8], env[4], fenv, mono


def sdk_presets(elf, npre, cache):
    """an SDK module's presets (MOD3_PRESETS in its source: the object's .mod_presets, never in the image), packed: the
    PRST payload, b"" none; the first one is the INIT"""
    d = elf.read_bytes()
    sec = elf_sections(d).get(".mod_presets")
    rows = [PROW.unpack_from(d, sec["off"] + k * PROW.size) for k in range(sec["size"] // PROW.size)] if sec else []
    if sec and sec["size"] % PROW.size:
        raise SystemExit("mod_build: .mod_presets is not a list of MOD3_PRESET rows")
    if len(rows) != npre:
        raise SystemExit(f"mod_build: MOD3_PRESETS has {len(rows)} presets, the descriptor's npresets {npre}")
    if not rows:
        return b""
    presets = []
    for name, cat, *v in rows:
        e, env, fenv, mono = v[0:8], v[8:12], v[12], v[13]
        common = struct.pack("<8b4BbB2x", *e, *env, fenv, mono)
        presets.append((name.rstrip(b"\0"), cat, common, b""))
    key = hashlib.sha256(repr((mod_presets.VERSION, 0, presets)).encode() +
                         Path(mod_presets.__file__).read_bytes()).hexdigest()
    if cache.exists() and cache.with_suffix(".prst.key").exists() and cache.with_suffix(".prst.key").read_text() == key:
        return cache.read_bytes()
    payload = mod_presets.build(presets, 0)
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_bytes(payload)
    cache.with_suffix(".prst.key").write_text(key)
    return payload


def sdk_schema(src, tool):
    """an SDK module's schema (MOD3_SCHEMA in its source, mod_schema.h), as mod_dump.c writes it from the source built
    for the host (HOST_CC, else CC, else cc): the SCHM payload, b"" none"""
    if "MOD3_SCHEMA(" not in src.read_text():
        return b""
    cc = os.environ.get("HOST_CC", os.environ.get("CC", "cc")).split()
    tool.parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run([*cc, "-O0", "-w", "-DMOD_HOSTED", "-DMOD_DUMP", f"-DMOD_SOURCE=\"{src}\"", f"-I{SDK}",
                        str(SDK / "mod_dump.c"), "-o", str(tool)], capture_output=True, text=True)
    if r.returncode or (r := subprocess.run([str(tool)], capture_output=True, text=True)).returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("mod_build: the module's schema did not write (mod_dump.c, MOD3_SCHEMA)")
    f = r.stdout.split()
    payload = bytes.fromhex(f[2]) if len(f) == 3 and f[0] == "S" and f[2] != "-" else b""
    if len(payload) != int(f[1]) or (payload and zlib.crc32(payload[52:]) != struct.unpack_from("<I", payload, 48)[0]):
        raise SystemExit("mod_build: the module's schema bytes do not check")
    return payload


def presets_chunk(stem, engine, npre):
    """engine's factory presets as the host firmware makes them, packed (tools/mod_presets.py): the PRST payload"""
    tool = host_tool(SRC / "build" / "modules" / (stem + "_presets"))
    init, presets = dump(tool, engine)
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
    magic, abi, engine, flags, total, image, bss, nrel, huid, crc, name, version, npre, _, key, host, _ = HEAD.unpack_from(b)
    end = HEAD.size + image + 4 * nrel
    if magic != FILE_MAGIC or abi != ABI or total != len(b) or \
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
    idnt = chunks.get(IDNT_TAG, b"")
    if not idnt or len(idnt) != IDNT.size:
        raise SystemExit(f"{path}: no IDNT chunk (who it is)")
    uid, iv, _, _, tag, vendor = IDNT.unpack(idnt)
    if iv != 1 or uid != uid_of(vendor.rstrip(b"\0").decode(), n) or uid != huid:
        raise SystemExit(f"{path}: its IDNT chunk's UID is not CRC-32 of vendor/name, or not the head's")
    schm = chunks.get(SCHM_TAG, b"")
    if schm and (len(schm) < 52 or zlib.crc32(schm[52:]) != struct.unpack_from("<I", schm, 48)[0]):
        raise SystemExit(f"{path}: its SCHM chunk does not check")
    print(f"{path}: {vendor.rstrip(bytes(1)).decode()}/{n} uid {uid:08x}, engine {engine}, ABI {abi} host {host}" + (f", key {key:08x} (another firmware's)" if key else "") +
          f", image {image} B, bss {bss} B, {nrel} address words, {npre} presets, schema {len(schm)} B, version {version}: ok")


WASM_TAG = 0x4D534157                          # "WASM": the module as a WebAssembly side module (Studio's core)
EMSDK_IMAGE = os.environ.get("EMSDK_IMAGE", "emscripten/emsdk:3.1.74")


def emcc(args):
    """emcc: a local one, else the emsdk Docker image (as web/core/build.sh)"""
    if shutil.which("emcc"):
        cmd = ["emcc", *args]
    else:
        cmd = ["docker", "run", "--rm", "-v", f"{SRC}:/src", "-w", "/src", "-u", f"{os.getuid()}:{os.getgid()}",
               EMSDK_IMAGE, "emcc", *args]
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"mod_build: emcc failed\n{r.stdout}{r.stderr}")


def wasm(path, out):
    """path (a built .mmod) again with its source as a WebAssembly side module in a WASM chunk, last"""
    b = bytearray(path.read_bytes())
    magic, abi, engine, flags, total, image, bss, nrel, desc, crc, name, version, npre, r0, key, host, r1 = HEAD.unpack_from(b)
    if magic != FILE_MAGIC or abi != ABI or total != len(b) or zlib.crc32(b[HEAD.size:]) != crc:
        raise SystemExit(f"{path}: not an ABI {ABI} module file")
    end = HEAD.size + image + 4 * nrel
    if flags and WASM_TAG in mod_presets.chunks(bytes(b), end):
        raise SystemExit(f"{path}: has a WASM chunk already")
    stem = path.stem
    src = SRC / "firmware" / "modules" / stem / (stem + ".c")
    if not src.exists():
        raise SystemExit(f"mod_build: no source {src.relative_to(SRC)} for {path.name}")
    rel = (out.parent / (stem + ".wasm")).resolve().relative_to(SRC)
    flags_c = ["-O2", "-ffp-contract=off", "-fPIC", "-sSIDE_MODULE=1", "-w", "-Ifirmware/modules/sdk", "-DMOD_IN_CORE=1"]
    flags_c += ["-Ifirmware/hal", "-Ibuild/gen", "-Ifirmware/src"] if is_engine(src) else ["-DMOD_HOSTED"]
    out.parent.mkdir(parents=True, exist_ok=True)
    emcc([*flags_c, str(src.relative_to(SRC)), "-o", str(rel)])
    w = (SRC / rel).read_bytes()
    body = bytes(b[HEAD.size:]) + mod_presets.chunk(WASM_TAG, w)
    head = HEAD.pack(magic, abi, engine, 1, HEAD.size + len(body), image, bss, nrel, desc, zlib.crc32(body), name, version,
                     npre, r0, key, host, r1)
    out.write_bytes(head + body)
    print(f"module {name.rstrip(bytes(1)).decode()}: WebAssembly {len(w)} B, file {len(head) + len(body)} B -> {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("src", type=Path)
    ap.add_argument("-o", "--out", type=Path)
    ap.add_argument("--version", type=int, default=1)
    ap.add_argument("--check", action="store_true", help="check a built .mmod file instead")
    ap.add_argument("--wasm", action="store_true", help="a built .mmod file with its module built for WebAssembly")
    a, rest = ap.parse_known_args()
    if a.check:
        return check(a.src)
    if a.wasm:
        if not SRC:
            raise SystemExit("mod_build: --wasm builds in Melodee's tree only")
        return wasm(a.src.resolve(), (a.out or SRC / "build" / "modules" / "wasm" / a.src.name).resolve())
    src = a.src.resolve()
    out = (a.out or (SRC / "build" / "modules" if SRC else src.parent) / (src.stem + ".mmod")).resolve()
    if any(not d.startswith("-D") for d in rest):
        raise SystemExit(f"mod_build: unknown arguments {rest}")
    build(src, out, a.version, rest)


if __name__ == "__main__":
    main()
