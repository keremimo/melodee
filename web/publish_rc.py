#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Publish a verified release candidate beside an existing site's stable and nightly builds."""
import argparse
import hashlib
import json
from pathlib import Path
from make_site import MODEL_STEMS, check_package, main, release_packages


def publish(out, release_dir):
    release_dir = Path(release_dir)
    info = json.loads((release_dir / "BUILD_INFO.json").read_text())
    version = info["version"]
    checksums = {name: digest for digest, name in
                 (line.split("  ", 1) for line in (release_dir / "SHA256SUMS").read_text().splitlines())}
    if set(info["core_builds"]) != {f"melodee-{version}.fwsc"}:
        raise SystemExit("Release candidates must contain dual-core firmware only")
    name = f"melodee-{version}.fwsc"
    package = release_dir / name
    data, product = check_package(package)
    build = info["core_builds"][name]
    if (hashlib.sha256(data).hexdigest() != checksums.get(name) or
            checksums.get(name) != build["sha256"] or product != info["product"] or
            build["MELODEE_DUAL_CORE"] != 1):
        raise SystemExit(f"Release candidate package failed provenance/checksum checks: {name}")
    # the SMK-37 packages beside it (make_site.py model_packages offers them to their models): each one this
    # release's, as BUILD_INFO and SHA256SUMS name it, or the publish stops
    smk = info.get("smk_builds", {})
    for stem in MODEL_STEMS.values():
        path = release_dir / f"{stem}-{version}.fwsc"
        if not path.is_file():
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if path.name not in smk or digest != checksums.get(path.name) or digest != smk[path.name]["sha256"]:
            raise SystemExit(f"Release candidate package failed provenance/checksum checks: {path.name}")
    stable, stable_packages = release_packages(out)
    main(stable_packages["dual"], stable, out,
         rc={"pkg": {"dual": package}, "version": version, "commit": info["commit"],
             "builtAt": info["builtAt"]}, installer_only=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", help="existing Pages checkout")
    parser.add_argument("release_dir", help="release assets including BUILD_INFO.json and SHA256SUMS")
    args = parser.parse_args()
    publish(args.out, args.release_dir)
