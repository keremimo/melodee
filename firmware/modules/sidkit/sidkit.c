/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SID KIT as a synth module (ABI 3): one Commodore 6581 for the kit, C64 frame programs, DIGI on its volume register (drum_sid.c). Its sources are the kits' (firmware/modules/kits, kit_module.h) */
#include "../mod_engine.h"
#define DRUM_ONLY 6u
#include "../kits/kit_module.h"

MOD3_IDENT("melodee", "sidkit", "SIDK");
MOD3_ENGINE_EXPORT(mod_sidkit) = {
    MOD3_ENGINE_HEAD, .engine = ENGI_SIDKIT, .name = "SID KIT", .npresets = KITSID_N, .eng = &ENG_KITSID,
};
