/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 johnskiller (https://github.com/johnskiller)
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* WT: a wavetable engine (eng_wt.c).
 *
 *   E0 BANK    the wavetable bank (melodee_wt.h, tools/gen_wt_tables.py)
 *   E1 MORPH   the position in the bank's 8 frames
 *   E2 TRK     key tracking: higher notes read further up the bank
 *   E3 UNI     OFF / DUO / TRI / WIDE: 1..2 extra detuned reads of the table
 *   E4 CUT     the low pass (dsp.c's trapezoidal SVF, as the other engines')
 *   E5 RES     its resonance
 *   E6 DRIVE   a soft clip after the filter
 *   E7 ENV     ENV -> MORPH (as ED FLT routes ENV -> CUT)
 *
 * The oscillator reads its bank with a linearly interpolated frame and sample,
 * the phase accumulator v->ph[0] (v->ph[1..2]: the detuned reads, spaced a
 * third of a cycle at the free voice's start). Per-voice state only: engines.c
 * eng_state_size 0, the generic voice path (the track's ADSR shapes and ends
 * the voice). The phases continue over a retrigger (voice.c restores them: no
 * click); the SVF and the DC blocker do too (engine_t.keep), the filter from
 * rest only at a stolen / finished voice's note_on.
 */
#include "melodee_wt.h"

#define WT_MORPH_STEP ((7 << 16) / 127)              /* MORPH 0..127 -> frames 0..7 (Q16) */
#define WT_TOP (7 << 16)                             /* the last frame's position */

static const char *const N_WT_TRK[] = {"OFF", "LOW", "MID", "HIGH"};
static const char *const N_WT_UNI[] = {"OFF", "DUO", "TRI", "WIDE"};

static void wt_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!voice_was) {                                /* a free voice: phases a third of a cycle
                                                      * apart (in phase the detuned reads would
                                                      * stack; spread they thicken) */
        v->ph[0] = (uint32_t)(v - t->v) * 0x5A17C151u + 0x9E3779B9u;
        v->ph[1] = v->ph[0] + 0x2AAAAAAAu;
        v->ph[2] = v->ph[0] + 0x55555555u;
        v->s[4] = v->s[5] = v->s[6] = 0;             /* the SVF and the DC blocker from rest */
    }                                                /* (a retrigger keeps phases and filter) */
}

/* the bank at phase ph and morph position mf (Q16, 0..7<<16): the two frames
 * around it, each read with a linearly interpolated sample, mixed (the last
 * frame holds: no wrap into the first) */
static inline int32_t wt_sample(const int16_t *bank, uint32_t ph, int32_t mf)
{
    uint32_t f0 = (uint32_t)mf >> 16, fr = (mf >> 1) & 0x7fffu;
    uint32_t i = ph >> 24, t = (ph >> 9) & 0x7fffu, nx = (i + 1u) & (WT_FRAME_PTS - 1u);
    const int16_t *a = bank + f0 * WT_FRAME_PTS;
    const int16_t *b = a + (f0 + 1u < WT_FRAMES ? WT_FRAME_PTS : 0);
    int32_t sa = a[i] + (((a[nx] - a[i]) * (int32_t)t) >> 15);
    int32_t sb = b[i] + (((b[nx] - b[i]) * (int32_t)t) >> 15);
    return sa + (((sb - sa) * (int32_t)fr) >> 15);
}

