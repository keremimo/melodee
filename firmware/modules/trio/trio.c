/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* TRIO as a synth module (ABI 3 from the contract alone: mod_sdk.h; engine 6, its number before it was retired):
 * firmware/src/eng_trio.c's engine, the same arithmetic over the SDK's helpers (bit-identical to the built-in engine:
 * tests/module_test.c). Three oscillators with ring modulation and hard sync into a 12 dB/oct multimode filter;
 * eng_trio.c has the full description. Its five sounds are its file's (the PRST chunk: tools/mod_build.py). */
#include "mod_sdk.h"

enum { TRW_OFF, TRW_TRI, TRW_SAW, TRW_PLS, TRW_NOI, TRW_ST, TRW_PS };
#define TRM_RING 1u
#define TRM_SYNC 2u
#define TRM_G3 11000            /* osc level with three oscillators sounding */
#define TRM_G2 15000            /* two */
#define TRM_G1 24000            /* one */

typedef struct {
    uint8_t w[3];                /* TRW_* of osc 1..3 */
    uint8_t flags;               /* TRM_RING | TRM_SYNC */
    int16_t g[3];                /* Q15 levels (0: silent; osc 3 still runs for RING) */
} trm_set_t;

static const char *const N_TRM_WAVE[] = {"SAW3", "PLS3", "PPT", "SST", "TRI3", "S&T", "P&S", "P+N",
                                          "NOIS", "SYNC", "SYNCP", "SYNC3", "RING", "RING3", "R+S", "R+SP"};
static const trm_set_t TRM_SETS[16] = {
    {{TRW_SAW, TRW_SAW, TRW_SAW}, 0, {TRM_G3, TRM_G3, TRM_G3}},                 /* SAW3 */
    {{TRW_PLS, TRW_PLS, TRW_PLS}, 0, {TRM_G3, TRM_G3, TRM_G3}},                 /* PLS3 */
    {{TRW_PLS, TRW_PLS, TRW_TRI}, 0, {TRM_G3, TRM_G3, TRM_G3 + 3000}},          /* PPT */
    {{TRW_SAW, TRW_SAW, TRW_TRI}, 0, {TRM_G3, TRM_G3, TRM_G3 + 3000}},          /* SST */
    {{TRW_TRI, TRW_TRI, TRW_TRI}, 0, {TRM_G3 + 2000, TRM_G3 + 2000, TRM_G3 + 2000}},   /* TRI3 */
    {{TRW_ST, TRW_ST, TRW_TRI}, 0, {TRM_G3 + 3000, TRM_G3 + 3000, TRM_G3}},     /* S&T */
    {{TRW_PS, TRW_PS, TRW_TRI}, 0, {TRM_G3 + 3000, TRM_G3 + 3000, TRM_G3}},     /* P&S */
    {{TRW_PLS, TRW_OFF, TRW_NOI}, 0, {TRM_G2, 0, TRM_G2 - 4000}},                /* P+N */
    {{TRW_NOI, TRW_OFF, TRW_OFF}, 0, {TRM_G1, 0, 0}},                             /* NOIS */
    {{TRW_SAW, TRW_SAW, TRW_OFF}, TRM_SYNC, {TRM_G2 - 5000, TRM_G2 + 5000, 0}}, /* SYNC */
    {{TRW_PLS, TRW_PLS, TRW_OFF}, TRM_SYNC, {TRM_G2 - 5000, TRM_G2 + 5000, 0}}, /* SYNCP */
    {{TRW_SAW, TRW_SAW, TRW_TRI}, TRM_SYNC, {TRM_G3 - 3000, TRM_G3 + 4000, TRM_G3}},  /* SYNC3 */
    {{TRW_TRI, TRW_OFF, TRW_OFF}, TRM_RING, {TRM_G1, 0, 0}},                     /* RING */
    {{TRW_TRI, TRW_OFF, TRW_TRI}, TRM_RING, {TRM_G2 + 2000, 0, TRM_G2 - 3000}}, /* RING3 */
    {{TRW_TRI, TRW_SAW, TRW_OFF}, TRM_RING | TRM_SYNC, {TRM_G2 + 2000, TRM_G2, 0}},   /* R+S */
    {{TRW_TRI, TRW_PLS, TRW_OFF}, TRM_RING | TRM_SYNC, {TRM_G2 + 2000, TRM_G2, 0}},   /* R+SP */
};
static const char *const N_TRM_MODE[] = {"LP", "BP", "HP", "NOT"};
/* filter output = (in * a + bp * b + lp * c) >> 10; HP and notch take k * bp off (k: per block) */
static const int16_t TRM_MIX[4][3] = {{0, 0, 1024}, {0, 1400, 0}, {1024, 0, -1024}, {1024, 0, 0}};

