#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages):

  index.html                        redirect to the installer (the old URL keeps working)
  firmware/melodee-VER.fwsc         the package, both cores (+ LICENSE, LICENSING.md, LICENSES/: the
                                    packages hold JieLi SDK files under Apache-2.0, see LICENSING.md)
  firmware/melodee-nightly*.fwsc    next's development builds (nightly.json; publish_nightly.py), kept as published
  firmware/melodee-*-rc.*.fwsc      published release candidate (rc.json; publish_rc.py), kept as published
  webapp/installer/index.html       index_pkg.html, self-contained (fm1pkg.js, fm1ota.js, metadata inlined)
  webapp/editor/index.html          editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt)
  webapp/manual/                    web/manual/ on a full publish (the stable manual: HTML and screenshots).
                                    Nightly and release-candidate updates leave it in place.
  src/                              not touched

  web/make_site.py build/melodee-X.Y.fwsc X.Y OUT_DIR

Packages must be made by tools/fm1pkg_make.py (Melodee's own loader, no vendor files).
Their identity is read from the package; the device must report it after installation.

Manual screenshots are generated into web/manual/ and are not stored in git. A full
publish and web/quick-host.sh run tools/manual_shots.py first — see BUILDING.md "Local site".
The figure names are the <img src> values in web/manual/index.html. A full publish checks
every one of them before it writes the output site, then replaces webapp/manual/ with the
manual page and those screenshots. Other PNGs and older version folders are removed; nightly/ is preserved. An
installer-only update does not read or replace the published manual.
"""
import json
import html as html_escape
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    """drop ES module import/export so the script can be inlined into index_pkg.html"""
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def check_package(pkg):
    """the package's bytes and identity; refuses anything that is not a Melodee package"""
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"(FM-1|SMK-37 Pro|SMK-37 Elite|MKE-P37)_9\d{2,4}", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a Melodee package (FM-1, SMK-37 Pro, SMK-37 Elite or MKE-P37, "
                         "_9 and two to four digits)")
    if b"FELUCCA-LOADER-1" not in raw:              # Melodee loader marker: never publish a package with vendor files
        raise SystemExit(f"{pkg}: no Melodee loader in it; the site ships only fm1pkg_make.py packages "
                         "(a package patched from an official one carries vendor files)")
    return raw, product


def release_packages(out):
    """A published site's release version and dual-core package path."""
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
    return release["version"], {"dual": out / "firmware" / Path(release["pkg"]["dual"]).name}


def published(out, name):
    """a channel the site already carries (firmware/NAME.json), its packages where they are; None without one"""
    path = out / "firmware" / f"{name}.json"
    if not path.exists():
        return None
    spec = json.loads(path.read_text())
    if "dual" not in spec["pkg"]:
        return None
    spec["pkg"] = {"dual": str(out / "firmware" / Path(spec["pkg"]["dual"]).name)}
    return spec


def channel(spec, label, stem, branch):
    """Validate a dual-core development package and prepare installer metadata."""
    if not re.fullmatch(r"[0-9a-f]{40}", spec["commit"]):
        raise SystemExit(f"{label} commit must be a full Git SHA")
    if set(spec["pkg"]) != {"dual"}:
        raise SystemExit(f"{label} must offer dual-core firmware only")
    path = Path(spec["pkg"]["dual"])
    data, identity = check_package(path)
    if branch == "nightly" and identity != "FM-1_900":
        raise SystemExit("Nightly firmware must use development identity FM-1_900")
    files = {"dual": (path, data, identity, f"{stem}.fwsc")}
    return files, {"version": spec["version"], "product": identity, "branch": branch, "commit": spec["commit"],
                   "builtAt": spec["builtAt"], "pkg": {"dual": "../../firmware/" + f"{stem}.fwsc"}}


def release_history(out, current):
    """Offer validated stable dual-core packages, including historical releases."""
    releases = {current["version"]: current}
    fw = Path(out) / "firmware"
    for dual in fw.glob("melodee-*.fwsc"):
        match = re.fullmatch(r"melodee-(\d+\.\d+\.\d+)\.fwsc", dual.name)
        if not match or int(match[1].split(".")[0]) < 1 or match[1] == current["version"]:
            continue
        version = match[1]
        raw, product = check_package(dual)
        releases[version] = {"version": version, "product": product,
                             "pkg": {"dual": "../../firmware/" + dual.name}}
    return [current] + [releases[v] for v in sorted(releases, key=lambda v: tuple(map(int, v.split("."))), reverse=True)
                        if v != current["version"]]


def manual_figures(manual):
    """PNG names embedded in the manual, in order; each src is a file name beside index.html"""
    page = manual / "index.html"
    if not page.is_file():
        raise SystemExit(f"{page} is missing")
    names = []
    for name in re.findall(r'<img\b[^>]*\bsrc="([^"]*)"', page.read_text(encoding="utf-8")):
        if name in names:
            continue
        if not re.fullmatch(r"[A-Za-z0-9_.-]+\.png", name):
            raise SystemExit(f"{page}: screenshot src {name!r} must name a PNG beside the manual")
        names.append(name)
    return names


def require_manual(manual):
    """every embedded screenshot exists; called before a full publish writes the output site"""
    missing = [name for name in manual_figures(manual) if not (manual / name).is_file()]
    if missing:
        raise SystemExit(f"{manual}: {len(missing)} screenshot(s) missing: {', '.join(missing)}")


def publish_manual(src, dest, version=None, preserve_nightly=False):
    """Publish the guide and embedded screenshots, optionally retaining its nightly child."""
    embedded = set(manual_figures(src))
    keep = {p.name for p in src.iterdir()
            if p.is_file() and (p.suffix.lower() != ".png" or p.name in embedded)}
    dest.mkdir(parents=True, exist_ok=True)
    for old in list(dest.iterdir()):
        if preserve_nightly and old.name == "nightly" and old.is_dir():
            continue
        if old.name in keep:
            continue
        if old.is_dir():
            shutil.rmtree(old)
        else:
            old.unlink()
    for name in sorted(keep):
        shutil.copy(src / name, dest / name)
    if version is not None:
        page = dest / "index.html"
        text = page.read_text(encoding="utf-8").replace(
            '<span data-firmware-version>development source</span>',
            '<span data-firmware-version>' + html_escape.escape(version) + '</span>')
        # A nightly guide lives one level below the stable guide.
        if dest.name == "nightly":
            text = text.replace('href="../editor/"', 'href="../../editor/"').replace(
                'href="../installer/"', 'href="../../installer/"')
        page.write_text(text, encoding="utf-8")


def main(pkg, version, out, nightly=None, installer_only=False, rc=None, manual=None):
    pkg, out = Path(pkg), Path(out)
    raw, product = check_package(pkg)
    if nightly is None:
        nightly = published(out, "nightly")
    nightly_files, nightly_meta = channel(nightly, "nightly", "melodee-nightly", "nightly") if nightly else ({}, None)
    if rc is None:
        rc = published(out, "rc")
    rc_files, rc_meta = {}, None
    if rc:
        if not re.fullmatch(r"\d\.\d{1,2}\.\d-rc\.\d+", rc["version"]):
            raise SystemExit("Release candidate version must be X.Y.Z-rc.N")
        rc_files, rc_meta = channel(rc, "release candidate", f"melodee-{rc['version']}", "next")
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1backup.js").read_text(encoding="utf-8"))
    v = re.sub(r'[^A-Za-z0-9.-]', '-', version)
    name = f"melodee-{v}.fwsc"
    release_meta = {"version": version, "product": product,
                    "pkg": {"dual": "../../firmware/" + name}}
    releases = release_history(out, release_meta)
    meta = json.dumps({**release_meta, "nightly": nightly_meta, "rc": rc_meta, "releases": releases})
    for mark in ("/*LIB*/", "/*META*/{}"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    html = html.replace("/*LIB*/", lib).replace("/*META*/{}", meta)
    inst, ed, man, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "webapp" / "manual", out / "firmware"
    man_src = Path(manual) if manual is not None else HERE / "manual"
    if not installer_only:
        require_manual(man_src)                     # before any write: a missing figure must leave the site untouched
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    keep = {name, "melodee-nightly.fwsc"}
    keep.update(file[3] for file in rc_files.values())
    keep.update(Path(path).name for release in releases for path in release["pkg"].values())
    for old in fw.glob("melodee-*.fwsc"):
        if old.name not in keep:
            old.unlink()
    (fw / "synth.json").unlink(missing_ok=True)
    retired_editor = out / "webapp" / "synth-editor"
    if retired_editor.exists():
        shutil.rmtree(retired_editor)
    (inst / "index.html").write_text(html, encoding="utf-8")
    if pkg.resolve() != (fw / name).resolve():
        shutil.copy(pkg, fw / name)
    (fw / "release.json").write_text(json.dumps(release_meta, indent=2) + "\n")
    (fw / "releases.json").write_text(json.dumps(releases, indent=2) + "\n")
    if nightly_meta:
        for path, data, identity, filename in nightly_files.values():
            (fw / filename).write_bytes(data)
        (fw / "nightly.json").write_text(json.dumps(nightly_meta, indent=2) + "\n")
    if rc_meta:
        for path, data, identity, filename in rc_files.values():
            (fw / filename).write_bytes(data)
        (fw / "rc.json").write_text(json.dumps(rc_meta, indent=2) + "\n")
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
    if not installer_only:
        publish_manual(man_src, man, version, preserve_nightly=True)
        shutil.copy(HERE / "editor.html", ed / "index.html")
        for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt", "fm1backup.js"):
            if (HERE / f).exists():
                shutil.copy(HERE / f, ed / f)
        (out / "index.html").write_text(
            '<!doctype html><meta charset="utf-8"><title>Melodee</title>'
            '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
            '<a href="webapp/installer/">Melodee installer</a>\n', encoding="utf-8")
    print(f"site: {out}: webapp/installer ({len(html)} B), webapp/editor, firmware/{name} ({len(raw)} B), {product}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
