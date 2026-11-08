#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""The studio bundle: what Melodee Studio (the web editor in its own repository) takes from this firmware, made from
the firmware's own sources and tables, versioned by a manifest. Run from anywhere after ./build.sh (or
tools/generate_assets.py: only build/gen is read); stdlib Python and a C compiler (CC, default cc).

  tools/studio_bundle.py [--out build/studio-bundle] [--work build/host/studio] [--core WASM] [--kit WASM]
  tools/studio_bundle.py --verify build/studio-bundle

build/studio-bundle/
  desc.json             parameter and engine tables, factory sounds (tests/descdump.c, as test_web.mjs reads them)
  layout.json           where a project record keeps what the page touches (tests/mel_fixture.c emit)
  fixtures/             projects as the device stores them (.hpj) and whole (.rec), Record kit's kits (kit-*.bin)
  protocol.json         the editor protocol: header, commands, limits, INFO's blocks, backup objects (tests/protodump.c)
  constants.json        tracks, patterns, slots, kit bounds, store geometry, record magics (tests/constdump.c)
  settings.json         the HOME menu's rows, kinds and values (tests/settingsdump.c)
  scales.json           the SCL catalogue and tunings (tools/gen_scales.py --json)
  prophet_factory.json  Sequential's 200 v1.03 programs (tools/gen_prophet_factory.py --json)
  palettes.json         the UI palettes and their derived colours (tools/gen_ui_palettes.py --json)
  melodee_core.wasm, melodee_kit.wasm   only when built (build/core, build/kit, or --core / --kit); standalone modules
                        (no JS glue; the core imports nothing, the kit maker only env.emscripten_notify_memory_growth);
                        the core's exports and ABI in the manifest ("core"), the kit maker's exports, its kit_max and
                        whether web/kit/check.mjs made the fixture's kits again byte for byte ("kit")
  modules/<name>.mmod   with the core: the five synths' module files with their WebAssembly (tools/mod_build.py --wasm,
                        web/core/build.sh: build/modules/wasm), which Studio links into the core (web/core/mod_link.mjs)
                        and installs as the device's files; listed in the manifest ("core": "modules")
  protocol-transcripts.jsonl  the core's replies to Studio's requests from a new device's power-on and with the song
                        fixture loaded (web/core/core_check.mjs --transcripts: needs the core and node)
  LICENSES/  SOURCE     the licences and where the complete corresponding source is
  manifest.json         firmware version and commit, protocol version and revision, the record magics written and
                        read, every file's size and sha256

The bundle is checked before it is written: the fixtures carry the magics constants.json says the firmware writes,
and the sizes the dumpers report agree with each other. --verify checks a bundle against its manifest (no file
missing, none extra, every hash). The bundle is written to OUT.tmp and moved into place whole.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FORMAT = 1
CFLAGS = ["-O1", "-ffp-contract=off", "-Wall", "-Wno-unused-function", "-w", "-Ibuild/gen", "-Ifirmware/src"]
# the dumpers (tests/<src>.c) and their flags beyond CFLAGS (run_tests.sh builds descdump and mel_fixture the same way)
DUMPERS = {
    "descdump": [],
    "mel_fixture": ["-DMELODEE_USB_AUDIO=1"],
    "protodump": [],
    "constdump": ["-DMELODEE_USB_AUDIO=1"],
    "settingsdump": ["-DMELODEE_USB_AUDIO=1", "-DMELODEE_USB_HOST=1", "-Itests/ble_fake"],
}
WASM = {"melodee_core.wasm": ["build/core/melodee_core.wasm"], "melodee_kit.wasm": ["build/kit/melodee_kit.wasm"]}
CORE_MODULES = ["sid", "drum", "cz", "fm6", "prophet"]   # the synths the core plays as modules (web/core/build.sh MODS)


def wasm_exports(path):
    """a WebAssembly module's exported names (its export section) and its imports' count"""
    b = path.read_bytes()

    def uleb(i):
        v = shift = 0
        while True:
            c = b[i]
            i += 1
            v |= (c & 0x7F) << shift
            shift += 7
            if c < 0x80:
                return v, i

    i, names, nimports = 8, [], 0
    while i < len(b):
        sid = b[i]
        size, i = uleb(i + 1)
        end = i + size
        if sid in (2, 7):
            n, j = uleb(i)
            if sid == 2:
                nimports = n
            else:
                for _ in range(n):
                    k, j = uleb(j)
                    names.append(b[j:j + k].decode())
                    j += k + 1                        # the kind
                    _, j = uleb(j)                    # the index
        i = end
    return sorted(names), nimports


