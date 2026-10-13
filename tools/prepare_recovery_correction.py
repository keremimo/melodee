#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare/verify replacement recovery assets without replacing release firmware."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

from fm1_install import STOCK_V15_SHA256
from fm1_recover import VERSION, sha256
from prepare_release import TARGETS

ROOT = Path(__file__).resolve().parents[1]


def checksums(folder):
    result = {}
    for line in (folder / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split("  ", 1)
        if not re.fullmatch(r"[0-9a-f]{64}", digest) or Path(name).name != name or name in result:
            raise RuntimeError("Invalid release checksum list")
        if sha256((folder / name).read_bytes()) != digest:
            raise RuntimeError(f"Release checksum mismatch: {name}")
        result[name] = digest
    files = {p.name for p in folder.iterdir() if p.is_file() and p.name != "SHA256SUMS"}
    if files != set(result):
        raise RuntimeError("Release checksum list must cover every asset")
    return result


def prepare(version, published, staged, output, commit, firmware_commit, verify=False):
    if version != VERSION:
        raise RuntimeError("Recovery correction version differs from release")
    old_sums, new_sums = checksums(published), checksums(staged)
    original = json.loads((published / "BUILD_INFO.json").read_text())
    build = json.loads((staged / "BUILD_INFO.json").read_text())
    if original["version"] != version or original["commit"] != firmware_commit:
        raise RuntimeError("Published firmware provenance differs from signed tag")
    if build["version"] != version or build["commit"] != commit:
        raise RuntimeError("Correction build provenance differs from source commit")
    for name, info in original["core_builds"].items():
        if old_sums.get(name) != info["sha256"]:
            raise RuntimeError(f"Published firmware checksum differs from provenance: {name}")
    names = [f"fm1-recover-{version}-{target}.zip" for target in TARGETS]
    if set(build["recovery_builds"]) != set(names):
        raise RuntimeError("Correction requires all four recovery platforms")
    source_hashes = {name: sha256((ROOT / "tools" / name).read_bytes())
                     for name in ("fm1_recover.py", "fm1_install.py", "fm1pkg_make.py", "build_recovery.py")}
    for target, name in zip(TARGETS, names):
        with zipfile.ZipFile(staged / name) as archive:
            prefix = name[:-4] + "/"
            info = json.loads(archive.read(prefix + "BUILD_INFO.json"))
            if (info != build["recovery_builds"][name] or info["commit"] != commit or
                    info["version"] != version or info["target"] != target or
                    not info["smoke_tests_passed"] or info.get("worktree_dirty", False)):
                raise RuntimeError(f"Wrong, dirty or untested recovery archive: {name}")
            if info.get("source_sha256") != source_hashes:
                raise RuntimeError(f"Recovery source hashes differ from correction checkout: {name}")
            executable = "fm1-recover.exe" if target == "windows-x64" else "fm1-recover"
            if sha256(archive.read(prefix + executable)) != info.get("executable_sha256"):
                raise RuntimeError(f"Recovery executable differs from build provenance: {name}")
            covered = set()
            for line in archive.read(prefix + "SHA256SUMS").decode().splitlines():
                digest, path = line.split("  ", 1)
                if sha256(archive.read(prefix + path)) != digest:
                    raise RuntimeError(f"Recovery archive checksum mismatch: {name}/{path}")
                covered.add(prefix + path)
            files = {p for p in archive.namelist() if not p.endswith("/") and p != prefix + "SHA256SUMS"}
            if files != covered:
                raise RuntimeError(f"Incomplete recovery archive checksums: {name}")
    if verify:
        if (original.get("recovery_correction", {}).get("commit") != commit or
                original["recovery_builds"] != build["recovery_builds"] or
                any(old_sums.get(name) != new_sums[name] for name in names)):
            raise RuntimeError("Published recovery correction differs from tested builds")
        print(f"PUBLIC RECOVERY CORRECTION VERIFIED: {commit}; firmware remains {firmware_commit}")
        return
    output.mkdir(parents=True, exist_ok=False)
    for name in names:
        shutil.copyfile(staged / name, output / name)
        old_sums[name] = new_sums[name]
    original["recovery_builds"] = build["recovery_builds"]
    original["recovery_correction"] = {
        "commit": commit, "firmware_commit": firmware_commit,
        "publishedAt": datetime.now(timezone.utc).isoformat(),
        "official_v15_sha256": STOCK_V15_SHA256,
        "scope": "Recovery tools only; firmware assets unchanged",
        "validation": "Native CI smoke tests and full host suite passed; physical recovery not tested",
    }
    blob = (json.dumps(original, indent=2) + "\n").encode()
    (output / "BUILD_INFO.json").write_bytes(blob)
    old_sums["BUILD_INFO.json"] = sha256(blob)
    (output / "SHA256SUMS").write_text("".join(f"{digest}  {name}\n" for name, digest in sorted(old_sums.items())))
    print(f"Prepared recovery-only correction: {output}; firmware remains {firmware_commit}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("published", type=Path)
    parser.add_argument("staged", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--verify-published", action="store_true")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    firmware_commit = subprocess.check_output(["git", "rev-parse", f"v{args.version}^{{commit}}"], cwd=ROOT, text=True).strip()
    prepare(args.version, args.published, args.staged, args.output, commit, firmware_commit, args.verify_published)


if __name__ == "__main__":
    main()
