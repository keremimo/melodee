#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Publish the synth module SDK (firmware/modules/sdk as a commit has it) into a checkout of its own repository: its
# files become the commit's, and one commit there names the Melodee commit when they changed. Pushing stays yours.
#   tools/sdk_publish.sh [DEST] [REV]     (default: melodee-module-sdk beside Melodee's main checkout, HEAD)
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
main=$(dirname "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)")
dest=${1:-"$(dirname "$main")/melodee-module-sdk"}
rev=${2:-HEAD}
sha=$(git -C "$here" rev-parse --short=8 "$rev^{commit}")
git -C "$here" cat-file -e "$sha:firmware/modules/sdk" || { echo "sdk_publish: no firmware/modules/sdk in $sha" >&2; exit 1; }
[ "$(git -C "$dest" rev-parse --show-toplevel 2>/dev/null)" = "$(cd "$dest" 2>/dev/null && pwd -P)" ] ||
    { echo "sdk_publish: $dest is not a git checkout's top (git init or clone it first)" >&2; exit 1; }
[ -z "$(git -C "$dest" status --porcelain)" ] || { echo "sdk_publish: $dest has changes of its own" >&2; exit 1; }
git -C "$dest" rm -rq --ignore-unmatch -- .
git -C "$here" archive "$sha:firmware/modules/sdk" | tar -x -C "$dest"
git -C "$dest" add -A
if git -C "$dest" diff --cached --quiet; then
    echo "sdk_publish: $dest already has the SDK of $sha"
else
    git -C "$dest" commit -q -m "SDK from Melodee $sha"
    echo "sdk_publish: $dest $(git -C "$dest" log -1 --format='%h %s')"
fi
