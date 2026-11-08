/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SID (engine 3) as a synth module (ABI 3): firmware/src/eng_sid.c as it was built in (tools/mod_build.py) */
#include "../mod_engine.h"
#include "eng_sid.c"

static sid_part_t *sid_part(uint32_t part) { return mod_part_mem(part, sizeof(sid_part_t)); }   /* (engines.c's) */

MOD3_IDENT("melodee", "sid", "SID");
MOD3_ENGINE_EXPORT(mod_sid) = {
    MOD3_ENGINE_HEAD, .engine = 3, .name = "SID", .npresets = SID_NPRESETS, .eng = &ENG_SID,
};
