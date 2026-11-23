#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Build a synth module (ABI 3: mod_abi3.h) from one C file: a .mmod file. The SDK's builder: this folder alone builds
an SDK module (mod_sdk.h), anywhere; in Melodee's tree it builds Melodee's engine-source modules too.

    mod_build.py SRC.c [-o OUT.mmod] [--version N] [-DNAME=V ...]
    mod_build.py --check FILE.mmod     (its head, CRC, address list, descriptor, presets, schema, who it is, signature)
    mod_build.py --wasm FILE.mmod [--src SRC.c] [-o OUT.mmod]   (the file with its module built for WebAssembly, a WASM
                                       chunk; SRC.c: its source, outside Melodee's tree)
    mod_build.py --native FILE.mmod [--src SRC.c] [-o OUT.mmod] (the file with its module built for AArch64, an AA64
                                       chunk: the plug-in hosts' code, melodee-modules; a signed module's signed
                                       again with it by the same key, an NSIG chunk)

Needs: Python 3, a host C compiler (cc, or HOST_CC: a module's schema is written by its own source, built natively),
and the JieLi pi32v2 toolchain (JIELI_TOOLCHAIN, else ~/.jieli/toolchain; off Linux x86-64 it runs in Docker,
JIELI_DOCKER_IMAGE). Signs the file with its author's key (MELODEE_AUTHOR_KEY, a file; "" none: unsigned; else
~/.config/melodee/author.key, made on the first build: keep it, it is who you are) and names that key in the IDNT
chunk: its ID comes from the key and its slug (mod_abi3.h), so only its author makes a file with that ID. Melodee's own
synths (vendor "melodee") are Melodee's key's: MELODEE_SIGN_KEY, else ~/.config/melodee/module-sign.key, else in
Melodee's tree the test key (mod_sign.py). A module without a signature installs with a one-line warning.

Two kinds of source, one file format:
  - an SDK module (#include "mod_sdk.h": TRIO, WHEEL, an outside author's synth) sees only the contract, built -Os
    without any firmware option. What it is comes from its source: MOD3_EXPORT (the descriptor), MOD3_IDENT (vendor,
    slug, tag: its ID from the slug and its key), MOD3_PRESETS (its factory presets, the first one the INIT), MOD3_SCHEMA
    (its parameters and pages, mod_schema.h: written by mod_dump.c, the source built for the host);
  - an engine-source module (#include "../mod_engine.h": SID, DRUM, CZ-1, FM6, the Prophet, Melodee's tree only) is a
    synth written against the firmware's engine headers, built as tools/build.py builds the image's audio code (IR,
    tools/size_fns.py, -O2) with the image's -D flags; its presets and schema are the host firmware's (tools/mod_presets.c).
Either links at address 0 (module.ld, mod_rt.c) with --emit-relocs and again at another base. The absolute words come
from the relocations (R_PI32V2_ABS32: the word at r_offset; R_PI32V2_MOV_ABS32: a 6-byte `rX = imm32`, the word at
r_offset + 2); every other relocation must be PC-relative inside the image. The two links must differ exactly in those
words, by the base difference: a word the list misses (an absolute address in some other form) fails the build instead
of crashing the device.

The image is packed in the file when that is smaller (mod_abi3.h MOD3_FILE_PACKED: LZMA, Python's lzma module; the
loader unpacks it into SRAM). The chunks after the address words: PRST (the presets, mod_presets.py), SCHM (the schema),
IDNT (who it is), SIGN (the signature, last).

--wasm: the same file with a WASM chunk last, the module's source built as a WebAssembly side module (emcc -fPIC
-sSIDE_MODULE, MOD_IN_CORE). Melodee Studio's virtual FM-1 (web/core: CORE_MODULES) links it into its own memory and
table and installs the file without the chunk: the device's file, byte for byte. emcc: a local one, else the emsdk
Docker image web/core/build.sh uses (EMSDK_IMAGE). Written to build/modules/wasm/<name>.mmod.

--native: the same file with an AA64 chunk last, an SDK module's source built for AArch64 (Linux on the MPC Sample's A55,
macOS on Apple silicon: x18 left alone, the platform register there) with clang and ld.lld (NATIVE_CC, NATIVE_LD),
freestanding as on the device (mod_rt.c, no library). It links twice as the device's image does (module_a64.ld: the
descriptor first, the data on 16 KB pages of its own) and keeps the 64-bit absolute words (R_AARCH64_ABS64) as its
address list. The chunk: version, the code's bytes (code, constants, the descriptor and engine, read-only once moved),
where the data begins, the data's bytes, .bss, the address words' count (six u32), then the code, the data and the
words (u32 offsets of 8-byte words the loader adds its base to). A plug-in host (melodee-modules) runs it; the device
never sees it: like WASM it follows SIGN, outside the signature, and a tool sending the file to the device leaves it
out. Written to build/modules/native/<name>.mmod."""
import argparse
import hashlib
import lzma
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
IDNT_TAG = 0x544E4449                                # "IDNT": who the module is (mod_abi3.h mod_ident_v3)
SIGN_TAG = 0x4E474953                                # "SIGN": Ed25519 of the bytes from the head's end to it, the last


IDNT = struct.Struct("<8sBBH4s16s16s32s16x")         # mod_ident_v3: id, version, flags, rsv, tag, slug, vendor, key
IDNT_VERSION, IDNT_SIGNED = 2, 1
MELODEE_VENDOR = "melodee"
# Melodee's release key (firmware/src/mod_sign_keys.h): every Melodee synth's identity key, whichever of Melodee's keys
# signs it; the test key's seed (the host builds' and a tree without the release key's: firmware/src/mod_sign_keys.h)
MELODEE_KEY = bytes.fromhex("45442ab682efbecc08950a41f914fefc2e29c472a8f0d364d02804e0708f7189")
TEST_SEED = hashlib.sha256(b"melodee module test key").digest()
MELODEE_SIGNERS = (MELODEE_KEY, mod_sign.public(TEST_SEED))


def key_file(var, default):
    """a key's seed: the file the environment variable names ("" none; a file not there stops the build), else default
    (None: not there)"""
    path = os.environ.get(var, str(default))
    if os.environ.get(var) and not Path(path).is_file():
        raise SystemExit(f"mod_build: {var}={path}: no such key file (\"\" builds unsigned)")
    return mod_sign.load(path) if path and Path(path).is_file() else None


def author_seed(make=True):
    """the author's key: MELODEE_AUTHOR_KEY ("" none: unsigned), else ~/.config/melodee/author.key, made if missing
    (make)"""
    path = os.environ.get("MELODEE_AUTHOR_KEY", str(Path.home() / ".config" / "melodee" / "author.key"))
    if not path:
        return None
    if "MELODEE_AUTHOR_KEY" in os.environ and not Path(path).is_file():
        raise SystemExit(f"mod_build: MELODEE_AUTHOR_KEY={path}: no such key file (\"\" builds unsigned)")
    if make and not Path(path).is_file() and "MELODEE_AUTHOR_KEY" not in os.environ:
        seed = os.urandom(32)
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        os.write(fd, (seed.hex() + "\n").encode())
        os.close(fd)
        print(f"mod_build: your author key is new: {path} (public {mod_sign.public(seed).hex()}). Keep a copy: your "
              "synths' IDs come from it, and an update needs the same key", file=sys.stderr)
    return mod_sign.load(path) if Path(path).is_file() else None


def melodee_seed(inside):
    """Melodee's signing key: MELODEE_SIGN_KEY ("" none), else ~/.config/melodee/module-sign.key, else in Melodee's tree
    the test key (a tree without the release key builds firmware that takes the test key: mod_sign_keys.h)"""
    seed = key_file("MELODEE_SIGN_KEY", Path.home() / ".config" / "melodee" / "module-sign.key")
    if seed is None and inside and os.environ.get("MELODEE_SIGN_KEY") is None:
        seed = TEST_SEED
    if seed is not None and mod_sign.public(seed) not in MELODEE_SIGNERS:
        raise SystemExit("mod_build: MELODEE_SIGN_KEY is not one of Melodee's keys (mod_sign_keys.h)")
    return seed


def module_id(key, slug, vendor):
    """the ID (mod_abi3.h): SHA-512 of "melodee/synth/1", key, slug (signed), or of "melodee/unsigned/1", vendor, "/",
    slug (key None), its first 8 bytes"""
    m = b"melodee/synth/1" + key + slug.encode() if key else b"melodee/unsigned/1" + vendor.encode() + b"/" + slug.encode()
    return hashlib.sha512(m).digest()[:8]


def ident(elf, name):
    """(vendor, slug, tag) from the object's .mod_ident (MOD3_IDENT in the source)"""
    d = elf.read_bytes()
    sec = elf_sections(d).get(".mod_ident")
    if not sec or sec["size"] != 36:
        raise SystemExit("mod_build: no MOD3_IDENT(\"vendor\", \"slug\", \"TAG\") in the module's source (mod_abi3.h)")
    raw = d[sec["off"]: sec["off"] + 36]
    vendor, slug, tag = (raw[a:b].rstrip(b"\0").decode("ascii") for a, b in ((0, 16), (16, 32), (32, 36)))
    if not vendor or not all(32 <= ord(c) < 127 for c in vendor) or not re.fullmatch(r"[a-z0-9-]{1,16}", slug) or \
            "\0" in tag or not all(32 <= ord(c) < 127 for c in tag):
        raise SystemExit(f"mod_build: MOD3_IDENT: vendor {vendor!r} (printable), slug {slug!r} (lowercase letters, "
                         f"digits, '-'), tag {tag!r}")
    return vendor, slug, tag


