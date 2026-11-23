/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The device's drum encoder (kit_take.c), for drum_real.c's decoder: tools/drum_codec.c's rule, searched the fast
 * way. Each slice takes the fewest bits whose error stays snr dB under the slice's own level (10 dB more in the
 * first 4 ms), at the scale of least error for that depth. Where the tool tries every depth from 1 bit up, this
 * starts at the last slice's depth and steps down while met, up while not; each sample's nearest level is taken
 * directly (not by trying every level) and the sums are fp32 (the FPU): 3.5 to 8 times faster on the host, within
 * 1 dB of the tool's quality at the same size.
 * x needs 64 zero samples after its n. out 0: count only. In place is safe: slice k's 8 bytes land at byte 8 k,
 * behind the 28 k bytes it has read. */
#include "x0x/fastmath.h"
#if MOD_SIDE
#define DKE_WDT() mod_host->wdt()                    /* (a long encode: the host's watchdog) */
#elif defined(DRK_CODEC_ONLY)
#define DKE_WDT() fm1_wdt_feed()                     /* (web/kit's) */
#else
#define DKE_WDT() mod_h4_wdt()                       /* (a host build's built-in engine: the firmware's) */
#endif
static __attribute__((noinline)) float dke_exp2(float x) { return fm_exp2f(x); }   /* (drum_lanes.c dlk_exp2's) */
typedef struct { int32_t h[4], w[4]; } dke_lms_t;
typedef struct {
    uint32_t bytes, loop_at, loop_slice;              /* written; the sample the loop starts at, its slice */
    int32_t loop[8];                                  /* the predictor there (h, w) */
} dke_out_t;
static inline int32_t dke_pred(const dke_lms_t *l)
{
    uint32_t p = 0;
    for (uint32_t i = 0; i < 4u; i++) p += (uint32_t)l->h[i] * (uint32_t)l->w[i];
    return (int32_t)p >> 13;
}
static inline int32_t dke_clip(int32_t v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }
/* the nearest level's index for a residual of magnitude a (Q2: 4 |t|) at scale sc, depth m: levels come in +- pairs */
static inline uint32_t dke_level(uint32_t m, uint32_t a, uint32_t sc)
{
    uint32_t j;
    switch (m) {
    case 0: return 0;
    case 1: return 2u * a > 13u * sc;                            /* 3 | 10 */
    case 2: j = 2u * a > 13u * sc; j += a > 14u * sc; j += 2u * a > 46u * sc; return j;   /* 3 | 10 | 18 | 28 */
    default:                                                     /* 2, 6 .. 30: a / (4 sc), 7 at most, */
        sc *= 4u;                                                /* by halves (no divide on the device) */
        if (a >= 8u * sc) return 7u;
        j = 0;
        if (a >= 4u * sc) { j = 4u; a -= 4u * sc; }
        if (a >= 2u * sc) { j += 2u; a -= 2u * sc; }
        return j + (a >= sc);
    }
}
/* one slice at depth m, scale sf: its error (with the tool's penalty for a predictor running away), the pure one;
 * code 0: not wanted. Past limit it stops (the error so far: already worse than the best) */
