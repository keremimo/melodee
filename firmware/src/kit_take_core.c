/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A drum take made into a user kit's pad, the part with no flash in it: what Record kit does on the device
 * (kit_take.c) and what the web's kit maker does (web/kit, this file in WebAssembly), so that both make a kit byte for
 * byte the same. The take trimmed to its hit, normalized, faded at its end, or, a cymbal still ringing when the
 * buffer filled, looped over its last 30 % (a crossfade back to the loop's start, darkening as it repeats); the kick
 * and the toms at 22.05 kHz. Encoded in place at the best quality that leaves room for the kit's empty pads, then
 * the kit's KIT1 record: the header, the other pads' takes as the old record has them.
 * The includer gives KT_ON (usb_audio_stream.c), drum_real.c, kit_store.c's record helpers, drum_enc.c, and kt_secs
 * and kt_write (the old record, the new one written: kit_take.c in the flash, web/kit in RAM). Every function here is
 * built for size and kept whole as tools/size_fns.py builds kit_take.c's (KT_SIZE): the device's code is as it was. */
enum { KM_NONE, KM_KEPT, KM_QUIET, KM_ROOM, KM_FLASH };   /* a take's end, as the sheet shows it */
#define KT_PAD_RESERVE 1536u                         /* bytes kept for each empty pad */
#define KT_SIZE __attribute__((minsize))
static const int8_t KT_SNR[] = {30, 24, 18, 12};    /* never worse than 12 dB: a take too long for that is cut */
static KT_SIZE int kt_cymbal(uint32_t d) { return d == D9_OH || d == D9_CR || d == D9_RD; }
/* a new kit's pads: level (Q12), -40 dB time (ms), TONE's pivot (Hz), in D9_* order (once tuned for recorded kits) */
static const uint16_t KT_PAD[DRK_NDRUM][3] = {{4700, 1200, 4000}, {4800, 900, 6000}, {4700, 1300, 3000}, {4700, 1200, 3000},
    {4700, 1100, 3000}, {5000, 400, 5000}, {5000, 700, 5000}, {4600, 260, 10000}, {4200, 620, 10000}, {4000, 2400, 10000},
    {3600, 2000, 10000}};
static KT_SIZE int kt_low(uint32_t d) { return d == D9_BD || d == D9_LT || d == D9_MT || d == D9_HT; }

/* slot u's record as the player reads it (0 sectors: none) */
static uint32_t kt_secs(uint32_t u, const uint32_t *secs[DRK_KSEC]);
/* slot u rewritten with pad's take replaced (s 0: cleared) by words (RAM, slices of them); the others as they were.
 * 0, -5 no room, or the storage's error; an empty kit is deleted */
static int kt_write(uint32_t u, uint32_t pad, const uint32_t *words, uint32_t slices, const drk_sample_t *s,
                    const int32_t *loop);

/* pad d's take in the old kit O (0: none), its slices */
static KT_SIZE const drk_sample_t *kt_old(const drk_kit_t *O, const uint32_t *const *secs, uint32_t nsec, uint32_t d,
                                          uint32_t *slices)
{
    if (!O || O->map[d].sample == 0xFFu) return 0;
    const drk_sample_t *s = &O->smp[O->map[d].sample];
    *slices = drk_take_slices(secs, nsec, s);
    return s;
}

/* The KIT1 header of the old kit O (secs, nsec; 0: none, a new kit named for slot u) with pad's take replaced by s
 * (slices of it, loop its predictor; s 0: the pad cleared), the others as they were: src and cnt each pad's take (0:
 * none) and slices, *takes how many (0: none left). The record's length in slices, the header's own counted */
static inline __attribute__((always_inline)) KT_SIZE uint32_t
kt_head(drk_kit_t *H, const drk_sample_t *src[DRK_NDRUM], uint32_t cnt[DRK_NDRUM], uint32_t *takes, const drk_kit_t *O,
        const uint32_t *const *secs, uint32_t nsec, uint32_t u, uint32_t pad, uint32_t slices, const drk_sample_t *s,
        const int32_t *loop)
{
    uint32_t at = DRK_KIT_BYTES / 8u, k = 0;
    memset(H, 0, sizeof *H);
    H->magic = DRK_KIT_MAGIC; H->bytes = DRK_KIT_BYTES; H->ndrum = DRK_NDRUM;
    if (O) memcpy(H->name, O->name, sizeof H->name);
    else { memcpy(H->name, "KIT  ", 5); H->name[4] = (char)('1' + u); }
    for (uint32_t d = 0; d < DRK_NDRUM; d++) {
        if (O) H->map[d] = O->map[d];
        else { H->map[d].out = KT_PAD[d][0]; H->map[d].ms = KT_PAD[d][1]; H->map[d].pivot = KT_PAD[d][2]; }
        H->map[d].sample = H->map[d].alt = 0xFFu;
        src[d] = d == pad ? s : kt_old(O, secs, nsec, d, &cnt[d]);
        if (d == pad) cnt[d] = slices;
        if (!src[d]) continue;
        H->smp[k] = *src[d];
        H->smp[k].at = (uint16_t)at;
        H->smp[k].st = (uint8_t)k;
        if (src[d]->loop_at)
            memcpy(H->loops[k], d == pad ? loop : O->loops[src[d]->st], sizeof H->loops[k]);
        H->map[d].sample = (uint8_t)k++;
        at += cnt[d];
    }
    H->nsmp = (uint8_t)k;
    *takes = k;
    return at;
}
/* the record kt_head describes into out (RAM, at * 8 bytes): the header, then the takes' slices in pad order, pad's
 * from words (kit_take.c streams the same bytes into the flash a page at a time) */
static inline __attribute__((always_inline)) void kt_record(uint8_t *out, const drk_kit_t *H,
                                                            const drk_sample_t *const src[DRK_NDRUM],
                                                            const uint32_t cnt[DRK_NDRUM], const uint32_t *const *secs,
                                                            uint32_t nsec, uint32_t pad, const uint32_t *words)
{
    memcpy(out, H, sizeof *H);                        /* (DRK_KIT_BYTES: no padding) */
    out += sizeof *H;
    for (uint32_t d = 0; d < DRK_NDRUM; d++) {
        if (!src[d]) continue;
        if (d == pad) memcpy(out, words, cnt[d] * 8u);
        else for (uint32_t j = 0; j < cnt[d]; j++) memcpy(out + 8u * j, drk_slice_at(secs, nsec, src[d]->at + j), 8u);
        out += cnt[d] * 8u;
    }
}

/* the take x (n samples, room for 64 more) for pad of slot u: processed, encoded, the kit rewritten. KM_* */
static KT_SIZE __attribute__((noinline)) uint32_t kt_keep(uint32_t u, uint32_t pad, int16_t *x, uint32_t n, uint32_t cap)
{
    int32_t peak = 0;
    for (uint32_t i = 0; i < n; i++) { int32_t a = x[i] < 0 ? -x[i] : x[i]; if (a > peak) peak = a; }
    if (peak < KT_ON) return KM_QUIET;
    uint32_t start = 0;                               /* the hit, 1 ms early */
    while (start < n && (x[start] < 0 ? -x[start] : x[start]) * 30 < peak) start++;
    start = start > 44u ? start - 44u : 0u;
    n -= start;
    int32_t g = (int32_t)((32000 * 4096) / peak);
    for (uint32_t i = 0; i < n; i++) x[i] = (int16_t)dke_clip((x[start + i] * g) >> 12);
    int looped = kt_cymbal(pad) && n + start >= cap;  /* still ringing when the buffer ran out */
    uint32_t la = 0;
    if (!looped) {
        uint32_t end = n;                             /* -60 dB under the peak, then 10 ms to silence */
        while (end > 1u && (x[end - 1u] < 0 ? -x[end - 1u] : x[end - 1u]) < 32) end--;
        n = end + 441u < n ? end + 441u : n;
        for (uint32_t i = end; i < n; i++) x[i] = (int16_t)((x[i] * (int32_t)(n - i)) / 441);
    }
    if (kt_low(pad)) {                                /* 22.05 kHz: half-band (-1 0 9 16 9 0 -1) / 32 */
        int32_t p3 = 0, p1 = 0;
        for (uint32_t i = n; i < n + 4u; i++) x[i] = 0;
        for (uint32_t j = 0; 2u * j < n; j++) {
            int32_t c = x[2u * j], n1 = x[2u * j + 1u], n3 = x[2u * j + 3u];
            x[j] = (int16_t)dke_clip((-p3 + 9 * p1 + 16 * c + 9 * n1 - n3) >> 5);
            p3 = p1; p1 = n1;
        }
        n = (n + 1u) / 2u;
    }
    if (n > 65535u) n = 65535u;
    /* the room: the kit's cap less the other pads' takes, a share kept for each empty pad */
    const uint32_t *secs[DRK_KSEC];
    uint32_t nsec = kt_secs(u, secs), others = 0, empty = 0, sl;
    const drk_kit_t *O = nsec ? (const drk_kit_t *)(const void *)secs[0] : 0;
    for (uint32_t d = 0; d < DRK_NDRUM; d++)
        if (d != pad) { if (kt_old(O, secs, nsec, d, &sl)) others += sl * 8u; else empty++; }
    uint32_t avail = DRK_KIT_MAX - DRK_KIT_BYTES - others, keep = empty * KT_PAD_RESERVE;
    uint32_t budget = avail - (keep < avail / 2u ? keep : avail / 2u);
    dke_out_t o;
    int32_t snr = -1;
    for (uint32_t tries = 0; snr < 0 && tries < 4u && n > 441u; tries++) {
        uint32_t bytes = 0;
        for (uint32_t i = n; i < n + 64u; i++) x[i] = 0;
        for (uint32_t q = 0; q < sizeof KT_SNR && snr < 0; q++)
            if ((bytes = dke_encode(x, n, KT_SNR[q], 0, 0, &o)) <= budget) snr = KT_SNR[q];
        if (snr < 0) {                                /* too long even at 12 dB: cut to fit (a little more), a */
            n = (uint32_t)((uint64_t)n * budget / bytes) * 15u / 16u;   /* one-shot faded, a cymbal looped */
            for (uint32_t i = n > 441u && !looped ? n - 441u : n; i < n; i++) x[i] = (int16_t)((x[i] * (int32_t)(n - i)) / 441);
        }
    }
    if (snr < 0) return KM_ROOM;
    if (looped) {                                     /* its last 30 % again and again: the end crossfaded into */
        la = n * 7u / 10u;                            /* what comes before the loop's start */
        uint32_t L = n - la < 2048u ? n - la : 2048u;
        if (L > la) L = la;
        for (uint32_t i = 0; i < L; i++)
            x[n - L + i] = (int16_t)(((int32_t)x[n - L + i] * (int32_t)(L - i) + (int32_t)x[la - L + i] * (int32_t)i) / (int32_t)L);
    }
    for (uint32_t i = n; i < n + 64u; i++) x[i] = 0;
    drk_sample_t s;
    memset(&s, 0, sizeof s);
    dke_encode(x, n, snr, la, (uint32_t *)(void *)x, &o);
    s.n = (uint16_t)n;
    s.inc = kt_low(pad) ? 16384u : 32768u;
    if (looped && o.loop_at) { s.loop_at = (uint16_t)o.loop_at; s.loop_slice = (uint16_t)o.loop_slice; s.dark = 2; }
    return kt_write(u, pad, (const uint32_t *)(const void *)x, o.bytes / 8u, &s, o.loop) ? KM_FLASH : KM_KEPT;
}
