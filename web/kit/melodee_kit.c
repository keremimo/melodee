/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The web kit maker's engine (melodee_kit.wasm, web/kit/build.sh): SAMPLER's own Record kit take (firmware/modules/kits:
 * kit_take_core.c, streamed as the device streams it), encoder (drum_enc.c), pad record check (kit_store.c) and player's
 * decoder (drum_real.c), each pad record in RAM laid out as the flash holds it. A kit made here is byte for byte the
 * one the device makes from the same takes (web/kit/check.mjs), ready for BACKUP PUT objects 92..99 (a KIT2 blob:
 * drum_kit.h drk_box_t, then the pad records in pad order).
 *   kit_begin(blob, len)     start from a kit's blob (BACKUP GET 92..99), or len 0: a new kit. 0, or -n not a kit
 *   kit_name(str)            the kit's name (1..5 printable characters; "" none) at the next build. 0, -1
 *   kit_pad(p, pcm, n, rate, trim_a, trim_b, fade_ms, level, decay_ms, tone_hz)
 *                            pad p (0..15: the 808's sixteen, KICK SNARE TOM L/M/H RIM CLAP HAT C HAT O CYMB BELL CONGL
 *                            CONGM CONGH CLAVE MARAC) at the next build: n > 0 a take, 16-bit mono at rate Hz,
 *                            pcm[trim_a, trim_b) of it (resampled to 44.1 kHz, copied: pcm may go) as Record kit takes it
 *                            from its first sample on; n 0 the pad cleared; n < 0 the pad's take as it is. fade_ms > 0:
 *                            the take faded out over its last fade_ms first. level (Q12, 4096 = as recorded, at most
 *                            16384), decay_ms (the -40 dB time, 1..30000; 0: held to the take's end), tone_hz (TONE's
 *                            pivot, 20..20000): 0 leaves the record's (a new take's: Record kit's defaults). 0, or -1
 *   kit_build(out, max)      the kit's blob: each pad set in pad order, as Record kit takes them one by one. Its
 *                            bytes, 0 nothing left (delete the kit), -2 not playable, -3 max too small, -5 no room
 *   kit_report(p)            pad p at the last build: int32[8] = status (0 untouched, KM_KEPT 1, KM_QUIET 2 too
 *                            quiet, KM_ROOM 3 no room, 5 cleared), quality (dB), 0, samples kept, their rate (Hz),
 *                            bytes, looped, samples fed
 *   kit_decode(p, out, max)  pad p of the last build (or the template) as the player plays it at TUNE 0: 16-bit at
 *                            44.1 kHz, its loop repeating up to max. Samples, -1 no such pad
 *   kit_max()                a blob's bytes at most; malloc / free for the page's buffers */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define DRK_CODEC_ONLY 1                         /* drum_real.c, kit_store.c: the format and the decoder alone */
#ifndef KT_ON
#error "KT_ON: usb_audio_stream.c's take trigger (web/kit/build.sh reads it from there)"
#endif
#include "x0x/fastmath.h"
typedef struct { int32_t e, k; } dre_t;          /* drum_808.c's: in drk_ch_t, the envelope the decoder leaves alone */
static float dlk_exp2(float x) { return fm_exp2f(x); }   /* drum_lanes.c's */
static void fm1_wdt_feed(void) {}
#define KW_SECS 128u                             /* the RAM flash: one pad record laid out at a time, from sector 0 */
static uint8_t kw_flash[KW_SECS * 4096u];
#define DRK_XIP(off) ((const uint8_t *)kw_flash + (off))
#include "drum_kit.h"                            /* the records' format, the codec's tables */
#include "drum_real.c"
#include "kit_store.c"
#include "drum_enc.c"
#define KW_PAD_MAX (KW_SECS * DRK_SECBYTES)      /* a pad record's bytes, at most here */
#define KW_BOX_MAX (DRK_BOX_BYTES + 108u * DRK_SECBYTES)   /* a kit's blob: the FM-1's whole store (rec_store.c) */

