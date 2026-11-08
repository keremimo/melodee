/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Data records (mod_abi3.h host 4): blobs in the pool that a synth module owns (its pads block names their slots: a
 * pads synth's recorded kits), pool records of type ST_RFILE0 + slot (storage.c), at most rsec sectors. Written whole,
 * copy-on-write as projects are (the old record stays current until the new one commits). What a record holds is its
 * module's: the firmware lists a record for the audio (rec_tab, pads.h) only once its module checked it
 * (engine_t.rec_check), a stored one when its module is in SRAM (rec_recheck: the boot finds them before any module is
 * loaded). After every write or delete the main loop waits until the audio has left any sectors it no longer owns, so
 * that a later save may erase them. */
#define REC_STORE 1                              /* (project.c: the boot scan; editor_backup.c: objects 92..99) */
_Static_assert(MOD3_REC_PAYLOAD == ST_LIN_PAYLOAD_MAX && MOD3_REC_OFF == ST_POOL_PAYLOAD_OFF && MOD3_NREC == ST_NREC &&
               MOD3_REC_RUN0 == ST_RUN0 && ST_PROJECT_SECTORS <= MOD3_REC_SEC, "a data record is a pool record");
static uint16_t rec_len[ST_NREC];                /* each slot's payload bytes as rec_tab lists it (0: none) */
static uint8_t rec_pend;                         /* slots whose stored record waits for its module's check */
static uint32_t rec_obj(uint32_t u) { return ST_RFILE0 + u; }
static uint64_t rec_put_owner;                    /* the record being written: its owner's ID (rec_put_begin) */
/* slot u's record's bounds (its owner's pads): its sectors at most (0: no owner), its bytes at least */
static uint32_t rec_bounds(uint32_t u, uint32_t *rmin)
{
    const pads_c_t *c = rec_owner(u);
    uint8_t b[MOD_SCH_PDHEAD];
    uint32_t wid;
    *rmin = 1;
    if (!c || pads_head(c->eng, b, &wid))
        return 0;
    *rmin = b[28] | (uint32_t)b[29] << 8;
    return b[27] < ST_PROJECT_SECTORS ? b[27] : ST_PROJECT_SECTORS;
}
/* a record's bytes at most (0: no synth has records), whether len bytes may be one */
static uint32_t rec_max(void) { uint32_t rmin; return rec_bounds(MOD3_NREC, &rmin) * MOD3_REC_PAYLOAD; }
static int rec_len_ok(uint32_t len) { uint32_t rmin, max = rec_bounds(MOD3_NREC, &rmin); return len >= rmin && len <= max * MOD3_REC_PAYLOAD; }
/* the table's slot u: n sectors (4 KB numbers), its number odd meanwhile */
static void rec_table_set(uint32_t u, const uint8_t *sec, uint32_t n)
{
    rec_gen++;
    __asm__ volatile("" ::: "memory");
    rec_tab[u].n = 0;
    for (uint32_t k = 0; k < n; k++)
        rec_tab[u].sec[k] = sec[k];
    rec_tab[u].n = (uint8_t)n;
    __asm__ volatile("" ::: "memory");
    rec_gen++;
}
/* two audio blocks: every part has seen the table's new number and left sectors it no longer owns. Main loop only,
 * the clock running (never at boot: persist_boot runs before TIMER5 counts fm1_ms, and no part plays yet) */
static void rec_settle(void)
{
#ifdef FM1_IRQ_TARGET
    uint32_t t0 = fm1_ms;
    for (uint32_t spin = 0; fm1_ms - t0 < 15u && spin < 20000000u; spin++)   /* (bounded: a stopped clock never hangs) */
        fm1_wdt_feed();
#endif
}
/* a record's sectors as the audio reads them; their count (0: none, too many, or not a list record: an old form's
 * 3,808-byte sectors would read wrong, so a record the converter could not move stays unplayed) */
static uint32_t rec_sectors(uint32_t u, const st_hdr_t *h, uint8_t sec[MOD3_REC_SEC], const uint32_t *p[MOD3_REC_SEC])
{
    uint8_t idx[ST_PROJECT_SECTORS];
    uint32_t rmin, max = rec_bounds(u, &rmin), n = st_rec_lin(h) ? st_rec_sectors(h, idx) : 0u;
    if (n > max || h->len < rmin)
        return 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t at = st_rec_addr(h, idx[k]);
        sec[k] = (uint8_t)(at >> 12);
        p[k] = (const uint32_t *)(const void *)(REC_BASE + at + ST_POOL_PAYLOAD_OFF);
    }
    return n;
}
/* its module's word on a record: 0 it plays it, 1 not, -1 no module in SRAM to ask */
static int rec_checked(uint32_t u, const uint32_t *const *p, uint32_t n, uint32_t len)
{
    const pads_c_t *c = rec_owner(u);
    const engine_t *g = c ? ENGINES[c->eng] : 0;
    if (!g || mod_idle(c->eng) || !g->rec_check)
        return -1;
    return MOD_MAIN(c->eng, g->rec_check(p, n, len)) ? 1 : 0;
}
/* slot u's current record into the table if its module trusts it (else none; not yet asked: rec_pend); 1 listed */
static int rec_scan_slot(uint32_t u)
{
    st_hdr_t h;
    uint8_t sec[MOD3_REC_SEC];
    const uint32_t *p[MOD3_REC_SEC];
    int copy = st_current(rec_obj(u), &h), ok;
    uint32_t n = copy >= 2 ? rec_sectors(u, &h, sec, p) : 0u;
    /* (a stored record whose owner no listed synth is yet waits for it: a module written after power-on) */
    ok = n ? rec_checked(u, p, n, h.len) : copy >= 2 && !rec_owner(u) ? -1 : 1;
    rec_pend = (uint8_t)(ok < 0 ? rec_pend | 1u << u : rec_pend & ~(1u << u));
    if (ok)
        n = 0;
    rec_len[u] = (uint16_t)(n ? h.len : 0u);
    if (n != rec_tab[u].n || memcmp(sec, rec_tab[u].sec, n))
        rec_table_set(u, sec, n);
    return n != 0;
}
/* boot (persist_boot): nothing plays yet, so no wait */
static void rec_hooks(void);
static void rec_scan(void)
{
    rec_hooks();
    for (uint32_t u = 0; u < ST_NREC; u++)
        (void)rec_scan_slot(u);
}
/* the records waiting for their module's check, once it is in SRAM (eng_module_main.c: a module registered; host
 * builds: their built-in engines answer at once). Records only join the table: no wait */
