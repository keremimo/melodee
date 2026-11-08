# Melodee synth module SDK

Everything here builds a synth module (`.mmod`) for Melodee on the FM-1 and the SMK-37, from one C file, outside
Melodee's source tree. A module built once keeps working across firmware updates (ABI 3, `mod_abi3.h`).

    mod_abi3.h    the contract: the frozen views of a part and a voice, the host table, the engine, the file format
    mod_sdk.h     the helpers a module uses (sine, soft clip, SVF, the voice's level), MOD3_EXPORT / IDENT / PRESETS
    mod_schema.h  optional: parameters and pages (MOD3_SCHEMA)
    module.ld, mod_rt.c, mod_dump.c, mod_build.py, mod_presets.py, mod_sign.py   the builder
    example/drone.c   a complete module

## Build

Needs Python 3, a host C compiler (`cc`) and the JieLi pi32v2 toolchain (`JIELI_TOOLCHAIN`, else
`~/.jieli/toolchain`; off Linux x86-64 it runs in Docker):

    python3 mod_build.py example/drone.c -o drone.mmod
    python3 mod_build.py --check drone.mmod

Install it with Melodee's web installer (Settings > Synth modules) or `tools/fm1_modules.py`.

## Who it is

`MOD3_IDENT("vendor", "TAG")` names the author; the module's UID is CRC-32 of `vendor/NAME`. A device gives each UID
an engine number: the one the descriptor asks for (22..29) when it is free, else the first free one. Projects, user
presets and `.mel` files carry the numbers they name, so a song moves between devices; a device without the module keeps
its sounds and plays them silent.

## Signed or not

Melodee signs the modules it ships. Yours installs all the same, with a one-line warning ("UNSIGNED: NAME").
