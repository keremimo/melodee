# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Build receipts and matching, committed source archives for public firmware."""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
import time
import zipfile


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args])


def submodules(root):
    """the checkout's submodules' paths (firmware/modules/sdk: melodee-module-sdk), [] without .gitmodules"""
    if not (root / ".gitmodules").is_file():
        return []
    r = subprocess.run(["git", "-C", str(root), "config", "-f", ".gitmodules", "--get-regexp", r"^submodule\..*\.path$"],
                       capture_output=True, text=True)
    return [line.split(None, 1)[1] for line in r.stdout.splitlines() if line.strip()]


def source_state(root):
    checkout = subprocess.run(["git", "-C", str(root), "rev-parse", "--show-toplevel"],
                              capture_output=True, text=True)
    in_git = checkout.returncode == 0 and Path(checkout.stdout.strip()).resolve() == root.resolve()
    archived = None
    if in_git:
        # (the submodules' files too: --recurse-submodules lists them in place of their gitlinks)
        names = (git(root, "ls-files", "--cached", "--recurse-submodules", "-z").decode().split("\0") +
                 git(root, "ls-files", "--others", "--exclude-standard", "-z").decode().split("\0"))
    else:
        metadata = root / "SOURCE_INFO.json"
        if not metadata.is_file():
            raise SystemExit("Build from a Git checkout or an official source ZIP with SOURCE_INFO.json.")
        archived = json.loads(metadata.read_text())["source"]
        names = archived["files"]
    files = {}
    for name in sorted(set(names) - {""}):
        path = root / name
        if path.is_symlink():
            data = str(path.readlink()).encode()
        elif path.is_file():
            data = path.read_bytes()
        else:
            continue
        files[name] = sha256(data)
    if in_git:
        return {"commit": git(root, "rev-parse", "HEAD").decode().strip(),
                "dirty": bool(git(root, "status", "--porcelain")), "files": files}
    return {"commit": archived["commit"], "dirty": files != archived["files"], "files": files}


def receipt_path(package):
    return package.with_suffix(".build.json")


def record_build(root, package, before, version, product, ble, base_revision, bsp_revision):
    if source_state(root) != before:
        raise SystemExit("Source changed during the firmware build; rebuild before preparing a release.")
    receipt = {"version": version, "product": product, "ble": ble,
               "package": package.name, "package_sha256": sha256(package.read_bytes()),
               "source": before, "dependencies": {"fm1-base": base_revision, "felucca-bsp": bsp_revision}}
    receipt_path(package).write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt


def validate_build(root, package, version, info=None):
    info = info or receipt_path(package)
    if not info.is_file():
        raise SystemExit("No build receipt: build the versioned firmware before preparing its public site.")
    receipt = json.loads(info.read_text())
    if receipt.get("version") != version or not receipt.get("ble"):
        raise SystemExit("The build receipt is not for this BLE release version.")
    if receipt.get("package_sha256") != sha256(package.read_bytes()):
        raise SystemExit("Firmware package differs from its build receipt.")
    state = source_state(root)
    if state["dirty"] or receipt["source"].get("dirty"):
        raise SystemExit("Commit all source changes before building a public release.")
    if state != receipt["source"]:
        raise SystemExit("Source differs from the firmware build receipt; rebuild before release.")
    return receipt


def _archive_repository(archive, root, prefix, timestamp):
    """Use committed objects only: no history, ignored outputs or device captures."""
    with tarfile.open(fileobj=io.BytesIO(git(root, "archive", "HEAD"))) as source:
        for entry in source:
            if entry.isdir():
                continue
            path = Path(entry.name)
            if path.is_absolute() or ".." in path.parts:
                raise ValueError("Unsafe path in source archive")
            info = zipfile.ZipInfo(prefix + entry.name, timestamp)
            info.create_system = 3
            if entry.issym():
                info.external_attr = 0o120777 << 16
                data = entry.linkname.encode()
            elif entry.isfile():
                info.external_attr = (0o100000 | entry.mode) << 16
                data = source.extractfile(entry).read()
            else:
                raise ValueError(f"Unsupported source archive entry: {entry.name}")
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)


