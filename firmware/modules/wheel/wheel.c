/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* WHEEL as a synth module (ABI 3 from the contract alone: mod_sdk.h; engine 7, its number before it was retired):
 * firmware/src/eng_wheel.c's tonewheel organ, the same arithmetic over the SDK's helpers (bit-identical:
 * tests/module_test.c). Nine drawbar partials per voice, percussion, key click, drive and a rotary speaker; its rotors
 * and bar gains run once per block (.block) in the part's memory (.state). eng_wheel.c has the full description. Its
 * five sounds are its file's (the PRST chunk: tools/mod_build.py). */
#include "mod_sdk.h"

#define WDR_NP 9                 /* partials */
#define WDR_FOLD 584366000u      /* ~6 kHz as a phase increment: partials above fold down an octave */
#define WDR_XOVER 77915000u      /* ~800 Hz: below the low rotor, above the high rotor */

/* drawbar level 0..8 -> Q15, 3 dB a step */
static const uint16_t WDR_LV[9] = {0, 2920, 4125, 5827, 8231, 11627, 16423, 23198, 32767};

/* the 16 registrations, one digit per bar: 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1' */
static const char *const N_WDR_REG[] = {"FLUTE", "MELLO", "HOLLW", "SMOOT", "3BAR", "BLUES", "GOSPL", "ROCK",
                                        "TOPS", "CLARI", "REED", "STRNG", "CHAPL", "BRITE", "BASS", "FULL"};
static const char WDR_REG[16][WDR_NP + 1] = {
    "008000000", /* FLUTE  one soft flute */
    "008400000", /* MELLO  flute and octave */
    "800800000", /* HOLLW  sub and octave, hollow */
    "838000000", /* SMOOT  smooth comping */
    "888000000", /* 3BAR   the first three bars out */
    "888800000", /* BLUES  three bars and the octave */
    "888600006", /* GOSPL  body with a little shimmer */
    "888000888", /* ROCK   body and the top three */
    "800000888", /* TOPS   sub and the top only */
    "008080800", /* CLARI  odd harmonics, reedy-hollow */
    "008765432", /* REED   a falling slope, brassy */
    "006876540", /* STRNG  bright and thin */
    "004432000", /* CHAPL  soft, churchy */
    "008888000", /* BRITE  even bars, bright */
    "858000000", /* BASS   for the left hand */
    "888888888", /* FULL   everything */
};
static const char *const N_WDR_PERC[] = {"OFF", "2ND", "3RD", "2SOFT", "3SOFT", "2SLOW", "3SLOW"};
static const char *const N_WDR_ROTR[] = {"OFF", "SLOW", "FAST"};

/* partial frequency / note frequency, as a shift-and-add of the note's increment; FOLD_LIM:
 * the note increment above which the partial would pass WDR_FOLD */
static const uint32_t WDR_FOLD_LIM[WDR_NP] = {
    WDR_FOLD * 2u, WDR_FOLD / 3u * 2u, WDR_FOLD, WDR_FOLD / 2u, WDR_FOLD / 3u, WDR_FOLD / 4u, WDR_FOLD / 5u,
    WDR_FOLD / 6u, WDR_FOLD / 8u,
};
static inline uint32_t wdr_ratio(uint32_t k, uint32_t x)
{
    switch (k) {
    case 0: return x >> 1;
    case 1: return x + (x >> 1);
    case 2: return x;
    case 3: return x << 1;
    case 4: return x * 3u;
    case 5: return x << 2;
    case 6: return x * 5u;
    case 7: return x * 6u;
    default: return x << 3;
    }
}

/* rotor speeds as phase increments per block (MOD_CTL samples): Hz * MOD_CTL / FS * 2^32 */
#define WDR_HZ(h) ((int32_t)((h) * 3116457.0))
static const int32_t WDR_ROT_SPD[2][3] = {
    {0, WDR_HZ(0.8), WDR_HZ(6.7)},   /* high rotor: OFF (stops), SLOW, FAST */
    {0, WDR_HZ(0.7), WDR_HZ(5.8)},   /* low rotor */
};

