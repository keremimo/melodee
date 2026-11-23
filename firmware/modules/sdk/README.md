# Melodee synth module SDK

Everything here builds a synth module (`.mmod`) for Melodee on the FM-1 and the SMK-37, from one C file, outside
Melodee's source tree. A module built once keeps working across firmware updates (ABI 3, `mod_abi3.h`).

    mod_abi3.h    the contract: the frozen views of a part and a voice, the host table, the engine, the file format
    mod_sdk.h     the helpers a module uses (sine, soft clip, SVF, the voice's level), MOD3_EXPORT / IDENT / PRESETS
    mod_schema.h  optional: parameters and pages (MOD3_SCHEMA)
    module.ld, module_a64.ld, mod_rt.c, mod_dump.c, mod_build.py, mod_presets.py, mod_sign.py   the builder
    get_toolchain.sh  the JieLi toolchain the builder compiles with
    example/drone.c   a complete module

The SDK lives in Melodee's source tree (`firmware/modules/sdk`), beside the firmware that keeps its contract, and is
published from there on its own.

## Build

Needs Python 3, a host C compiler (`cc`) and the JieLi pi32v2 toolchain (`JIELI_TOOLCHAIN`, else
`~/.jieli/toolchain`; off Linux x86-64 it runs in Docker). Get the toolchain once:

    sh get_toolchain.sh

Then:

    python3 mod_build.py example/drone.c -o drone.mmod
    python3 mod_build.py --check drone.mmod

Install it with Melodee's web installer (Settings > Synth modules).

For the plug-in hosts (melodee-modules: the Melodee Modules CLAP on the MPC Sample and on a Mac), add the module built
for AArch64 with LLVM's `clang` and `ld.lld` (`module_a64.ld`):

    python3 mod_build.py --native drone.mmod --src example/drone.c -o native/drone.mmod

The AA64 chunk follows SIGN, and a signed module's code is signed too: an NSIG chunk after it, by the same key, over
the whole file before it (the head too, but its CRC). The plug-in hosts run a signed module's AArch64 code only when NSIG checks, so `--native`
needs the key that signed the file (yours; Melodee's for Melodee's synths). An unsigned module's code runs unsigned,
as an unsigned module installs on the FM-1 with its warning. The FM-1 takes the file without AA64: keep the two apart
(`-o native/...`).

## Who it is

`MOD3_IDENT("vendor", "slug", "TAG")`: the vendor is how the lists show you, the slug (lowercase letters, digits, `-`)
is your synth's own name for good, the tag its four letters. Its ID is made from your key and the slug (`mod_abi3.h`),
so it is yours alone: nobody else can make a file with that ID, and a file with the same ID is an update of your synth.
Rename the synth as you like; keep the slug. A device gives each synth an engine number of its own, and projects, user
presets and `.mel` files name the synths they use, so a song moves between devices; a device without the synth keeps
its sounds and plays them silent.

## Your key

The first build makes your key, `~/.config/melodee/author.key` (or the file `MELODEE_AUTHOR_KEY` names), and signs
every module with it. Keep a copy somewhere safe: an update of your synth needs the same key, and a synth signed with
another key is another synth. `mod_build.py --check FILE.mmod` shows a file's ID and who signed it.

## Signed or not

A module signed by its key installs quietly. `MELODEE_AUTHOR_KEY=` (empty) builds an unsigned one: it installs with a
one-line warning ("UNSIGNED: NAME"), and anyone can make a file with its ID. A device refuses a file whose ID its key
did not make, or that its key did not sign.

## Licence

The SDK is MPL-2.0: changes to its own files stay open, your synth may carry any licence you like.
