/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The parts' Prophet programs (Rev-4 wire patches, prophet_patch.c): the firmware's data, written by the stores, the
 * editor, the panel and the presets (prophet_programs.c), read by the Prophet engine (eng_prophet_test.c: a synth
 * module on the device, firmware/modules/prophet). With them the voices the engine renders (p5_cap: the engine's
 * cap, the panel's hints) */
#pragma once
#include "prophet_patch.c"
#define P5_FACTORY_N 200u                        /* Sequential's programs: the Prophet's presets after INIT (its
                                                  * module's file; melodee_prophet_factory.h on host builds) */
#if MELODEE_DUAL_CORE
#define P5_POLY 8u
#else
#define P5_POLY 5u
#endif
static uint32_t p5_poly(void)
{
#if MELODEE_DUAL_CORE
    if (audio_worker_online) return P5_POLY;
#endif
    return 5u;
}
#if MOD_SIDE                              /* (a synth module: the firmware's, module_legacy.h) */
static p5_patch_t *mod_imp_p5_patch;
static uint8_t *mod_imp_p5_ready;
#define p5_patch mod_imp_p5_patch
#define p5_ready mod_imp_p5_ready
#else
static p5_patch_t p5_patch[NPART] __attribute__((section(".pool")));
static uint8_t p5_ready[NPART];
#endif
static p5_patch_t *p5_patch_of(const track_t *t)
{
    uint32_t k = trk_ix(t) % NPART;
    if (!p5_ready[k]) { p5_patch_init(&p5_patch[k]); p5_ready[k] = 1; }
    return &p5_patch[k];
}
static uint32_t p5_cap(const track_t *t)
{
    const uint8_t *p=p5_patch_of(t)->raw;
    return p[P5_UNISON] && t->p[P_VOICE]==V_UNISON ?
        (uint32_t)clamp(p[P5_UNISON_COUNT]?p[P5_UNISON_COUNT]:5,1,(int32_t)p5_poly()) : p5_poly();
}