/* polyBLEP of a step j (the value after it minus the one before, at level g Q15) that lies a
 * fraction x (Q15) of a sample before sample i: half the step is spread over the sample before
 * it (cr[0]) and sample i (cr[1]), with the usual quadratic residual; cr = the block's
 * corrections + i */
static inline void trm_blep(int32_t j, int32_t x, int32_t g, int32_t *cr)
{
    int32_t h = ((j >> 1) * g) >> 15, y = 32767 - x;
    cr[0] += (h * ((x * x) >> 15)) >> 15;
    cr[1] -= (h * ((y * y) >> 15)) >> 15;
}

/* how far (Q15 of one sample) a phase is past a step it crossed: past / inc */
static inline int32_t trm_frac(uint32_t past, uint32_t inc)
{
    uint32_t d = inc >> 15, x;
    if (!d)
        return 0;
    x = past / d;
    return x > 32767u ? 32767 : (int32_t)x;
}

typedef struct {
    uint32_t pw;                 /* pulse width as a phase */
    int32_t pdc, psm;            /* means of the naive pulse and of pulse AND saw (taken off) */
} trm_pw_t;

static inline int32_t trm_tri16(uint32_t ph)          /* the 16-bit triangle of a combined wave */
{
    return (int32_t)(((ph & 0x80000000u) ? ~ph : ph) >> 15) & 0xFFFF;
}
static inline int32_t trm_st(uint32_t ph) { return ((int32_t)(ph >> 16) & trm_tri16(ph)) - 16384; }
static inline int32_t trm_ps(uint32_t ph, const trm_pw_t *q)
{
    return (ph >= q->pw ? (int32_t)(ph >> 16) : 0) - q->psm;
}

/* naive value of a wave at phase ph (the step of a sync restart) */
static int32_t trm_naive(uint32_t w, uint32_t ph, const trm_pw_t *q)
{
    switch (w) {
    case TRW_TRI:
        return md_osc_tri(ph);
    case TRW_SAW:
        return (int32_t)(ph >> 16) - 32768;
    case TRW_PLS:
        return (ph < q->pw ? 32767 : -32768) - q->pdc;
    case TRW_ST:
        return trm_st(ph);
    case TRW_PS:
        return trm_ps(ph, q);
    default:
        return 0;
    }
}

/* one oscillator over the block: mix[i] += its sample at level g, its steps into cr; one loop per
 * wave, so the wave is not looked at per sample. wx (osc 1 of a SYNC set: saw or pulse): where it
 * starts a cycle, how far before sample i (Q15), else -1 */