def make_source_archive(root, version, output, receipt, base):
    state = source_state(root)
    if state["dirty"] or state != receipt["source"]:
        raise SystemExit("Source no longer matches the clean firmware build.")
    for name, repo in (("fm1-base", base), ("felucca-bsp", base / "bsp")):
        if git(repo, "rev-parse", "HEAD").decode().strip() != receipt["dependencies"][name] or git(repo, "status", "--porcelain"):
            raise SystemExit(f"Pinned {name} source changed; cannot archive a matching dependency.")
    for sub in submodules(root):                     # (each at the commit HEAD pins, clean: its files are the source)
        pin = git(root, "rev-parse", f"HEAD:{sub}").decode().strip()
        if git(root / sub, "rev-parse", "HEAD").decode().strip() != pin or git(root / sub, "status", "--porcelain"):
            raise SystemExit(f"Submodule {sub} is not clean at the commit HEAD pins; cannot archive matching source.")
    prefix = f"melodee-{version}-source/"
    timestamp = time.gmtime(int(git(root, "show", "-s", "--format=%ct", "HEAD")))[:6]
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        _archive_repository(archive, root, prefix, timestamp)
        for sub in submodules(root):                 # (git archive leaves a submodule out: its own, in its place)
            _archive_repository(archive, root / sub, prefix + sub + "/", timestamp)
        _archive_repository(archive, base, prefix + "dependencies/fm1-base/", timestamp)
        _archive_repository(archive, base / "bsp", prefix + "dependencies/fm1-base/bsp/", timestamp)
        archive.writestr(prefix + "SOURCE_INFO.json", json.dumps(receipt, indent=2) + "\n")
        archive.writestr(prefix + "SOURCE_README.txt", f"""Melodee {version} matching firmware source

Application commit: {state['commit']}
Package SHA-256: {receipt['package_sha256']}

The application source is at this directory's root. Exact fm1-base and
felucca-bsp source copies are under dependencies/fm1-base/ and its bsp/.
SOURCE_INFO.json records the revisions and application file hashes.

Follow BUILDING.md to rebuild with ./build.sh --release {version}.
The wrapper fetches the recorded revisions into build/deps/.
The JieLi compiler and SDK must be installed separately as described there;
the vendor SDK binaries and compiler are not included in this source ZIP.

MPL-covered source is supplied under MPL-2.0. Other files retain their own
licences/notices. Read LICENSE, LICENSING.md and the LICENSES directories.
Factory sound data retains its vendor attribution and separate terms.
""")
    if source_state(root) != state:
        raise SystemExit("Source changed while assembling its release archive.")
    validate_source_archive(output, version, receipt)


def validate_source_archive(path, version, receipt):
    prefix = f"melodee-{version}-source/"
    with zipfile.ZipFile(path) as archive:
        if archive.testzip():
            raise SystemExit("Corrupt release source archive.")
        info = json.loads(archive.read(prefix + "SOURCE_INFO.json"))
        if info != receipt:
            raise SystemExit("Source archive metadata differs from the firmware build receipt.")
        for name, digest in receipt["source"]["files"].items():
            if sha256(archive.read(prefix + name)) != digest:
                raise SystemExit(f"Source archive does not match the firmware: {name}")
        for name in ("dependencies/fm1-base/LICENSE", "dependencies/fm1-base/bsp/LICENSE"):
            if prefix + name not in archive.namelist():
                raise SystemExit("Dependency source/notices missing from the source archive.")
    return {"sha256": sha256(path.read_bytes()), "commit": receipt["source"]["commit"],
            "licence": "MPL-2.0", "dependencies": receipt["dependencies"]}
