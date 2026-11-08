/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Synth modules in the pool (eng_module.c plays them): slot k is the record ST_MFILE0 + k holding a module file as
 * tools/mod_build.py wrote it (a file longer than a record, ST_PROJECT_MAX: its first ST_PROJECT_MAX bytes there,
 * the rest in the record ST_MXFILE0 + k). At power-on only the heads are read (mod_catalog: the engines listed, nothing in
 * SRAM), unless the last boot died early (bootguard: a module that crashes the device must not keep it from
 * starting). A module is loaded when it is wanted (eng_module.c mod_want, mod_service below) and goes back to the
 * flash when nothing has used it for a while; an upload is loaded once when it is committed (its check). The loader
 * checks the file's head, copies the image into the audio working arena, checks the CRC while it moves the address
 * words (and reads the chunks after them for it: never into SRAM), then the descriptor, and only then registers the
 * engine. A file's factory presets (its PRST chunk, module_presets.h) are read where the file is: checked when it is
 * listed, their names and categories for the lists, one preset unpacked when it is picked, the module loaded or not */
#define MODULE_STORE 1

/* ---- loading: a file's bytes through rd (off, dst, n -> 0 ok) into slot k. Returns MS_LOADED or why not */
typedef int (*mod_read_t)(uint32_t off, void *dst, uint32_t n);
static const mod_desc_v3 *(*mod_host_desc)(uint32_t engine);   /* host tests: the module built for the host */

/* the file's head, as far as it can be checked without its body (any ABI's: an older firmware's module is known by
 * its engine and name); 0 when bad */
static int mod_head_ok(const mod_file3_t *f, uint32_t len)
{
    uint32_t body = MOD3_HEAD + f->image + 4u * f->nrel;   /* (then the chunks, if any: module_presets.h) */
    if (f->magic != MOD3_FILE_MAGIC || f->total != len || !mod_engine_ok(f->engine) || !f->name[0])
        return 0;
    if (f->abi != MOD3_ABI)
        return f->abi == 1u || f->abi == 2u;        /* (ABI 1, ABI 2: an older firmware's, MS_STALE) */
    return !(f->image % 4u) && f->nrel <= f->image / 4u && f->image <= len && f->nrel <= len &&
           !(f->flags & ~MOD3_FILE_CHUNKS) &&
           (f->flags & MOD3_FILE_CHUNKS ? f->total >= body + 8u && !((f->total - body) % 4u) : f->total == body) &&
           f->image >= sizeof(mod_desc_v3) && f->image <= MOD3_IMAGE_MAX && f->bss <= MOD3_BSS_MAX;
}
/* a file this firmware plays: ABI 3, a host table it has, no legacy data (a nonzero key: host 1..3's firmware data,
 * gone since host 4) */
static int mod_head_mine(const mod_file3_t *f)
{
    return f->abi == MOD3_ABI && f->host <= MOD3_HOST && !f->key;
}
/* the relocated descriptor: this ABI, its file's head, every address it holds inside the block (target), the calls
 * the firmware needs set */
