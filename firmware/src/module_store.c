/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Synth modules in the pool (eng_module.c plays them): slot k is the record ST_MFILE0 + k holding a module file as
 * tools/mod_build.py wrote it. Every stored module is loaded at power-on (the boot project may play it), unless the
 * last boot died early (bootguard: a module that crashes the device must not keep it from starting); an upload is
 * loaded when it is committed. The loader checks the file's head, copies the image into the audio working arena,
 * checks the CRC while it moves the address words, then the descriptor, and only then registers the engine. */
#define MODULE_STORE 1

/* ---- loading: a file's bytes through rd (off, dst, n -> 0 ok) into slot k. Returns MS_LOADED or why not */
typedef int (*mod_read_t)(uint32_t off, void *dst, uint32_t n);
#define MOD_IMAGE_MAX 24576u                        /* code + data of one module */
#define MOD_BSS_MAX 8192u
static const mod_desc_t *(*mod_host_native)(uint32_t engine);   /* host tests: the module built for the host */

/* the relocated descriptor (target: its addresses moved to the block): the numbers in range, and every address
 * word (MOD_PTRS: word indices of mod_desc_t on the 32-bit target) inside the block, the ones that must be set set */
#ifdef FM1_IRQ_TARGET
static const uint8_t MOD_PTRS[32] = {2, 3, 4, 5, 8, 9, 10, 13, 14, 15, 18, 19, 20, 23, 24, 25, 28, 29, 30, 33, 34, 35,
                                     38, 39, 40, 43, 44, 45, 50, 51, 52, 53};
