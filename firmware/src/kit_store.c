/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The user's drum kits (KIT 1..8, drum_real.c plays them): pool records of type ST_KFILE0 + slot (storage.c), a
 * drk_kit_t header then the takes' slices, at most DRK_KSEC sectors. Written whole, copy-on-write as projects are
 * (the old record stays current until the new one commits); every write is checked as the player will read it
 * (drk_kit_check) before it is published. The main loop keeps drk_user[] (the player's sector table) in step:
 * at boot and after every write or delete, then waits until the audio side has left any sectors it no longer owns,
 * so that a later save may erase them. */
#define KIT_STORE 1                              /* (project.c: the boot scan) */
_Static_assert(DRK_SECBYTES == ST_POOL_PAYLOAD_MAX && DRK_SECOFF == ST_POOL_PAYLOAD_OFF && DK_NUSER == ST_NKIT &&
               DRK_KSEC <= ST_PROJECT_SECTORS && DRK_KIT_MAX <= ST_PROJECT_MAX, "a user kit is a pool record");
/* ------------------------------------------------------------ a user kit's record --- */
/* slice k's first word, sector by sector (0 past the payload) */
static const uint32_t *drk_slice_at(const uint32_t *const *secs, uint32_t nsec, uint32_t k)
{
    return k / DRK_SECSLICES < nsec ? secs[k / DRK_SECSLICES] + 2u * (k % DRK_SECSLICES) : 0;
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
static uint16_t kit_len[ST_NKIT];                /* each slot's payload bytes (0: empty), as drk_user[] has it */
static uint32_t kit_obj(uint32_t u) { return ST_KFILE0 + u; }

/* the player's table for slot u: n sectors (4 KB numbers), the number odd meanwhile */
static void kit_table_set(uint32_t u, const uint8_t *sec, uint32_t n)
{
    drk_user_gen++;
    __asm__ volatile("" ::: "memory");
    drk_user[u].n = 0;
    for (uint32_t k = 0; k < n; k++)
        drk_user[u].sec[k] = sec[k];
    drk_user[u].n = (uint8_t)n;
    __asm__ volatile("" ::: "memory");
    drk_user_gen++;
}
/* two audio blocks: every part has seen the table's new number and left sectors it no longer owns. Main loop only,
 * the clock running (never at boot: persist_boot runs before TIMER5 counts fm1_ms, and no part plays yet) */
static void kit_settle(void)
{
#ifdef FM1_IRQ_TARGET
    uint32_t t0 = fm1_ms;
    for (uint32_t spin = 0; fm1_ms - t0 < 15u && spin < 20000000u; spin++)   /* (bounded: a stopped clock never hangs) */
        fm1_wdt_feed();
#endif
}
/* a record's sectors as the player reads them; their count (0: none, or not a pool record) */
static uint32_t kit_sectors(const st_hdr_t *h, uint8_t sec[DRK_KSEC], const uint32_t *p[DRK_KSEC])
{
    uint8_t idx[ST_PROJECT_SECTORS];
    uint32_t n = st_rec_sectors(h, idx);
    if (n > DRK_KSEC)
        return 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t at = st_pool_at(idx[k]);
        sec[k] = (uint8_t)(at >> 12);
        p[k] = (const uint32_t *)(const void *)DRK_XIP(at + ST_POOL_PAYLOAD_OFF);
    }
    return n;
}
/* slot u's current record into the table, if the player can trust it; 1: a kit is there */
static int kit_scan_slot(uint32_t u)
{
    st_hdr_t h;
    uint8_t sec[DRK_KSEC];
    const uint32_t *p[DRK_KSEC];
    int copy = st_current(kit_obj(u), &h);
    uint32_t n = copy >= 2 ? kit_sectors(&h, sec, p) : 0u;
    if (n && drk_kit_check(p, n, h.len))
        n = 0;
    kit_len[u] = (uint16_t)(n ? h.len : 0u);
    if (n != drk_user[u].n || memcmp(sec, drk_user[u].sec, n))
        kit_table_set(u, sec, n);
    return n != 0;
}
/* boot (persist_boot): nothing plays yet, so no wait */
static void kit_scan(void)
{
    for (uint32_t u = 0; u < ST_NKIT; u++)
        kit_scan_slot(u);
}
/* slot u emptied: the player leaves it first, then every copy's header goes (storage.c) */
static int kit_delete(uint32_t u)
{
    if (u >= ST_NKIT)
        return -1;
    kit_table_set(u, 0, 0);
    kit_len[u] = 0;
    kit_settle();
    int rc = st_project_delete(kit_obj(u));
    kit_scan_slot(u);
    return rc;
}

/* A whole record into slot u: begin with its length, write its bytes in order (RAM), commit. The stage is scratch
 * until commit; any other save meanwhile makes it stale (-6). */
static int kit_put_begin(st_stream_t *w, uint32_t u, uint32_t len)
{
    if (u >= ST_NKIT || len < DRK_KIT_BYTES || len > DRK_KIT_MAX)
        return -1;
    return st_stream_begin(w, kit_obj(u), len);
}
static int kit_put_write(st_stream_t *w, const void *src, uint32_t n) { return st_stream_write(w, src, n); }
/* the CRC (the caller's sum of every byte, -1 for none) and the record as the player reads it, then published;
 * -2: not a kit the player could play */
static int kit_put_commit(st_stream_t *w, uint32_t u)
{
    uint8_t sec[DRK_KSEC];
    const uint32_t *p[DRK_KSEC];
    int rc = st_stream_check(w);
    if (rc)
        return rc;
    uint32_t n = kit_sectors(&w->head, sec, p);
    if (!n || drk_kit_check(p, n, w->head.len))
        return -2;
    if ((rc = st_stream_publish(w)) != 0)
        return rc;
    kit_scan_slot(u);
    kit_settle();
    return 0;
}
