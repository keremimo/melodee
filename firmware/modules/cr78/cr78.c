/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* CR78 as a synth module (ABI 3): the CR-78's networks on one voice (drum_lanes.c, drum_cr78.c). Its sources are the kits' (firmware/modules/kits, kit_module.h) */
#include "../mod_engine.h"
#define DRUM_ONLY 7u
#include "../kits/kit_module.h"

MOD3_IDENT("melodee", "cr78", "CR78");
MOD3_ENGINE_EXPORT(mod_cr78) = {
    MOD3_ENGINE_HEAD, .engine = ENGI_CR78, .name = "CR78", .npresets = KITCR_N, .eng = &ENG_KITCR,
};