#define MOD_PTR_NEEDED 0x7124924Fu                  /* bit i: MOD_PTRS[i] must be set (name, titles, labels, host, calls) */
#endif
static int mod_check(const mod_desc_t *d, const uint8_t *p, uint32_t len, uint32_t engine)
{
    if (d->magic != MOD_DESC_MAGIC || d->abi != MOD_ABI || d->size != sizeof(mod_desc_t) || d->engine != engine ||
        d->poly > NVOICE || d->part_bytes > 32768u || !d->note_on || !d->render)
        return 0;
#ifdef FM1_IRQ_TARGET
    const uint32_t *w = (const uint32_t *)(const void *)d;
    uint32_t base = (uint32_t)(uintptr_t)p;
    for (uint32_t i = 0; i < sizeof MOD_PTRS; i++) {
        uint32_t v = w[MOD_PTRS[i]];
        if (v ? v - base >= len : (MOD_PTR_NEEDED >> i) & 1u)
            return 0;
    }
    if (d->npresets && (!d->presets || (uint32_t)(uintptr_t)(d->presets + d->npresets) - base > len))
        return 0;
    for (uint32_t k = 0; k < d->npresets; k++)
        if ((uint32_t)(uintptr_t)d->presets[k].name - base >= len)
            return 0;
#else
    (void)p; (void)len;
#endif
    for (uint32_t k = 0; k < 4u; k++)
        if (d->knob[k] >= 12u)
            return 0;
    for (uint32_t k = 0; k < 8u; k++) {
        const mod_param_t *q = &d->edit[k];
        if (q->fmt > MF_ONOFF || q->min > q->max || q->def < q->min || q->def > q->max ||
            (q->fmt == MF_ENUM && (q->min < 0 || q->max > 63 || !q->names)))
            return 0;
    }
    return 1;
}
/* the file's head, as far as it can be checked without its body; 0 when bad */
static int mod_head_ok(const mod_file_t *f, uint32_t len)
{
    if (f->magic != MOD_FILE_MAGIC || f->abi != MOD_ABI || f->total != len || !mod_engine_ok(f->engine) ||
        f->image < sizeof(mod_desc_t) || f->image > MOD_IMAGE_MAX || f->bss > MOD_BSS_MAX || f->image % 4u ||
        f->nrel > f->image / 4u || f->desc % 4u || f->desc > f->image - sizeof(mod_desc_t) ||
        f->total != sizeof *f + f->image + 4u * f->nrel)
        return 0;
    return f->name[0] != 0;
}
static int mod_load(uint32_t k, uint32_t len, mod_read_t rd)
{
    mod_file_t f;
    mod_slot_t *s = &mods[k];
    if (s->state == MS_LOADED)
        mod_unregister(k);
    memset(s, 0, sizeof *s);
    if (!len)
        return MS_EMPTY;
    s->len = len;
    if (len < sizeof f || rd(0, &f, sizeof f) || !mod_head_ok(&f, len))
        return s->state = MS_BAD;
    s->engine = f.engine;
    s->version = (uint16_t)f.version;
    memcpy(s->name, f.name, MOD_NAME_MAX);
    for (uint32_t j = 0; j < NMOD; j++)
        if (j != k && mods[j].state == MS_LOADED && mods[j].engine == f.engine)
            return s->state = MS_CLASH;             /* two modules for one number: the first one plays */
    if (mod_skipped)
        return s->state = MS_SKIPPED;
    if (mod_at[f.engine] || ENGINES[f.engine] != &ENG_RETIRED)
        return s->state = MS_CLASH;                 /* a built-in engine has the number */
    uint32_t body = (f.image + f.bss + 3u) & ~3u;
    uint32_t g = mod_guard();
    uint8_t *p = resource_allocate_code(RES_MODULE0 + k, body + (uint32_t)sizeof(engine_t));
    mod_unguard(g);
    if (!p)
        return s->state = MS_NORAM;
    s->state = MS_BAD;
    uint32_t crc = 0xFFFFFFFFu, w[32], base = (uint32_t)(uintptr_t)p;
    if (rd(sizeof f, p, f.image))
        goto bad;
    crc = st_crc32_update(crc, p, f.image);
    for (uint32_t r = 0; r < f.nrel; r += 32u) {    /* the address words, 32 at a time */
        uint32_t m = f.nrel - r < 32u ? f.nrel - r : 32u;
        if (rd(sizeof f + f.image + 4u * r, w, 4u * m))
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
    if (~crc != f.crc)
        goto bad;                                   /* (.bss: the arena's blocks come zeroed) */
#ifdef FM1_IRQ_TARGET
    const mod_desc_t *d = (const mod_desc_t *)(const void *)(p + f.desc);
#else
    const mod_desc_t *d = mod_host_native ? mod_host_native(f.engine) : 0;
#endif
    if (!d || !mod_check(d, p, body, f.engine))
        goto bad;
    *d->host = &MOD_HOST;
#ifdef FM1_IRQ_TARGET
    fm1_code_sync();                                /* the new code, before anything calls it */
#endif
    mod_register(k, d, (engine_t *)(void *)(p + body));
    return MS_LOADED;
bad:
    g = mod_guard();
    resource_release(RES_MODULE0 + k);
    mod_unguard(g);
    return s->state = MS_BAD;
}

/* ---- the pool records */
static uint32_t mod_obj(uint32_t k) { return ST_MFILE0 + k; }
static uint32_t mod_rd_obj, mod_rd_copy;
static int mod_flash_read(uint32_t off, void *dst, uint32_t n)
{
    return st_read_range(mod_rd_obj, mod_rd_copy, off, dst, n) ? 1 : 0;
}
/* slot k (again) from its record; MS_* */
static int mod_slot_load(uint32_t k)
{
    st_hdr_t h;
    int copy = st_current(mod_obj(k), &h);
    if (copy < 0)
        return mod_load(k, 0, mod_flash_read);
    mod_rd_obj = mod_obj(k);
    mod_rd_copy = (uint32_t)copy;
    return mod_load(k, h.len, mod_flash_read);
}
/* power-on (persist_boot, before the settings and the boot project): every slot */
static void mod_boot(void)
{
    mod_skipped = bootguard.failed != 0;            /* the last boot died in its first 30 s */
    for (uint32_t k = 0; k < NMOD; k++)
        (void)mod_slot_load(k);
}

/* ---- an upload (editor_module.c): begin with the file's length, write its bytes in order, commit */
static st_stream_t mod_up;
static uint8_t mod_up_slot = 0xFFu;
static int mod_put_begin(uint32_t k, uint32_t len)
{
    mod_up_slot = 0xFFu;
    if (k >= NMOD || len < sizeof(mod_file_t) + sizeof(mod_desc_t) || len > ST_PROJECT_MAX)
        return -1;
    int rc = st_stream_begin(&mod_up, mod_obj(k), len);
    if (!rc)
        mod_up_slot = (uint8_t)k;
    return rc;
}
static int mod_put_write(uint32_t k, uint32_t off, const void *src, uint32_t n)
{
    if (k != mod_up_slot || off != mod_up.pos)
        return -1;
    return st_stream_write(&mod_up, src, n);
}
/* the file as written: its head checked, no other slot playing as its number, published, loaded. MS_* (>= 0), or
 * a storage error (< 0; -2: not a module file, -3: another slot has its number) */
static int mod_put_commit(uint32_t k)
{
    mod_file_t f;
    if (k != mod_up_slot)
        return -1;
    mod_up_slot = 0xFFu;
    int rc = st_stream_check(&mod_up);
    if (rc)
        return rc;
    if (st_stream_read(&mod_up, 0, &f, sizeof f) || !mod_head_ok(&f, mod_up.head.len))
        return -2;
    for (uint32_t j = 0; j < NMOD; j++)
        if (j != k && mods[j].len && mods[j].engine == f.engine)
            return -3;
    if ((rc = st_stream_publish(&mod_up)) != 0)
        return rc;
    return mod_slot_load(k);
}
static int mod_delete(uint32_t k)
{
    if (k >= NMOD)
        return -1;
    (void)mod_load(k, 0, mod_flash_read);           /* unloaded, the slot empty */
    return st_project_delete(mod_obj(k));
}