def abi_num(name):
    return int(re.search(rf"#define {name} (0x[0-9A-Fa-f]+|\d+)u", ABI_H)[1], 0)


FILE_MAGIC, MAGIC, ABI, HOST = abi_num("MOD3_FILE_MAGIC"), abi_num("MOD3_MAGIC"), abi_num("MOD3_ABI"), abi_num("MOD3_HOST")
STACK_MAX, IMAGE_MAX, BSS_MAX = abi_num("MOD3_STACK_MAX"), abi_num("MOD3_IMAGE_MAX"), abi_num("MOD3_BSS_MAX")
HEAD = struct.Struct("<IHBBIIIIII16sHBBIHH")        # mod_file3_t, 60 bytes (its uid where ABI 3 had desc 0)
assert HEAD.size == abi_num("MOD3_HEAD")
DESC = struct.Struct("<IHHHHBBBBIIII")              # mod_desc_v3 on pi32v2, 32 bytes (eng_module.c asserts it)
ENG_NOTE_ON, ENG_RENDER = 188, 192                  # mod_engine_v3's note_on and render on pi32v2 (eng_module.c)
FILE_CHUNKS, FILE_PACKED = abi_num("MOD3_FILE_CHUNKS"), abi_num("MOD3_FILE_PACKED")
PACK_LCLP, PACK_PB = abi_num("MOD3_PACK_LCLP"), abi_num("MOD3_PACK_PB")
# a packed image's encoder settings (the loader never sees them): the smallest file wins
PACK_EFFORT = [{"mode": lzma.MODE_NORMAL, "mf": mf, "nice_len": nice, "depth": 0}
               for mf in (lzma.MF_BT2, lzma.MF_BT4) for nice in (64, 273)]