static int mod_dcheck(const mod_desc_v3 *d, const mod_file3_t *f, const uint8_t *p, uint32_t len)
{
    if (d->magic != MOD3_MAGIC || d->abi != MOD3_ABI || d->size != sizeof(mod_desc_v3) || d->host != f->host ||
        d->host > MOD3_HOST || d->engine != f->engine || d->npresets != f->npresets || d->key != f->key ||
        d->eng_size < __builtin_offsetof(mod_engine_v3, caps) || d->eng_size > len || !d->eng || !d->bind || !d->name)
        return 0;
#ifdef FM1_IRQ_TARGET
    uint32_t base = (uint32_t)(uintptr_t)p;
    const mod_engine_v3 *e = d->eng;
    if ((uint32_t)(uintptr_t)d->eng - base > len - d->eng_size || (uint32_t)(uintptr_t)d->bind - base >= len ||
        (uint32_t)(uintptr_t)d->name - base >= len || !e->note_on || !e->render ||
        (uint32_t)(uintptr_t)e->note_on - base >= len || (uint32_t)(uintptr_t)e->render - base >= len)
        return 0;
#else
    (void)p; (void)len;
    if (!d->eng->note_on || !d->eng->render)
        return 0;
#endif
    return 1;
}
/* slot k (catalogued) into SRAM: MS_LOADED, MS_NORAM (still listed), or MS_BAD (no longer listed) */
static int mod_load(uint32_t k, mod_read_t rd)
{
    mod_file3_t f;
    mod_slot_t *s = &mods[k];
    if (s->state == MS_LOADED || (s->state != MS_STORED && s->state != MS_NORAM))
        return s->state;
    if (rd(0, &f, sizeof f) || !mod_head_ok(&f, s->len) || !mod_head_mine(&f) || f.engine != s->want)
        goto gone;
    uint32_t body = (f.image + f.bss + 7u) & ~7u;   /* (then its engine_t: mod_register) */
    uint32_t g = mod_guard();
    uint8_t *p = resource_allocate_code(RES_MODULE0 + k, body + (uint32_t)sizeof(engine_t));
    mod_unguard(g);
    if (!p)
        return s->state = MS_NORAM;
    uint32_t crc = 0xFFFFFFFFu, w[32], base = (uint32_t)(uintptr_t)p;
    if (rd(MOD3_HEAD, p, f.image))
        goto bad;
    crc = st_crc32_update(crc, p, f.image);
    for (uint32_t r = 0; r < f.nrel; r += 32u) {    /* the address words, 32 at a time */
        uint32_t m = f.nrel - r < 32u ? f.nrel - r : 32u;
        if (rd(MOD3_HEAD + f.image + 4u * r, w, 4u * m))
            goto bad;
        crc = st_crc32_update(crc, w, 4u * m);
        for (uint32_t j = 0; j < m; j++) {
            uint8_t *a = p + w[j];
            if (w[j] > f.image - 4u)
                goto bad;
            uint32_t v = (a[0] | (uint32_t)a[1] << 8 | (uint32_t)a[2] << 16 | (uint32_t)a[3] << 24) + base;
            a[0] = (uint8_t)v; a[1] = (uint8_t)(v >> 8); a[2] = (uint8_t)(v >> 16); a[3] = (uint8_t)(v >> 24);
        }
    }
    for (uint32_t at = MOD3_HEAD + f.image + 4u * f.nrel; at < f.total; ) {   /* the chunks: in the CRC, never in SRAM */
        uint32_t m = f.total - at < sizeof w ? f.total - at : (uint32_t)sizeof w;
        if (rd(at, w, m))
            goto bad;
        crc = st_crc32_update(crc, w, m);
        at += m;
    }
    if (~crc != f.crc)
        goto bad;                                   /* (.bss: the arena's blocks come zeroed) */
#ifdef FM1_IRQ_TARGET
    const mod_desc_v3 *d = (const mod_desc_v3 *)(const void *)p;
#else
    const mod_desc_v3 *d = mod_host_desc ? mod_host_desc(f.engine) : 0;
#endif
    if (!d || !mod_dcheck(d, &f, p, f.image + f.bss))
        goto bad;
#ifdef FM1_IRQ_TARGET
    fm1_code_sync();                                /* the new code, before anything calls it */
#endif
    mod_run.loop = (uint8_t)(k + 1u);              /* (the crash blame: its first call is its own) */
    int brc = d->bind(&MOD_HOST3, k | (uint32_t)s->engine << 8);   /* (the host table; its slot, its number) */
    mod_run.loop = 0;
    if (brc)
        goto bad;
    mod_register(k, d, (engine_t *)(void *)(p + body));
    return MS_LOADED;
bad:
    g = mod_guard();
    resource_release(RES_MODULE0 + k);
    mod_unguard(g);
gone:
    mod_unregister(k, 0);                           /* (a track on it keeps the number, retired) */
    return s->state = MS_BAD;
}

