/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SAMPLER as a synth module (ABI 3): the user's recorded kits, KIT 1..8 its data records, played on one voice (drum_real.c); Record kit's takes (kit_take.c, drum_enc.c). Its sources are the kits' (firmware/modules/kits, kit_module.h) */
#include "../mod_engine.h"
#define DRUM_ONLY 9u
#include "../kits/kit_module.h"

MOD3_IDENT("melodee", "sampler", "SMPL");
MOD3_ENGINE_EXPORT(mod_sampler) = {
    MOD3_ENGINE_HEAD, .engine = ENGI_SAMPLER, .name = "SAMPLER", .npresets = SAMPLER_N, .eng = &ENG_SAMPLER,
};
