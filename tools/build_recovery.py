#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and smoke-test a native standalone recovery executable and release ZIP."""
import argparse
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

from fm1_recover import Package, VERSION, read_loader, sha256

ROOT = Path(__file__).resolve().parents[1]
LIBUSB_VERSION = "1.0.30"
LIBUSB_HASHES = {"tar.bz2": "fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf",
                 "7z": "7fb1dfec805b97983763d7d0ae244320da12add1003d4249c96cc4d586398c79"}


def run(*args, cwd=None):
    subprocess.run([str(a) for a in args], cwd=cwd or ROOT, check=True)


def fetch(url, path, digest=None):
    with urllib.request.urlopen(url, timeout=60) as response:
        data = response.read()
    if digest and sha256(data) != digest:
        raise RuntimeError(f"Download checksum mismatch: {url}")
    path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True, choices=["windows-x64", "macos-arm64", "macos-x64", "linux-x64"])
    parser.add_argument("--stock-package", type=Path,
                        help="smoke-test genuine official V15 offline; the firmware is never included in the archive")
    args = parser.parse_args()
    if args.stock_package:
        if not Package(args.stock_package).stock_v15:
            raise RuntimeError("--stock-package requires the verified official M-VAVE V15 package")
    actual = (sys.platform, platform.machine().lower())
    expected = {"windows-x64": ("win32", "amd64"), "macos-arm64": ("darwin", "arm64"),
                "macos-x64": ("darwin", "x86_64"), "linux-x64": ("linux", "x86_64")}[args.target]
    if actual != expected:
        raise RuntimeError(f"Build {args.target} on its native runner: expected {expected}, got {actual}")
    read_loader(ROOT / "assets/recovery/wl82loader.bin")
    out = ROOT / "build/recovery" / args.target
    out.mkdir(parents=True, exist_ok=True)
    extension = "7z" if sys.platform == "win32" else "tar.bz2"
    archive = out / f"libusb-{LIBUSB_VERSION}.{extension}"
    base = f"https://github.com/libusb/libusb/releases/download/v{LIBUSB_VERSION}/"
    fetch(base + archive.name, archive, LIBUSB_HASHES[extension])
    # Include corresponding source even for the Windows prebuilt library.
    source_archive = out / f"libusb-{LIBUSB_VERSION}.tar.bz2"
    if extension == "7z":
        fetch(base + source_archive.name, source_archive, LIBUSB_HASHES["tar.bz2"])
        run("7z", "x", "-y", archive, f"-o{out / 'libusb-windows'}")
        candidates = list((out / "libusb-windows").glob("VS2022/MS64/dll/libusb-1.0.dll"))
        if len(candidates) != 1:
            raise RuntimeError("Expected the official VS2022 x64 libusb DLL")
        library = candidates[0]
        library_name = "libusb-1.0.dll"
    with tarfile.open(source_archive) as tar:
        tar.extractall(out, filter="data")
    source = out / f"libusb-{LIBUSB_VERSION}"
    if extension != "7z":
        prefix = out / "libusb-install"
        flags = [f"--prefix={prefix}", "--disable-static"]
        if sys.platform == "linux":
            flags.append("--disable-udev")
        run(source / "configure", *flags, cwd=source)
        run("make", "-j2", cwd=source)
        run("make", "install", cwd=source)
        library_name = "libusb-1.0.dylib" if sys.platform == "darwin" else "libusb-1.0.so.0"
        library = prefix / "lib" / library_name
    stage = out / "staging"
    stage.mkdir(exist_ok=True)
    shutil.copyfile(library, stage / library_name)
    run(sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onefile", "--name", "fm1-recover",
        "--collect-submodules", "usb", "--exclude-module", "mido", "--exclude-module", "tkinter",
        "--add-data", f"{ROOT / 'assets/recovery/wl82loader.bin'}{os.pathsep}.",
        "--add-binary", f"{stage / library_name}{os.pathsep}.",
        "--distpath", out / "frozen", "--workpath", out / "work", "--specpath", out,
        ROOT / "tools/fm1_recover.py")
    exe = out / "frozen" / ("fm1-recover.exe" if sys.platform == "win32" else "fm1-recover")
    run(exe, "--version")
    run(exe, "--help")
    # Check package decoding and the bundled RAM helper without USB or an SDK.
    sys.path.insert(0, str(ROOT / "tests"))
    from recovery_test import make_package
    fixture = out / "smoke.fwsc"
    fixture.write_bytes(make_package())
    run(exe, fixture, "--check")
    if args.stock_package:
        run(exe, args.stock_package.resolve(), "--check")
    # Enumerating an empty CI runner exercises the bundled PyUSB/libusb backend.
    probe = subprocess.run([str(exe), "--info", "--wait", "0"], capture_output=True, text=True)
    if probe.returncode != 1 or "ROM UBOOT device not found" not in probe.stderr:
        raise RuntimeError(f"Frozen USB backend smoke test failed: {probe.stdout}\n{probe.stderr}")
    name = f"fm1-recover-{VERSION}-{args.target}"
    bundle = out / name
    if bundle.exists():
        shutil.rmtree(bundle)
    bundle.mkdir()
    shutil.copy(exe, bundle)
    licenses = bundle / "LICENSES"
    licenses.mkdir(exist_ok=True)
    for path in (ROOT / "LICENSE", ROOT / "LICENSING.md", ROOT / "docs/RECOVERY.md"):
        shutil.copy(path, bundle)
    shutil.copy(ROOT / "LICENSES/Apache-2.0.txt", licenses)
    shutil.copy(source / "COPYING", licenses / "LGPL-2.1-libusb.txt")
    shutil.copy(source_archive, bundle)
    for dependency in ("pyusb", "pyinstaller"):
        dist = importlib.metadata.distribution(dependency)
        for file in dist.files or []:
            if any(word in Path(file).name.lower() for word in ("license", "copying", "copyright")):
                path = Path(dist.locate_file(file))
                if path.is_file():
                    shutil.copy(path, licenses / f"{dependency}-{Path(file).name}")
    fetch(f"https://raw.githubusercontent.com/python/cpython/v{platform.python_version()}/LICENSE",
          licenses / "Python.txt")
    manifest = {"version": VERSION, "target": args.target,
                "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "python": platform.python_version(), "pyusb": importlib.metadata.version("pyusb"),
                "pyinstaller": importlib.metadata.version("pyinstaller"), "libusb": LIBUSB_VERSION,
                "libusb_sha256": sha256((stage / library_name).read_bytes()),
                "executable_sha256": sha256(exe.read_bytes()), "smoke_tests_passed": True,
                "stock_v15_smoke_tested": bool(args.stock_package),
                "source_sha256": {name: sha256((ROOT / "tools" / name).read_bytes())
                                  for name in ("fm1_recover.py", "fm1_install.py", "fm1pkg_make.py", "build_recovery.py")},
                "worktree_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
                "hardware_tested": False}
    (bundle / "BUILD_INFO.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (bundle / "SHA256SUMS").write_text("".join(f"{sha256(p.read_bytes())}  {p.relative_to(bundle).as_posix()}\n"
                                               for p in sorted(bundle.rglob("*")) if p.is_file()))
    result = out / (name + ".zip")
    with zipfile.ZipFile(result, "w", zipfile.ZIP_DEFLATED) as zipout:
        for path in sorted(bundle.rglob("*")):
            if path.is_file():
                zipout.write(path, str(path.relative_to(out)))
    print(f"Standalone recovery: {result}; SHA-256 {sha256(result.read_bytes())}")


if __name__ == "__main__":
    main()
