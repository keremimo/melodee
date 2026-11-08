/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A user kit's record (KIT 1..8, drum_real.c plays them): DRUM's data record (the firmware keeps it in the pool:
 * rec_store.c), a drk_kit_t header then the takes' slices, at most DRK_KSEC sectors. Every record is checked as the
 * player will read it (drk_kit_check: DRUM's rec_check) before the firmware lists it. */
#ifndef DRK_CODEC_ONLY                           /* (web/kit: the record's helpers alone, below) */
_Static_assert(DRK_SECBYTES == MOD3_REC_PAYLOAD && DRK_SECOFF == MOD3_REC_OFF && DK_NUSER == MOD3_NREC &&
               DRK_RUN0 * 8u == MOD3_REC_RUN0 && DRK_SLOTS * 8u == 4096u - MOD3_REC_OFF && DRK_KSEC <= MOD3_REC_SEC,
               "a user kit is a data record");
#endif
/* ------------------------------------------------------------ a user kit's record --- */
/* slice k's first word, sector by sector (0 past the payload) */
static const uint32_t *drk_slice_at(const uint32_t *const *secs, uint32_t nsec, uint32_t k)
{
    return k / DRK_SECSLICES < nsec ? secs[k / DRK_SECSLICES] + DRK_SLOT(k % DRK_SECSLICES) : 0;
}
/* a take's slices, walked by their modes (a checked record) */
static uint32_t drk_take_slices(const uint32_t *const *secs, uint32_t nsec, const drk_sample_t *s)
{
    uint32_t k = s->at, idx = 0;
    while (idx < s->n) idx += DRK_NS[drk_slice_at(secs, nsec, k++)[0] >> 30];
    return k - s->at;
}
/* everything the player trusts in a record of len payload bytes in nsec sectors: the header's fields and every
 * take's slices (walked by their modes) inside the payload, each loop where its slice starts. 0: playable */
static int drk_kit_check(const uint32_t *const *secs, uint32_t nsec, uint32_t len)
{
    const drk_kit_t *H = (const drk_kit_t *)secs[0];
    if (!nsec || nsec > DRK_KSEC || len < DRK_KIT_BYTES || len > nsec * DRK_SECBYTES || len > DRK_KIT_MAX)
        return 1;
    if (H->magic != DRK_KIT_MAGIC || H->bytes != DRK_KIT_BYTES || H->ndrum != DRK_NDRUM || H->nsmp > DRK_NDRUM)
        return 2;
    for (uint32_t i = 0, end = 0; i < sizeof H->name; i++) {
        char ch = H->name[i];
        if (end ? ch != 0 : ch == 0 ? (end = 1, i == 0) : (ch < ' ' || ch > '~' || i >= DRK_NAMELEN))
            return 3;
    }
    for (uint32_t d = 0; d < DRK_NDRUM; d++) {
        const drk_map_t *m = &H->map[d];
        if ((m->sample != 0xFFu && m->sample >= H->nsmp) || (m->alt != 0xFFu && m->alt >= H->nsmp) ||
            m->st < -24 || m->st > 24 || m->tone < -64 || m->tone > 63 || m->out > 16384u || !m->ms || m->ms > 30000u ||
            m->pivot < 20u || m->pivot > 20000u)
            return 4;
    }
    uint32_t slices = len / 8u;
    for (uint32_t i = 0; i < H->nsmp; i++) {
        const drk_sample_t *s = &H->smp[i];
        if (s->at < DRK_KIT_BYTES / 8u || !s->n || s->inc < 4096u || s->inc > 32768u || s->st >= H->nsmp ||
            (s->loop_at ? s->loop_at >= s->n : s->loop_slice != 0))
            return 5;
        uint32_t k = s->at, idx = 0, looped = !s->loop_at;
        while (idx < s->n) {
            const uint32_t *w = k < slices ? drk_slice_at(secs, nsec, k) : 0;
            if (!w)
                return 6;
            if (s->loop_at && k == (uint32_t)s->at + s->loop_slice)
                looped = idx == s->loop_at;
            idx += DRK_NS[w[0] >> 30];
            k++;
        }
        if (!looped)
            return 7;
    }
    return 0;
}