def pack_filter(lc, lp, pb, **effort):
    return [{"id": lzma.FILTER_LZMA1, "lc": lc, "lp": lp, "pb": pb, "dict_size": 1 << 16, **effort}]


def pack_image(img):
    """the image packed as module_store.c unpacks it (mod_abi3.h MOD3_FILE_PACKED: the props byte, raw LZMA1 with its
    end marker, zero bytes to a word) with the props and the settings that make it smallest; None when that is no
    smaller than the image"""
    best = None
    for lc in range(PACK_LCLP + 1):
        for lp in range(PACK_LCLP + 1 - lc):
            for pb in range(PACK_PB + 1):
                for effort in PACK_EFFORT:
                    data = lzma.compress(img, format=lzma.FORMAT_RAW, filters=pack_filter(lc, lp, pb, **effort))
                    if best is None or 1 + len(data) < len(best):
                        best = bytes([lc + 9 * lp + 45 * pb]) + data
    best += bytes(-len(best) % 4)
    if unpack_image(best, len(img)) != img:
        raise SystemExit("mod_build: the packed image does not unpack to the image")
    return best if len(best) < len(img) else None


def unpack_image(b, size):
    """a packed image back, as strictly as the loader takes it (mod_unpack.h)"""
    pr = b[0] if b else 255
    lc, lp, pb = pr % 9, pr // 9 % 5, pr // 45
    if pr >= 225 or lc + lp > PACK_LCLP or pb > PACK_PB:
        raise SystemExit("mod_build: a packed image's props are not ones the loader takes")
    d = lzma.LZMADecompressor(lzma.FORMAT_RAW, filters=pack_filter(lc, lp, pb))
    try:
        out = d.decompress(b[1:])
    except lzma.LZMAError as e:
        raise SystemExit(f"mod_build: a packed image does not unpack ({e})")
    if not d.eof or len(out) != size or len(d.unused_data) > 3 or any(d.unused_data):
        raise SystemExit("mod_build: a packed image is not its image's size, or not to its end marker")
    return out