/* ------------------------------------------------------------- the pad records in RAM --- */
static uint8_t *kw_rec[DRK_NPAD];                /* each pad's record (0: none), kw_len its bytes */
static uint32_t kw_len[DRK_NPAD];
static uint8_t kw_list[KW_SECS];                 /* the RAM flash's sectors in order */
/* a record (len bytes) laid out in the RAM flash as the pool's list record holds it; drk_pad_check's answer */
static int kw_lay(const uint8_t *rec, uint32_t len)
{
    uint32_t nsec = (len + DRK_SECBYTES - 1u) / DRK_SECBYTES;
    if (!len || nsec > KW_SECS) return 1;
    memset(kw_flash, 0xFF, nsec * 4096u);
    for (uint32_t k = 0; k < KW_SECS; k++) kw_list[k] = (uint8_t)k;
    for (uint32_t i = 0; i < len / 8u; i++)
        memcpy(kw_flash + (i / DRK_SECSLICES) * 4096u + DRK_SECOFF + 4u * DRK_SLOT(i % DRK_SECSLICES), rec + 8u * i, 8u);
    return drk_pad_check(kw_list, nsec, len);
}
/* the take's host calls (kit_take_core.c): its bytes into kw_take, checked as the device's commit checks them */
static uint8_t kw_take[KW_PAD_MAX];
static uint32_t kw_take_len;
static int kt_h_write(uint32_t off, const void *src, uint32_t n)
{
    if (off > KW_PAD_MAX || n > KW_PAD_MAX - off) return -5;
    memcpy(kw_take + off, src, n);
    return 0;
}
static int kt_h_end(uint32_t slot, uint32_t len)
{
    (void)slot;
    if (kw_lay(kw_take, len)) return -2;
    kw_take_len = len;
    return 0;
}
static void kt_h_abort(void) { kw_take_len = 0; }
#include "kit_take_core.c"

/* ----------------------------------------------------------------------- the pads --- */
enum { KP_NONE, KP_TAKE, KP_CLEAR, KP_KEEP };
static struct { int16_t *x; uint32_t n, mode; uint16_t out, ms, pivot; uint8_t set; } kw_pad[DRK_NPAD];
static int32_t kw_report[DRK_NPAD][8];
static char kw_name[DRK_NAMELEN + 1u];
static uint8_t kw_base[KW_BOX_MAX];              /* the template */
static uint32_t kw_base_len;

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
static uint32_t kw_r32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void kw_w32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
/* the kit's pads from a blob (the template); 0, or not a kit */
static int kw_load(const uint8_t *b, uint32_t len)
{
    uint32_t at = DRK_BOX_BYTES;
    for (uint32_t p = 0; p < DRK_NPAD; p++) { free(kw_rec[p]); kw_rec[p] = 0; kw_len[p] = 0; }
    if (!len) return 0;
    if (len < DRK_BOX_BYTES || kw_r32(b) != DRK_BOX_MAGIC || (b[4] | b[5] << 8) != DRK_BOX_BYTES || b[6] != DRK_NPAD) return -1;
    for (uint32_t p = 0; p < DRK_NPAD; p++) {
        uint32_t n = kw_r32(b + 20u + 4u * p);
        if (!n) continue;
        if (n > len - at || n > KW_PAD_MAX || kw_lay(b + at, n)) return -2;
        if (!(kw_rec[p] = malloc(n))) return -3;
        memcpy(kw_rec[p], b + at, n);
        kw_len[p] = n;
        at += n;
    }
    return at == len ? 0 : -1;
}

int kit_max(void) { return (int)KW_BOX_MAX; }

int kit_begin(const uint8_t *rec, int len)
{
    for (uint32_t d = 0; d < DRK_NPAD; d++) { free(kw_pad[d].x); memset(&kw_pad[d], 0, sizeof kw_pad[d]); }
    memset(kw_report, 0, sizeof kw_report);
    kw_name[0] = 0;
    kw_base_len = 0;
    if (len < 0 || (uint32_t)len > KW_BOX_MAX) { kw_load(0, 0); return -1; }
    if (len) memcpy(kw_base, rec, (uint32_t)len);
    int rc = kw_load(kw_base, (uint32_t)len);
    if (rc) { kw_load(0, 0); return -10 + rc; }
    kw_base_len = (uint32_t)len;
    if (len) { memcpy(kw_name, kw_base + 8, DRK_NAMELEN); kw_name[DRK_NAMELEN] = 0; }
    return 0;
}

int kit_name(const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    if (n > DRK_NAMELEN) return -1;
    for (uint32_t i = 0; i < n; i++) if (s[i] < ' ' || s[i] > '~') return -1;
    memcpy(kw_name, s, n + 1u);
    return 0;
}