/* ---- the pool records: slot k's file in records st_mod_obj(k, 0..), ST_PROJECT_MAX bytes each, the last the rest */
static uint32_t mod_obj(uint32_t k, uint32_t j) { return st_mod_obj(k, j); }
/* n bytes of slot k's file at off, from the records mod_open found */
static int mod_rd(uint32_t k, uint32_t off, void *dst, uint32_t n)
{
    const mod_slot_t *s = &mods[k];
    uint8_t *d = dst;
    while (n) {
        uint32_t j = off / ST_PROJECT_MAX, at = off % ST_PROJECT_MAX, m = ST_PROJECT_MAX - at;
        if (j >= s->parts)
            return 1;
        m = n < m ? n : m;
        if (st_read_range(mod_obj(k, j), s->copy[j], at, d, m))
            return 1;
        d += m; off += m; n -= m;
    }
    return 0;
}
static uint32_t mod_rd_slot;                        /* the file mod_flash_read reads (the last mod_open) */
static int mod_flash_read(uint32_t off, void *dst, uint32_t n) { return mod_rd(mod_rd_slot, off, dst, n); }
static uint32_t mod_open(uint32_t k)                /* slot k's records for mod_rd: the file's length, 0 none */
{
    st_hdr_t h;
    mod_slot_t *s = &mods[k];
    uint32_t len = 0, j;
    mod_win_drop(k);
    for (j = 0; j < MOD_RECS; j++) {                /* (a record short of ST_PROJECT_MAX is the last) */
        int copy = st_current(mod_obj(k, j), &h);
        if (copy < 0)
            break;
        s->copy[j] = (uint16_t)copy;
        len += h.len;
        if (h.len < ST_PROJECT_MAX) {
            j++;
            break;
        }
    }
    s->parts = (uint8_t)j;
    if (!j)
        return 0;
    mod_rd_slot = k;
    mod_pread = mod_rd;                             /* (the presets' reader: eng_module_main.c) */
    return len;
}

/* slot k's factory presets (module_presets.h): its PRST chunk found among the chunks, checked (head, CRC: the lists read
 * it from here on), its categories into mod_cat. 1, or 0 (none: the file has no presets, or not as this firmware reads
 * them) */
