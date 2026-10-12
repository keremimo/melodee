#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages):

  index.html                        redirect to the installer (the old URL keeps working)
  firmware/melodee-VER.fwsc         the package, both cores (+ LICENSE, LICENSING.md, LICENSES/: the
                                    packages hold JieLi SDK files under Apache-2.0, see LICENSING.md)
  firmware/melodee-VER-1core.fwsc   the same release built with MELODEE_DUAL_CORE=0, for FM-1s that crash
                                    with the dual-core build; the installer offers both
  firmware/melodee-nightly*.fwsc    next's development builds (nightly.json; publish_nightly.py), kept as published
  firmware/melodee-synth*.fwsc      the synth-only firmware, both cores (synth.json; publish_synth.py: the
                                    synth-only branch, one track, no sequencer), kept as published; optional
  webapp/synth-editor/              the synth-only branch's editor (publish_synth.py), kept as published
  webapp/installer/index.html       index_pkg.html, self-contained (fm1pkg.js, fm1ota.js, metadata inlined)
  webapp/editor/index.html          editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt)
  webapp/manual/                    web/manual/ (index → latest v*/, each version dir has HTML + screenshots)
  src/                              not touched

  web/make_site.py build/melodee-X.Y.fwsc build/melodee-X.Y-1core.fwsc X.Y OUT_DIR

The packages must be ones made by tools/fm1pkg_make.py (Melodee's own loader, no vendor files), from one
release: their identity (FM-1_9xx, FM-1_9xxx from 0.10, FM-1_9xxxx for X.Y.Z) is read from them and must be
the same; the device must report it after the install.

For a complete local site (installer + editor + manual with screenshots), refresh the committed
screenshots when the UI changes, then assemble the site — see BUILDING.md "Local site".
"""
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def check_package(pkg):
    """the package's bytes and identity; refuses anything that is not a Melodee package"""
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d{2,4}", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a Melodee package (FM-1_9 and two to four digits)")
    if b"FELUCCA-LOADER-1" not in raw:              # Melodee loader marker: never publish a package with vendor files
        raise SystemExit(f"{pkg}: no Melodee loader in it; the site ships only fm1pkg_make.py packages "
                         "(a package patched from an official one carries vendor files)")
    return raw, product


def release_packages(out):
    """a published site's release: its version and its two packages' paths (publish_nightly.py, publish_synth.py)"""
    out = Path(out)
    manifest = out / "firmware/release.json"
    if manifest.exists():
        release = json.loads(manifest.read_text())
    else:  # Existing sites predate the release manifest.
        html = (out / "webapp/installer/index.html").read_text()
        match = re.search(r"^const meta = (\{.*\});$", html, re.M)
        if not match:
            raise SystemExit("Pages installer has no release metadata")
        release = json.loads(match[1])
    return release["version"], {k: out / "firmware" / Path(v).name for k, v in release["pkg"].items()}


def published(out, name):
    """a channel the site already carries (firmware/NAME.json), its packages where they are; None without one"""
    path = out / "firmware" / f"{name}.json"
    if not path.exists():
        return None
    spec = json.loads(path.read_text())
    spec["pkg"] = {k: str(out / "firmware" / Path(v).name) for k, v in spec["pkg"].items()}
    if set(spec["pkg"]) != {"dual", "single"}:      # an older manifest (the 320 MHz variant): until the next run
        return None
    return spec


def channel(spec, label, stem, branch):
    """a development channel's two packages (dual and single core) from one commit: {key: (path, bytes, identity,
    published name)} and the installer's metadata"""
    if not re.fullmatch(r"[0-9a-f]{40}", spec["commit"]):
        raise SystemExit(f"{label} commit must be a full Git SHA")
    files = {}
    for key, suffix in (("dual", ""), ("single", "-1core")):
        path = Path(spec["pkg"][key])
        data, identity = check_package(path)
        files[key] = (path, data, identity, f"{stem}{suffix}.fwsc")
    if files["dual"][2] != files["single"][2]:
        raise SystemExit(f"{label} variants must have the same package identity")
    if files["dual"][1] == files["single"][1]:
        raise SystemExit(f"{label} variants must be distinct builds")
    return files, {"version": spec["version"], "product": files["dual"][2], "branch": branch, "commit": spec["commit"],
                   "builtAt": spec["builtAt"], "pkg": {k: "../../firmware/" + f[3] for k, f in files.items()}}


