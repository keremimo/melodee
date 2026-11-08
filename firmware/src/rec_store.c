/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Data records (mod_abi3.h host 6): the kits a pads synth records (SAMPLER's KIT 1..8; its pads block names their
 * slots). A kit is up to MOD3_RPAD pad records, one take each, pool records of type ST_SFILE0 + kit * 16 + pad of any
 * length up to the whole free store (storage.c st_long), and a name record (ST_RFILE0 + kit), made only when the kit is
 * named. Written whole, copy-on-write as projects are (the old record stays current until the new one commits), or as
 * a take streams in (rec_take_*: its sectors erased before the take starts, its header written last). What a pad
 * record holds is its module's: the firmware lists one for the audio (rec_tab, rec_list: pads.h) only once its module
 * checked it (engine_t.rec_check), a stored one when its module is in SRAM (rec_recheck: the boot finds them before any
 * module is loaded). After every write or delete the main loop waits until the audio has left any sectors it no
 * longer owns, so that a later save may erase them. Outside the device a kit is one blob (the editor's backup objects
 * 92..99, a .mel file): a head (REC_BOX_*: magic, name, each pad's length) then the pad records in pad order. */
#define REC_STORE 1                              /* (project.c: the boot scan; editor_backup.c: objects 92..99) */
_Static_assert(MOD3_REC_PAYLOAD == ST_LIN_PAYLOAD_MAX && MOD3_REC_OFF == ST_POOL_PAYLOAD_OFF && MOD3_NREC == ST_NREC &&
               MOD3_REC_RUN0 == ST_RUN0 && MOD3_RPAD == ST_RPAD && MOD3_NSMP == ST_NSMP &&
               ST_LONG_SECTORS <= MOD3_REC_LIST, "a data record is a pool record");
#define REC_BOX_MAGIC 0x3254494Bu                /* KIT2 */
#define REC_BOX_HEAD 88u
#define REC_NAME_MAX 32u                         /* a name record's bytes read, at most */
static uint32_t rec_r32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void rec_w32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint16_t rec_pend;                        /* kits with a stored pad record waiting for its module's check */
static uint64_t rec_put_owner;                   /* the record being written: its owner's ID (rec_put_begin) */
static uint32_t rec_name_obj(uint32_t u) { return ST_RFILE0 + u; }
static uint32_t rec_pad_obj(uint32_t s) { return ST_SFILE0 + s; }

/* kit u's pad records' bounds (its owner's pads): their sectors at most (0: no owner), bytes at least, the kit's pads */
static uint32_t rec_bounds(uint32_t u, uint32_t *rmin, uint32_t *rpad)
{
    const pads_c_t *c = rec_owner(u);
    uint8_t b[MOD_SCH_PDHEAD];
    uint32_t wid;
    *rmin = 1; *rpad = 0;
    if (!c || pads_head(c->eng, b, &wid))
        return 0;
    *rmin = b[28] | (uint32_t)b[29] << 8;
    *rpad = b[30] < MOD3_RPAD ? b[30] : MOD3_RPAD;
    return b[27] && b[27] < ST_LONG_SECTORS ? b[27] : ST_LONG_SECTORS;
}
/* the name's place in a name record (its owner's pads: rname, rnlen), 0 none */
static uint32_t rec_name_at(uint32_t u, uint32_t *len)
{
    const pads_c_t *c = rec_owner(u);
    uint8_t b[MOD_SCH_PDHEAD];
    uint32_t wid, off;
    *len = 0;
    if (!c || pads_head(c->eng, b, &wid))
        return 0;
    off = b[24] | (uint32_t)b[25] << 8;
    *len = b[26] < sizeof rec_nm[0] ? b[26] : sizeof rec_nm[0] - 1u;
    return off + *len <= REC_NAME_MAX ? off : (*len = 0, 0u);
}
/* a pad record's bytes at most (0: no synth has records), whether len bytes may be one */
static uint32_t rec_max(void) { uint32_t rmin, rpad; return rec_bounds(MOD3_NREC, &rmin, &rpad) * MOD3_REC_PAYLOAD; }
static int rec_len_ok(uint32_t len)
{
    uint32_t rmin, rpad, max = rec_bounds(MOD3_NREC, &rmin, &rpad);
    return len >= rmin && len <= max * MOD3_REC_PAYLOAD;
}
/* the table's pad record s: n sectors (4 KB numbers), its number odd meanwhile. The list holds the listed records'
 * sectors one run after another: s's old run goes, the runs after it move down, the new one goes last */
static void rec_table_set(uint32_t s, const uint8_t *sec, uint32_t n)
{
    uint32_t total = 0, a = rec_tab[s].at, m = rec_tab[s].n;
    for (uint32_t t = 0; t < MOD3_NSMP; t++)
        if (rec_tab[t].n && rec_tab[t].at + rec_tab[t].n > total) total = rec_tab[t].at + rec_tab[t].n;
    rec_gen++;
    __asm__ volatile("" ::: "memory");
    if (m) {
        for (uint32_t i = a; i + m < total; i++) rec_list[i] = rec_list[i + m];
        for (uint32_t t = 0; t < MOD3_NSMP; t++)
            if (rec_tab[t].n && rec_tab[t].at > a) rec_tab[t].at = (uint8_t)(rec_tab[t].at - m);
        total -= m;
        rec_tab[s].n = rec_tab[s].at = 0;
    }
    if (n && total + n <= MOD3_REC_LIST) {
        for (uint32_t k = 0; k < n; k++) rec_list[total + k] = sec[k];
        rec_tab[s].at = (uint8_t)total;
        rec_tab[s].n = (uint8_t)n;
    }
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
/* a pad record's sectors as the audio reads them (4 KB numbers); their count (0: none, too many, or not a list record) */
static uint32_t rec_sectors(uint32_t s, const st_hdr_t *h, uint8_t sec[ST_REC_MAX])
{
    uint8_t idx[ST_REC_MAX];
    uint32_t rmin, rpad, max = rec_bounds(s / MOD3_RPAD, &rmin, &rpad), n = st_rec_lin(h) ? st_rec_sectors(h, idx) : 0u;
    if (n > max || h->len < rmin || s % MOD3_RPAD >= rpad)
        return 0;
    for (uint32_t k = 0; k < n; k++)
        sec[k] = (uint8_t)(st_rec_addr(h, idx[k]) >> 12);
    return n;
}
/* its module's word on a pad record of kit u: 0 it plays it, 1 not, -1 no module in SRAM to ask */
static int rec_checked(uint32_t u, const uint8_t *sec, uint32_t n, uint32_t len)
{
    const pads_c_t *c = rec_owner(u);
    const engine_t *g = c ? ENGINES[c->eng] : 0;
    if (!g || mod_idle(c->eng) || !g->rec_check)
        return -1;
    return MOD_MAIN(c->eng, g->rec_check(sec, n, len)) ? 1 : 0;
}
/* pad record s's current record into the table if its module trusts it (else none; not yet asked: rec_pend); 1 listed */
static int rec_scan_pad(uint32_t s)
{
    st_hdr_t h;
    uint8_t sec[ST_REC_MAX];
    uint32_t u = s / MOD3_RPAD;
    int copy = st_current(rec_pad_obj(s), &h), ok;
    uint32_t n = copy >= 0 ? rec_sectors(s, &h, sec) : 0u;
    /* (a stored record whose owner no listed synth is yet waits for it: a module written after power-on) */
    ok = n ? rec_checked(u, sec, n, h.len) : copy >= 0 && !rec_owner(u) ? -1 : 1;
    if (ok < 0) rec_pend |= (uint16_t)(1u << u);
    if (ok)
        n = 0;
    if (n != rec_tab[s].n || (n && memcmp(sec, &rec_list[rec_tab[s].at], n)))
        rec_table_set(s, sec, n);
    return n != 0;
}
/* kit u's name record into the names the lists show (none: "") */
static void rec_scan_name(uint32_t u)
{
    uint32_t len, at = rec_name_at(u, &len), k = 0;
    uint8_t b[REC_NAME_MAX];
    st_hdr_t h;
    int copy = st_current(rec_name_obj(u), &h);
    if (copy >= 0 && len && at + len <= h.len && !st_read_range(rec_name_obj(u), (uint32_t)copy, 0, b, at + len))
        for (; k < len && b[at + k] >= ' ' && b[at + k] <= '~'; k++) rec_nm[u][k] = (char)b[at + k];
    for (; k < sizeof rec_nm[u]; k++) rec_nm[u][k] = 0;
}
/* kit u: its pads and its name (its pend bit set again by any pad still waiting) */
static void rec_scan_kit(uint32_t u)
{
    rec_pend &= (uint16_t)~(1u << u);
    for (uint32_t p = 0; p < MOD3_RPAD; p++) (void)rec_scan_pad(u * MOD3_RPAD + p);
    rec_scan_name(u);
}
/* boot (persist_boot): nothing plays yet, so no wait */
static void rec_hooks(void);
static void rec_scan(void)
{
    rec_hooks();
    for (uint32_t u = 0; u < ST_NREC; u++)
        rec_scan_kit(u);
}
/* the records waiting for their module's check, once it is in SRAM (eng_module_main.c: a module registered; host
 * builds: their built-in engines answer at once). Records only join the table: no wait */
static void rec_recheck(void)
{
    for (uint32_t u = 0; rec_pend && u < ST_NREC; u++)
        if (rec_pend >> u & 1u)
            rec_scan_kit(u);
}
/* pad record s is stored (listed or not) */
static int rec_pad_stored(uint32_t s)
{
    st_hdr_t h;
    return s < ST_NSMP && st_current(rec_pad_obj(s), &h) >= 0;
}
/* kit u holds anything: a name, a pad record (listed or not: one its synth refused or has not checked is still the
 * kit's); 2: a pad record the audio does not list */
static int rec_used(uint32_t u)
{
    st_hdr_t h;
    int any = u < ST_NREC && st_current(rec_name_obj(u), &h) >= 0;
    for (uint32_t p = 0; u < ST_NREC && p < MOD3_RPAD; p++) {
        uint32_t s = u * MOD3_RPAD + p;
        if (rec_tab[s].n) any = 1;
        else if (rec_pad_stored(s)) return 2;
    }
    return any;
}
/* pad record s stored but not listed (its synth refused it, or has not checked it yet): Record kit's take or its
 * Clear pad there would write over a record the audio never showed */
static int rec_hidden(uint32_t s) { return s < ST_NSMP && !rec_tab[s].n && rec_pad_stored(s); }
/* pad record s emptied: the audio leaves it first, then every copy's header goes (storage.c) */
static int rec_delete_pad(uint32_t s)
{
    if (s >= ST_NSMP)
        return -1;
    if (rec_tab[s].n) {
        rec_table_set(s, 0, 0);
        rec_settle();
    }
    int rc = st_project_delete(rec_pad_obj(s));
    (void)rec_scan_pad(s);
    return rc;
}
/* kit u emptied: its pads, then its name */
static int rec_delete(uint32_t u)
{
    int rc = 0;
    if (u >= ST_NREC)
        return -1;
    for (uint32_t p = 0; p < MOD3_RPAD; p++) {
        int r = rec_delete_pad(u * MOD3_RPAD + p);
        if (r && !rc) rc = r;
    }
    int r = st_project_delete(rec_name_obj(u));
    rec_scan_name(u);
    return rc ? rc : r;
}
/* the writer's ID as the owner of kit u (a module's call: its own; else the kit's), its bounds checked against it */
static uint32_t rec_owner_bounds(uint32_t u, uint32_t *rmin, uint32_t *rpad)
{
    uint64_t owner = mod_running_uid(), keep = rec_uid[u];
    uint32_t max;
    rec_put_owner = owner ? owner : rec_owner_uid(u);
    rec_uid[u] = rec_put_owner;                      /* (its bounds: the writer's pads) */
    max = rec_bounds(u, rmin, rpad);
    rec_uid[u] = keep;
    return max;
}
/* A whole pad record into s: begin with its length, write its bytes in order (RAM), commit. The stage is scratch until
 * commit; any other save meanwhile makes it stale (-6). -1: not a record its module could hold */
static int rec_put_begin(st_stream_t *w, uint32_t s, uint32_t len)
{
    uint32_t rmin, rpad, max;
    if (s >= ST_NSMP)
        return -1;
    max = rec_owner_bounds(s / MOD3_RPAD, &rmin, &rpad);
    if (len < rmin || len > max * MOD3_REC_PAYLOAD || s % MOD3_RPAD >= rpad)
        return -1;
    return st_stream_begin(w, rec_pad_obj(s), len);
}
static int rec_put_write(st_stream_t *w, const void *src, uint32_t n) { return st_stream_write(w, src, n); }
/* the record as the audio reads it checked by its module (none in SRAM: stored, listed once it checks), then
 * published; -2: not a record its module plays */
static int rec_put_commit(st_stream_t *w, uint32_t s)
{
    uint8_t sec[ST_REC_MAX];
    uint32_t u = s / MOD3_RPAD, n;
    int rc = st_stream_check(w);
    if (rc)
        return rc;
    uint64_t keep = rec_uid[u];
    rec_uid[u] = rec_put_owner;                     /* (checked by the writer's module) */
    n = rec_sectors(s, &w->head, sec);
    if (!n || rec_checked(u, sec, n, w->head.len) > 0 || (rc = st_stream_publish(w)) != 0) {
        rec_uid[u] = keep;
        return rc ? rc : -2;
    }
    if (rec_uid[u] != keep)
        eng_map_dirty = 1;                          /* (its owner kept in the owners record) */
    (void)rec_scan_pad(s);
    rec_settle();
    return 0;
}
/* kit u's name (n characters, 0: none): its name record written, or deleted */
static int rec_put_name(uint32_t u, const char *name, uint32_t n)
{
    uint8_t b[REC_NAME_MAX];
    uint32_t len, at = u < ST_NREC ? rec_name_at(u, &len) : 0u;
    int rc;
    if (u >= ST_NREC || !len)
        return -1;
    if (!n || !name[0]) {
        rc = st_project_delete(rec_name_obj(u));
    } else {
        memset(b, 0, sizeof b);
        b[0] = 'K'; b[1] = 'I'; b[2] = 'T'; b[3] = 'N';                       /* (SAMPLER's drk_name_t magic) */
        for (uint32_t k = 0; k < n && k < len && name[k]; k++) b[at + k] = (uint8_t)name[k];
        rc = st_project_save(rec_name_obj(u), b, at + len < 20u ? 20u : at + len);
    }
    rec_scan_name(u);
    return rc;
}

/* ---- a take streamed into pad record s as it is recorded (Record kit, rec_ui.c; the synth writes through the host's
 * take calls): one at a time */
static st_stream_t rec_w;
static uint8_t rec_take_on;
static uint32_t rec_take_slot;
/* the free store made ready for it (every free sector erased now, none while the take is recorded): its room in bytes,
 * -n none (-5 the store is full) */
static int rec_take_begin(uint32_t s)
{
    uint32_t rmin, rpad, max;
    int rc;
    rec_take_on = 0;
    if (s >= ST_NSMP)
        return -1;
    max = rec_owner_bounds(s / MOD3_RPAD, &rmin, &rpad);
    if (!max || s % MOD3_RPAD >= rpad || (rc = st_stream_begin_lazy(&rec_w, rec_pad_obj(s))) != 0)
        return -1;
    rc = st_stream_prepare(&rec_w, max);
    if (rc < 0)
        return rc;
    if (rc < (int)(rmin + 256u))
        return -5;
    rec_take_on = 1;
    rec_take_slot = s;
    return rc;
}
static int rec_take_write(uint32_t off, const void *src, uint32_t n)
{
    if (!rec_take_on)
        return -1;
    int rc = st_stream_write_at(&rec_w, off, src, n);
    return rc == -5 || rc == -1 ? -5 : rc;
}
static void rec_take_abort(void)
{
    rec_take_on = 0;
    if (st_stream_open == rec_w.generation) st_stream_open = 0;
}
/* the take's len bytes: their CRC read back, its module's check, published */
static int rec_take_end(uint32_t s, uint32_t len)
{
    int rc;
    if (!rec_take_on || s != rec_take_slot)
        return -1;
    rec_take_on = 0;
    if ((rc = st_stream_end(&rec_w, len)) != 0)
        return rc;
    {
        uint32_t crc = 0xFFFFFFFFu;                 /* (written in pieces, the header last: the CRC from the flash) */
        for (uint32_t off = 0; off < len; off += sizeof st_buf) {
            uint32_t k = len - off < sizeof st_buf ? len - off : sizeof st_buf;
            if (st_stream_read(&rec_w, off, st_buf, k)) return -7;
            crc = st_crc32_update(crc, st_buf, k);
            if (!(off & 0xFFFFu)) ST_IDLE();
        }
        st_stream_seal(&rec_w, crc);
    }
    return rec_put_commit(&rec_w, s);
}

/* ---- a kit as one blob (the editor's backup objects 92..99, a .mel file): REC_BOX_HEAD bytes (SAMPLER's drk_box_t:
 * magic, its head's bytes, npad, the name at 8, each pad's length at 20 + 4 p), then the pad records in pad order */
/* One kit's blob at a time, read (rec_box_open: the editor's LIST and GET) or written (rec_box_put_*: PUT, the web's
 * core): its head, each pad record's copy (read), where the writing stands (written) */
static struct {
    uint8_t head[REC_BOX_HEAD];
    uint16_t copy[MOD3_RPAD];
    uint32_t u, len, pos, pad, at, err, gen;
    uint8_t open;
    st_stream_t *w;
} rec_box;
/* kit u's blob opened for reading (kept while the store is as it was: st_pool_gen); its length, 0 an empty kit. Every
 * stored pad record goes in, listed or not */
static uint32_t rec_box_open(uint32_t u)
{
    uint32_t total = 0, any = 0;
    if (u >= ST_NREC) return 0;
    if (rec_box.open && rec_box.u == u && rec_box.gen == st_pool_gen) return rec_box.len;
    memset(&rec_box, 0, sizeof rec_box);
    rec_w32(rec_box.head, REC_BOX_MAGIC);
    rec_box.head[4] = (uint8_t)REC_BOX_HEAD;
    rec_box.head[6] = (uint8_t)MOD3_RPAD;
    for (uint32_t k = 0; k < sizeof rec_nm[u] && rec_nm[u][k]; k++) { rec_box.head[8u + k] = (uint8_t)rec_nm[u][k]; any = 1; }
    for (uint32_t p = 0; p < MOD3_RPAD; p++) {
        st_hdr_t h;
        int copy = st_current(rec_pad_obj(u * MOD3_RPAD + p), &h);
        uint32_t len = copy >= 0 ? h.len : 0u;
        rec_box.copy[p] = (uint16_t)(copy >= 0 ? copy : 0);
        rec_w32(rec_box.head + 20u + 4u * p, len);
        total += len;
        any |= len != 0u;
    }
    rec_box.u = u; rec_box.gen = st_pool_gen; rec_box.open = 1;
    return rec_box.len = any ? REC_BOX_HEAD + total : 0u;
}
/* n bytes of the open blob from off into dst; 0 */
static int rec_box_read(uint32_t off, uint8_t *dst, uint32_t n)
{
    if (!rec_box.open || rec_box.gen != st_pool_gen || off > rec_box.len || n > rec_box.len - off) return -1;
    while (n) {
        uint32_t k;
        if (off < REC_BOX_HEAD) {
            k = REC_BOX_HEAD - off < n ? REC_BOX_HEAD - off : n;
            memcpy(dst, rec_box.head + off, k);
        } else {
            uint32_t at = REC_BOX_HEAD, p = 0, len = 0;
            for (; p < MOD3_RPAD; p++, at += len)
                if (off < at + (len = rec_r32(rec_box.head + 20u + 4u * p))) break;
            if (p == MOD3_RPAD) return -1;
            k = at + len - off < n ? at + len - off : n;
            if (st_read_range(rec_pad_obj(rec_box.u * MOD3_RPAD + p), rec_box.copy[p], off - at, dst, k)) return -1;
        }
        off += k; dst += k; n -= k;
    }
    return 0;
}
/* the open blob's CRC (zlib's), read through */
static uint32_t rec_box_crc(void)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t off = 0; off < rec_box.len; off += sizeof st_buf) {
        uint32_t k = rec_box.len - off < sizeof st_buf ? rec_box.len - off : sizeof st_buf;
        if (rec_box_read(off, st_buf, k)) return 0;
        crc = st_crc32_update(crc, st_buf, k);
        if (!(off & 0x3FFFu)) ST_IDLE();
    }
    return ~crc;
}
/* A blob into kit u as it comes (PUT): rec_box_put_begin(len), its bytes in order (rec_box_put_write), then
 * rec_box_put_end. Each pad record is written whole as its bytes arrive (checked and published as rec_put_commit does:
 * the pad's old record stays until then); at the end the pads the blob leaves empty go and the name is set. 0, -1 not
 * a kit's blob, -2 a pad its synth does not play, -n the store's error */
