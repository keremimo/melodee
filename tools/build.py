#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Copyright (C) 2026 ChoralRoot FM-1 contributors
# Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build Melodee: the app, the update loader and an installable .fwsc package.

  tools/build.py [--release X.Y[.Z][-suffix]]

Outputs in build/: melodee.bin (app), loader/ota.bin (update loader),
melodee.fwsc (package). A release build (--release X.Y) writes melodee-X.Y.fwsc and a folder
release-X.Y/ with the package, the app, SHA256SUMS, the licence files.
See BUILDING.md for the toolchain and the SDK.

The JieLi toolchain is Linux x86-64 only. JIELI_TOOLCHAIN points at it; on
macOS (or with JIELI_DOCKER=1) each tool runs in a linux/amd64 container.
"""
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
FW = SRC / "firmware"
OUT = SRC / "build"
GEN = OUT / "gen"
LDR = OUT / "loader"
STB = OUT / "stub"
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make  # noqa: E402
import lz4blk  # noqa: E402

STUB_XIP = 0x02000120
STUB_LEN = fm1pkg_make.STUB_LEN
APP_XIP = 0x02001000                # validated app body, after the boot stub
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
CFLAGS = ["-mcpu=r3", "-mfprev1", "-ffp-contract=off", "-Werror=double-promotion", "-Os", "-ffunction-sections", "-fdata-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")

# SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}

PRODUCT = "FM-1_900"                # package identity; release builds are FM-1_9XY[Z] (0.10: FM-1_9010, 0.11.1: FM-1_90111)
VERSION = None                      # MELODEE_VERSION for release builds (default: firmware/src/melodee.c)


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN")
    if not tc or not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain "
                         "(the directory with pi32v2/ and common/; see tools/get_toolchain.sh)")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd SRC; paths relative to SRC"""
    rel = [str(Path(a).resolve().relative_to(SRC)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":                # the toolchain's cc wrapper needs python3; call clang directly
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
    if use_docker():
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{SRC}:/work",
               "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work", DOCKER_IMAGE, f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    with ThreadPoolExecutor(len(cmds)) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def generate():
    """generated headers (UI fonts, icons, keycaps, palettes, tables)"""
    GEN.mkdir(parents=True, exist_ok=True)
    for old in ("melodee_font.h", "melodee_icons.h"):     # headers of the bitmap font and icon atlas
        (GEN / old).unlink(missing_ok=True)
    tools = SRC / "tools"
    subprocess.run([sys.executable, str(tools / "gen_scales.py"), "--check"], check=True)
    cmds = [[tools / "gen_aa_font.py", GEN / "ui_fonts.h", "--preset", "rubik"],
            [tools / "gen_icons.py", GEN / "ui_icons.h"],
            [tools / "gen_aa_keycaps.py", GEN / "ui_keycaps.h"],
            [tools / "gen_ui_palettes.py", GEN / "ui_palettes.h"],
            [tools / "gen_tables.py", GEN / "melodee_tables.h"],
            [tools / "gen_fm6_patches.py", GEN / "melodee_fm6.h"],
            [tools / "gen_cz1_factory.py", GEN / "melodee_cz1.h"],
            [tools / "gen_prophet_factory.py", GEN / "melodee_prophet_factory.h", "--web", "check"],
            [tools / "gen_wt_tables.py", GEN / "melodee_wt.h"]]
    procs = [subprocess.Popen([sys.executable, *map(str, c)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True) for c in cmds]
    failed = []
    for c, p in zip(cmds, procs):
        sys.stdout.write(p.communicate()[0])
        if p.returncode:
            failed.append(c[0].name)
    if failed:
        raise SystemExit(f"build: {', '.join(failed)} failed")


# ---- update loader

def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = FW / "loader"
    flags = [*CFLAGS, "-Ifirmware/hal", "-Ifirmware/src"]
    tc_all(("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
           ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin"),
                               ("common/bin/objdump", "-d", "-mcpu=r3", "-mattr=+fprev1", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (LDR / "loader.dis").write_text(dis)
    for ln in hdr.splitlines():     # the loader rewrites the flash: nothing may run from XIP
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    if len(ota) > 0x4000:
        raise SystemExit("loader reaches the update-record sector; recovery cleanup would erase it")
    (LDR / "ota.bin").write_bytes(ota)
    bss = [int(ln.split()[0], 16) for ln in syms.splitlines() if ln.rstrip().endswith("_bss_end")]
    print(f"loader: image {len(image)} B at {LOADER_LOAD:#x}" + (f", bss end {bss[0]:#x}" if bss else ""))
    print(f"loader: ota.bin {len(ota)} B")
    return ota


# ---- boot stub

def build_stub():
    """firmware/stub -> app.bin's first STUB_LEN bytes (0xFF padded). Code and constants only, built from its own
    sources alone, so it is the same bytes in every build: the loader rewrites sector 0x4000 only when it changes
    (docs/BOOT-SAFETY.md). firmware/stub/stub.lock records the version and hash of the stub that is meant to ship."""
    STB.mkdir(parents=True, exist_ok=True)
    src = FW / "stub"
    tc_all(("cc", "-Ifirmware/stub", "-c", src / "crt0_stub.S", "-o", STB / "crt0_stub.o"),
           ("cc", *CFLAGS, "-Ifirmware/stub", "-c", src / "stub.c", "-o", STB / "stub.o"))
    elf = STB / "stub.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "stub.ld",
       STB / "crt0_stub.o", STB / "stub.o", "-o", elf)
    (STB / "stub.bin").unlink(missing_ok=True)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, STB / "stub.bin"),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (STB / "stub.dis").write_text(dis)
    img = (STB / "stub.bin").read_bytes()
    for ln in hdr.splitlines():     # no .data, no .bss: nothing but .text may have a size
        p = ln.split()
        if len(p) > 3 and p[0].isdigit() and p[1].startswith(".") and int(p[2], 16) and \
                p[1] not in (".text", ".shstrtab", ".symtab", ".strtab"):
            raise SystemExit(f"stub: section {p[1]} holds {int(p[2], 16)} B; the stub is code and constants only")
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != STUB_XIP:
        raise SystemExit(f"stub: _start is not at {STUB_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        raise SystemExit(f"stub: starts with {img[:4].hex()}, not the entry the SPL expects (goto +4; rts)")
    if not fm1pkg_make.has_stub(img + bytes(STUB_LEN)):
        raise SystemExit("stub: no CRSTUB signature at offset 8")
    if not img or len(img) > STUB_LEN - 256:     # keep a margin: a stub that grows to the brim cannot be fixed
        raise SystemExit(f"stub: {len(img)} B does not fit {STUB_LEN} B with 256 B to spare")
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM (as the app's check)
        mm = LINE.match(ln)
        for v in (re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)) if mm else ()):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                raise SystemExit(f"stub: reference to ROM address {val:#010x}")
    used = len(img)
    img += b"\xff" * (STUB_LEN - len(img))
    ver, sha = fm1pkg_make.stub_version(img), hashlib.sha256(img).hexdigest()
    print(f"stub: {used} B of {STUB_LEN} ({100 * used / STUB_LEN:.1f}%), version {ver}, sha256 {sha[:16]}")
    lock = src / "stub.lock"
    want = lock.read_text().split() if lock.exists() else []
    if want != [str(ver), sha]:
        if os.environ.get("FM1_STUB_LOCK") == "update":
            lock.write_text(f"{ver} {sha}\n")
            print(f"stub: {lock.relative_to(SRC)} updated")
        else:
            msg = (f"the boot stub differs from {lock.relative_to(SRC)} ({' '.join(want)[:20] or 'none'}..): installing "
                   "this build rewrites sector 0x4000, the one window an interruption is not rescued in. If the "
                   "change is meant: bump STUB_VERSION (firmware/stub/stub_core.h), rebuild with "
                   "FM1_STUB_LOCK=update, and say so in the release notes")
            if VERSION:
                raise SystemExit("build: " + msg)
            print("WARNING: " + msg)
    return img


# ---- app

def build_app(stub):
    subprocess.run([sys.executable, SRC / "tools/gen_909_tables.py", GEN / "x0x_drum_tables.h"], check=True)
    flags = [*CFLAGS, "-Ifirmware/hal", "-Ifirmware/src", "-Ibuild/gen"]
    for flag in ("MELODEE_FLASH", "MELODEE_OTA", "MELODEE_OTA_DRYRUN", "MELODEE_OTA_RAMONLY", "MELODEE_CDC",
                 "MELODEE_UART", "MELODEE_USB_AUDIO", "MELODEE_ICONS", "MELODEE_PROPHET_PROTOTYPE",
                 "MELODEE_BENCH_SILENT", "MELODEE_FX_PROFILE", "MELODEE_AUDIO_PROFILE", "MELODEE_BATCH_AUDIO", "MELODEE_ADMISSION", "MELODEE_AUDIO_BENCH", "MELODEE_DUAL_CORE", "MELODEE_CACHE_RAM", "MELODEE_CORE1_TEST", "MELODEE_RAM_AUDIO", "MELODEE_HW_EQ"):
        v = os.environ.get(flag)    # unset: the default in firmware/src/melodee.c
        if v in ("0", "1"):
            flags.append(f"-D{flag}={v}")
    if os.environ.get("P5_OVERSAMPLE") in ("1", "2"):
        flags.append("-DP5_OVERSAMPLE=" + os.environ["P5_OVERSAMPLE"])
    flags.append(f'-DMELODEE_ID="{PRODUCT}"')
    if VERSION:
        flags.append(f'-DMELODEE_VERSION="{VERSION}"')
    # melodee.c goes to LLVM IR without the optimizer, the main-loop functions (UI, stores, editor) are
    # marked minsize (tools/size_fns.py), then the IR is compiled at -Os. MELODEE_SIZE=0: -Os everywhere
    size = os.environ.get("MELODEE_SIZE") != "0"
    cmain = (("cc", *flags, "-S", "-emit-llvm", "-Xclang", "-disable-llvm-optzns", "-c",
              FW / "src" / "melodee.c", "-o", OUT / "melodee.ll") if size else
             ("cc", *flags, "-c", FW / "src" / "melodee.c", "-o", OUT / "melodee.o"))
    tc_all(("cc", *[f for f in flags if f.startswith("-DMELODEE_DUAL_CORE=")], "-c", FW / "crt0.S", "-o", OUT / "crt0.o"),
           ("cc", "-c", FW / "hal" / "fm1_vec.S", "-o", OUT / "fm1_vec.o"),
           ("cc", "-c", FW / "hal" / "fm1_isr.S", "-o", OUT / "fm1_isr.o"),
           cmain)
    if size:
        subprocess.run([sys.executable, SRC / "tools" / "size_fns.py", OUT / "melodee.ll", OUT / "melodee_size.ll"],
                       check=True)
        irflags = [("-O2" if f == "-Os" else f) for f in flags if not f.startswith(("-I", "-D", "-W"))]
        if os.environ.get("MELODEE_RAM_AUDIO") == "1":
            tc("cc", *irflags, "-S", "-emit-llvm", OUT / "melodee_size.ll", "-o", OUT / "melodee_audio_opt.ll")
            subprocess.run([sys.executable, SRC / "tools/audio_ram.py", OUT / "melodee_audio_opt.ll", OUT / "melodee_audio.ll"], check=True)
            tc("cc", *irflags, "-c", OUT / "melodee_audio.ll", "-o", OUT / "melodee.o")
        else:
            tc("cc", *irflags, "-c", OUT / "melodee_size.ll", "-o", OUT / "melodee.o")
    elif os.environ.get("MELODEE_RAM_AUDIO") == "1":
        raise SystemExit("resident audio requires MELODEE_SIZE=1 for the verified IR placement pass")
    elf = OUT / "melodee.elf"
    extra_ld = ["--defsym=AUDIO_RESIDENT=1"] if os.environ.get("MELODEE_RAM_AUDIO") == "1" else []
    tc("pi32v2/bin/ld", "--gc-sections", *extra_ld, "-T", FW / "app.ld", OUT / "crt0.o", OUT / "fm1_vec.o", OUT / "fm1_isr.o",
       OUT / "melodee.o", "-o", elf)
    for sect in ("text.bin", "data.bin", "ramtext.bin", "dsptext.bin", "dsptables.bin", "audiotext.bin"):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis, rt = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".dsp_text", elf, OUT / "dsptext.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".dsp_tables", elf, OUT / "dsptables.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".audio_text", elf, OUT / "audiotext.bin"),
                               ("common/bin/objdump", "-t", elf),
                               ("common/bin/objdump", "-d", "-mcpu=r3", "-mattr=+fprev1", elf),
                               ("common/bin/objdump", "-d", "-mcpu=r3", "-mattr=+fprev1", "-j", ".ram_text", elf))
    (OUT / "melodee.dis").write_text(dis)

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    # .ram_text and .data follow .text at their load addresses; crt0 copies them by words
    for sect, lname in (("ramtext.bin", "_rt_load"), ("dsptext.bin", "_dsp_load"), ("dsptables.bin", "_dt_load"), ("data.bin", "_data_load"), ("audiotext.bin", "_audio_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"{lname} {load:#x} is not word aligned")
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)
    img = bytearray(stub) + img
    (OUT / "melodee.bin").write_bytes(img)
    return bytes(img), syms, dis, rt


def check(img, syms, dis, rt):
    errors, notes = [], []
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        errors.append(f"image starts with {img[:4].hex()}, not the entry stub")
    rt_calls = [ln for ln in rt.splitlines() if re.search(r"\bcall\b", ln)]
    if rt_calls:                    # RAM code runs with the flash off: no calls into XIP
        errors.append(f".ram_text contains calls: {rt_calls[:3]}")
    else:
        notes.append(f".ram_text: {len([ln for ln in rt.splitlines() if LINE.match(ln)])} insns, no calls")
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM
        mm = LINE.match(ln)
        if not mm:
            continue
        for v in re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                errors.append(f"reference to ROM address {val:#010x}")
    if len(img) - STUB_LEN > fm1pkg_make.BODY_MAX:
        errors.append("app body overlaps the commit-marker sector")

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    code_end = sym("_audio_end") if sym("_audio_end") > sym("_audio_start") else sym("_dsp_end")
    if sym("_guard_ram_end") != code_end:
        errors.append("hardware PC guard does not cover all executable SRAM sections")
    if os.environ.get("MELODEE_RAM_AUDIO") == "1":
        manifest = json.loads((OUT / "melodee_audio.json").read_text())
        live = 0
        for name in manifest["resident"]:
            address = sym(name)
            if address:
                live += 1
                if not sym("_rt_start") <= address < sym("_guard_ram_end") <= 0x01C74000:
                    errors.append(f"audio function {name} escaped SRAM")
        current = None
        for line in dis.splitlines():
            label = re.match(r"^([\w.]+):$", line)
            if label: current = label[1]
            if current in manifest["resident"]:
                call = re.search(r"\bcall -?\d+ <[^>]* : ([0-9a-f]+) >", line)
                if call and int(call[1], 16) >= APP_XIP:
                    errors.append(f"audio function {current} calls XIP directly")
        notes.append(f"audio RAM: {live} linked functions verified in SRAM; optional bounded FX inserts")
    for name in ("rev_room", "rev_spring", "rev_hall", "dly_bus", "chorus", "distortion", "sl_enter", "sl_block", "perf_begin", "perf_block", "perf_end"):
        if sym(name):
            errors.append(f"removed effect {name} must not be linked")
    # FM6's sample loops share internal RAM with Prophet. Outlined helpers
    # must stay there too: a flash fetch per sample defeats the optimization,
    # and direct RAM/XIP calls exceed the target's branch range.
    if sym("fm6_render"):
        kernel = "fm6_run_into" if sym("fm1_core1_start") else "fm6_run"
        for name in (kernel, "fm6_op", "fm6_op_fb", "fm6_op_loop"):
            address = sym(name)
            if (name == kernel or address) and not sym("_dsp_start") <= address < sym("_dsp_end"):
                errors.append(f"{name} must remain in internal DSP RAM")
        notes.append("FM6: sample kernel and operator helpers in internal DSP RAM")
    for name in ("fm6_output", "fm6_dc_samples", "fm6_eg_go", "fm6_eg_step", "fm6_peg_go", "p5_dc_samples", "mix_mono_samples", "master_samples", "master_out"):
        address = sym(name)
        if address and not sym("_dsp_start") <= address < sym("_dsp_end"):
            errors.append(f"{name} must remain in internal DSP RAM")
    for name in ("dr_bd", "dr_sd", "dr_tom", "dr_rs", "dr_cl", "dr_cp", "dr_ma",
                 "dr_metal", "dr_cb", "dr_cy", "dr_hat"):
        address = sym(name)
        if address and not sym("_dsp_start") <= address < sym("_dsp_end"):
            errors.append(f"{name} must remain in internal DSP RAM")
    if sym("TANH_Q15") and not sym("_dt_start") <= sym("TANH_Q15") < sym("_dt_end"):
        errors.append("808 saturation table must remain in internal DSP RAM")
    if sym("fm1_core1_start"):
        for name in ("fm1_core1_main", "audio_worker_loop"):
            if not 0x01C00000 <= sym(name) < sym("_dsp_end"):
                errors.append(f"{name} must remain in internal RAM for flash-safe idle")
        if sym("_cpu1_stacks_hi") - sym("_cpu1_stacks_lo") != 8192:
            errors.append("CPU1 requires separate 4 KiB user and supervisor stacks")
        # Idle may call only the job's register-held callback; a compiler
        # outlined XIP helper would break flash writes after completion.
        body = dis.split("\naudio_worker_loop:", 1)[-1].split("\n\n", 1)[0]
        if re.search(r"\bcall -?\d+", body):
            errors.append("CPU1 polling loop contains a direct call; helpers must be inlined")
        notes.append("CPU1: RAM polling loop, private 8 KiB stacks, bounded startup/join")
    bss = sym("_bss_end") - 0x01C08000
    pool = sym("_pool_end") - sym("_pool_start")
    notes.append(f"image {len(img)} B; RAM .data+.bss {bss} B of 98304; pool {pool} B of {0x54000}")
    arena = sym("_resource_end") - sym("_resource_start")
    if arena:
        minimum = sym("_resource_minimum")
        notes.append(f"audio working arena {arena} B available on demand (minimum {minimum})")
        if arena < minimum: errors.append("audio working arena below worst-case budget")
    if bss > 96 * 1024:
        errors.append("RAM region overflow")
    tails = 0
    for name, floor, ceiling in (("low", sym("_dt_end"), 0x01C07F00),
                                 ("data", sym("_bss_end"), 0x01C20000),
                                 ("retained", sym("_noinit_end"), 0x01C7FD50)):
        begin, end = sym(f"_resource_{name}_start"), sym(f"_resource_{name}_end")
        if not floor <= begin <= end <= ceiling or (begin | end) % 4:
            errors.append(f"{name} resource bank crosses a reserved SRAM boundary")
        else:
            tails += end - begin
            notes.append(f"{name} SRAM tail {end - begin} B: {begin:#010x}..{end:#010x} (end exclusive)")
    notes.append(f"total audio resources {arena + tails} B in four separate SRAM banks; reclaimed {tails} B")
    if sym("fm1_cache_init"):
        if not sym("_rt_start") <= sym("fm1_cache_init") < sym("_rt_end"):
            errors.append("cache configuration must execute wholly in SRAM")
        if (sym("_resource_cache_start"), sym("_resource_cache_end")) != (0x01F28000, 0x01F2F000):
            errors.append("cache resource bank must cover exactly the seven freed data-cache ways")
        notes.append(f"cache RAM: optional 28672 B after boot self-test; maximum resources {arena + tails + 28672} B")
    if 0x54000 - pool < 8192:                     # keep >= 8 KiB of the pool spare
        errors.append(f"pool headroom {0x54000 - pool} B < 8192 B")
    return errors, notes


# Every hardware register access lives in hal/. In src/ and loader/ (comments stripped):
#  - no volatile pointer cast, except of a C object's address (`*(volatile T *)&x`: a read-once
#    of a RAM flag shared with an ISR, e.g. usb.c ota_wire_send);
#  - no literal in a register or reserved window (core SFRs 0x10000-0x13FFF, SFC 0x40000-0x43FFF,
#    GPIO/IOMAP 0x50000-0x51FFF, CPU 0x1EE0000-0x1EEFFFF, RAM top 0x01C7F000- (boot info,
#    mailbox, vectors), XIP 0x02000000-0x020FFFFF);
#  - no inline asm, except the empty compiler barrier RING_PUBLISH() (emits no instruction);
#  - no HAL register macro (a hal/ #define that is, or expands to, a volatile access).
# Linker symbols (_bss_start[], _rt_load[], ...) are plain C objects and pass.
MMIO_LIT = re.compile(r"\b0x0*(1[0-3][0-9a-f]{3}|4[0-3][0-9a-f]{3}|5[01][0-9a-f]{3}|1ee[0-9a-f]{4}|"
                      r"1c7f[0-9a-f]{3}|20[0-9a-f]{5})u?l?\b", re.I)
MMIO_CAST = re.compile(r"\(\s*(?:const\s+)?volatile\b[^()]*\*\s*\)(?!\s*&)")
MMIO_ASM = re.compile(r"\b(?:__asm__|asm)\b(?!\s+volatile\s*\(\s*\"\"\s*:::\s*\"memory\"\s*\))")


def mmio_check():
    def strip(s):
        s = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), s, flags=re.S)
        return re.sub(r"//[^\n]*", "", s)
    defs = {}
    for h in sorted((FW / "hal").glob("*.h")):
        for m in re.finditer(r"^#define\s+(\w+)(?:\([^)]*\))?\s+(.*)$", strip(h.read_text()), re.M):
            defs[m.group(1)] = m.group(2)
    regs = {n for n, b in defs.items() if re.search(r"\bvolatile\b", b)}
    while True:                                   # macros built on register macros (FM1_WR_LIMIT_H -> FM1_X2)
        more = {n for n, b in defs.items() if n not in regs and set(re.findall(r"\w+", b)) & regs}
        if not more:
            break
        regs |= more
    errors = []
    for f in sorted([*(FW / "src").glob("*.[ch]"), *(FW / "loader").glob("*.c")]):
        for no, ln in enumerate(strip(f.read_text()).splitlines(), 1):
            where = f"{f.relative_to(FW)}:{no}"
            for rx, what in ((MMIO_LIT, "register/window address"), (MMIO_CAST, "volatile pointer cast"),
                             (MMIO_ASM, "inline asm")):
                m = rx.search(ln)
                if m:
                    errors.append(f"{where}: {what} outside hal/ ({m.group(0).strip()}); add a hal/ helper")
            used = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", ln)) & regs
            if used:
                errors.append(f"{where}: HAL register macro {sorted(used)[0]} outside hal/; use a hal/ helper")
    return errors


def main():
    global PRODUCT, VERSION
    ap = argparse.ArgumentParser()
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--nightly", action="store_true", help="development package from the current next commit")
    mode.add_argument("--release", metavar="X.Y[.Z]", help="release build: identity FM-1_9XY[Z], version string vX.Y[.Z]")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    a = ap.parse_args()
    name = "melodee.fwsc"
    variant = "-1core" if os.environ.get("MELODEE_DUAL_CORE") == "0" else ""
    if a.release:                   # X one digit, Y one or two (1.0: FM-1_910; 1.0.0: FM-1_91000),
        try:
            PRODUCT = fm1pkg_make.release_product(a.release)
        except ValueError as error:
            raise SystemExit(str(error)) from error
        VERSION = "v" + a.release.lower()      # e.g. v1.0, v1.1-rc1
        name = f"melodee-{a.release}{variant}.fwsc"
    if a.nightly:
        revision = subprocess.check_output(["git", "rev-parse", "--short=7", "HEAD"], cwd=SRC, text=True).strip()
        VERSION = "next-" + revision
        name = f"melodee-nightly{variant}.fwsc"
    fm1pkg_make.SDK = a.sdk
    for rel, sha in SDK_SHA256.items():          # fail early without the SDK
        if hashlib.sha256(fm1pkg_make.sdk_file(rel)).hexdigest() != sha:
            print(f"warning: SDK {rel} differs from AC79NN_SDK_V1.2.1; the package will not match the reference")
    OUT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(2) as ex:
        gen, ldr = ex.submit(generate), ex.submit(build_loader)
        gen.result()
        ota = ldr.result()
    stub = build_stub()
    img, syms, dis, rt = build_app(stub)
    errors, notes = check(img, syms, dis, rt)
    hal_err = mmio_check()
    errors += hal_err
    if not hal_err:
        notes.append("register access: hal/ only (src/, loader/ clean)")
    for n in notes:
        print("  ok   ", n)
    for e in errors:
        print("  FAIL ", e)
    if errors:
        raise SystemExit("build: checks failed")
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    (OUT / name).write_bytes(pkg)
    (OUT / "ATTRIBUTION.txt").unlink(missing_ok=True)
    print(f"app      {OUT / 'melodee.bin'}  {len(img)} B")
    print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
    print(f"package  {OUT / name}  {len(pkg)} B, identity {PRODUCT}")
    if a.release:                   # what a release carries: the package, the app and every licence they need
        rel = OUT / f"release-{a.release}{variant}"
        shutil.rmtree(rel, ignore_errors=True)
        (rel / "LICENSES").mkdir(parents=True)
        app = f"melodee-{a.release}{variant}-app.bin"
        (rel / name).write_bytes(pkg)
        (rel / app).write_bytes(img)
        (rel / "SHA256SUMS").write_text("".join(f"{hashlib.sha256(b).hexdigest()}  {n}\n"
                                                for n, b in ((name, pkg), (app, bytes(img)))))
        for f in sorted((SRC / "LICENSES").glob("*.txt")):
            shutil.copy(f, rel / "LICENSES" / f.name)
        for doc in ("LICENSE", "LICENSING.md"):
            shutil.copy(SRC / doc, rel / doc)
        print(f"release  {rel}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