def main(pkg, single, version, out, nightly=None, installer_only=False, synth=None):
    """synth: the synth-only firmware ({pkg: {dual, single}, version, commit, builtAt, web}: web, the synth-only
    branch's web/ for its editor); None keeps what the site has"""
    pkg, single, out = Path(pkg), Path(single), Path(out)
    raw, product = check_package(pkg)
    raw1, product1 = check_package(single)
    if product1 != product:
        raise SystemExit(f"{single}: identity {product1}, {pkg} is {product}: build both from one release")
    if raw1 == raw:
        raise SystemExit(f"{single} is {pkg}: the single-core package is built with MELODEE_DUAL_CORE=0")
    if nightly is None:
        nightly = published(out, "nightly")
    nightly_files, nightly_meta = channel(nightly, "nightly", "melodee-nightly", "next") if nightly else ({}, None)
    if synth is None:
        synth = published(out, "synth")
    synth_files, synth_meta = channel(synth, "synth", "melodee-synth", "synth-only") if synth else ({}, None)
    if synth_meta:
        synth_meta["editor"] = "../synth-editor/"
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1backup.js").read_text(encoding="utf-8"))
    v = re.sub(r'[^A-Za-z0-9.-]', '-', version)
    name, name1 = f"melodee-{v}.fwsc", f"melodee-{v}-1core.fwsc"
    release_meta = {"version": version, "product": product,
                    "pkg": {"dual": "../../firmware/" + name, "single": "../../firmware/" + name1}}
    meta = json.dumps({**release_meta, "nightly": nightly_meta, "synth": synth_meta})
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    inst, ed, man, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "webapp" / "manual", out / "firmware"
    for d in (inst, ed, man, fw):
        d.mkdir(parents=True, exist_ok=True)
    keep = {name, name1, "melodee-nightly.fwsc", "melodee-nightly-1core.fwsc", "melodee-synth.fwsc",
            "melodee-synth-1core.fwsc"}
    for old in fw.glob("melodee-*.fwsc"):
        if old.name not in keep:
            old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    for src, dest in ((pkg, fw / name), (single, fw / name1)):
        if src.resolve() != dest.resolve():
            shutil.copy(src, dest)
    (fw / "release.json").write_text(json.dumps(release_meta, indent=2) + "\n")
    for files, chan, label in ((nightly_files, nightly_meta, "nightly"), (synth_files, synth_meta, "synth")):
        if chan:
            for path, data, identity, filename in files.values():
                (fw / filename).write_bytes(data)
            (fw / f"{label}.json").write_text(json.dumps(chan, indent=2) + "\n")
    if synth and synth.get("web"):                  # the synth-only branch's editor (its protocol: one track, FX)
        web, sed = Path(synth["web"]), out / "webapp" / "synth-editor"
        sed.mkdir(parents=True, exist_ok=True)
        shutil.copy(web / "editor.html", sed / "index.html")
        for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt", "fm1backup.js"):
            shutil.copy(web / f, sed / f)
    lic = HERE.parent / "LICENSES"                  # the packages hold JieLi SDK files (Apache-2.0): their
    (fw / "LICENSES").mkdir(exist_ok=True)          # licence travels next to it, with Melodee's own
    names = sorted(f.name for f in lic.glob("*.txt"))
    for n in names:
        shutil.copy(lic / n, fw / "LICENSES" / n)
    (fw / "LICENSES" / "index.html").write_text(    # the installer links this folder: Pages lists no folders
        '<!doctype html><meta charset="utf-8"><title>Melodee licences</title><h1>Licence texts</h1><ul>'
        + "".join(f'<li><a href="{n}">{n}</a></li>' for n in names)
        + '</ul><p><a href="../LICENSING.md">LICENSING.md</a> · <a href="../LICENSE">LICENSE (GPL-3.0)</a></p>\n',
        encoding="utf-8")
    for doc in ("LICENSE", "LICENSING.md"):
        shutil.copy(HERE.parent / doc, fw / doc)
    shutil.copy(HERE / "editor.html", ed / "index.html")
    man_src = HERE / "manual"                    # versioned manuals: vX.Y.Z/ (index.html + screenshots)
    ver_re = re.compile(r"^v\d+(?:\.\d+)*$")
    vers = sorted(
        (p for p in man_src.iterdir() if p.is_dir() and ver_re.match(p.name)),
        key=lambda p: tuple(int(x) for x in p.name[1:].split(".")),
    )
    if not vers:
        raise SystemExit(f"no versioned manuals in {man_src}: add web/manual/vX.Y.Z/")
    for p in man_src.iterdir():                  # index.html, versions.json, …
        if p.is_file():
            shutil.copy(p, man / p.name)
    for vdir in vers:
        dest = man / vdir.name
        dest.mkdir(parents=True, exist_ok=True)
        pngs = sorted(vdir.glob("*.png"))
        if not pngs:
            raise SystemExit(f"no screenshots in {vdir}: run tools/manual_shots.py --version {vdir.name}")
        if not (vdir / "index.html").is_file():
            raise SystemExit(f"{vdir}/index.html is missing")
        for p in vdir.iterdir():
            if p.is_file():
                shutil.copy(p, dest / p.name)
    (man / "versions.json").write_text(          # always derive from the dirs on disk
        json.dumps([p.name for p in vers], indent=2) + "\n", encoding="utf-8")
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt", "fm1backup.js"):
        if not installer_only and (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
    (out / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Melodee</title>'
        '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
        '<a href="webapp/installer/">Melodee installer</a>\n', encoding="utf-8")
    print(f"site: {out}: webapp/installer ({len(html)} B), webapp/editor, webapp/manual, firmware/{name} ({len(raw)} B) and "
          f"{name1} ({len(raw1)} B), {product}")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(*sys.argv[1:5])
