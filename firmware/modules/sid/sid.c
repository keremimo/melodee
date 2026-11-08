/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SID (engine 3) as a native synth module: firmware/src/eng_sid.c as it was built in (tools/mod_build.py --native) */
#include "mod_native.h"
#include "eng_sid.c"

static sid_part_t *sid_part(uint32_t part) { return mod_part_mem(part, sizeof(sid_part_t)); }   /* (engines.c's) */

MOD_NATIVE_EXPORT(mod_native_sid) = {
    MOD_NATIVE_HEAD, .engine = 3, .name = "SID", .npresets = SID_NPRESETS, .eng = &ENG_SID,
};