static void trm_pass(uint32_t w, uint32_t *php, uint32_t inc, int32_t g, const trm_pw_t *q, int32_t *nz,
                      int32_t *nst, int32_t *mix, int32_t *cr, int16_t *wx, uint32_t n)
{
    uint32_t ph = *php, o, i, pw = q->pw;
    int32_t pdc = q->pdc, z = *nz;
    switch (w) {
    case TRW_TRI:
        for (i = 0; i < n; i++) {
            ph += inc;
            mix[i] += (md_osc_tri(ph) * g) >> 15;
        }
        break;
    case TRW_SAW:
        for (i = 0; i < n; i++) {
            o = ph;
            ph += inc;
            if (ph < o) {
                int32_t x = trm_frac(ph, inc);
                trm_blep(-65536, x, g, cr + i);
                if (wx)
                    wx[i] = (int16_t)x;
            }
            mix[i] += (((int32_t)(ph >> 16) - 32768) * g) >> 15;
        }
        break;
    case TRW_PLS:
        for (i = 0; i < n; i++) {
            o = ph;
            ph += inc;
            if (ph < o) {
                int32_t x = trm_frac(ph, inc);
                trm_blep(65535, x, g, cr + i);
                if (wx)
                    wx[i] = (int16_t)x;
            }
            if (ph - pw < o - pw)                       /* crossed pw */
                trm_blep(-65535, trm_frac(ph - pw, inc), g, cr + i);
            mix[i] += (((ph < pw ? 32767 : -32768) - pdc) * g) >> 15;
        }
        break;
    case TRW_NOI:
        for (i = 0; i < n; i++) {
            o = ph;
            ph += inc;
            if ((o ^ ph) & 0x08000000u)
                z = (int32_t)(md_noise32(nst) >> 16) - 32768;
            mix[i] += (z * g) >> 15;
        }
        break;
    case TRW_ST:
        for (i = 0; i < n; i++) {
            ph += inc;
            mix[i] += (trm_st(ph) * g) >> 15;
        }
        break;
    case TRW_PS:
        for (i = 0; i < n; i++) {
            ph += inc;
            mix[i] += (trm_ps(ph, q) * g) >> 15;
        }
        break;
    default:                                            /* silent: the phase still runs (RING) */
        ph += inc * n;
        break;
    }
    *php = ph;
    *nz = z;
}

/* osc 1 as a triangle whose sign osc 3's top phase bit flips (RING; osc 3 starts at p3); each flip
 * is a step of twice the triangle's value. wx as in trm_pass */
static void trm_ring(uint32_t *php, uint32_t inc, int32_t g, uint32_t p3, uint32_t inc3, int32_t *mix, int32_t *cr,
                      int16_t *wx, uint32_t n)
{
    uint32_t ph = *php, o, o3, i;
    for (i = 0; i < n; i++) {
        int32_t s, sg;
        o = ph;
        ph += inc;
        o3 = p3;
        p3 += inc3;
        if (wx && ph < o)
            wx[i] = (int16_t)trm_frac(ph, inc);
        s = md_osc_tri(ph);
        sg = (p3 & 0x80000000u) ? -g : g;
        if ((o3 ^ p3) & 0x80000000u)                    /* flipped: a step of 2 s */
            trm_blep(2 * s, trm_frac(p3 & 0x7FFFFFFFu, inc3), sg, cr + i);
        mix[i] += (s * sg) >> 15;
    }
    *php = ph;
}

/* osc 2, saw or pulse, restarted where osc 1 starts a cycle (wx, from trm_pass / trm_ring): the
 * restart lies x before sample i, so osc 2 is x of its own cycle in; the jump from where it was
 * to its start is one step */
static void trm_sync(uint32_t w, uint32_t *php, uint32_t inc, int32_t g, const trm_pw_t *q, const int16_t *wx,
                      int32_t *mix, int32_t *cr, uint32_t n)
{
    uint32_t ph = *php, o, i, pw = q->pw;
    int32_t pdc = q->pdc, pls = w == TRW_PLS;
    for (i = 0; i < n; i++) {
        o = ph;
        ph += inc;
        if (wx[i] >= 0) {
            uint32_t back = (uint32_t)wx[i] * (inc >> 15);
            trm_blep(trm_naive(w, 0, q) - trm_naive(w, ph - back, q), wx[i], g, cr + i);
            ph = o = back;                              /* no step of its own in this sample */
        }
        if (pls) {
            if (ph < o)
                trm_blep(65535, trm_frac(ph, inc), g, cr + i);
            if (ph - pw < o - pw)
                trm_blep(-65535, trm_frac(ph - pw, inc), g, cr + i);
            mix[i] += (((ph < pw ? 32767 : -32768) - pdc) * g) >> 15;
        } else {
            if (ph < o)
                trm_blep(-65536, trm_frac(ph, inc), g, cr + i);
            mix[i] += (((int32_t)(ph >> 16) - 32768) * g) >> 15;
        }
    }
    *php = ph;
}

