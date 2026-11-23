#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# The JieLi pi32v2 toolchain mod_build.py compiles a module with: JieLi's Linux toolchain unpacked into DEST and
# linked as DEST/toolchain, where the builder looks first (~/.jieli/toolchain; else JIELI_TOOLCHAIN names it).
# Off Linux x86-64 the builder runs it in Docker.
#   sh get_toolchain.sh [DEST]      (default ~/.jieli)
set -eu
dest=${1:-"$HOME/.jieli"}
mkdir -p "$dest"
curl -fL --retry 3 https://pkgman.jieliapp.com/s/linux-toolchain | tar -xJ -C "$dest"
tc=$(cd "$dest" && ls -d jieli-linux-toolchains-* | sort | tail -n 1)
ln -sfn "$tc" "$dest/toolchain"
echo "JIELI_TOOLCHAIN=$dest/toolchain ($tc)"
