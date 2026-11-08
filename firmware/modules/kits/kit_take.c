/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A drum take into a user kit (the firmware's Record kit sheet drives it and records it from USB audio: DRUM's
 * rec_take, a kit's pad): processed and encoded in place by kit_take_core.c (kt_keep, which the web's kit maker runs
 * too), then the kit rewritten whole through the host's data records (the other pads' slices copied from the old
 * record, which stays current until the new one commits). */
#ifndef KT_ON
#define KT_ON MOD3_TAKE_ON                       /* (the host's take starts above it: kt_keep's quiet take) */
#endif
#include "drum_enc.c"
#include "kit_take_core.c"
_Static_assert(KM_NONE == MOD3_TK_NONE && KM_KEPT == MOD3_TK_KEPT && KM_QUIET == MOD3_TK_QUIET && KM_ROOM == MOD3_TK_ROOM &&
               KM_FLASH == MOD3_TK_FLASH, "a take's end as the host shows it");
#if MOD_SIDE                                     /* (the host's data records; a host build's built-in engine: its own) */
#define kt_rec_begin(u, len) mod_host->rec_begin(u, len)
#define kt_rec_write(p, n) mod_host->rec_write(p, n)
#define kt_rec_commit(u) mod_host->rec_commit(u)
#define kt_rec_delete(u) mod_host->rec_delete(u)
#else
#define kt_rec_begin mod_h4_rec_begin
#define kt_rec_write mod_h4_rec_write
#define kt_rec_commit mod_h4_rec_commit
#define kt_rec_delete mod_h4_rec_delete
#endif

/* slot u's record as the player reads it (0 sectors: none) */
static uint32_t kt_secs(uint32_t u, const uint32_t *secs[DRK_KSEC])
{
    uint32_t n = u < DK_NUSER && !(drk_user_gen & 1u) ? drk_user[u].n : 0u;
    if (n > DRK_KSEC)
        n = 0;
    for (uint32_t k = 0; k < n; k++)
        secs[k] = (const uint32_t *)(const void *)DRK_XIP(drk_user[u].sec[k] * 4096u + DRK_SECOFF);
    return n;
}

/* slot u rewritten (kit_take_core.c kt_head), written whole through the host */
static int kt_write(uint32_t u, uint32_t pad, const uint32_t *words, uint32_t slices, const drk_sample_t *s,
                    const int32_t *loop)
{
    const uint32_t *secs[DRK_KSEC];
    uint32_t nsec = kt_secs(u, secs), k, cnt[DRK_NDRUM];
    const drk_kit_t *O = nsec ? (const drk_kit_t *)(const void *)secs[0] : 0;
    const drk_sample_t *src[DRK_NDRUM];
    drk_kit_t H;
    uint32_t at = kt_head(&H, src, cnt, &k, O, secs, nsec, u, pad, slices, s, loop);
    if (!k) return kt_rec_delete(u);
    if (at * 8u > DRK_KIT_MAX) return -5;
    uint32_t page[32];
    int rc = kt_rec_begin(u, at * 8u);
    if (!rc) rc = kt_rec_write(&H, sizeof H);       /* (DRK_KIT_BYTES: no padding) */
    for (uint32_t d = 0; !rc && d < DRK_NDRUM; d++) {
        if (!src[d]) continue;
        if (d == pad) { rc = kt_rec_write(words, slices * 8u); continue; }
        for (uint32_t j = 0, f = 0; !rc && j < cnt[d]; j++) {   /* XIP into RAM: the program window reads RAM only */
            const uint32_t *p = drk_slice_at(secs, nsec, src[d]->at + j);
            page[f++] = p[0]; page[f++] = p[1];
            if (f == 32u || j + 1u == cnt[d]) { rc = kt_rec_write(page, f * 4u); f = 0; }
        }
    }
    return rc ? rc : kt_rec_commit(u);
}

/* DRUM's data records (mod_abi3.h host 4): a record it plays, a take into a kit's pad, whether a pad has one */
static int drum_rec_check(const uint32_t *const *secs, uint32_t nsec, uint32_t len) { return drk_kit_check(secs, nsec, len); }
static uint32_t drum_rec_take(uint32_t u, uint32_t pad, int16_t *x, uint32_t n, uint32_t cap)
{
    return u < DK_NUSER && pad < DRK_NDRUM ? (uint32_t)kt_keep(u, pad, x, n, cap) : (uint32_t)KM_FLASH;
}
static int drum_rec_pad(uint32_t u, uint32_t pad)
{
    const uint32_t *secs[DRK_KSEC];
    uint32_t n = kt_secs(u, secs);
    const drk_kit_t *O = n ? (const drk_kit_t *)(const void *)secs[0] : 0;
    return O && pad < DRK_NDRUM && O->map[pad].sample != 0xFFu;
}