static void wt_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const int16_t *bank = WT_BANKS[(uint32_t)p[P_E0] % WT_N];
    uint32_t uni = (uint32_t)p[P_E3] & 3u, trk = (uint32_t)p[P_E2] & 3u;
    uint32_t ph = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], i;
    uint32_t iu = 0, idn = 0;
    int32_t mf, ic1 = v->s[4], ic2 = v->s[5], dc = v->s[6];
    int32_t pre = 4096 + p[P_E6] * 48;               /* DRIVE: into the clip x1..~x2.5 (Q12) */
    tsvf_t fc;
    if (!n)
        return;
    if (uni) {                                       /* the detuned reads: +9 / -18 cents (DUO),
                                                      * +9 / -9 (TRI), +18 / -18 (WIDE) */
        iu = tuned_pitch_inc((uint32_t)clamp(m->pitch16 + (uni == 3u ? 288 : 144), 0, 2047));
        if (uni > 1u)
            idn = tuned_pitch_inc((uint32_t)clamp(m->pitch16 - (uni == 3u ? 288 : 144), 0, 2047));
    }
    /* the morph position: the knob, ENV (E7, as ED FLT routes ENV -> CUT), the
     * fixed FLT routing, key tracking; held inside the bank */
    mf = p[P_E1] * WT_MORPH_STEP + mulq15(m->envq15, p[P_E7] * 258) * 7 + (m->cutoff >> 4);
    if (trk)
        mf += (m->pitch16 - 60 * 16) * (160 * (int32_t)trk);   /* LOW / MID / HIGH: up the bank */
    mf = clamp(mf, 0, WT_TOP);
    tsvf_coef_k(&fc, clamp(p[P_E4] * 256 + (m->cutoff >> 3), 0, 127 << 8), 8192 - clamp(p[P_E5], 0, 127) * 7600 / 127);   /* RES 0..127: damping 2.0 .. ~0.15 (dsp.c's old tsvf_coef) */
    for (i = 0; i < n; i++) {
        int32_t x, y;
        ph += m->inc;
        x = wt_sample(bank, ph, mf);
        if (uni) {                                   /* (1/3 cycle apart at the free start) */
            int32_t xu, xd;
            ph1 += iu;
            xu = wt_sample(bank, ph1, mf);
            if (uni == 1u) {
                x = (x + xu) >> 1;
            } else {
                ph2 += idn;
                xd = wt_sample(bank, ph2, mf);
                x = (x + xu + xd) / 3;               /* TRI and WIDE: three reads */
            }
        }
        dc += x - (dc >> 10);                        /* the DC blocker (as CZ-1's line: a morphed
                                                      * frame pair can sit off centre) */
        y = (x - (dc >> 10)) >> 1;
        y = tsvf_lp(&fc, y, &ic1, &ic2);             /* the SVF (dsp.c), state through keep */
        if (pre > 4096)                              /* DRIVE: only when up (softclip's table read
                                                      * is the loop's dearest call) */
            y = softclip((y * pre) >> 12);
        out[i] += voice_amp(y, m, i) * 4;
    }
    v->ph[0] = ph;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[4] = ic1;
    v->s[5] = ic2;
    v->s[6] = dc;
}

static const preset_t WT_PRESETS[] = {
    {"INIT WT",     {0, 64, 0, 0, 127, 0, 0, 0},    {0, 36, 127, 36},  0, 0},
    {"SCAN BASS",   {0, 40, 2, 0, 45, 45, 30, 0},   {0, 63, 60, 36},   30, 1},
    {"GLASS BELL",  {1, 110, 0, 0, 100, 30, 0, 40}, {0, 69, 0, 90},    0, 0},
    {"FORMANT PAD", {3, 70, 0, 2, 80, 15, 0, 25},   {85, 90, 100, 97}, 0, 0},
    {"ACID LINE",   {2, 45, 3, 0, 30, 85, 60, 20},  {0, 57, 40, 0},    40, 1},
    {"WIDTH LEAD",  {4, 90, 0, 1, 110, 25, 20, 10}, {0, 69, 110, 57},  0, 1},
    {"SWEEP PLUCK", {0, 20, 2, 0, 60, 90, 0, 60},   {0, 57, 0, 49},    0, 0},
    {"ROUGH KEYS",  {3, 40, 1, 1, 70, 60, 40, 15},  {0, 36, 127, 36},  0, 0},
};

static const engine_t ENG_WT = {
    .name = "WT",
    .page_title = {"WT OSC", "WT TONE"},
    .edit = {
        {"BANK", F_ENUM, 0, WT_N - 1, 0, WT_BANK_NAMES, 0},
        {"MORPH", F_PCT, 0, 127, 64, 0, 0},
        {"TRK", F_ENUM, 0, 3, 0, N_WT_TRK, 0},
        {"UNI", F_ENUM, 0, 3, 0, N_WT_UNI, 0},
        {"CUT", F_PCT, 0, 127, 127, 0, 0},
        {"RES", F_INT, 0, 127, 0, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 0, 0, 0},
        {"ENV", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = WT_PRESETS,
    .npresets = NELEM(WT_PRESETS),
    .note_on = wt_note_on,
    .render = wt_render,
    .knob = {P_E1, P_E4, P_E5, P_E6},
    .poly = NPOLY,                                   /* the generic cap (voice.c voice_room) */
    .keep = 0x70u,                                   /* the SVF and the DC blocker over a retrigger */
};