/* osc 2 / 3 increment: the base pitch + semitones + cents, with the voice's fine factor (unison,
 * TUNE) that m->inc carries */
static uint32_t trm_inc(int32_t pitch16, int32_t semi, int32_t ct, int32_t fine)
{
    return md_cents_inc(pitch16 + semi * 16, ct, fine);
}

static void trm_note_on(mod_part_v3 *t, mod_voice_v3 *v)
{
    (void)t;
    v->ph[0] = 0;
    v->ph[1] = 0x1C000000u;      /* start phases a little apart (a third apart would cancel all but
                                  * every third harmonic of equal-pitched saws until DTN drifts them) */
    v->ph[2] = 0x38000000u;
    v->s[0] = v->s[1] = 0;       /* filter */
    if (!v->s[2])
        v->s[2] = 0x2468ACE + (int32_t)v->age;   /* noise state */
    v->s[3] = 0;                 /* held noise value */
    v->s[4] = 0;                 /* the sample waiting for its step corrections */
    v->s[5] = 0;                 /* cutoff drift */
}

static void trm_render(mod_part_v3 *t, mod_voice_v3 *v, int32_t *out, uint32_t n, const mod_vmod_v3 *m)
{
    const int16_t *p = md_e(t);
    const trm_set_t *ws = &TRM_SETS[(uint32_t)p[0] & 15u];
    uint32_t w1 = ws->w[0], w2 = ws->w[1], w3 = ws->w[2], i;
    uint32_t ring = ws->flags & TRM_RING, sync = ws->flags & TRM_SYNC;
    int32_t g1 = ws->g[0], g2 = ws->g[1], g3 = ws->g[2];
    uint32_t base = mh->pitch_inc(m->pitch16);
    int32_t fine = (base >> 12) ? (int32_t)(m->inc - base) / (int32_t)(base >> 12) : 0;
    uint32_t inc1 = m->inc;
    uint32_t inc2 = trm_inc(m->pitch16, p[1], p[3], fine);
    uint32_t inc3 = trm_inc(m->pitch16, p[2], -p[3], fine);
    int32_t duty = md_clamp((p[7] << 8) + m->shape - (64 << 8), 768, 32000);   /* Q15 */
    trm_pw_t q;
    uint32_t mode = (uint32_t)p[4] & 3u;
    int32_t reso = p[6], k = 8192 - reso * 7168 / 127; /* damping 2.0 .. 0.25 (Q12): no self-oscillation */
    int32_t ma = TRM_MIX[mode][0], mb = TRM_MIX[mode][1], mc = TRM_MIX[mode][2];
    int32_t a1, a2, a3, cut, cur;
    md_svf_t flt;
    int32_t mix[MOD_CTL], cr[MOD_CTL + 1];                      /* blocks are MOD_CTL long (dsp.c amp_at) */
    int16_t wx[MOD_CTL];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], ph3s = ph2;   /* state in locals: out[] may alias v->s[] */
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2], nz = v->s[3], prev = v->s[4], drift = v->s[5];
    if (n > MOD_CTL)
        n = MOD_CTL;
    q.pw = (uint32_t)duty << 17;
    q.pdc = 2 * duty - 32768;
    q.psm = 32768 - ((duty * duty) >> 15);
    if (mode >= 2u)
        mb = -(k >> 2);                                 /* HP / notch: in - k bp (- lp) */
    /* the cutoff wanders a little, once per block (the slightly unstable filter of the chips) */
    drift += (((int32_t)(md_noise32(&nst) >> 22) - 512) - drift) >> 3;
    cut = md_clamp((p[5] << 8) + m->cutoff + drift, 0, 127 << 8);
    md_svf_coef_k(&flt, cut, k);
    a1 = flt.a1;
    a2 = flt.a2;
    a3 = flt.a3;
    for (i = 0; i < n; i++) {
        mix[i] = cr[i] = 0;
        wx[i] = -1;
    }
    cr[n] = 0;
    trm_pass(w3, &ph2, inc3, g3, &q, &nz, &nst, mix, cr, 0, n);   /* first: RING reads its start phase */
    if (ring)
        trm_ring(&ph0, inc1, g1, ph3s, inc3, mix, cr, sync ? wx : 0, n);
    else
        trm_pass(w1, &ph0, inc1, g1, &q, &nz, &nst, mix, cr, sync ? wx : 0, n);
    if (sync)
        trm_sync(w2, &ph1, inc2, g2, &q, wx, mix, cr, n);
    else
        trm_pass(w2, &ph1, inc2, g2, &q, &nz, &nst, mix, cr, 0, n);
    cur = prev;
    for (i = 0; i < n; i++) {
        int32_t x = cur + cr[i], y, v1, v2, v3, a;     /* one sample late: its corrections are complete */
        cur = mix[i];
        x = md_soft_knee(x, 16000) >> 1;                   /* the input saturates above 16000 */
        /* SVF (dsp.c tsvf_lp, with the band-pass out); the band-pass state is compressed */
        v3 = x - ic2;
        v1 = (a1 * ic1 + a2 * v3) >> 13;
        v2 = ic2 + ((a2 * ic1 + a3 * v3) >> 13);
        ic1 = 2 * v1 - ic1;
        ic2 = md_clamp(2 * v2 - ic2, -150000, 150000);
        a = md_clamp(ic1, -49152, 49152);                  /* beyond +-49152 a quarter of the excess */
        ic1 = a + ((ic1 - a) >> 2);
        y = md_soft_knee((x * ma + v1 * mb + v2 * mc) >> 10, 16000);   /* linear up to half scale */
        out[i] += md_voice_amp(y << 1, m, i) << 1;
    }
    prev = cur + cr[n];
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
    v->s[3] = nz;
    v->s[4] = prev;
    v->s[5] = drift;
}

