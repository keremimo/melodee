/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A drum take into a user kit (kit_rec_ui.c drives it; usb_audio_stream.c kt_hear records it): processed and encoded
 * in place by kit_take_core.c (kt_keep, which the web's kit maker runs too), then the kit rewritten whole in the flash
 * here (the other pads' slices copied from the old record, which stays current until the new one commits). */
#include "drum_enc.c"
#include "kit_take_core.c"

/* slot u's record as the player reads it (0 sectors: none) */
static uint32_t kt_secs(uint32_t u, const uint32_t *secs[DRK_KSEC])
{
    uint32_t n = u < ST_NKIT && !(drk_user_gen & 1u) ? drk_user[u].n : 0u;
    for (uint32_t k = 0; k < n; k++)
        secs[k] = (const uint32_t *)(const void *)DRK_XIP(drk_user[u].sec[k] * 4096u + DRK_SECOFF);
    return n;
}

/* slot u rewritten (kit_take_core.c kt_head), streamed into the flash */
static int kt_write(uint32_t u, uint32_t pad, const uint32_t *words, uint32_t slices, const drk_sample_t *s,
                    const int32_t *loop)
{
    const uint32_t *secs[DRK_KSEC];
    uint32_t nsec = kt_secs(u, secs), k, cnt[DRK_NDRUM];
    const drk_kit_t *O = nsec ? (const drk_kit_t *)(const void *)secs[0] : 0;
    const drk_sample_t *src[DRK_NDRUM];
    drk_kit_t H;
    uint32_t at = kt_head(&H, src, cnt, &k, O, secs, nsec, u, pad, slices, s, loop);
    if (!k) return kit_delete(u);
    if (at * 8u > DRK_KIT_MAX) return -5;
    st_stream_t w;
    uint32_t page[32];
    int rc = kit_put_begin(&w, u, at * 8u);
    if (!rc) rc = kit_put_write(&w, &H, sizeof H);    /* (DRK_KIT_BYTES: no padding) */
    for (uint32_t d = 0; !rc && d < DRK_NDRUM; d++) {
        if (!src[d]) continue;
        if (d == pad) { rc = kit_put_write(&w, words, slices * 8u); continue; }
        for (uint32_t j = 0, f = 0; !rc && j < cnt[d]; j++) {   /* XIP into RAM: the program window reads RAM only */
            const uint32_t *p = drk_slice_at(secs, nsec, src[d]->at + j);
            page[f++] = p[0]; page[f++] = p[1];
            if (f == 32u || j + 1u == cnt[d]) { rc = kit_put_write(&w, page, f * 4u); f = 0; }
        }
    }
    return rc ? rc : kit_put_commit(&w, u);
}