int kit_pad(int d, const int16_t *pcm, int n, int rate, int trim_a, int trim_b, int fade_ms, int level, int decay_ms,
            int tone_hz)
{
    if (d < 0 || d >= (int)DRK_NPAD || level < 0 || level > 16384 || decay_ms < 0 || decay_ms > 30000 ||
        (tone_hz && (tone_hz < 20 || tone_hz > 20000)) || fade_ms < 0)
        return -1;
    free(kw_pad[d].x);
    memset(&kw_pad[d], 0, sizeof kw_pad[d]);
    kw_pad[d].out = (uint16_t)level; kw_pad[d].ms = (uint16_t)decay_ms; kw_pad[d].pivot = (uint16_t)tone_hz;
    kw_pad[d].set = level || decay_ms || tone_hz;
    kw_pad[d].mode = n > 0 ? KP_TAKE : n == 0 ? KP_CLEAR : KP_KEEP;
    if (n <= 0) return 0;
    if (rate < 8000 || rate > 384000 || trim_a < 0 || trim_b > n || trim_a >= trim_b) { kw_pad[d].mode = KP_NONE; return -1; }
    uint32_t m = (uint32_t)((uint64_t)(uint32_t)(trim_b - trim_a) * 44100u / (uint32_t)rate) + 2u;
    int16_t *x = malloc((m + 64u) * sizeof *x);
    if (!x) { kw_pad[d].mode = KP_NONE; return -1; }
    if (rate == 44100) { m = (uint32_t)(trim_b - trim_a); memcpy(x, pcm + trim_a, m * sizeof *x); }
    else m = kw_resample(pcm, (uint32_t)trim_a, (uint32_t)trim_b, (uint32_t)rate, x, m);
    uint32_t f = (uint32_t)fade_ms * 441u / 10u;
    if (f > m) f = m;
    for (uint32_t i = 0; i < f; i++) x[m - f + i] = (int16_t)((int32_t)x[m - f + i] * (int32_t)(f - i) / (int32_t)f);
    kw_pad[d].x = x; kw_pad[d].n = m;
    return 0;
}

int kit_build(uint8_t *out, int max)
{
    uint32_t total = DRK_BOX_BYTES, any = 0;
    memset(kw_report, 0, sizeof kw_report);
    if (kw_load(kw_base, kw_base_len)) return -2;
    for (uint32_t d = 0; d < DRK_NPAD; d++) {
        int32_t *r = kw_report[d];
        if (kw_pad[d].mode == KP_CLEAR) {
            free(kw_rec[d]); kw_rec[d] = 0; kw_len[d] = 0;
            r[0] = 5;
        } else if (kw_pad[d].mode == KP_TAKE) {   /* Record kit's stream: the samples in its pieces, then the end */
            kw_take_len = 0;
            tk_begin(d, d, KW_PAD_MAX);
            uint32_t st = KM_NONE;
            for (uint32_t at = 0; at < kw_pad[d].n && st == KM_NONE; at += 1024u)
                st = tk_feed(kw_pad[d].x + at, kw_pad[d].n - at < 1024u ? kw_pad[d].n - at : 1024u);
            uint32_t km = tk_end();
            if (km == KM_FLASH) return -2;
            r[0] = (int32_t)km;
            r[7] = (int32_t)kw_pad[d].n;
            if (km == KM_KEPT) {
                free(kw_rec[d]);
                if (!(kw_rec[d] = malloc(kw_take_len))) return -3;
                memcpy(kw_rec[d], kw_take, kw_take_len);
                kw_len[d] = kw_take_len;
            }
        }
        if (!kw_rec[d]) continue;
        drk_pad_t *H = (drk_pad_t *)(void *)kw_rec[d];  /* the page's settings over the take's */
        if (kw_pad[d].out) H->out = kw_pad[d].out;
        if (kw_pad[d].set) H->ms = kw_pad[d].ms;
        if (kw_pad[d].pivot) H->pivot = kw_pad[d].pivot;
        if (kw_lay(kw_rec[d], kw_len[d])) return -2;
        if (r[0] == KM_KEPT) {
            r[1] = TK_SNR; r[3] = (int32_t)H->n; r[4] = H->inc == 16384u ? 22050 : 44100; r[5] = (int32_t)kw_len[d];
            r[6] = H->loop_at != 0;
        }
        total += kw_len[d];
        any = 1;
    }
    if (!any) return 0;
    if (total > KW_BOX_MAX) return -5;
    if (max < (int)total) return -3;
    memset(out, 0, DRK_BOX_BYTES);
    kw_w32(out, DRK_BOX_MAGIC);
    out[4] = (uint8_t)DRK_BOX_BYTES; out[6] = (uint8_t)DRK_NPAD;
    memcpy(out + 8, kw_name, strlen(kw_name));
    uint32_t at = DRK_BOX_BYTES;
    for (uint32_t d = 0; d < DRK_NPAD; d++) {
        kw_w32(out + 20u + 4u * d, kw_len[d]);
        if (kw_len[d]) memcpy(out + at, kw_rec[d], kw_len[d]);
        at += kw_len[d];
    }
    return (int)total;
}

int32_t *kit_report(int d) { return d >= 0 && d < (int)DRK_NPAD ? kw_report[d] : 0; }

int kit_decode(int d, int16_t *out, int max)
{
    if (d < 0 || d >= (int)DRK_NPAD || max < 0 || !kw_rec[d] || kw_lay(kw_rec[d], kw_len[d])) return -1;
    const drk_pad_t *H = (const drk_pad_t *)(const void *)DRK_SEC(0);
    static drk_ch_t c;
    memset(&c, 0, sizeof c);
    c.inc = (uint32_t)H->inc * 2u;               /* (drk_hit's, TUNE 0) */
    c.list = kw_list;
    c.s = H;
    drk_stream_start(&c, H);
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
