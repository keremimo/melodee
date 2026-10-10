#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages):

  index.html                        redirect to the installer (the old URL keeps working)
  firmware/melodee-VER.fwsc         the package, both cores (+ LICENSE, LICENSING.md, LICENSES/: the
                                    packages hold JieLi SDK files under Apache-2.0, see LICENSING.md)
  firmware/melodee-VER-1core.fwsc   the same release built with MELODEE_DUAL_CORE=0, for FM-1s that crash
                                    with the dual-core build; the installer offers both
  webapp/installer/index.html       index_pkg.html, self-contained (fm1pkg.js, fm1ota.js, metadata inlined)
  webapp/editor/index.html          editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt)
  src/                              not touched

  web/make_site.py build/melodee-X.Y.fwsc build/melodee-X.Y-1core.fwsc X.Y OUT_DIR

The packages must be ones made by tools/fm1pkg_make.py (Melodee's own loader, no vendor files), from one
release: their identity (FM-1_9xx, FM-1_9xxx from 0.10, FM-1_9xxxx for X.Y.Z) is read from them and must be
the same; the device must report it after the install.
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


def main(pkg, single, version, out):
    pkg, single, out = Path(pkg), Path(single), Path(out)
    raw, product = check_package(pkg)
    raw1, product1 = check_package(single)
    if product1 != product:
        raise SystemExit(f"{single}: identity {product1}, {pkg} is {product}: build both from one release")
    if raw1 == raw:
        raise SystemExit(f"{single} is {pkg}: the single-core package is built with MELODEE_DUAL_CORE=0")
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1backup.js").read_text(encoding="utf-8"))
    v = re.sub(r'[^A-Za-z0-9.-]', '-', version)
    name, name1 = f"melodee-{v}.fwsc", f"melodee-{v}-1core.fwsc"
    meta = json.dumps({"version": version, "product": product,
                       "pkg": {"dual": "../../firmware/" + name, "single": "../../firmware/" + name1}})
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    inst, ed, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "firmware"
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    for old in fw.glob("melodee-*.fwsc"):          # one release: the current one, both builds
        old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    shutil.copy(pkg, fw / name)
    shutil.copy(single, fw / name1)
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
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt", "fm1backup.js"):
        if (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
    (out / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Melodee</title>'
        '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
        '<a href="webapp/installer/">Melodee installer</a>\n', encoding="utf-8")
    print(f"site: {out}: webapp/installer ({len(html)} B), webapp/editor, firmware/{name} ({len(raw)} B) and "
          f"{name1} ({len(raw1)} B), {product}")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(*sys.argv[1:5])