static const mod_engine_v3 TRIO = {
    .name = "TRIO",
    .page_title = {"OSC", "TONE"},
    .edit = {
        {"WAVE", MOD3_F_ENUM, 0, 15, 0, N_TRM_WAVE, 0},
        {"INT2", MOD3_F_SEMI, -24, 24, 0, 0, 0},
        {"INT3", MOD3_F_SEMI, -24, 24, -12, 0, 0},
        {"DTN", MOD3_F_INT, 0, 50, 6, 0, "ct"},
        {"MODE", MOD3_F_ENUM, 0, 3, 0, N_TRM_MODE, 0},
        {"CUT", MOD3_F_CUTOFF, 0, 127, 80, 0, 0},
        {"RES", MOD3_F_PCT, 0, 127, 40, 0, 0},
        {"PW", MOD3_F_PCT, 0, 127, 64, 0, 0},
    },
    .npresets = 5,
    .knob = {MOD3_P_E0 + 5, MOD3_P_E0 + 6, MOD3_P_E0 + 7, MOD3_P_E0 + 1},
    .note_on = trm_note_on,
    .render = trm_render,
};

MOD3_IDENT("melodee", "TRIO");
MOD3_EXPORT(mod_trio) = {
    MOD3_SDK_HEAD,
    .engine = 6,                                 /* TRIO's number before it was retired: old sounds play again */
    .name = "TRIO",
    .npresets = 5,                               /* (FAT BASS, ARP LEAD, SYNC LEAD, RING BELL, CHIP CHOIR) */
    .eng = &TRIO,
};
