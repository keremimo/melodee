/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* CZ-1 (engine 15) as a native synth module: firmware/src/eng_cz.c (cz_native.c, its core in eng_phase.c) as it was
 * built in. The tones stay the firmware's (cz_parts.h), Casio's factory tones too: the presets are made there
 * (cz_tones.c cz_preset_make, imported). tools/mod_build.py --native */
#include "mod_native.h"
#include "melodee_cz1.h"                         /* (CZ_FACTORY_N: the presets' count; the tones: the firmware's) */
#include "eng_phase.c"
static void cz_preset_make(uint32_t k, preset_t *p, char *name) { mod_imp_cz_preset_make(k, p, name); }
#include "eng_cz.c"

static cz_part_t *cz_part(uint32_t part) { return mod_part_mem(part, sizeof(cz_part_t)); }   /* (engines.c's) */

MOD_NATIVE_EXPORT(mod_native_cz) = {
    MOD_NATIVE_HEAD, .engine = ENGI_CZ, .name = "CZ-1", .npresets = CZ_FACTORY_N + 1u, .eng = &ENG_CZ,
};