def toolchain():
    tc = Path(os.environ.get("JIELI_TOOLCHAIN", Path.home() / ".jieli" / "toolchain")).resolve()
    if not (tc / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit(f"mod_build: no JieLi toolchain in {tc} (sh {SDK / 'get_toolchain.sh'})")
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
    vendor, slug, tag = ident(e0, name)
    melodee = vendor == MELODEE_VENDOR
    seed = melodee_seed(inside) if melodee else author_seed()
    ikey = (MELODEE_KEY if melodee else mod_sign.public(seed)) if seed else None
    mid = module_id(ikey, slug, vendor)
    uid = struct.unpack_from("<I", mid)[0]
    if not uid:
        raise SystemExit(f"mod_build: {vendor}/{slug}'s ID begins with 4 zero bytes: pick another slug")
    packed = pack_image(img)                         # (the file holds it packed when that is smaller)
    body = (packed or img) + b"".join(struct.pack("<I", w) for w in words)
    if engine_src:                                   # (the host firmware's makers and descriptions)
        prst = presets_chunk(stem, engine, npre) if npre else b""
        schm = schema_chunk(stem, engine)
    else:                                            # (the source's own: MOD3_PRESETS, MOD3_SCHEMA)
        prst = sdk_presets(e0, npre, work / (stem + ".prst"))
        schm = sdk_schema(src, work / (stem + "_dump"))
    idnt = IDNT.pack(mid, IDNT_VERSION, IDNT_SIGNED if ikey else 0, 0, tag.encode().ljust(4, b"\0"),
                     slug.encode().ljust(16, b"\0"), vendor.encode().ljust(16, b"\0"), ikey or bytes(32))
    chunks = (mod_presets.chunk(mod_presets.TAG, prst) if prst else b"") + \
        (mod_presets.chunk(SCHM_TAG, schm) if schm else b"") + mod_presets.chunk(IDNT_TAG, idnt)
    if ikey:
        chunks += mod_presets.chunk(SIGN_TAG, mod_sign.sign(seed, body + chunks))
    head = HEAD.pack(FILE_MAGIC, ABI, engine, FILE_CHUNKS | (FILE_PACKED if packed else 0),
                     HEAD.size + len(body) + len(chunks), len(packed or img), nbss, len(words), uid,
                     zlib.crc32(body + chunks), name.encode()[:16].ljust(16, b"\0"), version, npre, 0, key, host,
                     len(img) if packed else 0)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(head + body + chunks)
    signer = ("Melodee's key" + (" (test)" if seed == TEST_SEED else "")) if melodee and ikey else \
        f"key {ikey.hex()[:16]}" if ikey else "unsigned"
    print(f"module {name}: {vendor}/{slug} ID {mid[::-1].hex()} ({signer}) tag {tag!r}, ABI {ABI} host {host}, engine {engine}, " +
          (f"key {key:08x}, " if key else "") +
          f"image {len(img)} B" + (f" (packed {len(packed)} B)" if packed else "") +
          f", bss {nbss} B, {len(words)} address words, stack {need} B, {npre} presets {len(prst)} B, "
          f"schema {len(schm)} B, " + ("signed, " if ikey else "unsigned, ") +
          f"file {len(head) + len(body) + len(chunks)} B -> {out}")


SCHM_TAG = 0x4D484353                          # "SCHM": the module's schema (firmware/modules/sdk/mod_schema.h)


def host_tool(out):
    """tools/mod_presets.c, built for the host against the host firmware (Melodee's tree: the engines and their makers
    built in)"""
    cc = os.environ.get("HOST_CC", os.environ.get("CC", "cc")).split()
    out.parent.mkdir(parents=True, exist_ok=True)
    # (-O1, as the host tests build: gcc at -O0 emits the hal's always-inline functions, the FM-1's csync with them)
    r = subprocess.run([*cc, "-O1", "-w", "-ffp-contract=off", "-Ibuild/gen", "-Ifirmware/src", "-Itests",
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
    size = sec["size"] if sec else 0                  # (rows of PROW, each with its patch after it: MOD3_PRESETS_PATCH)
    step = size // npre if npre else PROW.size
    if size and (not npre or size % npre or step < PROW.size):
        raise SystemExit(f"mod_build: .mod_presets ({size} B) is not npresets ({npre}) MOD3_PRESETS rows")
    rows = [d[sec["off"] + k * step: sec["off"] + (k + 1) * step] for k in range(size // step)] if size else []
    if len(rows) != npre:
        raise SystemExit(f"mod_build: MOD3_PRESETS has {len(rows)} presets, the descriptor's npresets {npre}")
    if not rows:
        return b""
    presets = []
    for r in rows:
        name, cat, *v = PROW.unpack_from(r)
        e, env, fenv, mono = v[0:8], v[8:12], v[12], v[13]
        common = struct.pack("<8b4BbB2x", *e, *env, fenv, mono)
        presets.append((name.rstrip(b"\0"), cat, common, bytes(r[PROW.size:])))
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
    magic, abi, engine, flags, total, image, bss, nrel, huid, crc, name, version, npre, _, key, host, unpacked = \
        HEAD.unpack_from(b)
    end = HEAD.size + image + 4 * nrel
    if magic != FILE_MAGIC or abi != ABI or total != len(b) or flags & ~(FILE_CHUNKS | FILE_PACKED) or \
            (total < end + 8 if flags & FILE_CHUNKS else total != end) or bool(flags & FILE_PACKED) != bool(unpacked):
        raise SystemExit(f"{path}: not an ABI {ABI} module file")
    if zlib.crc32(b[HEAD.size:]) != crc:
        raise SystemExit(f"{path}: CRC mismatch")
    img = b[HEAD.size: HEAD.size + image]
    if flags & FILE_PACKED:
        img = unpack_image(img, unpacked)
    words = [u32(b, HEAD.size + image + 4 * k) for k in range(nrel)]
    e, n, k, kk, hh = describe(img, words, bss)
    if e != engine or n.encode()[:16] != name.rstrip(b"\0") or k != npre or kk != key or hh != host:
        raise SystemExit(f"{path}: head and descriptor differ")
    chunks = mod_presets.chunks(b, end) if flags & FILE_CHUNKS else {}
    tags = [t for t, _ in chunk_places(b, end)] if flags & FILE_CHUNKS else []
    if len(set(tags)) != len(tags):
        raise SystemExit(f"{path}: a chunk twice")
    if npre or mod_presets.TAG in chunks:
        if mod_presets.TAG not in chunks:
            raise SystemExit(f"{path}: {npre} presets, no PRST chunk")
        init, presets = mod_presets.parse(chunks[mod_presets.TAG])
        if len(presets) != npre:
            raise SystemExit(f"{path}: PRST holds {len(presets)} presets, the head says {npre}")
    idnt = chunks.get(IDNT_TAG, b"")
    if not idnt or len(idnt) != IDNT.size:
        raise SystemExit(f"{path}: no IDNT chunk (who it is)")
    mid, iv, iflags, _, tag, slug, vendor, key = IDNT.unpack(idnt)
    slug, vendor = slug.rstrip(b"\0").decode(), vendor.rstrip(b"\0").decode()
    signed = iflags & IDNT_SIGNED
    if iv != IDNT_VERSION or mid != module_id(key if signed else None, slug, vendor) or \
            struct.unpack_from("<I", mid)[0] != huid:
        raise SystemExit(f"{path}: its IDNT chunk's ID is not made from its key and slug, or not the head's")
    sig, covered, at = chunks.get(SIGN_TAG, b""), b"", end
    while sig and at + 8 <= len(b):                  # (the bytes from the head's end to the SIGN chunk)
        tag_at, n_at = struct.unpack_from("<II", b, at)
        if tag_at == SIGN_TAG:
            covered = b[HEAD.size: at]
            break
        at += 8 + ((n_at + 3) & ~3)
    if signed and (len(sig) != 64 or not any(mod_sign.verify(k, covered, sig) for k in
                                             (MELODEE_SIGNERS if key == MELODEE_KEY else (key,)))):
        raise SystemExit(f"{path}: not signed by the key its IDNT chunk names")
    schm = chunks.get(SCHM_TAG, b"")
    if schm and (len(schm) < 52 or zlib.crc32(schm[52:]) != struct.unpack_from("<I", schm, 48)[0]):
        raise SystemExit(f"{path}: its SCHM chunk does not check")
    nat = chunks.get(NATIVE_TAG, b"")
    if nat:
        if len(nat) < NATIVE_HEAD.size:
            raise SystemExit(f"{path}: its AA64 chunk is too short")
        nv, ncode, ndat_at, ndat, _, nw = NATIVE_HEAD.unpack_from(nat)
        if nv != NATIVE_VERSION or len(nat) != NATIVE_HEAD.size + ncode + ndat + 4 * nw or ndat_at % NATIVE_PAGE or \
                ndat_at < ncode or struct.unpack_from("<I", nat, NATIVE_HEAD.size)[0] != MAGIC:
            raise SystemExit(f"{path}: its AA64 chunk does not check")
        for k in range(nw):
            w = struct.unpack_from("<I", nat, NATIVE_HEAD.size + ncode + ndat + 4 * k)[0]
            if not (w + 8 <= ncode or ndat_at <= w and w + 8 <= ndat_at + ndat):
                raise SystemExit(f"{path}: its AA64 chunk lists a word outside its code and data")
        at = dict((t, a) for t, a in chunk_places(b, end))
        if signed and not native_signed(b, at.get(NATIVE_TAG), at.get(NSIG_TAG),
                                        MELODEE_SIGNERS if key == MELODEE_KEY else (key,)):
            raise SystemExit(f"{path}: its AArch64 code is not signed by the key its IDNT chunk names (NSIG)")
    signer = ("Melodee's key" if key == MELODEE_KEY else f"key {key.hex()}") if signed else "unsigned"
    print(f"{path}: {vendor}/{slug} ({n}) ID {mid[::-1].hex()}, {signer}, engine {engine}, ABI {abi} host {host}" +
          f", image {len(img)} B" + (f" (packed {image} B)" if flags & FILE_PACKED else "") +
          f", bss {bss} B, {nrel} address words, {npre} presets, schema {len(schm)} B, version {version}" +
          (f", AArch64 {len(nat)} B" + (" signed" if signed else "") if nat else "") +
          (", WebAssembly" if WASM_TAG in chunks else "") + ": ok")


WASM_TAG = 0x4D534157                          # "WASM": the module as a WebAssembly side module (Studio's core)
EMSDK_IMAGE = os.environ.get("EMSDK_IMAGE", "emscripten/emsdk:3.1.74")


def emcc(args, root):
    """emcc in root (its paths relative to it): a local one, else the emsdk Docker image (as web/core/build.sh)"""
    if shutil.which("emcc"):
        cmd = ["emcc", *args]
    else:
        cmd = ["docker", "run", "--rm", "-v", f"{root}:/src", "-w", "/src", "-u", f"{os.getuid()}:{os.getgid()}",
               EMSDK_IMAGE, "emcc", *args]
    r = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"mod_build: emcc failed\n{r.stdout}{r.stderr}")


def wasm(path, out, src=None):
    """path (a built .mmod) again with its source (src; Melodee's tree: firmware/modules/<name>/<name>.c) as a
    WebAssembly side module in a WASM chunk, last"""
    b = bytearray(path.read_bytes())
    magic, abi, engine, flags, total, image, bss, nrel, desc, crc, name, version, npre, r0, key, host, r1 = HEAD.unpack_from(b)
    if magic != FILE_MAGIC or abi != ABI or total != len(b) or zlib.crc32(b[HEAD.size:]) != crc:
        raise SystemExit(f"{path}: not an ABI {ABI} module file")
    end = HEAD.size + image + 4 * nrel
    if flags & FILE_CHUNKS and WASM_TAG in mod_presets.chunks(bytes(b), end):
        raise SystemExit(f"{path}: has a WASM chunk already")
    stem = path.stem
    src = src or (SRC / "firmware" / "modules" / stem / (stem + ".c") if SRC else None)
    if not src or not src.exists():
        raise SystemExit(f"mod_build: no source for {path.name} (--src)")
    if is_engine(src) and not SRC:
        raise SystemExit("mod_build: an engine-source module (mod_engine.h) builds in Melodee's tree only")
    out.parent.mkdir(parents=True, exist_ok=True)
    inside = SRC and all(SRC in p.parents for p in (src, out, SDK))
    root = SRC if inside else Path(os.path.commonpath([SDK, src, out.parent]))   # (the folder emcc sees)
    w_out = out.parent / (stem + ".wasm")
    rel = lambda p: str(p.resolve().relative_to(root))
    flags_c = ["-O2", "-ffp-contract=off", "-fPIC", "-sSIDE_MODULE=1", "-w", "-I" + rel(SDK), "-DMOD_IN_CORE=1"]
    flags_c += ["-Ifirmware/hal", "-Ibuild/gen", "-Ifirmware/src"] if is_engine(src) else ["-DMOD_HOSTED"]
    emcc([*flags_c, rel(src), "-o", rel(w_out)], root)
    w = w_out.read_bytes()
    body = bytes(b[HEAD.size:]) + mod_presets.chunk(WASM_TAG, w)
    head = HEAD.pack(magic, abi, engine, flags | FILE_CHUNKS, HEAD.size + len(body), image, bss, nrel, desc,
                     zlib.crc32(body), name, version, npre, r0, key, host, r1)
    out.write_bytes(head + body)
    print(f"module {name.rstrip(bytes(1)).decode()}: WebAssembly {len(w)} B, file {len(head) + len(body)} B -> {out}")


NATIVE_TAG = 0x34364141                        # "AA64": the module built for AArch64 (melodee-modules' plug-in host)
NATIVE_HEAD = struct.Struct("<IIIIII")         # version, code bytes, data's place, data bytes, bss, address words
NATIVE_VERSION = 1
NATIVE_PAGE = 0x4000                           # (module_a64.ld: the data's alignment)
NSIG_TAG = 0x4749534E                          # "NSIG": a signed module's AArch64 code signed by its key (mod_abi3.h)
NSIG_PREFIX = b"melodee/native/1"              # (MOD3_NSIG_PREFIX: the signed bytes' first)
HEAD_CRC = 28                                  # (the head's crc: the one head field NSIG does not cover)


def nsig_message(head, body):
    """what NSIG signs: MOD3_NSIG_PREFIX, the head without its crc and the body up to the NSIG chunk"""
    return NSIG_PREFIX + bytes(head[:HEAD_CRC]) + bytes(head[HEAD_CRC + 4: HEAD.size]) + bytes(body)
R_AARCH64_ABS64 = 257
NATIVE_CFLAGS = ["--target=aarch64-none-elf", "-mcpu=cortex-a55", "-O2", "-ffp-contract=off", "-ffreestanding",
                 "-fno-builtin", "-fno-pic", "-mcmodel=small", "-ffixed-x18", "-fno-stack-protector",
                 "-fno-asynchronous-unwind-tables", "-fno-unwind-tables", "-ffunction-sections", "-fdata-sections",
                 "-mno-outline-atomics", "-Wall", "-Werror", "-Wno-unused-function"]


def elf64_sections(d):
    shoff, = struct.unpack_from("<Q", d, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x3A)
    sh = [struct.unpack_from("<IIQQQQIIQQ", d, shoff + i * shentsize) for i in range(shnum)]
    names = sh[shstrndx][4]
    out = []
    for s in sh:
        nm = d[names + s[0]: d.index(b"\0", names + s[0])].decode()
        out.append(dict(name=nm, type=s[1], addr=s[3], off=s[4], size=s[5], info=s[7]))
    return out


def native_image(elf, base):
    """an AArch64 link at base: its code, the data's place (from base) and bytes, .bss, the 8-byte address words (from
    base, in the image as it lies in memory)"""
    d = elf.read_bytes()
    secs = elf64_sections(d)
    sec = {s["name"]: s for s in secs}
    text, data, bss = sec[".text"], sec.get(".data"), sec.get(".bss")
    if text["addr"] != base:
        raise SystemExit(f"mod_build: .text at {text['addr']:#x}, not {base:#x}")
    code = d[text["off"]: text["off"] + text["size"]]
    data_at = (data["addr"] - base) if data else (len(code) + NATIVE_PAGE - 1) & ~(NATIVE_PAGE - 1)
    dat = d[data["off"]: data["off"] + data["size"]] if data and data["size"] else b""
    if data_at % NATIVE_PAGE or data_at < len(code):
        raise SystemExit(f"mod_build: the data at {data_at:#x} is not on its own {NATIVE_PAGE:#x} page")
    nbss = bss["size"] if bss else 0
    if nbss and bss["addr"] - base != data_at + len(dat):
        raise SystemExit("mod_build: .bss does not follow the data")
    words = set()
    for s in secs:
        if s["type"] != 4 or secs[s["info"]]["name"] not in (".text", ".data"):   # SHT_RELA of the image's sections
            continue
        for k in range(s["size"] // 24):
            off, info, _ = struct.unpack_from("<QQq", d, s["off"] + 24 * k)
            if info & 0xFFFFFFFF == R_AARCH64_ABS64:
                words.add(off - base)
    for w in words:
        if not (0 <= w and w + 8 <= len(code) or data_at <= w and w + 8 <= data_at + len(dat)):
            raise SystemExit(f"mod_build: address word at {w:#x} outside the code and the data")
    return code, data_at, dat, nbss, sorted(words)


def chunk_places(b, at):
    """a module file's chunks after its address words (at): [(tag, the place of its 8-byte head), ...]"""
    out = []
    while at + 8 <= len(b):
        tag, n = struct.unpack_from("<II", b, at)
        out.append((tag, at))
        at += 8 + ((n + 3) & ~3)
    return out


def native_signed(b, aa64_at, nsig_at, keys):
    """1: the NSIG chunk at nsig_at, after the AA64 chunk at aa64_at, is one of keys' signature of the file before it
    (nsig_message)"""
    if aa64_at is None or nsig_at is None or nsig_at < aa64_at or u32(b, nsig_at + 4) != 64:
        return 0
    sig = bytes(b[nsig_at + 8: nsig_at + 72])
    return any(mod_sign.verify(k, nsig_message(b[:HEAD.size], b[HEAD.size: nsig_at]), sig) for k in keys)


def native_seed(path, b, end):
    """the key that signs a module's AArch64 code: its IDNT chunk's (the author's, else Melodee's for Melodee's synths),
    once the file's own signature checks; None: an unsigned module"""
    at = dict((t, a) for t, a in chunk_places(b, end))
    if IDNT_TAG not in at or u32(b, at[IDNT_TAG] + 4) != IDNT.size:
        raise SystemExit(f"{path}: no IDNT chunk (who it is): build it again with this SDK")
    _, _, iflags, _, _, _, _, key = IDNT.unpack_from(b, at[IDNT_TAG] + 8)
    if not iflags & IDNT_SIGNED:
        return None
    keys = MELODEE_SIGNERS if key == MELODEE_KEY else (key,)
    sig = bytes(b[at[SIGN_TAG] + 8: at[SIGN_TAG] + 72]) if SIGN_TAG in at else b""
    if len(sig) != 64 or not any(mod_sign.verify(k, bytes(b[HEAD.size: at[SIGN_TAG]]), sig) for k in keys):
        raise SystemExit(f"{path}: not signed by the key its IDNT chunk names")
    if key == MELODEE_KEY:
        seed = melodee_seed(SRC is not None)
        if seed is None:
            raise SystemExit("mod_build: a Melodee synth's AArch64 code is signed with Melodee's key (MELODEE_SIGN_KEY)")
        return seed
    seed = author_seed(make=False)
    if seed is None or mod_sign.public(seed) != key:
        raise SystemExit(f"mod_build: {path.name} is signed by key {key.hex()[:16]}; its AArch64 code is signed with "
                         "the same key (MELODEE_AUTHOR_KEY, else ~/.config/melodee/author.key)")
    return seed


def native(path, out, src=None):
    """path (a built .mmod) again with its source (src; Melodee's tree: firmware/modules/<name>/<name>.c) built for
    AArch64 in an AA64 chunk, last"""
    b = bytearray(path.read_bytes())
    magic, abi, engine, flags, total, image, bss, nrel, desc, crc, name, version, npre, r0, key, host, r1 = HEAD.unpack_from(b)
    if magic != FILE_MAGIC or abi != ABI or total != len(b) or zlib.crc32(b[HEAD.size:]) != crc:
        raise SystemExit(f"{path}: not an ABI {ABI} module file")
    end = HEAD.size + image + 4 * nrel
    if flags & FILE_CHUNKS and NATIVE_TAG in mod_presets.chunks(bytes(b), end):
        raise SystemExit(f"{path}: has an AA64 chunk already")
    seed = native_seed(path, b, end) if flags & FILE_CHUNKS else None
    stem = path.stem
    src = src or (SRC / "firmware" / "modules" / stem / (stem + ".c") if SRC else None)
    if not src or not src.exists():
        raise SystemExit(f"mod_build: no source for {path.name} (--src)")
    if is_engine(src):
        raise SystemExit("mod_build: --native builds SDK modules (mod_sdk.h); an engine-source module needs the "
                         "firmware's headers and tables")
    cc, ld = os.environ.get("NATIVE_CC", "clang"), os.environ.get("NATIVE_LD", "ld.lld")
    for tool in (cc, ld):
        if not shutil.which(tool):
            raise SystemExit(f"mod_build: --native needs {tool} (LLVM's clang and ld.lld: NATIVE_CC, NATIVE_LD)")
    work = out.parent / ".mod_native"
    work.mkdir(parents=True, exist_ok=True)
    obj, rt = work / (stem + ".o"), work / "mod_rt.o"
    e0, e1 = work / (stem + ".elf"), work / (stem + ".b2.elf")
    inc = ["-I" + str(SDK)]

    def run(cmd):
        r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
        if r.returncode:
            sys.stderr.write(r.stdout + r.stderr)
            raise SystemExit("mod_build: the AArch64 build failed")

    run([cc, *NATIVE_CFLAGS, *inc, "-c", src, "-o", obj])
    run([cc, *NATIVE_CFLAGS, "-c", SDK / "mod_rt.c", "-o", rt])
    ldf = ["--gc-sections", "--emit-relocs", "-T", SDK / "module_a64.ld"]
    run([ld, *ldf, "--defsym=MOD_BASE=0", obj, rt, "-o", e0])
    run([ld, *ldf, f"--defsym=MOD_BASE={BASE2:#x}", obj, rt, "-o", e1])
    code, data_at, dat, nbss, words = native_image(e0, 0)
    code1, data_at1, dat1, nbss1, words1 = native_image(e1, BASE2)
    if (len(code), data_at, len(dat), nbss, words) != (len(code1), data_at1, len(dat1), nbss1, words1):
        raise SystemExit("mod_build: the two AArch64 links differ in layout")
    span0 = code + bytes(data_at - len(code)) + dat
    span1 = code1 + bytes(data_at - len(code1)) + dat1
    listed = set()
    for w in words:
        a, c = struct.unpack_from("<Q", span0, w)[0], struct.unpack_from("<Q", span1, w)[0]
        if (c - a) & 0xFFFFFFFFFFFFFFFF != BASE2:
            raise SystemExit(f"mod_build: listed word {w:#x} moved by {(c - a):#x}, not {BASE2:#x}")
        listed.update(range(w, w + 8))
    for i, (x, y) in enumerate(zip(span0, span1)):
        if x != y and i not in listed:
            raise SystemExit(f"mod_build: byte {i:#x} depends on the base but no address word covers it "
                             "(an absolute address the loader would not move)")
    dmagic, dabi, _, dhost, _, deng, dnpre = struct.unpack_from("<IHHHHBB", code, 0)
    if dmagic != MAGIC or dabi != abi or dhost != host or deng != engine or dnpre != npre:
        raise SystemExit("mod_build: the AArch64 descriptor and the file's head differ")
    payload = NATIVE_HEAD.pack(NATIVE_VERSION, len(code), data_at, len(dat), nbss, len(words)) + code + dat + \
        b"".join(struct.pack("<I", w) for w in words)
    body = bytes(b[HEAD.size:]) + mod_presets.chunk(NATIVE_TAG, payload)
    if seed:                                         # (the code signed with the file, its head but the crc: NSIG, last)
        total = HEAD.size + len(body) + 8 + 64
        signed = HEAD.pack(magic, abi, engine, flags | FILE_CHUNKS, total, image, bss, nrel, desc, 0, name, version,
                           npre, r0, key, host, r1)
        body += mod_presets.chunk(NSIG_TAG, mod_sign.sign(seed, nsig_message(signed, body)))
    head = HEAD.pack(magic, abi, engine, flags | FILE_CHUNKS, HEAD.size + len(body), image, bss, nrel, desc,
                     zlib.crc32(body), name, version, npre, r0, key, host, r1)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(head + body)
    print(f"module {name.rstrip(bytes(1)).decode()}: AArch64 code {len(code)} B, data {len(dat)} B, bss {nbss} B, "
          f"{len(words)} address words, " + ("signed" if seed else "unsigned") +
          f", file {len(head) + len(body)} B -> {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("src", type=Path)
    ap.add_argument("-o", "--out", type=Path)
    ap.add_argument("--version", type=int, default=1)
    ap.add_argument("--check", action="store_true", help="check a built .mmod file instead")
    ap.add_argument("--wasm", action="store_true", help="a built .mmod file with its module built for WebAssembly")
    ap.add_argument("--native", action="store_true", help="a built .mmod file with its module built for AArch64")
    ap.add_argument("--src", dest="src_c", type=Path,
                    help="--wasm, --native: the module's source (outside Melodee's tree)")
    a, rest = ap.parse_known_args()
    if a.check:
        return check(a.src)
    if a.wasm:
        if not SRC and not a.src_c:
            raise SystemExit("mod_build: --wasm outside Melodee's tree needs the module's source (--src SRC.c)")
        out = a.out or (SRC / "build" / "modules" / "wasm" if SRC else a.src.parent / "wasm") / a.src.name
        return wasm(a.src.resolve(), out.resolve(), a.src_c.resolve() if a.src_c else None)
    if a.native:
        if not SRC and not a.src_c:
            raise SystemExit("mod_build: --native outside Melodee's tree needs the module's source (--src SRC.c)")
        out = a.out or (SRC / "build" / "modules" / "native" if SRC else a.src.parent / "native") / a.src.name
        return native(a.src.resolve(), out.resolve(), a.src_c.resolve() if a.src_c else None)
    src = a.src.resolve()
    out = (a.out or (SRC / "build" / "modules" if SRC else src.parent) / (src.stem + ".mmod")).resolve()
    if any(not d.startswith("-D") for d in rest):
        raise SystemExit(f"mod_build: unknown arguments {rest}")
    build(src, out, a.version, rest)


if __name__ == "__main__":
    main()
