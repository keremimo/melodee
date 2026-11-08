/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FM6 (engine 12) as a native synth module: firmware/src/eng_fm6.c (its synthesis: fm6_core.c) as it was built in.
 * The parts' voices stay the firmware's (fm6_parts.h), with their formats, the factory voices and DX7 SysEx
 * (fm6_voices.c, fm6_store.c); its queue for CPU1 and CPU1's pairing are the firmware's (imported).
 * tools/mod_build.py --native */
#include "mod_native.h"
#include "eng_fm6.c"

static fm6_part_t *fm6_part(uint32_t part) { return mod_part_mem(part, sizeof(fm6_part_t)); }   /* (engines.c's) */

MOD_NATIVE_EXPORT(mod_native_fm6) = {
    MOD_NATIVE_HEAD, .engine = ENGI_FM6, .name = "FM6", .npresets = FM6_NFAC + 1u, .eng = &ENG_FM6,
};
