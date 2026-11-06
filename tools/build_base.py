#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Build Melodee on fm1-base and felucca-bsp (platform/) from exact dependency revisions.

  tools/build_base.py [--board fm1|smk37] [--model pro|elite|mke] [fm1-base build options]

The FM-1 build (the default) is unchanged. --board smk37 needs --model and makes platform/build/smk37-MODEL/ and
build/melodee-smk37[-elite|-mke].fwsc from the base with the SMK-37 board layer, which installs on nothing here.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
from release_support import source_state, record_build

ROOT=Path(__file__).resolve().parents[1]
# charlesvestal/fm1-base main and its felucca-bsp (hugelton/felucca-bsp main), fetched 2026-11-05
REV="698e2cba46b33bf14d9bf756675036053535638e"
BSP_REV="f878800967e66e721dfbc9a4f5fe1835772ae8fc"
# The SMK-37 board layer: the two PR branches feat/smk37-board (fm1-base on REV, felucca-bsp on BSP_REV), cloned from
# the local repositories until they are pushed (SMK_FROM). The revisions pin it as REV and BSP_REV pin the FM-1's.
SMK_REV="115d3d76d8dde22404b5a969670179357998aebe"
SMK_BSP_REV="5e4af66a7da9cffae1ddaf45222f6dff55452ac6"
SMK_FROM=(Path.home()/"code/personal/fm1-base", Path.home()/"code/personal/felucca-bsp")
SMK_MODELS={"pro":("Pro",""),"elite":("Elite","-elite"),"mke":("MKE","-mke")}  # the product's name, the file's suffix
parser=argparse.ArgumentParser(add_help=False)
parser.add_argument("--board",choices=("fm1","smk37"),default="fm1")
parser.add_argument("--model",choices=tuple(SMK_MODELS))
board_args,build_args=parser.parse_known_args()
board,model=board_args.board,board_args.model
if board=="smk37" and not model:
    raise SystemExit("--board smk37 needs --model pro, elite or mke")
if board=="fm1" and model:
    raise SystemExit("--model is the SMK-37's: use it with --board smk37")
smk=board=="smk37"
if smk:
    REV,BSP_REV=SMK_REV,SMK_BSP_REV
    source=ROOT/"build/deps"/("fm1-base-smk-"+REV[:12])
    work=ROOT/"build/fm1-base-smk37-platform"
else:
    source=ROOT/"build/deps"/("fm1-base-"+REV[:12])
    work=ROOT/"build/fm1-base-platform"
def run(args,**kw):subprocess.run([str(x) for x in args],check=True,**kw)
if smk and not source.exists():
    source.parent.mkdir(parents=True,exist_ok=True)
    run(["git","clone","--no-checkout",SMK_FROM[0],source])
    run(["git","-C",source,"checkout","--detach",REV])
    run(["git","-C",source,"config","submodule.bsp.url",SMK_FROM[1]])
    run(["git","-C",source,"-c","protocol.file.allow=always","submodule","update","--init","--recursive"])
if not source.exists():
    source.parent.mkdir(parents=True,exist_ok=True)
    run(["git","clone","--no-checkout","https://github.com/charlesvestal/fm1-base.git",source])
    run(["git","-C",source,"checkout","--detach",REV])
    run(["git","-C",source,"submodule","update","--init","--recursive"])
if subprocess.check_output(["git","-C",str(source),"rev-parse","HEAD"],text=True).strip()!=REV:
    raise SystemExit("fm1-base audit checkout is not at the pinned revision")
if subprocess.check_output(["git","-C",str(source/"bsp"),"rev-parse","HEAD"],text=True).strip()!=BSP_REV:
    raise SystemExit("felucca-bsp is not at the pinned revision")
if subprocess.check_output(["git","-C",str(source),"status","--porcelain"],text=True).strip():
    raise SystemExit("Pinned dependency has local changes; preserve them and use a clean checkout")
if work.exists():
    shutil.rmtree(work)  # disposable build overlay only; never the dependency checkout
shutil.copytree(source,work,ignore=shutil.ignore_patterns(".git","build","__pycache__"))
# The overlay is kept in the repository; dependency sources remain untouched.
from base_overlay import apply, smk37_linker, smk37_toml
if smk:
    name=SMK_MODELS[model][0]
    (work/"device/app_smk37.ld").write_text(smk37_linker((ROOT/"platform/app.ld").read_text()))
    (work/"melodee-fm1.toml").write_text(smk37_toml((ROOT/"platform/fm1.toml").read_text(),model,name))
    apply(work,board,model,"Melodee "+name)
else:
    shutil.copyfile(ROOT/"platform/app.ld",work/"device/app.ld")
    apply(work)
    from base_bootloader import prepare  # m-bootloader is the FM-1's: nothing of it on the SMK-37
    prepare(work, ROOT/"platform/build/bootloader")
# Asset generation is independent of the retired firmware compiler and packager.
from generate_assets import generate
generate()
env=os.environ.copy()
env.setdefault("JIELI_TOOLCHAIN",str(Path.home()/".jieli/toolchain"))
env.setdefault("AC79_SDK",str(Path.home()/"fw-AC79_AIoT_SDK"))
out=ROOT/"platform/build"/f"smk37-{model}" if smk else ROOT/"platform/build"
target="--host" not in build_args and "--web" not in build_args
before = source_state(ROOT)
run([sys.executable,work/"tools/build.py",ROOT/"platform",*build_args],env=env,cwd=ROOT)

if target:
    stem="melodee-smk37"+SMK_MODELS[model][1] if smk else "melodee"
    if "--no-ble" in build_args:
        stem+="-usb"
    if "--release" in build_args:
        stem+="-"+build_args[build_args.index("--release")+1]
    check_env=os.environ.copy()
    if smk:
        check_env.update(MELODEE_OUT=str(out),MELODEE_BASE=str(work),MELODEE_BOARD=board)
    run([sys.executable,ROOT/"tools/check_base.py",out/(stem+".fwsc")],env=check_env)
    version = build_args[build_args.index("--release")+1] if "--release" in build_args else None
    if version:
        image = (out/"melodee.bin").read_bytes()
        if ("v" + version).encode() not in image:   # MELODEE_VERSION "v" APP_VERSION (melodee_app.c): menu, editor, splash
            raise SystemExit("The release image does not carry its requested version.")
    from fm1_install import product_of
    package = out/(stem+".fwsc")
    record_build(ROOT, package, before, version, product_of(package.read_bytes()), "--no-ble" not in build_args,
                 REV, BSP_REV)
    res = out/"melodee-res.bin"                      # (the installers push it after an update: tools/fm1_resources.py)
    if res.exists():
        shutil.copyfile(res, ROOT/"build"/(package.stem + "-res.bin"))
    if smk:
        shutil.copyfile(package,ROOT/"build"/package.name)
        print(f"package  {ROOT/'build'/package.name}")
