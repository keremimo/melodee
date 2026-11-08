/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* CZ-1 (engine 15) as a synth module (ABI 3): firmware/src/eng_cz.c (cz_native.c, its core in eng_phase.c) as it was
 * built in. The parts' tones stay the firmware's (cz_parts.h); INIT TONE and Casio's 64 tones are this file's presets
 * (its PRST chunk: tools/mod_build.py writes them as the firmware made them, module_presets.h).
 * tools/mod_build.py */
#include "mod_engine.h"
#include "eng_phase.c"
#include "eng_cz.c"

static cz_part_t *cz_part(uint32_t part) { return mod_part_mem(part, sizeof(cz_part_t)); }   /* (engines.c's) */

MOD3_ENGINE_EXPORT(mod_cz) = {
    MOD3_ENGINE_HEAD, .engine = ENGI_CZ, .name = "CZ-1", .npresets = CZ_FACTORY_N + 1u, .eng = &ENG_CZ,
};