typedef struct {                 /* per part, once per block (whl_block) */
    int32_t g[WDR_NP];           /* bar gains, Q15, normalised */
    int32_t pk, plev;            /* percussion: partial (WDR_NP = off), level Q15 */
    uint32_t pdec;               /* its decay per block, Q16 */
    int32_t clk;                 /* click level Q15 */
    int32_t dg, dcomp;           /* drive gain, Q8; output compensation, Q8 (0 = no drive) */
    uint32_t rph[2];             /* rotor phases (0 high, 1 low) */
    int32_t rsp[2];              /* rotor speeds (phase increment per block) */
    int32_t rwet;                /* rotary amount, Q15 (ramps on / off) */
    int32_t am[2];               /* this block's amplitude, Q15, per rotor */
    int32_t fm[2];               /* this block's pitch offset, Q16 of the increment, per rotor */
} wdr_trk_t;

typedef struct {                 /* per voice */
    uint32_t ph[WDR_NP];
    int32_t gp[WDR_NP];          /* each partial's gain at the end of the last block (the ramp start) */
    int32_t perc;                /* percussion envelope, Q15 */
    int32_t clk;                 /* click envelope, Q15 */
    int32_t nz, hp;              /* click noise state, its high-pass state */
    uint8_t gate, init;
} wdr_vc_t;

#define WHL_POLY 8u
typedef struct { wdr_trk_t t; wdr_vc_t v[WHL_POLY]; } wdr_part_t;   /* the part's memory (.state) */
#define wdr_of(t) ((wdr_part_t *)md_part(t, sizeof(wdr_part_t)))


/* the bar level 0..8 of partial k: the registration plus SUB / BODY / TOP */
static int32_t wdr_level(const int16_t *p, uint32_t k)
{
    int32_t d = WDR_REG[(uint32_t)p[0] & 15u][k] - '0';
    d += k < 2u ? p[1] : k < 4u ? p[2] : p[3];
    return md_clamp(d, 0, 8);
}

/* once per block, before the voices: bar gains, percussion, click, drive, the rotors */
static void whl_block(mod_part_v3 *t)
{
    wdr_part_t *P = wdr_of(t);
    if (!P)
        return;
    wdr_trk_t *T = &P->t;
    const int16_t *p = md_e(t);
    uint32_t k, perc = (uint32_t)p[4] % 7u, rot = (uint32_t)p[7] % 3u;
    int32_t s = 0, lv[WDR_NP];
    uint32_t norm;
    for (k = 0; k < WDR_NP; k++) {
        lv[k] = perc && k == WDR_NP - 1u ? 0 : WDR_LV[wdr_level(p, k)];   /* PERC on: the 1' bar is off */
        s += lv[k];
    }
    /* sum of the gains = 2.4 s / (s + 1.5) (s in units of a full bar): one bar 0.96, three 1.6,
     * all nine 2.06 */
    norm = (uint32_t)(2u * 39322u * 32768u) / (uint32_t)(s + 49152);
    if (perc == 1u || perc == 2u || perc == 5u || perc == 6u)
        norm = (norm * 23170u) >> 15;                   /* normal percussion: the bars -3 dB */
    for (k = 0; k < WDR_NP; k++)
        T->g[k] = (int32_t)(((uint32_t)lv[k] * norm) >> 15);
    T->pk = perc ? (int32_t)(3u + ((perc - 1u) & 1u)) : WDR_NP;   /* 2ND: 4', 3RD: 2 2/3' */
    T->plev = perc == 3u || perc == 4u ? 12000 : 24000;
    T->pdec = perc >= 5u ? 65457u : 65219u;             /* tau 0.6 s / 0.15 s */
    T->clk = p[5] * 180;
    if (p[6]) {                                      /* y = 6 / (2 + g) tanh(g x / 2) */
        T->dg = 256 + p[6] * 6;                      /* 1 .. 4 */
        T->dcomp = (6 * 256 * 256) / (512 + T->dg);
    } else {
        T->dg = 0;
    }
    /* rotors: each speed eases to its target (high rotor tau ~0.37 s, low ~1.5 s) */
    for (k = 0; k < 2u; k++) {
        int32_t sp, c, sn, dep;
        T->rsp[k] += (WDR_ROT_SPD[k][rot] - T->rsp[k]) >> (k ? 11 : 9);
        sp = T->rsp[k];
        T->rph[k] += (uint32_t)sp;
        sn = md_sine(T->rph[k]);
        c = md_sine(T->rph[k] + 0x40000000u);
        dep = k ? 7000 : 13000;                         /* AM depth: low rotor 21 %, high 40 % */
        T->am[k] = 32767 - md_mulq15(dep, (32767 - c) >> 1);
        T->fm[k] = (sn * (sp >> (k ? 17 : 16))) >> 15;  /* Doppler: +-0.5 % fast (high), +-0.2 % (low) */
    }
    T->rwet += ((rot ? 32767 : 0) - T->rwet) >> 5;      /* ~25 ms in or out */
    for (k = 0; k < 2u; k++) {
        T->am[k] = 32767 - md_mulq15(T->rwet, 32767 - T->am[k]);
        T->fm[k] = md_mulq15(T->fm[k], T->rwet);
    }
}