static int mod_pst_find(uint32_t k, const mod_file3_t *f)
{
    mod_slot_t *s = &mods[k];
    uint32_t at = MOD3_HEAD + f->image + 4u * f->nrel, c[2] = {0, 0}, crc = 0xFFFFFFFFu, n = f->npresets, off, m;
    mod_pst_head_t ph;
    uint8_t r[MOD_PST_REC];
    for (;; at += 8u + ((c[1] + 3u) & ~3u)) {      /* (mod_head_ok: the chunks fill the file to its end) */
        if (!(f->flags & MOD3_FILE_CHUNKS) || at + 8u > f->total || mod_rd(k, at, c, 8u) || c[1] > f->total - at - 8u)
            return 0;
        if (c[0] == MOD_PST_TAG)
            break;
    }
    at += 8u;
    if (c[1] < MOD_PST_HEAD + MOD_PST_REC * (n + 1u) || mod_rd(k, at, &ph, sizeof ph) || ph.version != MOD_PST_VERSION ||
        ph.count != n || ph.ulen < MOD_PST_COMMON || ph.dict + ph.ulen > MOD_PST_BUF)
        return 0;
    mod_winq[0].slot = 0xFFu;                       /* (window 0: the CRC's buffer) */
    for (off = MOD_PST_HEAD; off < c[1]; off += m) {
        m = c[1] - off < MOD_WIN ? c[1] - off : MOD_WIN;
        if (mod_rd(k, at + off, mod_win[0], m))
            return 0;
        crc = st_crc32_update(crc, mod_win[0], m);
    }
    if (~crc != ph.crc || mod_rd(k, at + MOD_PST_HEAD + MOD_PST_REC * n, r, sizeof r))
        return 0;
    off = MOD_PST_HEAD + MOD_PST_REC * (n + 1u) + (r[4] | (uint32_t)r[5] << 8);   /* (the dictionary's place) */
    if (off > c[1] || ph.dict > c[1] - off || mod_le32(r) > c[1])
        return 0;
    s->pst = at;
    s->pst_len = c[1];
    s->pst_dat = off;
    s->pst_init = ph.init;
    s->pst_dict = ph.dict;
    s->pst_ulen = ph.ulen;
    s->cat_at = MOD_PST_NONE;
    if (mod_cat_n + n > MOD_CATS)
        return 1;                                   /* (RAM full: its categories read from the file) */
    for (uint32_t i = 0, q = mod_cat_n; i < n; i++, q++) {   /* the categories into RAM, the records in order */
        if (mod_pst_rd(k, MOD_PST_HEAD + MOD_PST_REC * i, r, sizeof r))
            return (int)(s->pst = 0);
        mod_cat[q / 2u] = (uint8_t)((mod_cat[q / 2u] & ~(15u << (q % 2u * 4u))) | (r[6] & 15u) << (q % 2u * 4u));
    }
    s->cat_at = mod_cat_n;
    mod_cat_n = (uint16_t)(mod_cat_n + n);
    return 1;
}
/* slot k's schema (module_schema.h SCHM): found and checked (its CRC, its places inside the chunk): 1, 0 none, -1 bad */
static int mod_sch_find(uint32_t k, const mod_file3_t *f)
{
    mod_slot_t *s = &mods[k];
    uint32_t at = MOD3_HEAD + f->image + 4u * f->nrel, c[2] = {0, 0}, crc = 0xFFFFFFFFu, off, m;
    mod_sch_head h;
    uint8_t w[64];
    for (;; at += 8u + ((c[1] + 3u) & ~3u)) {
        if (!(f->flags & MOD3_FILE_CHUNKS) || at + 8u > f->total || mod_rd(k, at, c, 8u) || c[1] > f->total - at - 8u)
            return 0;
        if (c[0] == MOD_SCH_TAG)
            break;
    }
    at += 8u;
    if (c[1] < MOD_SCH_HEAD || mod_rd(k, at, &h, sizeof h) || h.version != MOD_SCH_VERSION || h.patch > MOD3_PATCH_MAX ||
        h.str + h.nstr > c[1] || h.def + h.patch > c[1] || h.param + h.nparam * MOD_SCH_PREC > c[1] ||
        h.page + h.npage * MOD_SCH_GREC > c[1] || h.role + h.nrole * MOD_SCH_RREC > c[1])
        return -1;
    for (off = MOD_SCH_HEAD; off < c[1]; off += m) {
        m = c[1] - off < sizeof w ? c[1] - off : sizeof w;
        if (mod_rd(k, at + off, w, m))
            return -1;
        crc = st_crc32_update(crc, w, m);
    }
    if (~crc != h.crc)
        return -1;
    s->sch = at;
    s->sch_len = c[1];
    return 1;
}
/* who a file's head says it is: its UID, or (a file from before the map) "melodee/<name>"'s, the synths Melodee made */
static uint32_t mod_head_uid(const mod_file3_t *f)
{
    char v[MOD3_NAME_MAX + 8u];
    uint32_t n = 0;
    if (f->uid)
        return f->uid;
    for (const char *p = SHIP_VENDOR "/"; *p; p++)
        v[n++] = *p;
    for (uint32_t i = 0; i < MOD3_NAME_MAX && f->name[i]; i++)
        v[n++] = f->name[i];
    return st_crc32(v, n);
}
/* slot k's chunk tag: its payload's place in the file (*len its length), 0: none */
static uint32_t mod_chunk(uint32_t k, const mod_file3_t *f, uint32_t tag, uint32_t *len)
{
    uint32_t at = MOD3_HEAD + f->image + 4u * f->nrel, c[2] = {0, 0};
    for (;; at += 8u + ((c[1] + 3u) & ~3u)) {
        if (!(f->flags & MOD3_FILE_CHUNKS) || at + 8u > f->total || mod_rd(k, at, c, 8u) || c[1] > f->total - at - 8u)
            return 0;
        if (c[0] == tag) {
            *len = c[1];
            return at + 8u;
        }
    }
}
/* who slot k's module is (its IDNT chunk): its UID and tag into the slot; a file without one (built before the map):
 * "melodee/<name>", the synths Melodee made then. 0, or 1 (a bad chunk) */
