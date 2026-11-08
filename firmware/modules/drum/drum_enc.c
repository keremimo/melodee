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
    default: j = a / (4u * sc); return j > 7u ? 7u : j;          /* 2, 6 .. 30 */
    }
}
/* one slice at depth m, scale sf: its error (with the tool's penalty for a predictor running away), the pure one;
 * code 0: not wanted */
static float dke_slice(const dke_lms_t *st, const int16_t *x, uint32_t m, uint32_t sf, dke_lms_t *out, float *pure,
                       uint64_t *code)
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
        for (uint32_t k = 0; k < 4u; k++) wp += (float)l.w[k] * (float)l.w[k];
        e += er * er + (wp > 805306367.0f ? wp * (1.0f / 262144.0f) - 3071.0f : 0.0f);
        if (code) *code = (*code << b) | bk;
        int32_t d = r >> 4;
        for (uint32_t k = 0; k < 4u; k++) l.w[k] += l.h[k] < 0 ? -d : d;
        l.h[0] = l.h[1]; l.h[1] = l.h[2]; l.h[2] = l.h[3]; l.h[3] = s;
    }
    *out = l; *pure = ep;
    return e;
}
/* depth m's best scale: all 16 (a narrower walk from the last slice's scale lost up to 12 dB on hats and snares) */
static uint32_t dke_scale(const dke_lms_t *st, const int16_t *x, uint32_t m, float *pure)
{
    dke_lms_t t;
    float best = 3.4e38f, pe;
    uint32_t b = 0;
    for (uint32_t s = 0; s < 16u; s++) {
        float e = dke_slice(st, x, m, s, &t, &pe, 0);
        if (e < best) { best = e; *pure = pe; b = s; }
    }
    return b;
}
/* n samples of x at snr dB into out (32-bit words, two a slice, the high one first; 0: count only); a loop from
 * the first slice starting at or after loop_at (0: none). The bytes written */
static uint32_t dke_encode(const int16_t *x, uint32_t n, int32_t snr, uint32_t loop_at, uint32_t *out, dke_out_t *o)
{
    dke_lms_t st;
    memset(&st, 0, sizeof st);
    memset(o, 0, sizeof *o);
    st.w[2] = -(1 << 13); st.w[3] = 1 << 14;
    float g = dke_exp2(-0.33219281f * (float)snr), g4 = dke_exp2(-0.33219281f * (float)(snr + 10));
    uint32_t pm = 3u;                                 /* the last slice's depth: where the search starts */
    for (uint32_t at = 0, k = 0; at < n; k++) {
        if (loop_at && !o->loop_at && at >= loop_at) {
            o->loop_at = at; o->loop_slice = k;
            for (uint32_t i = 0; i < 4u; i++) { o->loop[i] = st.h[i]; o->loop[4u + i] = st.w[i]; }
        }
        float tg[4], pe;
        uint32_t sf[4], ok[4] = {0, 0, 0, 0}, done[4] = {0, 0, 0, 0}, m = pm;
        for (uint32_t mm = 0; mm < 4u; mm++) {        /* each depth's target: the slice's level less snr dB */
            float en = 0.0f;
            for (uint32_t i = 0; i < DRK_NS[mm]; i++) en += (float)x[at + i] * (float)x[at + i];
            tg[mm] = en * (at < 176u ? g4 : g);
            if (tg[mm] < (float)DRK_NS[mm] * 2.25f) tg[mm] = (float)DRK_NS[mm] * 2.25f;
        }
        for (;;) {                                    /* the fewest bits that meet it: down while met, up while not */
            if (!done[m]) { sf[m] = dke_scale(&st, x + at, m, &pe); ok[m] = pe <= tg[m]; done[m] = 1; }
            if (ok[m] ? m == 0u || (done[m - 1u] && !ok[m - 1u]) : m == 3u || (done[m + 1u] && ok[m + 1u])) {
                if (!ok[m] && m < 3u) m++;
                break;
            }
            m = ok[m] ? m - 1u : m + 1u;
        }
        uint32_t ns = DRK_NS[m];
        uint64_t code = 0;
        dke_lms_t next;
        dke_slice(&st, x + at, m, sf[m], &next, &pe, &code);
        if (out) {
            uint64_t w = ((uint64_t)m << 62) | ((uint64_t)sf[m] << 58) | (code << (58u - ns * (m + 1u)));
            out[2u * k] = (uint32_t)(w >> 32);
            out[2u * k + 1u] = (uint32_t)w;
        }
        st = next;
        pm = m;
        o->bytes += 8u;
        at += ns;
        if (!(k & 63u)) DKE_WDT();
    }
    return o->bytes;
}
