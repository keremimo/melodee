/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The Prophet's patch: the part's bytes are a Rev-4 program as p5_patch_t holds it (prophet_patch.c: 133 raw bytes,
 * then size, model, command, group, program; what Melodee 1.x stored), read by the engine where the firmware keeps it
 * (mod_patch). With the voices it renders (p5_cap: the engine's cap, the pages' hints) and the program's voice
 * settings becoming the part's (p5_track_accept: a new program, an edit of its unison or priority) */
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
static p5_patch_t *p5_patch_of(const track_t *t) { return (p5_patch_t *)(void *)mod_patch(t); }
static uint32_t p5_cap_of(const track_t *t, const uint8_t *p)
{
    return p[P5_UNISON] && t->p[P_VOICE]==V_UNISON ?
        (uint32_t)clamp(p[P5_UNISON_COUNT]?p[P5_UNISON_COUNT]:5,1,(int32_t)p5_poly()) : p5_poly();
}
static uint32_t p5_cap(const track_t *t) { return p5_cap_of(t, p5_patch_of(t)->raw); }
/* Native performance controls become the track's allocator settings at load or native edit. Afterwards Melodee's
 * VOICE/PRIO/DETUNE pages can override */
static void p5_track_accept_raw(track_t *t, const uint8_t *p)
{
    t->p[P_VOICE]=p[P5_UNISON]?V_UNISON:V_POLY;
    t->p[P_PRIO]=p[P5_RETRIGGER]<2u?1:0;
    t->p[P_DETUNE]=clamp(p[P5_UNISON_DETUNE],0,7)*127/7;
    t->p[P_ALLOC]=1; /* repeated keys reuse a voice, as the original does */
}