static int mod_id_find(uint32_t k, const mod_file3_t *f)
{
    mod_slot_t *s = &mods[k];
    mod_ident_v3 id;
    uint32_t len = 0, at = mod_chunk(k, f, MOD3_IDNT_TAG, &len);
    s->uid = mod_head_uid(f);
    if (!at)
        return 0;
    if (len != sizeof id || mod_rd(k, at, &id, sizeof id) || id.version != MOD3_IDNT_VERSION || id.uid != s->uid)
        return 1;                                   /* (the chunk and the head say who it is: they agree) */
    memcpy(s->tag, id.tag, sizeof s->tag);
    return 0;
}
/* slot k listed from its record's head, nothing loaded (power-on, an upload): MS_STORED or why not */
static int mod_catalog(uint32_t k)
{
    mod_file3_t f;
    mod_slot_t *s = &mods[k];
    mod_unregister(k, 0);
    memset(s, 0, sizeof *s);
    mod_win_drop(k);
    sch_win_drop(k);
    sch_gen++;
    uint32_t len = mod_open(k);
    if (!len)
        return MS_EMPTY;
    s->len = len;
    if (len < sizeof f || mod_flash_read(0, &f, sizeof f) || !mod_head_ok(&f, len))
        return s->state = MS_BAD;
    s->engine = s->want = f.engine;                 /* (the number it asks for, until the map gives it one) */
    s->version = f.version;
    s->npresets = f.npresets;
    s->abi = (uint8_t)f.abi;
    s->host = f.abi == MOD3_ABI ? f.host : 0u;
    s->crc = f.crc;
    s->key = f.abi == 1u ? 0u : f.key;              /* (a native ABI 2 file's layout key sits where ABI 3's key is) */
    memcpy(s->name, f.name, MOD3_NAME_MAX);
    if (f.abi != MOD3_ABI || f.key)
        return s->state = MS_STALE;                 /* (an older firmware's: the installer replaces it, editor_module.c) */
    if (f.host > MOD3_HOST)
        return s->state = MS_NEWER;                 /* (a later firmware's: it needs Melodee's newer host table) */
    if (mod_id_find(k, &f))
        return s->state = MS_BAD;
    {
        uint32_t e = eng_map_take(s->uid, s->name, f.engine);   /* its number here (eng_map.c) */
        if (e >= NENGINES || (e < ENGI_MOD0 && eng_map_default(e) != s->uid))
            return s->state = MS_CLASH;             /* no number left, or Melodee's own number for another UID */
        s->engine = (uint8_t)e;
    }
    if (mod_slot_of(s->engine) < NMOD)
        return s->state = MS_CLASH;                 /* two modules for one UID: the first one plays */
    if (mod_skipped || mod_blamed == k + 1u)
        return s->state = MS_SKIPPED;
    if (ENGINES[s->engine] != &ENG_RETIRED)
        return s->state = MS_CLASH;                 /* a built-in engine has the number */
    if (!mod_pst_find(k, &f)) {
        s->npresets = 0;                            /* (a module's presets are its file's) */
        if (f.npresets)
            return s->state = MS_BAD;
    }
    if (mod_sch_find(k, &f) < 0)
        return s->state = MS_BAD;                   /* (a schema it carries must read: its patch is its pages') */
    mod_list_idle(k);
    sch_gen++;
    sch_info_sync();
    return MS_STORED;
}
/* slot k into SRAM (eng_module.c mod_fetch): when the arena is full, the modules nothing uses go first */
static int mod_used(uint32_t k);
static int mod_bring(uint32_t k)
{
    if (mods[k].state == MS_LOADED || !mod_open(k))
        return mods[k].state;
    int rc = mod_load(k, mod_flash_read);
    if (rc == MS_NORAM) {
        for (uint32_t j = 0; j < NMOD; j++)
            if (j != k && mods[j].state == MS_LOADED && !mod_used(j)) {
                mod_unregister(j, 1);
                mods[j].state = MS_STORED;
            }
        rc = mod_load(k, mod_flash_read);
    }
    return rc;
}
/* ---- the device's engine map in the pool (eng_map.c): the record ST_MAPOBJ */
static uint8_t *wire_take(void);                    /* (project.c: the main loop's staging RAM) */
static void mod_map_load(void)
{
    uint8_t *b = wire_take();
    int n = st_load(ST_MAPOBJ, b, ST_SECTOR);
    eng_remap_reset();
    if (n < 8 || mod_le32(b) != EMAP_MAGIC || b[4] > NENGINES || b[5] != MOD3_NREC ||
        (uint32_t)n != 8u + b[4] * EMAP_ENTRY + 4u * MOD3_NREC)
        return;
    for (uint32_t k = 0, e; k < b[4]; k++) {
        const uint8_t *q = b + 8u + k * EMAP_ENTRY;
        e = q[0];
        if (e >= NENGINES || e < ENGI_MOD0)
            continue;                               /* (Melodee's own numbers: always the defaults) */
        eng_map[e].uid = mod_le32(q + 4);
        memcpy(eng_map[e].name, q + 8, MOD3_NAME_MAX);
    }
    for (uint32_t u = 0; u < MOD3_NREC; u++)
        rec_uid[u] = mod_le32(b + 8u + b[4] * EMAP_ENTRY + 4u * u);
}
static int mod_map_save(void)
{
    uint8_t *b = wire_take();
    uint32_t n = 0, any = 0;
    for (uint32_t e = ENGI_MOD0; e < NENGINES; e++) {   /* (the modules on the device: the numbers they keep; one
                                                         * only a store named gets one again when it is read) */
        uint32_t held = 0;
        for (uint32_t k = 0; k < NMOD; k++)
            held |= mods[k].len && mods[k].uid == eng_map[e].uid && mods[k].state != MS_BAD;
        if (eng_map_own(e) && held)
            eng_map_put(b + 8u + EMAP_ENTRY * n++, e);
    }
    for (uint32_t u = 0; u < MOD3_NREC; u++) {
        for (uint32_t i = 0; i < 4u; i++)
            b[8u + EMAP_ENTRY * n + 4u * u + i] = (uint8_t)(rec_uid[u] >> (8u * i));
        any |= rec_uid[u];
    }
    eng_map_dirty = 0;
    if (!n && !any)
        return st_exists(ST_MAPOBJ) ? st_project_delete(ST_MAPOBJ) : 0;
    b[0] = (uint8_t)EMAP_MAGIC; b[1] = (uint8_t)(EMAP_MAGIC >> 8); b[2] = (uint8_t)(EMAP_MAGIC >> 16);
    b[3] = (uint8_t)(EMAP_MAGIC >> 24);
    b[4] = (uint8_t)n; b[5] = MOD3_NREC; b[6] = b[7] = 0;
    int rc = st_save(ST_MAPOBJ, b, 8u + n * EMAP_ENTRY + 4u * MOD3_NREC);
    if (rc)
        eng_map_dirty = 1;
    return rc;
}
/* the module the last run crashed in (core.h mod_run: a fault, or the watchdog, while its code ran): mod_blamed, which
 * this boot leaves out (and says so: ui_boot.c). The marker starts over for this run */
