/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* 909 as a synth module (ABI 3): the x0x 909 in fp32 (x0x/). Its sources are the kits' (firmware/modules/kits, kit_module.h) */
#include "../mod_engine.h"
#define DRUM_ONLY 5u
#include "../kits/kit_module.h"

MOD3_IDENT("melodee", "909", "909");
MOD3_ENGINE_EXPORT(mod_909) = {
    MOD3_ENGINE_HEAD, .engine = ENGI_909, .name = "909", .npresets = KIT909_N, .eng = &ENG_KIT909,
};