static int rec_box_put_begin(st_stream_t *w, uint32_t u, uint32_t len)
{
    memset(&rec_box, 0, sizeof rec_box);
    rec_box.u = u; rec_box.len = len; rec_box.w = w;
    return u < ST_NREC && len >= REC_BOX_HEAD ? 0 : -1;
}
static int rec_box_next(void)                       /* the next pad with bytes: its record begun (none: pad = 16) */
{
    while (rec_box.pad < MOD3_RPAD && !rec_r32(rec_box.head + 20u + 4u * rec_box.pad)) rec_box.pad++;
    if (rec_box.pad == MOD3_RPAD) return 0;
    return rec_put_begin(rec_box.w, rec_box.u * MOD3_RPAD + rec_box.pad, rec_r32(rec_box.head + 20u + 4u * rec_box.pad));
}
static int rec_box_put_write(const uint8_t *src, uint32_t n)
{
    int rc = 0;
    while (n && !rec_box.err) {
        if (rec_box.pos < REC_BOX_HEAD) {
            uint32_t k = REC_BOX_HEAD - rec_box.pos < n ? REC_BOX_HEAD - rec_box.pos : n, total = REC_BOX_HEAD;
            memcpy(rec_box.head + rec_box.pos, src, k);
            rec_box.pos += k; src += k; n -= k;
            if (rec_box.pos < REC_BOX_HEAD) break;
            for (uint32_t p = 0; p < MOD3_RPAD; p++) total += rec_r32(rec_box.head + 20u + 4u * p);
            if (rec_r32(rec_box.head) != REC_BOX_MAGIC || rec_box.head[4] != REC_BOX_HEAD || rec_box.head[6] != MOD3_RPAD ||
                total != rec_box.len) { rec_box.err = 1; return -1; }
            if ((rc = rec_box_next()) != 0) break;
            continue;
        }
        if (rec_box.pad >= MOD3_RPAD) { rc = -1; break; }
        uint32_t len = rec_r32(rec_box.head + 20u + 4u * rec_box.pad), k = len - rec_box.at < n ? len - rec_box.at : n;
        if ((rc = rec_put_write(rec_box.w, src, k)) != 0) break;
        rec_box.at += k; rec_box.pos += k; src += k; n -= k;
        if (rec_box.at == len) {
            if ((rc = rec_put_commit(rec_box.w, rec_box.u * MOD3_RPAD + rec_box.pad)) != 0) break;
            rec_box.pad++; rec_box.at = 0;
            if ((rc = rec_box_next()) != 0) break;
        }
    }
    if (rc) rec_box.err = 1;
    return rc;
}
static int rec_box_put_end(void)
{
    uint32_t n = 0;
    int rc;
    if (rec_box.err || rec_box.pos != rec_box.len || rec_box.pos < REC_BOX_HEAD)
        return -1;
    for (uint32_t p = 0; p < MOD3_RPAD; p++)        /* (the pads the blob leaves empty) */
        if (!rec_r32(rec_box.head + 20u + 4u * p) && rec_pad_stored(rec_box.u * MOD3_RPAD + p) &&
            (rc = rec_delete_pad(rec_box.u * MOD3_RPAD + p)) != 0)
            return rc;
    while (n < 12u && rec_box.head[8u + n]) n++;
    rc = rec_put_name(rec_box.u, (const char *)rec_box.head + 8u, n);
    return rc == -1 && !n ? 0 : rc;
}

/* the host's calls (mod_abi3.h host 4, 6, pads.h rec_hook): one record written at a time, a module's */
static int rec_h_begin(uint32_t slot, uint32_t len) { return rec_put_begin(&rec_w, slot, len); }
static int rec_h_write(const void *src, uint32_t n) { return rec_put_write(&rec_w, src, n); }
static int rec_h_commit(uint32_t slot) { return rec_put_commit(&rec_w, slot); }
static void rec_hooks(void)
{
    rec_hook.begin = rec_h_begin;
    rec_hook.write = rec_h_write;
    rec_hook.commit = rec_h_commit;
    rec_hook.del = rec_delete_pad;
    rec_hook.recheck = rec_recheck;
    rec_hook.take_begin = rec_take_begin;
    rec_hook.take_write = rec_take_write;
    rec_hook.take_end = rec_take_end;
    rec_hook.take_abort = rec_take_abort;
}