def fail(msg):
    raise SystemExit(f"studio_bundle: {msg}")


def run(cmd, **kw):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)
    if r.returncode:
        fail(f"{' '.join(map(str, cmd))} failed ({r.returncode}):\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
    return r.stdout


def compile_dumpers(work):
    cc = shlex.split(os.environ.get("CC", "cc"))
    work.mkdir(parents=True, exist_ok=True)

    def one(item):
        name, flags = item
        run(cc + CFLAGS + flags + ["-o", str(work / name), f"tests/{name}.c", "-lm"])
        return name

    with concurrent.futures.ThreadPoolExecutor(len(DUMPERS)) as pool:
        list(pool.map(one, DUMPERS.items()))


def dump(work, name, *args):
    """a dumper's JSON (stdout), parsed"""
    out = run([str(work / name), *args])
    try:
        return json.loads(out)
    except json.JSONDecodeError as e:
        fail(f"{name} wrote no valid JSON: {e}")


def write_json(path, obj, compact=False):
    path.write_text((json.dumps(obj, separators=(",", ":")) if compact else json.dumps(obj, indent=1)) + "\n")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(*args):
    r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def source_url():
    url = git("remote", "get-url", "origin")
    m = re.match(r"(?:git@|ssh://git@)([^:/]+)[:/](.+?)(?:\.git)?$", url)
    if m:
        return f"https://{m.group(1)}/{m.group(2)}"
    return re.sub(r"\.git$", "", url) or "https://github.com/keremimo/melodee"


def firmware_version():
    m = re.search(r'^#define MELODEE_VERSION "([^"]+)"', (ROOT / "firmware/src/melodee.c").read_text(), re.M)
    if not m:
        fail("no MELODEE_VERSION in firmware/src/melodee.c")
    return m.group(1)


def protocol_version():
    """the highest vN the protocol document describes (web/EDITOR_PROTOCOL.md's sections)"""
    found = re.findall(r"^## (?:Historical )?v(\d+)\b", (ROOT / "web/EDITOR_PROTOCOL.md").read_text(), re.M)
    return max(map(int, found)) if found else 0


def magic_names(record):
    return {k: [{"hex": m["hex"], "le": m["le"], "be": m["be"]} for m in record[k]] for k in ("writes", "reads")}


def check(out, consts, layout, desc, proto, settings, palettes):
    """the bundle's parts against each other and the fixtures against the magics the firmware says it writes"""
    rec = consts["records"]
    music, kits, store = consts["music"], consts["kits"], consts["storage"]

    def u32(data, at):
        return struct.unpack_from("<I", data, at)[0]

    def writes(kind):
        return {m["u32"] for m in rec[kind]["writes"]}

    problems = []
    fixtures = sorted((out / "fixtures").iterdir())
    for f in fixtures:
        data = f.read_bytes()
        if f.suffix == ".rec":
            if len(data) != store["record_bytes"]:
                problems.append(f"{f.name}: {len(data)} bytes, a record is {store['record_bytes']}")
            if u32(data, 0) not in writes("project_record"):
                problems.append(f"{f.name}: record magic {u32(data, 0):#010x} is not one the firmware writes")
            at = rec["project_wire"]["magic_at"]
            if u32(data, at) not in writes("project_wire"):
                problems.append(f"{f.name}: project magic {u32(data, at):#010x} at {at} is not one the firmware writes")
        elif f.suffix == ".hpj":
            if u32(data, 0) not in writes("project_stored"):
                problems.append(f"{f.name}: stored magic {u32(data, 0):#010x} is not one the firmware writes")
        elif f.name.startswith("kit-"):
            if u32(data, 0) not in writes("kit") or len(data) > kits["kit_max"]:
                problems.append(f"{f.name}: not a kit the firmware writes ({u32(data, 0):#010x}, {len(data)} bytes)")
    if not any(f.suffix == ".rec" for f in fixtures) or not any(f.name.startswith("kit-") for f in fixtures):
        problems.append("fixtures: no project records or no kits")
    pairs = [("layout REC_BYTES", layout["REC_BYTES"], store["record_bytes"]),
             ("layout FUN_BYTES", layout["FUN_BYTES"], store["wire_bytes"]),
             ("layout TAIL_AT", layout["TAIL_AT"], store["tail_at"]),
             ("layout KIT_MAX", layout["KIT_MAX"], kits["kit_max"]),
             ("layout KIT_FIRST", layout["KIT_FIRST"], kits["user_first"]),
             ("layout KIT_COUNT", layout["KIT_COUNT"], kits["user_count"]),
             ("layout PROJECTS", layout["PROJECTS"], music["project_slots"]),
             ("layout FBKS", layout["FBKS"], next(m["u32"] for m in rec["project_record"]["writes"] if m["le"] == "FBKS")),
             ("layout PZ_MAGIC", layout["PZ_MAGIC"], rec["project_stored"]["writes"][0]["u32"]),
             ("layout KIT_MAGIC", layout["KIT_MAGIC"], rec["kit"]["writes"][0]["u32"]),
             ("desc P_COUNT", desc["P_COUNT"], music["p_count"]), ("desc G_COUNT", desc["G_COUNT"], music["g_count"]),
             ("desc NSTEP", desc["NSTEP"], music["steps"]), ("desc P_E0", desc["P_E0"], music["p_e0"]),
             ("protocol tracks", proto["limits"]["tracks_listed"], desc["NTRK"]),
             ("protocol motion_max", proto["limits"]["motion_max"], music["motion_max"]),
             ("protocol kits", proto["backup"]["kits"], kits["store_slots"]),
             ("protocol project objects", sum(o["kind"] == "project" for o in proto["backup"]["objects"]),
              music["project_slots"])]
    problems += [f"{what}: {a} != {b}" for what, a, b in pairs if a != b]
    color = next((r for r in settings["rows"] if r["key"] == "COLOR"), None)
    names = [p["name"].lower() for p in palettes["palettes"]]
    if not color or [v.lower() for v in color.get("values", [])] != names:
        problems.append(f"settings COLOR values {color and color.get('values')} are not the palettes {names}")
    if problems:
        fail("the bundle does not agree with itself:\n  " + "\n  ".join(problems))


def build(out, work, wasm_args):
    if not (ROOT / "build/gen/melodee_tables.h").exists():
        fail("no build/gen: run ./build.sh (or tools/generate_assets.py) first")
    tmp = out.with_name(out.name + ".tmp")
    shutil.rmtree(tmp, ignore_errors=True)
    (tmp / "fixtures").mkdir(parents=True)
    compile_dumpers(work)

    desc = dump(work, "descdump")
    write_json(tmp / "desc.json", desc, compact=True)
    mel = work / "mel"
    shutil.rmtree(mel, ignore_errors=True)
    run([str(work / "mel_fixture"), "emit", str(mel)])
    layout = json.loads((mel / "layout.json").read_text())
    write_json(tmp / "layout.json", layout)
    for f in sorted(mel.iterdir()):
        if f.suffix in (".rec", ".hpj") or (f.name.startswith("kit-") and f.suffix == ".bin"):
            shutil.copyfile(f, tmp / "fixtures" / f.name)
    proto = dump(work, "protodump")
    write_json(tmp / "protocol.json", proto)
    consts = dump(work, "constdump")
    write_json(tmp / "constants.json", consts)
    settings = dump(work, "settingsdump")
    write_json(tmp / "settings.json", settings)
    py = sys.executable
    run([py, "tools/gen_scales.py", "--check", "--json", str(tmp / "scales.json")])
    run([py, "tools/gen_prophet_factory.py", "--json", str(tmp / "prophet_factory.json")])
    run([py, "tools/gen_ui_palettes.py", "--json", str(tmp / "palettes.json")])
    palettes = json.loads((tmp / "palettes.json").read_text())
    check(tmp, consts, layout, desc, proto, settings, palettes)

    wasm = {}
    for name, candidates in WASM.items():
        given = wasm_args.get(name)
        paths = [Path(given)] if given else [ROOT / c for c in candidates]
        found = next((p for p in paths if p.is_file()), None)
        if given and not found:
            fail(f"{given}: no such file")
        if found:
            if found.read_bytes()[:4] != b"\0asm":
                fail(f"{found}: not a WebAssembly module")
            shutil.copyfile(found, tmp / name)
            wasm[name] = True
        else:
            wasm[name] = False
    core = None
    if wasm["melodee_core.wasm"]:
        exports, nimports = wasm_exports(tmp / "melodee_core.wasm")
        abi = re.search(r"#define CORE_ABI (\d+)u", (ROOT / "web/core/melodee_core.c").read_text())
        if nimports:
            fail(f"melodee_core.wasm imports {nimports} functions: Studio gives it none")
        core = {"abi": int(abi.group(1)) if abi else None, "exports": [e for e in exports if e.startswith("core_")],
                "transcripts": False, "modules": []}
        (tmp / "modules").mkdir()
        for m in CORE_MODULES:                        # (the synths are modules in the core: their files beside it)
            src = ROOT / "build" / "modules" / "wasm" / f"{m}.mmod"
            if not src.is_file():
                fail(f"{src.relative_to(ROOT)}: no such file (web/core/build.sh)")
            shutil.copyfile(src, tmp / "modules" / f"{m}.mmod")
            core["modules"].append(f"modules/{m}.mmod")
        if shutil.which("node"):                      # the core's own replies, for Studio's tests
            run(["node", "web/core/core_check.mjs", "--core", str(tmp / "melodee_core.wasm"), "--modules", str(tmp / "modules"), "--mel", str(mel),
                 "--transcripts", str(tmp / "protocol-transcripts.jsonl"), "--transcripts-only"])
            core["transcripts"] = True
    kit = None
    if wasm["melodee_kit.wasm"]:
        exports, nimports = wasm_exports(tmp / "melodee_kit.wasm")
        if nimports != 1:                             # (env.emscripten_notify_memory_growth: Studio gives a no-op)
            fail(f"melodee_kit.wasm imports {nimports} functions: Studio gives it one (memory growth)")
        kit = {"exports": [e for e in exports if e.startswith("kit_")], "kit_max": consts["kits"]["kit_max"], "checked": False}
        if shutil.which("node"):                      # the fixture's kits made again from its takes, byte for byte
            said = run(["node", "web/kit/check.mjs"],
                       env={**os.environ, "MEL_DIR": str(mel), "KIT_WASM": str(tmp / "melodee_kit.wasm")})
            if "kit: all ok" not in said:
                fail(f"web/kit/check.mjs:\n{said[-2000:]}")
            kit["checked"] = True

    lic = tmp / "LICENSES"
    lic.mkdir()
    shutil.copyfile(ROOT / "LICENSE", lic / "GPL-3.0-only.txt")
    shutil.copyfile(ROOT / "LICENSING.md", lic / "LICENSING.md")
    for f in sorted((ROOT / "LICENSES").iterdir()):
        if f.is_file():
            shutil.copyfile(f, lic / f.name)
    shutil.copyfile(ROOT / "assets/prophet5-factory/README.md", lic / "prophet5-factory-README.md")

    commit = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain", "--untracked-files=no"))
    version = firmware_version()
    url = source_url()
    (tmp / "SOURCE").write_text(
        "Melodee firmware: the complete corresponding source of everything in this bundle (GPL-3.0-only; the parts\n"
        "with licences of their own are listed in LICENSES/LICENSING.md).\n"
        f"repository: {url}\ncommit: {commit or 'unknown'}{' (with uncommitted changes)' if dirty else ''}\n"
        f"version: {version}\nmade by: tools/studio_bundle.py\n")

    proto_bytes = (tmp / "protocol.json").read_bytes()
    files = {}
    for f in sorted(p for p in tmp.rglob("*") if p.is_file()):
        files[f.relative_to(tmp).as_posix()] = {"bytes": f.stat().st_size, "sha256": sha256(f)}
    manifest = {
        "format": FORMAT,
        "firmware": {"version": version, "commit": commit, "dirty": dirty, "date": git("log", "-1", "--format=%cI"),
                     "source": url},
        "protocol": {"version": protocol_version(), "revision": hashlib.sha256(proto_bytes).hexdigest()[:16],
                     "commands": len(proto["commands"]), "info_tags": [t["char"] for t in proto["info_tags"]]},
        "records": {k: magic_names(v) for k, v in consts["records"].items()},
        "wasm": wasm,
        "core": core,
        "kit": kit,
        "files": files,
    }
    write_json(tmp / "manifest.json", manifest)
    shutil.rmtree(out, ignore_errors=True)
    tmp.rename(out)
    return manifest


def verify(out):
    """a bundle against its manifest: every file there, none other, every hash; the JSON files parse"""
    mpath = out / "manifest.json"
    if not mpath.is_file():
        fail(f"{out}: no manifest.json")
    manifest = json.loads(mpath.read_text())
    if manifest.get("format") != FORMAT:
        fail(f"{mpath}: format {manifest.get('format')}, this tool reads {FORMAT}")
    have = {p.relative_to(out).as_posix() for p in out.rglob("*") if p.is_file()} - {"manifest.json"}
    listed = set(manifest["files"])
    problems = [f"missing: {f}" for f in sorted(listed - have)] + [f"not in the manifest: {f}" for f in sorted(have - listed)]
    for name in sorted(listed & have):
        entry, f = manifest["files"][name], out / name
        if f.stat().st_size != entry["bytes"] or sha256(f) != entry["sha256"]:
            problems.append(f"changed: {name}")
        elif f.suffix == ".json":
            try:
                json.loads(f.read_text())
            except json.JSONDecodeError as e:
                problems.append(f"{name}: {e}")
    for name, present in manifest["wasm"].items():
        if present != (name in listed):
            problems.append(f"wasm {name}: manifest says {present}")
    core = manifest.get("core")
    if core and core.get("transcripts") != ("protocol-transcripts.jsonl" in listed):
        problems.append(f"protocol-transcripts.jsonl: manifest says {core.get('transcripts')}")
    if core and "melodee_core.wasm" in listed:
        exports, _ = wasm_exports(out / "melodee_core.wasm")
        if [e for e in exports if e.startswith("core_")] != core["exports"]:
            problems.append("melodee_core.wasm: its exports are not the manifest's")
    kit = manifest.get("kit")
    if kit and "melodee_kit.wasm" in listed:
        exports, _ = wasm_exports(out / "melodee_kit.wasm")
        if [e for e in exports if e.startswith("kit_")] != kit["exports"]:
            problems.append("melodee_kit.wasm: its exports are not the manifest's")
    for need in ("desc.json", "layout.json", "protocol.json", "constants.json", "settings.json", "scales.json",
                 "prophet_factory.json", "palettes.json", "SOURCE", "LICENSES/GPL-3.0-only.txt"):
        if need not in listed:
            problems.append(f"the bundle lacks {need}")
    if problems:
        fail(f"{out} does not match its manifest:\n  " + "\n  ".join(problems))
    return manifest


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=ROOT / "build/studio-bundle")
    ap.add_argument("--work", type=Path, default=ROOT / "build/host/studio", help="where the dumpers are built and run")
    ap.add_argument("--core", help="melodee_core.wasm to include (default: build/core/melodee_core.wasm when there)")
    ap.add_argument("--kit", help="melodee_kit.wasm to include (default: build/kit/melodee_kit.wasm when there)")
    ap.add_argument("--verify", type=Path, metavar="DIR", help="check a bundle against its manifest, build nothing")
    a = ap.parse_args()
    if a.verify:
        m = verify(a.verify.resolve())
        print(f"studio bundle {a.verify}: {len(m['files'])} files match the manifest "
              f"({m['firmware']['version']} {m['firmware']['commit'][:12]})")
        return
    m = build(a.out.resolve(), a.work.resolve(), {"melodee_core.wasm": a.core, "melodee_kit.wasm": a.kit})
    wasm = ", ".join(f"{k} {'in' if v else 'not built'}" for k, v in m["wasm"].items())
    print(f"studio bundle: {a.out} ({len(m['files'])} files; {m['firmware']['version']} "
          f"{m['firmware']['commit'][:12]}{' dirty' if m['firmware']['dirty'] else ''}; protocol v{m['protocol']['version']} "
          f"{m['protocol']['revision']}; {wasm})")


if __name__ == "__main__":
    main()