static void mod_blame(void)
{
    uint32_t b = 0, wd = 0;
#ifdef FM1_IRQ_TARGET
    wd = (fm1_boot.p3_rst & 4u) != 0;               /* (the watchdog: a hang) */
#endif
    if (mod_run.magic == MODRUN_MAGIC && mod_run.alive && (mod_run.crashed || wd))
        b = mod_run.loop ? mod_run.loop : mod_run.audio;
    mod_blamed = (uint8_t)(b <= NMOD ? b : 0u);
    mod_run.magic = MODRUN_MAGIC;
    mod_run.audio = mod_run.loop = mod_run.crashed = 0;
    mod_run.pc = 0;
    mod_run.alive = 1;
}
/* power-on (persist_boot, before the settings and the boot project): every slot's head */
static void mod_boot(void)
{
    mod_blame();
    mod_skipped = bootguard.failed != 0 && !mod_blamed;   /* the last boot died in its first 30 s, in no module's code:
                                                          * all of them out (one to blame: that one alone) */
    mod_fetch = mod_bring;
    eng_map_saver = mod_map_save;
    eng_map_defaults();
    mod_map_load();                                 /* (the numbers this device gave its modules) */
    mod_cat_n = 0;
    for (uint32_t k = 0; k < NMOD; k++)
        (void)mod_catalog(k);
}