static float dke_slice_lim(const dke_lms_t *st, const int16_t *x, uint32_t m, uint32_t sf, dke_lms_t *out, float *pure,
                           uint64_t *code, float limit)
{
    dke_lms_t l = *st;
    float e = 0.0f, ep = 0.0f;
    uint32_t n = DRK_NS[m], b = m + 1u, sc = DRK_SF[sf];
    for (uint32_t i = 0; i < n; i++) {
        int32_t p = dke_pred(&l), t = x[i] - p;
        uint32_t bk = 2u * dke_level(m, (uint32_t)(t < 0 ? -t : t) * 4u, sc) + (t < 0);
        int32_t r = (DRK_LV[m][bk] * (int32_t)sc) >> 2, s = dke_clip(p + r);
        float er = (float)(x[i] - s), wp = 0.0f;
        ep += er * er;
        /* the penalty needs w.w past 805,306,367: four weights within +-14186 never reach it (4 x 14186^2 is below) */
        if ((uint32_t)(l.w[0] + 14186) > 28372u || (uint32_t)(l.w[1] + 14186) > 28372u ||
            (uint32_t)(l.w[2] + 14186) > 28372u || (uint32_t)(l.w[3] + 14186) > 28372u) {
            for (uint32_t k = 0; k < 4u; k++) wp += (float)l.w[k] * (float)l.w[k];
            e += wp > 805306367.0f ? wp * (1.0f / 262144.0f) - 3071.0f : 0.0f;
        }
        e += er * er;
        if (e > limit) return e;
        if (code) *code = (*code << b) | bk;
        int32_t d = r >> 4;
        for (uint32_t k = 0; k < 4u; k++) l.w[k] += l.h[k] < 0 ? -d : d;
        l.h[0] = l.h[1]; l.h[1] = l.h[2]; l.h[2] = l.h[3]; l.h[3] = s;
    }
    *out = l; *pure = ep;
    return e;
}
static float dke_slice(const dke_lms_t *st, const int16_t *x, uint32_t m, uint32_t sf, dke_lms_t *out, float *pure,
                       uint64_t *code)
{
    return dke_slice_lim(st, x, m, sf, out, pure, code, 3.4e38f);
}
/* depth m's best scale: five around the one the slice's open-loop residual suggests (its mean magnitude, against
 * DRK_SF's geometric midpoints, a bias per depth). Every one of the 16 cost 4.3 times the trials for +0.1 dB on the
 * tool's material and system sounds (a walk from the last slice's scale lost up to 12 dB on hats and snares) */
static const float DKE_MID[15] = {1.414f, 2.449f, 4.243f, 8.124f, 14.83f, 26.83f, 48.74f, 88.99f, 161.7f, 294.5f,
                                  536.8f, 977.2f, 1779.2f, 3238.5f, 5898.0f};
static const float DKE_BIAS[4] = {2.2974f, 1.1487f, 0.4353f, 0.3536f};   /* 2^(1.2, 0.2, -1.2, -1.5) */
static uint32_t dke_scale(const dke_lms_t *st, const int16_t *x, uint32_t m, float *pure)
{
    dke_lms_t t;
    float best = 3.4e38f, pe, sa = 0.0f;
    uint32_t b = 0, n = DRK_NS[m], e = 0;
    int32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3];
    for (uint32_t i = 0; i < n; i++) {               /* the slice against its start's weights, the samples as they are */
        uint32_t p = (uint32_t)h0 * (uint32_t)st->w[0] + (uint32_t)h1 * (uint32_t)st->w[1] +
                     (uint32_t)h2 * (uint32_t)st->w[2] + (uint32_t)h3 * (uint32_t)st->w[3];
        int32_t r = x[i] - ((int32_t)p >> 13);
        sa += (float)(r < 0 ? -r : r);
        h0 = h1; h1 = h2; h2 = h3; h3 = x[i];
    }
    sa = sa / (float)n * DKE_BIAS[m];
    while (e < 15u && DKE_MID[e] < sa) e++;
    /* the estimate first, then its neighbours, each stopped once it is worse than the best so far: the scale of least
     * error, the lowest on a tie, as trying all five whole */
    for (uint32_t i = 0; i < 5u; i++) {
        uint32_t s = i ? (i & 1u ? e - (i + 1u) / 2u : e + i / 2u) : e;
        if (s >= 16u) continue;                      /* (below 0 wraps: past 15 too) */
        float er = dke_slice_lim(st, x, m, s, &t, &pe, 0, best);
        if (er < best || (er == best && s < b)) { best = er; *pure = pe; b = s; }
    }
    return b;
}
/* A stream of slices (Record kit's take as it comes, kit_take_core.c): dke_begin, then dke_step with the samples so
 * far; slices are taken while 58 samples (the longest slice) follow, at the end all of them (64 zeros after the last
 * sample). snr dB, a loop from the first slice starting at or after loop_at (0: none) */
