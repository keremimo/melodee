/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FM KIT as a synth module (ABI 3): 2-op FM drums on one voice (drum_lanes.c, drum_fm.c). Its sources are the kits' (firmware/modules/kits, kit_module.h) */
#include "../mod_engine.h"
#define DRUM_ONLY 8u
#include "../kits/kit_module.h"

MOD3_IDENT("melodee", "fmkit", "FMK");
MOD3_ENGINE_EXPORT(mod_fmkit) = {
    MOD3_ENGINE_HEAD, .engine = ENGI_FMKIT, .name = "FM KIT", .npresets = KITFM_N, .eng = &ENG_KITFM,
};
