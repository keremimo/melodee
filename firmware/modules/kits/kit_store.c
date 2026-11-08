/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A pad record (drum_kit.h: KIT 1..8's takes, drum_real.c plays them): SAMPLER's data record (the firmware keeps it in
 * the pool: rec_store.c), a drk_pad_t header then the take's slices, as many sectors as it needs. Every record is
 * checked as the player will read it (drk_pad_check: SAMPLER's rec_check) before the firmware lists it. */
#ifndef DRK_CODEC_ONLY                           /* (web/kit: the record's helpers alone, below) */
_Static_assert(DRK_SECBYTES == MOD3_REC_PAYLOAD && DRK_SECOFF == MOD3_REC_OFF && DK_NUSER == MOD3_NREC &&
               DRK_RUN0 * 8u == MOD3_REC_RUN0 && DRK_SLOTS * 8u == 4096u - MOD3_REC_OFF, "a pad record is a data record");
#endif
/* ------------------------------------------------------------ a pad record --- */
/* slice k's first word, sector by sector (secs: their 4 KB numbers; 0 past the payload) */
static const uint32_t *drk_slice_at(const uint8_t *secs, uint32_t nsec, uint32_t k)
{
    return k / DRK_SECSLICES < nsec ? DRK_SEC(secs[k / DRK_SECSLICES]) + DRK_SLOT(k % DRK_SECSLICES) : 0;
}
/* everything the player trusts in a record of len payload bytes in nsec sectors: the header's fields and every slice
 * of the take (walked by their modes) inside the payload, the loop where its slice starts. 0: playable */
static int drk_pad_check(const uint8_t *secs, uint32_t nsec, uint32_t len)
{
    if (!nsec)
        return 1;
    const drk_pad_t *H = (const drk_pad_t *)(const void *)DRK_SEC(secs[0]);
    if (len < DRK_PAD_BYTES || len > nsec * DRK_SECBYTES || len <= (nsec - 1u) * DRK_SECBYTES || (len & 7u))
        return 1;
    if (H->magic != DRK_PAD_MAGIC || H->bytes != DRK_PAD_BYTES || H->pad >= DRK_NPAD || H->codec != DRK_CODEC_LMS)
        return 2;
    if (!H->n || H->inc < 4096u || H->inc > 32768u || !H->gain || H->st < -24 || H->st > 24 ||
        H->tone < -64 || H->tone > 63 || H->out > 16384u || H->ms > 30000u || H->pivot < 20u || H->pivot > 20000u ||
        (H->loop_at ? H->loop_at >= H->n : H->loop_slice != 0))
        return 4;
    uint32_t slices = len / 8u, k = DRK_FIRST, idx = 0, looped = !H->loop_at;
    while (idx < H->n) {
        const uint32_t *w = k < slices ? drk_slice_at(secs, nsec, k) : 0;
        if (!w)
            return 6;
        if (H->loop_at && k == DRK_FIRST + H->loop_slice)
            looped = idx == H->loop_at;
        idx += DRK_NS[w[0] >> 30];
        k++;
    }
    return looped ? 0 : 7;
}
