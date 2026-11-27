/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The Prophet (engine 19) as a synth module (ABI 3): firmware/src/eng_prophet_test.c as it was built in. The parts'
 * programs stay the firmware's (p5_parts.h); INIT and Sequential's 200 programs are this file's presets (its PRST
 * chunk: tools/mod_build.py writes them as the firmware made them, module_presets.h). The pitch wheel's target is
 * voice.c's (p5_update_bend, imported); its queue for CPU1 and CPU1's pairing are the firmware's (imported).
 * tools/mod_build.py */
#define MOD3_ENGINE_TABLES MOD3_ENGINE_T(MOD3_T_P5_TANH)   /* (its tanh) */
#include "../mod_engine.h"
#include "eng_prophet_test.c"

static p5_part_t *p5_part(uint32_t part) { return mod_part_mem(part, sizeof(p5_part_t)); }   /* (engines.c's) */

MOD3_IDENT("melodee", "prophet", "P5");
MOD3_ENGINE_EXPORT(mod_p5) = {
    MOD3_ENGINE_HEAD, .engine = ENGI_PROPHET, .name = "PROPHET", .npresets = P5_FACTORY_N + 1u, .eng = &ENG_P5_TEST,
};
