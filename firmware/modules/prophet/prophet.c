/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The Prophet (engine 19) as a native synth module: firmware/src/eng_prophet_test.c as it was built in. The parts'
 * programs stay the firmware's (p5_parts.h); INIT and Sequential's 200 programs are this file's presets (its PRST
 * chunk: tools/mod_build.py writes them as the firmware made them, module_presets.h). The pitch wheel's target is
 * voice.c's (p5_update_bend, imported); its queue for CPU1 and CPU1's pairing are the firmware's (imported).
 * tools/mod_build.py --native */
#include "mod_native.h"
#include "eng_prophet_test.c"

static p5_part_t *p5_part(uint32_t part) { return mod_part_mem(part, sizeof(p5_part_t)); }   /* (engines.c's) */

MOD_NATIVE_EXPORT(mod_native_p5) = {
    MOD_NATIVE_HEAD, .engine = ENGI_PROPHET, .name = "PROPHET", .npresets = P5_FACTORY_N + 1u, .eng = &ENG_P5_TEST,
};