typedef struct {
    dke_lms_t st;
    float g, g4;
    uint32_t pm, pos, loop_at;                        /* the last slice's depth (where the search starts); samples */
    dke_out_t o;
} dke_t;
static void dke_begin(dke_t *e, int32_t snr, uint32_t loop_at)
{
    memset(e, 0, sizeof *e);
    e->st.w[2] = -(1 << 13); e->st.w[3] = 1 << 14;
    e->g = dke_exp2(-0.33219281f * (float)snr);
    e->g4 = dke_exp2(-0.33219281f * (float)(snr + 10));
    e->pm = 3u;
    e->loop_at = loop_at;
}
/* slices from x (n samples, the stream's next; end: the last of them, 64 zeros after) into out (32-bit words, two a
 * slice, the high one first; 0: count only), max slices at most: the samples it took (the caller keeps the rest) */
static uint32_t dke_step(dke_t *e, const int16_t *x, uint32_t n, int end, uint32_t *out, uint32_t max)
{
    uint32_t at = 0, k = 0;
    while (at < n && (end || at + 58u <= n) && k < max) {
        if (e->loop_at && !e->o.loop_at && e->pos >= e->loop_at) {
            e->o.loop_at = e->pos; e->o.loop_slice = e->o.bytes / 8u;
            for (uint32_t i = 0; i < 4u; i++) { e->o.loop[i] = e->st.h[i]; e->o.loop[4u + i] = e->st.w[i]; }
        }
        float tg[4], pe;
        uint32_t sf[4], ok[4] = {0, 0, 0, 0}, done[4] = {0, 0, 0, 0}, m = e->pm;
        for (uint32_t mm = 0; mm < 4u; mm++) {        /* each depth's target: the slice's level less snr dB */
            float en = 0.0f;
            for (uint32_t i = 0; i < DRK_NS[mm]; i++) en += (float)x[at + i] * (float)x[at + i];
            tg[mm] = en * (e->pos < 176u ? e->g4 : e->g);
            if (tg[mm] < (float)DRK_NS[mm] * 2.25f) tg[mm] = (float)DRK_NS[mm] * 2.25f;
        }
        for (;;) {                                    /* the fewest bits that meet it: down while met, up while not */
            if (!done[m]) { sf[m] = dke_scale(&e->st, x + at, m, &pe); ok[m] = pe <= tg[m]; done[m] = 1; }
            if (ok[m] ? m == 0u || (done[m - 1u] && !ok[m - 1u]) : m == 3u || (done[m + 1u] && ok[m + 1u])) {
                if (!ok[m] && m < 3u) m++;
                break;
            }
            m = ok[m] ? m - 1u : m + 1u;
        }
        uint32_t ns = DRK_NS[m];
        uint64_t code = 0;
        dke_lms_t next;
        dke_slice(&e->st, x + at, m, sf[m], &next, &pe, &code);
        if (out) {
            uint64_t w = ((uint64_t)m << 62) | ((uint64_t)sf[m] << 58) | (code << (58u - ns * (m + 1u)));
            out[2u * k] = (uint32_t)(w >> 32);
            out[2u * k + 1u] = (uint32_t)w;
        }
        e->st = next;
        e->pm = m;
        e->o.bytes += 8u;
        e->pos += ns;
        at += ns;
        if (!(++k & 63u)) DKE_WDT();
    }
    return at < n ? at : n;
}
/* n samples of x at snr dB into out (0: count only), whole; a loop from the first slice starting at or after loop_at
 * (0: none). The bytes written; o the loop. In place is safe: slice k's 8 bytes land at byte 8 k, behind the 28 k bytes
 * it has read */
static uint32_t dke_encode(const int16_t *x, uint32_t n, int32_t snr, uint32_t loop_at, uint32_t *out, dke_out_t *o)
{
    dke_t e;
    dke_begin(&e, snr, loop_at);
    dke_step(&e, x, n, 1, out, 0xFFFFFFFFu);
    *o = e.o;
    return e.o.bytes;
}