/* ---- lazy loading (main loop): a part, a song section or a pattern's sound on slot k's engine */
static int mod_used(uint32_t k)
{
    uint32_t n = mods[k].engine;
    for (uint32_t p = 0; p < NPART; p++)
        if (trk[p].engine == n || trk[p].eng_req == n)
            return 1;
    for (uint32_t t = 0; t < NTRK; t++) {
        for (uint32_t r = 0; r < chain_config.count; r++) {
            const song_sound_t *s = song_sound_get(chain_config.sound[r][t]);
            if (s && s->engine == n)
                return 1;
        }
        for (uint32_t b = 0; b < NPAT; b++) {
            const song_sound_t *s = song_sound_get(pattern_sound[t][b]);
            if (s && s->engine == n)
                return 1;
        }
    }
    return 0;
}
/* the used modules in SRAM, the others out after MOD_IDLE_MS (a module the arena had no room for: tried again
 * each second) */
static void mod_service(uint32_t now)
{
    static uint32_t tried;
    sch_info_sync();                                /* (the synths' patches in RAM for the audio: schema.c) */
    sch_part_sync();
    for (uint32_t k = 0; k < NMOD; k++) {
        if (!mod_listed(k))
            continue;
        if (mod_used(k)) {
            mod_last[k] = now;
            if (mods[k].state == MS_STORED || (mods[k].state == MS_NORAM && now - tried >= 1000u)) {
                tried = now;
                (void)mod_bring(k);
            }
        } else if (mods[k].state == MS_LOADED && now - mod_last[k] >= MOD_IDLE_MS) {
            mod_unregister(k, 1);
            mods[k].state = MS_STORED;
        }
    }
    eng_map_service(!transport_busy());             /* (a module's new number kept once nothing plays) */
}

/* ---- an upload (editor_module.c): begin with the file's length, write its bytes in order, commit. A file longer
 * than a record: each record is published when its bytes are in (the first one checked first), the next one begun
 * (one stream at a time: storage_stream.c); until the last one is published the slot reads as a bad file */