static void rec_recheck(void)
{
    for (uint32_t u = 0; rec_pend && u < ST_NREC; u++)
        if (rec_pend >> u & 1u)
            (void)rec_scan_slot(u);
}
/* slot u holds a record the audio does not list (its synth refused it, or has not checked it yet): Record kit's sheet
 * writes a kit whole from the listed one, so a take or a cleared pad there would write over it with an empty kit */
static int rec_hidden(uint32_t u)
{
    st_hdr_t h;
    return u < ST_NREC && !rec_tab[u].n && st_current(rec_obj(u), &h) >= 0;
}
/* slot u emptied: the audio leaves it first, then every copy's header goes (storage.c) */
static int rec_delete(uint32_t u)
{
    if (u >= ST_NREC)
        return -1;
    rec_table_set(u, 0, 0);
    rec_len[u] = 0;
    rec_settle();
    int rc = st_project_delete(rec_obj(u));
    (void)rec_scan_slot(u);
    return rc;
}
/* A whole record into slot u: begin with its length, write its bytes in order (RAM), commit. The stage is scratch until
 * commit; any other save meanwhile makes it stale (-6). -1: not a record its module could hold */
static int rec_put_begin(st_stream_t *w, uint32_t u, uint32_t len)
{
    uint32_t rmin, max;
    uint64_t owner = mod_running_uid();             /* (a module's call: its own record; else the slot's) */
    if (u >= ST_NREC)
        return -1;
    rec_put_owner = owner ? owner : rec_owner_uid(u);
    {
        uint64_t keep = rec_uid[u];
        rec_uid[u] = rec_put_owner;                      /* (its bounds: the writer's pads) */
        max = rec_bounds(u, &rmin);
        rec_uid[u] = keep;
    }
    if (len < rmin || len > max * MOD3_REC_PAYLOAD)
        return -1;
    return st_stream_begin(w, rec_obj(u), len);
}
static int rec_put_write(st_stream_t *w, const void *src, uint32_t n) { return st_stream_write(w, src, n); }
/* the record as the audio reads it checked by its module (none in SRAM: stored, listed once it checks), then
 * published; -2: not a record its module plays */
static int rec_put_commit(st_stream_t *w, uint32_t u)
{
    uint8_t sec[MOD3_REC_SEC];
    const uint32_t *p[MOD3_REC_SEC];
    int rc = st_stream_check(w);
    if (rc)
        return rc;
    uint64_t keep = rec_uid[u];
    uint32_t n;
    rec_uid[u] = rec_put_owner;                     /* (checked by the writer's module) */
    n = rec_sectors(u, &w->head, sec, p);
    if (!n || rec_checked(u, p, n, w->head.len) > 0 || (rc = st_stream_publish(w)) != 0) {
        rec_uid[u] = keep;
        return rc ? rc : -2;
    }
    if (rec_uid[u] != keep)
        eng_map_dirty = 1;                          /* (its owner kept in the owners record) */
    (void)rec_scan_slot(u);
    rec_settle();
    return 0;
}
/* the host's calls (mod_abi3.h host 4, pads.h rec_hook): one record written at a time, a module's (Record kit) */
static st_stream_t rec_w;
static int rec_h_begin(uint32_t slot, uint32_t len) { return rec_put_begin(&rec_w, slot, len); }
static int rec_h_write(const void *src, uint32_t n) { return rec_put_write(&rec_w, src, n); }
static int rec_h_commit(uint32_t slot) { return rec_put_commit(&rec_w, slot); }
static void rec_hooks(void)
{
    rec_hook.begin = rec_h_begin;
    rec_hook.write = rec_h_write;
    rec_hook.commit = rec_h_commit;
    rec_hook.del = rec_delete;
    rec_hook.recheck = rec_recheck;
}
