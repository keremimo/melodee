/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The SDK's example: two sine oscillators a fifth or an octave apart, detuned, through the firmware's envelope (its
 * amplitude ramp, mod_vmod_v3 amp0..amp1). Everything a module needs is here: the engine (its pages' eight values and
 * its callbacks), who it is (MOD3_IDENT), its presets (MOD3_PRESETS) and its descriptor (MOD3_EXPORT).
 *     python3 mod_build.py example/drone.c -o drone.mmod */
#include "mod_sdk.h"

static const char *const N_DRONE_IV[] = {"UNIS", "5TH", "OCT"};
static const int32_t DRONE_IV[] = {0, 7 * 16, 12 * 16};   /* the second oscillator, in pitch16 steps (1/16 semitone) */

static void drone_note_on(mod_part_v3 *t, mod_voice_v3 *v)
{
    (void)t;
    v->ph[0] = 0;
    v->ph[1] = 0x40000000u;                       /* (a quarter apart: no cancellation at UNIS) */
}
static void drone_render(mod_part_v3 *t, mod_voice_v3 *v, int32_t *out, uint32_t n, const mod_vmod_v3 *m)
{
    const int16_t *e = md_e(t);
    uint32_t inc0 = md_cents_inc(m->pitch16, 0, 0),
             inc1 = md_cents_inc(m->pitch16 + DRONE_IV[md_clamp(e[0], 0, 2)], md_clamp(e[1], 0, 50), 0),
             ph0 = v->ph[0], ph1 = v->ph[1];
    int32_t mix = md_clamp(e[2], 0, 127);         /* the second oscillator's level */
    for (uint32_t i = 0; i < n; i++, ph0 += inc0, ph1 += inc1) {
        int32_t s = (md_sine(ph0) * (127 - mix) + md_sine(ph1) * mix) >> 7;
        out[i] += md_voice_amp(s, m, i);
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
}

static const mod_engine_v3 DRONE = {
    .name = "DRONE",
    .page_title = {"DRONE", "-"},
    .edit = {
        {"IVAL", MOD3_F_ENUM, 0, 2, 1, N_DRONE_IV, 0},
        {"DTUN", MOD3_F_INT, 0, 50, 6, 0, "ct"},
        {"MIX", MOD3_F_PCT, 0, 127, 64, 0, 0},
        {"-", MOD3_F_INT, 0, 0, 0, 0, 0}, {"-", MOD3_F_INT, 0, 0, 0, 0, 0}, {"-", MOD3_F_INT, 0, 0, 0, 0, 0},
        {"-", MOD3_F_INT, 0, 0, 0, 0, 0}, {"-", MOD3_F_INT, 0, 0, 0, 0, 0},
    },
    .npresets = 2,
    .knob = {MOD3_P_E0, MOD3_P_E0 + 1, MOD3_P_E0 + 2, MOD3_P_REL},
    .note_on = drone_note_on,
    .render = drone_render,
};

MOD3_IDENT("example", "DRON");                    /* its UID: CRC-32 of "example/DRONE" */
MOD3_PRESETS = {                                  /* name, category (8: OTHER), E1..E8, ATK DEC SUS REL, FENV, MONO */
    {"FIFTHS", 8, {1, 6, 64, 0, 0, 0, 0, 0}, {40, 64, 127, 70}, 0, 0},
    {"OCTAVE PAD", 8, {2, 12, 90, 0, 0, 0, 0, 0}, {90, 64, 110, 90}, 0, 0},
};
MOD3_EXPORT(mod_drone) = {
    MOD3_SDK_HEAD,
    .engine = 22,                                 /* the number it asks for (a device gives the first free one if taken) */
    .name = "DRONE",
    .npresets = 2,
    .eng = &DRONE,
};