static void whl_note_on(mod_part_v3 *t, mod_voice_v3 *v)
{
    wdr_part_t *P = wdr_of(t);
    uint32_t vi = v->index % WHL_POLY, k, held = 0;
    if (!P)
        return;
    for (k = 0; k < MOD3_NVOICE; k++) {                /* single trigger: another key of the part held? */
        const mod_voice_v3 *w = mod3_voice(t, k);
        held |= k != v->index && w->active && w->gate && w->stage != 4u;
    }
    wdr_vc_t *V = &P->v[vi];
    if (!V->init) {                                     /* the wheels: spread start phases, then free running */
        for (k = 0; k < WDR_NP; k++)
            V->ph[k] = (k * 0x9E3779B9u) ^ (vi * 0x7F4A7C15u);
        V->nz = 0x13579BDF + (int32_t)vi;
        V->init = 1;
    }
    V->perc = held ? 0 : 32767;
    V->clk = 32767;
    V->hp = 0;
    V->gate = 1;
}

static void whl_render(mod_part_v3 *t, mod_voice_v3 *v, int32_t *out, uint32_t n, const mod_vmod_v3 *m)
{
    wdr_part_t *P = wdr_of(t);
    if (!P)
        return;
    const wdr_trk_t *T = &P->t;
    wdr_vc_t *V = &P->v[v->index % WHL_POLY];
    int32_t acc[MOD_CTL], perc1, clk0, clk1;
    uint32_t i, k;
    if (n > MOD_CTL)
        n = MOD_CTL;
    for (i = 0; i < n; i++)
        acc[i] = 0;
    perc1 = (int32_t)(((uint32_t)V->perc * T->pdec) >> 16);
    for (k = 0; k < WDR_NP; k++) {
        uint32_t x = m->inc, inc, ph = V->ph[k];
        int32_t g1 = T->g[k], g0 = V->gp[k], r;
        if ((int32_t)k == T->pk)
            g1 += md_mulq15(T->plev, perc1);
        while (x > WDR_FOLD_LIM[k])                     /* fold back above ~6 kHz */
            x >>= 1;
        inc = wdr_ratio(k, x);
        r = inc < WDR_XOVER;                            /* 1: the low rotor */
        g1 = md_mulq15(g1, T->am[r]);
        inc += (uint32_t)((int32_t)(inc >> 16) * T->fm[r]);
        V->gp[k] = g1;
        if (!g0 && !g1) {
            V->ph[k] = ph + inc * n;
            continue;
        }
        if (g0 == g1) {
            for (i = 0; i < n; i++) {
                ph += inc;
                acc[i] += (md_sine_tab[ph >> 22] * g1) >> 15;
            }
        } else {                                        /* gain ramp over the block (MOD_CTL = 32) */
            int32_t gq = g0 << 5, dg = g1 - g0;
            for (i = 0; i < n; i++) {
                ph += inc;
                gq += dg;
                acc[i] += (md_sine_tab[ph >> 22] * (gq >> 5)) >> 15;
            }
        }
        V->ph[k] = ph;
    }
    V->perc = perc1;
    /* key click: high-passed noise, at the key contact and (half) at the release */
    if (V->gate && !v->gate) {
        V->gate = 0;
        V->clk = 16384;
    }
    clk0 = md_mulq15(V->clk, T->clk);
    if (clk0 > 16) {
        int32_t nst = V->nz, hp = V->hp, dc;
        V->clk = (V->clk * 3) >> 2;                     /* tau ~2.5 ms */
        clk1 = md_mulq15(V->clk, T->clk);
        dc = clk1 - clk0;
        clk0 <<= 5;
        for (i = 0; i < n; i++) {
            int32_t z = (int32_t)(md_noise32(&nst) >> 17) - 16384, y = z - hp;
            hp += y >> 3;
            clk0 += dc;
            acc[i] += (y * (clk0 >> 5)) >> 15;
        }
        V->nz = nst;
        V->hp = hp;
    }
    if (T->dg) {                                        /* overdrive */
        int32_t dg = T->dg, dcomp = T->dcomp;
        for (i = 0; i < n; i++)
            acc[i] = (md_softclip((acc[i] * dg) >> 9) * dcomp) >> 8;
    }
    for (i = 0; i < n; i++)                             /* acc: Q15 with headroom to 4.0 */
        out[i] += md_voice_amp(acc[i] >> 1, m, i) << 1;
}

