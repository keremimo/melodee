/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The web kit maker's engine (melodee_kit.wasm, web/kit/build.sh): the firmware's own Record kit (kit_take_core.c
 * kt_keep), encoder (drum_enc.c), record checks (kit_store.c) and player's decoder (drum_real.c), with the kit in RAM
 * laid out as the flash holds it. A kit made here is byte for byte the one the device makes from the same takes
 * (web/kit/check.mjs), ready for BACKUP PUT objects 92..99.
 *   kit_begin(rec, len)      start from a kit's record (BACKUP GET 92..99), or len 0: a new kit. 0, or -n not a kit
 *   kit_name(str)            the kit's name (1..5 printable characters) at the next build. 0, -1
 *   kit_pad(d, pcm, n, rate, trim_a, trim_b, fade_ms, level, decay_ms, tone_hz)
 *                            pad d (D9_*: BD SD LT MT HT RS CP CH OH CR RD) at the next build: n > 0 a take, 16-bit
 *                            mono at rate Hz, pcm[trim_a, trim_b) of it (resampled to 44.1 kHz, copied: pcm may go);
 *                            n 0 the pad cleared; n < 0 the pad's take as it is. fade_ms 0: the take as Record kit's
 *                            buffer would hold it (a one-shot ends 10 ms after it falls 60 dB; a cymbal, OH CR RD,
 *                            loops over its last 30 %, as one that filled the buffer); fade_ms > 0: a one-shot faded
 *                            out over its last fade_ms. level (Q12, 4096 = as recorded, at most 16384), decay_ms (the
 *                            -40 dB time, 1..30000), tone_hz (TONE's pivot, 20..20000): 0 leaves the record's (a new
 *                            kit's: Record kit's defaults). 0, or -1
 *   kit_build(out, max)      the kit: the template, then each pad set in pad order, as Record kit takes them one by
 *                            one. Its bytes (at most DRK_KIT_MAX), 0 no takes left (delete the kit), -2 not playable,
 *                            -3 max too small, -5 no room
 *   kit_report(d)            pad d at the last build: int32[8] = status (0 untouched, KM_KEPT 1, KM_QUIET 2 too
 *                            quiet, KM_ROOM 3 no room, 5 cleared), quality reached (dB: 30 24 18 12), cut (1: shortened
 *                            to fit), samples kept, their rate (Hz), bytes, looped, samples before any cut
 *   kit_decode(d, out, max)  pad d of the last build (or the template) as the player plays it at TUNE 0: 16-bit at
 *                            44.1 kHz, its loop repeating up to max. Samples, -1 no such pad
 *   kit_max()                DRK_KIT_MAX; malloc / free for the page's buffers */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define DRK_CODEC_ONLY 1                         /* drum_real.c, kit_store.c: the format and the decoder alone */
#ifndef KT_ON
#error "KT_ON: usb_audio_stream.c's take trigger (web/kit/build.sh reads it from there)"
#endif
#include "x0x/fastmath.h"
enum { D9_BD, D9_SD, D9_LT, D9_MT, D9_HT, D9_RS, D9_CP, D9_CH, D9_OH, D9_CR, D9_RD, D9_NUM };   /* x0x/drum909.h's */
typedef struct { int32_t e, k; } dre_t;          /* drum_808.c's: in drk_ch_t, the envelope the decoder leaves alone */
static float dlk_exp2(float x) { return fm_exp2f(x); }   /* drum_lanes.c's */
static void fm1_wdt_feed(void) {}
#include "drum_kit.h"                            /* the record's format, the codec's tables */
#include "drum_real.c"
#include "kit_store.c"
#include "drum_enc.c"
_Static_assert(D9_NUM == DRK_NDRUM, "the kit's pads: the 909's drums");

/* what kt_keep's encoder saw: the take as shaped (its first count), as kept (the encode into place) */
static struct { uint32_t calls, n0, n; int32_t snr; uint32_t bytes; } kw_seen;
static uint32_t kw_encode(const int16_t *x, uint32_t n, int32_t snr, uint32_t loop_at, uint32_t *out, dke_out_t *o)
{
    uint32_t b = dke_encode(x, n, snr, loop_at, out, o);
    if (!kw_seen.calls++) kw_seen.n0 = n;
    if (out) { kw_seen.n = n; kw_seen.snr = snr; kw_seen.bytes = b; }
    return b;
}
#define dke_encode kw_encode
#include "kit_take_core.c"
#undef dke_encode

/* ----------------------------------------------------------------- the kit in RAM --- */
static uint32_t kw_flash[DRK_KSEC][1024];       /* 4 KB sectors as the pool's: payload from byte 32, a guard at 0xF00 */
static const uint32_t *kw_sec[DRK_KSEC];         /* their payloads */
static uint32_t kw_nsec, kw_len;                 /* the kit now: sectors (0: none), record bytes */
static int kw_rc;                                /* the last write's answer */
static uint8_t kw_rec[DRK_KIT_MAX], kw_base[DRK_KIT_MAX];
static uint32_t kw_base_len;

/* the record (len bytes) into the sectors, as kit_store.c's stream lays it; drk_kit_check's answer */
static int kw_lay(const uint8_t *rec, uint32_t len)
{
    uint32_t slices = len / 8u;
    kw_nsec = (slices + DRK_SECSLICES - 1u) / DRK_SECSLICES;
    kw_len = len;
    memset(kw_flash, 0xFF, sizeof kw_flash);
    for (uint32_t k = 0; k < DRK_KSEC; k++) kw_sec[k] = (const uint32_t *)((const uint8_t *)kw_flash[k] + DRK_SECOFF);
    for (uint32_t i = 0; i < slices; i++)
        memcpy((uint32_t *)kw_sec[i / DRK_SECSLICES] + DRK_SLOT(i % DRK_SECSLICES), rec + 8u * i, 8u);
    return kw_nsec ? drk_kit_check(kw_sec, kw_nsec, len) : 0;
}
static uint32_t kt_secs(uint32_t u, const uint32_t *secs[DRK_KSEC])
{
    (void)u;
    for (uint32_t k = 0; k < kw_nsec; k++) secs[k] = kw_sec[k];
    return kw_nsec;
}
/* kit_take.c's, into RAM: the record whole (kt_head, kt_record), checked as the device's commit checks it */
static int kt_write(uint32_t u, uint32_t pad, const uint32_t *words, uint32_t slices, const drk_sample_t *s,
                    const int32_t *loop)
{
    const uint32_t *secs[DRK_KSEC];
    uint32_t nsec = kt_secs(u, secs), k, cnt[DRK_NDRUM];
    const drk_kit_t *O = nsec ? (const drk_kit_t *)(const void *)secs[0] : 0;
    const drk_sample_t *src[DRK_NDRUM];
    drk_kit_t H;
    uint32_t at = kt_head(&H, src, cnt, &k, O, secs, nsec, u, pad, slices, s, loop);
    if (!k) { kw_nsec = kw_len = 0; return kw_rc = 0; }   /* (the device deletes an empty kit) */
    if (at * 8u > DRK_KIT_MAX) return kw_rc = -5;
    kt_record(kw_rec, &H, src, cnt, secs, nsec, pad, words);
    return kw_rc = kw_lay(kw_rec, at * 8u) ? -2 : 0;
}

/* ----------------------------------------------------------------------- the pads --- */
enum { KP_NONE, KP_TAKE, KP_CLEAR, KP_KEEP };
static struct { int16_t *x; uint32_t n, cap, mode; uint16_t out, ms, pivot; } kw_pad[DRK_NDRUM];
static int32_t kw_report[DRK_NDRUM][8];
static char kw_name[DRK_NAMELEN + 1u];

/* x[a, b) at rate Hz to 44.1 kHz into y (its samples, at most m): a windowed sinc, its cutoff below both Nyquists */
static uint32_t kw_resample(const int16_t *x, uint32_t a, uint32_t b, uint32_t rate, int16_t *y, uint32_t m)
{
    const double PI = 3.14159265358979323846, r = (double)rate / 44100.0, fc = (r > 1.0 ? 1.0 / r : 1.0) * 0.95;
    const int H = (int)ceil(16.0 / fc);
    uint32_t n = (uint32_t)((double)(b - a) / r);
    if (n > m) n = m;
    for (uint32_t i = 0; i < n; i++) {
        double t = (double)a + (double)i * r, acc = 0.0;
        int c = (int)floor(t);
        for (int k = c - H + 1; k <= c + H; k++) {
            if (k < (int)a || k >= (int)b) continue;
            double u = t - (double)k, w = fabs(u) >= (double)H ? 0.0 : 0.42 + 0.5 * cos(PI * u / H) + 0.08 * cos(2.0 * PI * u / H);
            double v = u == 0.0 ? fc : sin(PI * fc * u) / (PI * u);
            acc += (double)x[k] * v * w;
        }
        long q = lround(acc);
        y[i] = (int16_t)(q < -32768 ? -32768 : q > 32767 ? 32767 : q);
    }
    return n;
}

int kit_max(void) { return (int)DRK_KIT_MAX; }

int kit_begin(const uint8_t *rec, int len)
{
    for (uint32_t d = 0; d < DRK_NDRUM; d++) { free(kw_pad[d].x); memset(&kw_pad[d], 0, sizeof kw_pad[d]); }
    memset(kw_report, 0, sizeof kw_report);
    kw_name[0] = 0;
    kw_base_len = 0;
    if (len < 0 || (uint32_t)len > DRK_KIT_MAX || (len && (len < (int)DRK_KIT_BYTES || len % 8))) { kw_lay(0, 0); return -1; }
    if (len) memcpy(kw_base, rec, (uint32_t)len);
    int rc = kw_lay(kw_base, (uint32_t)len);
    if (rc) { kw_lay(0, 0); return -10 - rc; }
    kw_base_len = (uint32_t)len;
    return 0;
}

int kit_name(const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    if (n < 1u || n > DRK_NAMELEN) return -1;
    for (uint32_t i = 0; i < n; i++) if (s[i] < ' ' || s[i] > '~') return -1;
    memcpy(kw_name, s, n + 1u);
    return 0;
}

int kit_pad(int d, const int16_t *pcm, int n, int rate, int trim_a, int trim_b, int fade_ms, int level, int decay_ms,
            int tone_hz)
{
    if (d < 0 || d >= (int)DRK_NDRUM || level < 0 || level > 16384 || decay_ms < 0 || decay_ms > 30000 ||
        (tone_hz && (tone_hz < 20 || tone_hz > 20000)) || fade_ms < 0)
        return -1;
    free(kw_pad[d].x);
    memset(&kw_pad[d], 0, sizeof kw_pad[d]);
    kw_pad[d].out = (uint16_t)level; kw_pad[d].ms = (uint16_t)decay_ms; kw_pad[d].pivot = (uint16_t)tone_hz;
    kw_pad[d].mode = n > 0 ? KP_TAKE : n == 0 ? KP_CLEAR : KP_KEEP;
    if (n <= 0) return 0;
    if (rate < 8000 || rate > 384000 || trim_a < 0 || trim_b > n || trim_a >= trim_b) { kw_pad[d].mode = KP_NONE; return -1; }
    uint32_t m = (uint32_t)((uint64_t)(uint32_t)(trim_b - trim_a) * 44100u / (uint32_t)rate) + 2u;
    int16_t *x = malloc((m + 64u) * sizeof *x);
    if (!x) { kw_pad[d].mode = KP_NONE; return -1; }
    if (rate == 44100) { m = (uint32_t)(trim_b - trim_a); memcpy(x, pcm + trim_a, m * sizeof *x); }
    else m = kw_resample(pcm, (uint32_t)trim_a, (uint32_t)trim_b, (uint32_t)rate, x, m);
    uint32_t f = (uint32_t)fade_ms * 441u / 10u, cap = m;
    if (fade_ms) {                                /* a one-shot faded out: a take that ended inside its buffer */
        if (f > m) f = m;
        for (uint32_t i = 0; i < f; i++) x[m - f + i] = (int16_t)((int32_t)x[m - f + i] * (int32_t)(f - i) / (int32_t)f);
        cap = m + 1u;
    }
    kw_pad[d].x = x; kw_pad[d].n = m; kw_pad[d].cap = cap;
    return 0;
}

int kit_build(uint8_t *out, int max)
{
    static int16_t *x;
    static uint32_t xn;
    memset(kw_report, 0, sizeof kw_report);
    if (kw_lay(kw_base, kw_base_len)) return -2;
    for (uint32_t d = 0; d < DRK_NDRUM; d++) {
        int32_t *r = kw_report[d];
        if (kw_pad[d].mode == KP_CLEAR) {
            if (kt_write(0, d, 0, 0, 0, 0)) return kw_rc;
            r[0] = 5;
        } else if (kw_pad[d].mode == KP_TAKE) {   /* kt_keep encodes in place: a copy, so the pad builds again */
            uint32_t n = kw_pad[d].n;
            if (xn < n + 64u) { free(x); x = malloc((n + 64u) * sizeof *x); xn = x ? n + 64u : 0u; if (!x) return -3; }
            memcpy(x, kw_pad[d].x, n * sizeof *x);
            memset(&kw_seen, 0, sizeof kw_seen);
            uint32_t km = kt_keep(0, d, x, n, kw_pad[d].cap);
            if (km == KM_FLASH) return kw_rc;
            r[0] = (int32_t)km;
            r[1] = km == KM_KEPT ? kw_seen.snr : 0;
            r[7] = (int32_t)kw_seen.n0;
        }
    }
    if (!kw_nsec) return 0;
    uint32_t len = kw_len;                       /* the record now, then the page's name and pad settings */
    for (uint32_t i = 0; i < len / 8u; i++) memcpy(kw_rec + 8u * i, drk_slice_at(kw_sec, kw_nsec, i), 8u);
    drk_kit_t *H = (drk_kit_t *)(void *)kw_rec;
    if (kw_name[0]) { memset(H->name, 0, sizeof H->name); memcpy(H->name, kw_name, strlen(kw_name)); }
    for (uint32_t d = 0; d < DRK_NDRUM; d++) {
        if (kw_pad[d].out) H->map[d].out = kw_pad[d].out;
        if (kw_pad[d].ms) H->map[d].ms = kw_pad[d].ms;
        if (kw_pad[d].pivot) H->map[d].pivot = kw_pad[d].pivot;
        int32_t *r = kw_report[d];
        if (r[0] != KM_KEPT || H->map[d].sample == 0xFFu) continue;
        const drk_sample_t *s = &H->smp[H->map[d].sample];
        r[3] = s->n; r[4] = s->inc == 16384u ? 22050 : 44100; r[5] = (int32_t)(8u * drk_take_slices(kw_sec, kw_nsec, s));
        r[6] = s->loop_at != 0; r[2] = (uint32_t)r[3] < (uint32_t)r[7];   /* (its samples before any cut, at its rate) */
    }
    if (kw_lay(kw_rec, len)) return -2;
    if (max < (int)len) return -3;
    memcpy(out, kw_rec, len);
    return (int)len;
}

int32_t *kit_report(int d) { return d >= 0 && d < (int)DRK_NDRUM ? kw_report[d] : 0; }

int kit_decode(int d, int16_t *out, int max)
{
    if (!kw_nsec || d < 0 || d >= (int)DRK_NDRUM || max < 0) return -1;
    const drk_kit_t *H = (const drk_kit_t *)(const void *)kw_sec[0];
    if (H->map[d].sample == 0xFFu) return -1;
    const drk_sample_t *s = &H->smp[H->map[d].sample];
    static drk_ch_t c;
    memset(&c, 0, sizeof c);
    c.inc = (uint32_t)((float)s->inc * 2.0f * dlk_exp2(0.0f));   /* (drk_hit's, TUNE 0) */
    c.secs = kw_sec;
    c.s = s;
    c.loop = H->loops[s->st];
    drk_stream_start(&c, s);
    int w = 0;
    while (w < max) {
        int32_t y[32];
        uint32_t m = (uint32_t)(max - w) < 32u ? (uint32_t)(max - w) : 32u;
        int live = drk_run(&c, y, m);
        for (uint32_t i = 0; i < m; i++) out[w + (int)i] = (int16_t)(y[i] < -32768 ? -32768 : y[i] > 32767 ? 32767 : y[i]);
        w += (int)m;
        if (!live) break;
    }
    return w;
}