static st_stream_t mod_up;
static mod_file3_t mod_up_head;                     /* (the file's head, kept from its first record) */
static uint8_t mod_up_slot = 0xFFu, mod_up_part;    /* the slot written; the record being written */
static uint32_t mod_up_len;
#define MOD_FILE_MAX (MOD_RECS * ST_PROJECT_MAX)
static uint32_t mod_up_rec(uint32_t j)              /* the bytes record j of the upload holds */
{
    uint32_t at = j * ST_PROJECT_MAX;
    return mod_up_len - at < ST_PROJECT_MAX ? mod_up_len - at : ST_PROJECT_MAX;
}
static int mod_put_begin(uint32_t k, uint32_t len)
{
    mod_up_slot = 0xFFu;
    if (k >= NMOD || len < MOD3_HEAD + sizeof(mod_desc_v3) || len > MOD_FILE_MAX)
        return -1;
    mod_up_len = len;
    int rc = st_stream_begin(&mod_up, mod_obj(k, 0), mod_up_rec(0));
    if (!rc) {
        mods[k].pst = 0;                            /* (its presets: the new file's, once it is committed) */
        mods[k].sch = mods[k].sch_len = 0;          /* (its schema too) */
        mod_win_drop(k);
        sch_win_drop(k);
        sch_gen++;
        mod_up_slot = (uint8_t)k;
        mod_up_part = 0;
    }
    return rc;
}
static int mod_put_check(uint32_t k, uint32_t len);
static int mod_put_write(uint32_t k, uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *b = src;
    if (k != mod_up_slot || off != mod_up.pos + mod_up_part * ST_PROJECT_MAX || n > mod_up_len - off)
        return -1;
    while (n) {
        uint32_t room = mod_up.head.len - mod_up.pos, m = n < room ? n : room;
        int rc = st_stream_write(&mod_up, b, m);
        if (rc)
            return rc;
        b += m; n -= m;
        if (mod_up.pos == ST_PROJECT_MAX && mod_up_len > (mod_up_part + 1u) * ST_PROJECT_MAX) {
            /* a record full and more to come: checked (the first one: the file's head) and published, the next begun */
            if ((rc = mod_up_part ? st_stream_check(&mod_up) : mod_put_check(k, mod_up_len)) != 0 ||
                (rc = st_stream_publish(&mod_up)) != 0 ||
                (rc = st_stream_begin(&mod_up, mod_obj(k, mod_up_part + 1u), mod_up_rec(mod_up_part + 1u))) != 0) {
                mod_up_slot = 0xFFu;
                return rc;
            }
            mod_up_part++;
        }
    }
    return 0;
}
/* the first record's head (in mod_up): a module file of len bytes, no other slot holding the same module */
static int mod_put_check(uint32_t k, uint32_t len)
{
    int rc = st_stream_check(&mod_up);
    if (rc)
        return rc;
    if (st_stream_read(&mod_up, 0, &mod_up_head, sizeof mod_up_head) || !mod_head_ok(&mod_up_head, len) ||
        !mod_head_mine(&mod_up_head))
        return -2;                                  /* (not a module, or not one this firmware plays) */
    for (uint32_t j = 0, uid = mod_head_uid(&mod_up_head); j < NMOD; j++)   /* (the same module: its UID) */
        if (j != k && mods[j].len && mods[j].state != MS_BAD && mods[j].state != MS_STALE && mods[j].state != MS_NEWER &&
            mods[j].uid == uid)
            return -3;
    return 0;
}
/* the file as written: its head checked, no other slot holding the same module, published, loaded. MS_* (>= 0), or
 * a storage error (< 0; -2: not a module file, -3: another slot has this module) */
static int mod_put_commit(uint32_t k)
{
    if (k != mod_up_slot)
        return -1;
    mod_up_slot = 0xFFu;
    int rc = mod_up_part ? st_stream_check(&mod_up) : mod_put_check(k, mod_up_len);
    if (rc)
        return rc;
    if ((rc = st_stream_publish(&mod_up)) != 0)
        return rc;
    for (uint32_t j = mod_up_part + 1u; j < MOD_RECS; j++)
        if (st_exists(mod_obj(k, j)))
            (void)st_project_delete(mod_obj(k, j));   /* (a longer file's rest: gone with it) */
    rc = mod_catalog(k);
    if (rc != MS_STORED)
        return rc;
    mod_fetch = mod_bring;
    mod_last[k] = fm1_ms;                           /* loaded once (its check); out again when nothing uses it */
    return mod_bring(k);
}
static int mod_delete(uint32_t k)
{
    if (k >= NMOD)
        return -1;
    mod_unregister(k, 0);                           /* unloaded, the slot empty */
    memset(&mods[k], 0, sizeof mods[k]);
    eng_map_dirty = 1;                              /* (its number: no longer kept past this run) */
    mod_win_drop(k);
    for (uint32_t j = MOD_RECS; j-- > 1u;)          /* (the rest first: a cut leaves a first record, a bad file) */
        if (st_exists(mod_obj(k, j)))
            (void)st_project_delete(mod_obj(k, j));
    return st_project_delete(mod_obj(k, 0));
}