static uint32_t whl_state(const mod_part_v3 *t) { (void)t; return sizeof(wdr_part_t); }

static const mod_engine_v3 WHEEL = {
    .name = "WHEEL",
    .page_title = {"BARS", "TONE"},
    .edit = {
        {"REG", MOD3_F_ENUM, 0, 15, 4, N_WDR_REG, 0},
        {"SUB", MOD3_F_INT, -8, 8, 0, 0, 0},
        {"BODY", MOD3_F_INT, -8, 8, 0, 0, 0},
        {"TOP", MOD3_F_INT, -8, 8, 0, 0, 0},
        {"PERC", MOD3_F_ENUM, 0, 6, 0, N_WDR_PERC, 0},
        {"CLICK", MOD3_F_PCT, 0, 127, 40, 0, 0},
        {"DRV", MOD3_F_PCT, 0, 127, 0, 0, 0},
        {"ROTR", MOD3_F_ENUM, 0, 2, 1, N_WDR_ROTR, 0},
    },
    .npresets = 5,
    .knob = {MOD3_P_E0, MOD3_P_E0 + 4, MOD3_P_E0 + 6, MOD3_P_E0 + 7},
    .note_on = whl_note_on,
    .render = whl_render,
    .block = whl_block,
    .state = whl_state,
};

MOD3_IDENT("melodee", "WHEL");
MOD3_PRESETS = {
    {"FULL ORGAN", 8, {15, 0, 0, 0, 0, 30, 20, 1}, {0, 64, 127, 45}, 0, 0},
    {"JAZZ PERC", 8, {4, 0, 0, 0, 2, 50, 8, 1}, {0, 64, 127, 40}, 0, 0},
    {"GOSPEL", 8, {6, 0, 0, 0, 1, 60, 40, 2}, {0, 64, 127, 45}, 0, 0},
    {"SOFT FLUTE", 8, {1, 0, -2, 0, 0, 10, 0, 1}, {0, 64, 127, 55}, 0, 0},
    {"ROCK DRIVE", 8, {7, 0, 0, 0, 0, 70, 100, 2}, {0, 64, 127, 40}, 0, 0},
};
MOD3_EXPORT(mod_wheel) = {
    MOD3_SDK_HEAD,
    .engine = 7,                                 /* WHEEL's number before it was retired */
    .name = "WHEEL",
    .npresets = 5,                               /* (FULL ORGAN, JAZZ PERC, GOSPEL, SOFT FLUTE, ROCK DRIVE) */
    .eng = &WHEEL,
};
