/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* 808 as a synth module (ABI 3): synthesized TR-808 circuits, eight lanes (drum_808.c). Its sources are the kits' (firmware/modules/kits, kit_module.h) */
#include "../mod_engine.h"
#define DRUM_ONLY 4u
#include "../kits/kit_module.h"

MOD3_IDENT("melodee", "808", "808");
MOD3_ENGINE_EXPORT(mod_808) = {
    MOD3_ENGINE_HEAD, .engine = ENGI_808, .name = "808", .npresets = KIT808_N, .eng = &ENG_KIT808,
};
