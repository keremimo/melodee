/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* CZ-1 is a separate engine: native Casio tone data, never PHASE knob conversion.
 * Envelope state and note reset utilities are shared with the phase family (eng_phase.c). The tones are the
 * firmware's (cz_parts.h), Casio's factory presets too (cz_tones.c cz_preset_make). A synth module on the device:
 * firmware/modules/cz */
#include "cz_native.c"
static int cz_native_done(track_t *t, voice_t *v)
{
    cz_voice_t *c = cz_voice(t, v);
    const uint8_t *b = cz_patch_of((uint32_t)(t - trk) % NTRK)->raw;
    uint32_t ls = b[0] & 3u;
    uint32_t a = ls == 1u ? 1u : 0u, z = ls >= 2u ? 1u : a;
    return c->native_eg[a][2].stage > (b[CZ_ENV_END[a][2]] & 7u) &&
        c->native_eg[z][2].stage > (b[CZ_ENV_END[ls == 2u ? 0u : z][2]] & 7u);
}
static int32_t cz_batch(const track_t *t) { (void)t; return AP_CZ; }   /* (its voices on CPU1: voice.c engine_render) */
static uint32_t cz_state(const track_t *t) { (void)t; return sizeof(cz_part_t); }   /* (its part memory) */
static const engine_t ENG_CZ = {
    .name = "CZ-1", .page_title = {"CZ-1", "TONE"},
    .edit = {
        {"-", F_INT, 0, 7, 0, 0, 0}, {"-", F_INT, 0, 16, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"TONE", F_INT, CZ_NATIVE, CZ_NATIVE, CZ_NATIVE, 0, 0},
    },
#if !MOD_NATIVE_SIDE
    .preset_make = cz_preset_make,     /* (host builds; a synth module's presets are in its file) */
#endif
    .npresets = CZ_FACTORY_N + 1u,
    .ownenv = 1, .done = cz_native_done, .keep = 0x0fu,
    .note_on = cz_native_note_on, .render = cz_native_render, .block = cz_native_block,
    .knob = {P_E0, P_E1, P_E7, P_E7},
    .state = cz_state,
    .batch = cz_batch, .wclass = 2,
    .menv = phase_env_source,    /* (native DCA: the matrix's ENV source) */
};
