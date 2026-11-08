/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A synth's schema (firmware/modules/sdk/mod_schema.h, its file's SCHM chunk): read where it is, the module's code
 * loaded or not. A synth module's from its file in the pool (module_store.c found and checked the chunk when it listed
 * the file: mods[k].sch); a host build's built-in engine's written at first use from the same C description its module
 * is built from (engines.c sch_builtin_src). Two windows over the flash keep the reads small; nothing of a schema is
 * held in RAM but what is on the screen (the pages' cells: ui).
 * Then the patches as the firmware handles them: their length and starting bytes, a parameter's value by its id, a
 * write told to the engine. Built for size (tools/size_fns.py). Included after eng_module_main.c. */
#define SCH_WIN 128u
static uint8_t sch_win[2][SCH_WIN] __attribute__((section(".pool")));
static struct { uint8_t slot; uint16_t n; uint32_t at; } sch_winq[2] = {{0xFFu, 0, 0}, {0xFFu, 0, 0}};
static uint8_t sch_win_last;
static void sch_win_drop(uint32_t k)
{
    for (uint32_t w = 0; w < 2u; w++)
        if (sch_winq[w].slot == k)
            sch_winq[w].slot = 0xFFu;
}
#if MELODEE_SCH_BUILTIN
static const mod_sch_src *sch_builtin_src(uint32_t e);   /* engines.c: a built-in engine's description, 0 none */
static uint8_t *sch_mem[NENGINES];
static uint16_t sch_mem_len[NENGINES];
static uint8_t sch_mem_done[NENGINES];
static const uint8_t *sch_builtin(uint32_t e, uint32_t *len)
{
    static uint8_t pool[NENGINES][12288];
    if (!sch_mem_done[e] || (!sch_mem[e] && ENGINES[e] != &ENG_RETIRED)) {   /* (a host test may put a built-in back) */
        const mod_sch_src *s = sch_builtin_src(e);
        uint32_t n = s ? mod_sch_write(s, pool[e], sizeof pool[e]) : 0;
        sch_mem_done[e] = 1;
        sch_mem[e] = n ? pool[e] : 0;
        sch_mem_len[e] = (uint16_t)n;
    }
    *len = sch_mem_len[e];
    return sch_mem[e];
}
#endif
/* the slot whose file holds e's schema while e plays from it or waits for it (as mod_pst_of), NMOD: built in / none */
static uint32_t sch_slot(uint32_t e)
{
#if MELODEE_MODULES
    uint32_t k = e < NENGINES ? mod_slot_of(e) : NMOD;
    if (k < NMOD && mods[k].sch_len && (mod_at[e] == k + 1u || ENGINES[e] == &ENG_MOD_IDLE))
        return k;
#else
    (void)e;
#endif
    return NMOD;
}
/* n bytes of e's schema at off: 0 ok */
static int sch_rd(uint32_t e, uint32_t off, void *dst, uint32_t n)
{
    e %= NENGINES;
#if MELODEE_MODULES
    uint32_t k = sch_slot(e), w;
    if (k < NMOD) {
        const mod_slot_t *s = &mods[k];
        if (off > s->sch_len || n > s->sch_len - off)
            return 1;
        if (n > SCH_WIN)
            return mod_pread(k, s->sch + off, dst, n);
        for (w = 0; w < 2u; w++)
            if (sch_winq[w].slot == k && off >= sch_winq[w].at && off + n <= sch_winq[w].at + sch_winq[w].n)
                break;
        if (w == 2u) {
            uint32_t m = s->sch_len - off < SCH_WIN ? s->sch_len - off : SCH_WIN;
            w = sch_win_last ^ 1u;
            sch_winq[w].slot = 0xFFu;
            if (mod_pread(k, s->sch + off, sch_win[w], m))
                return 1;
            sch_winq[w].slot = (uint8_t)k;
            sch_winq[w].at = off;
            sch_winq[w].n = (uint16_t)m;
        }
        sch_win_last = (uint8_t)w;
        memcpy(dst, sch_win[w] + (off - sch_winq[w].at), n);
        return 0;
    }
#endif
#if MELODEE_SCH_BUILTIN
    uint32_t len;
    const uint8_t *b = sch_builtin(e, &len);
    if (b && off <= len && n <= len - off) {
        memcpy(dst, b + off, n);
        return 0;
    }
#endif
    (void)off; (void)dst; (void)n;
    return 1;
}
static uint32_t sch_u16(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 8; }
/* e's schema's bytes, 0 none */
static uint32_t sch_size(uint32_t e)
{
    e %= NENGINES;
#if MELODEE_MODULES
    uint32_t k = sch_slot(e);
    if (k < NMOD)
        return mods[k].sch_len;
#endif
#if MELODEE_SCH_BUILTIN
    uint32_t len;
    return sch_builtin(e, &len) ? len : 0u;
#else
    return 0;
#endif
}
static uint8_t sch_gen = 1;                      /* +1 when a schema may have changed (module_store.c: a file listed,
                                                  * written or deleted) */
/* e's schema's head: 0 ok (1: e has none) */
static int sch_head(uint32_t e, mod_sch_head *h)
{
    return sch_rd(e, 0, h, sizeof *h) || h->version != MOD_SCH_VERSION;
}
static int sch_has(uint32_t e) { mod_sch_head h; return !sch_head(e, &h); }
static uint32_t patch_store_tag(uint32_t e);

/* ---- parameters */
typedef struct {
    uint16_t id;
    uint8_t kind, fmt;
    int16_t min, max, def;
    uint16_t off;
    uint8_t shift, width, flags, flags2;
    uint16_t label, name, names, unit;           /* string offsets (MOD_SCH_NONE: none) */
} sch_param_t;
static void sch_param_decode(const uint8_t *b, sch_param_t *p)
{
    p->id = (uint16_t)sch_u16(b);
    p->kind = b[2];
    p->fmt = b[3];
    p->min = (int16_t)sch_u16(b + 4);
    p->max = (int16_t)sch_u16(b + 6);
    p->def = (int16_t)sch_u16(b + 8);
    p->off = (uint16_t)sch_u16(b + 10);
    p->shift = b[12];
    p->width = b[13];
    p->flags = b[14];
    p->flags2 = b[15];
    p->label = (uint16_t)sch_u16(b + 16);
    p->name = (uint16_t)sch_u16(b + 18);
    p->names = (uint16_t)sch_u16(b + 20);
    p->unit = (uint16_t)sch_u16(b + 22);
}
/* e's i-th parameter (ids ascending): 0 ok */
static int sch_param_at(uint32_t e, const mod_sch_head *h, uint32_t i, sch_param_t *p)
{
    uint8_t b[MOD_SCH_PREC];
    if (i >= h->nparam || sch_rd(e, h->param + i * MOD_SCH_PREC, b, sizeof b))
        return 1;
    sch_param_decode(b, p);
    return 0;
}
/* e's parameter id: 0 ok, 1 none */
static int sch_param(uint32_t e, uint32_t id, sch_param_t *p)
{
    mod_sch_head h;
    uint32_t lo = 0, hi;
    if (sch_head(e, &h))
        return 1;
    hi = h.nparam;
    while (lo < hi) {                            /* (ids ascending) */
        uint32_t mid = (lo + hi) / 2u;
        if (sch_param_at(e, &h, mid, p))
            return 1;
        if (p->id == id)
            return 0;
        if (p->id < id)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return 1;
}
/* the string at off into b (cap bytes, cut): its length */
static uint32_t sch_str(uint32_t e, uint32_t off, char *b, uint32_t cap)
{
    mod_sch_head h;
    uint32_t n = 0;
    if (!cap)
        return 0;
    if (off != MOD_SCH_NONE && !sch_head(e, &h))
        while (n + 1u < cap && off + n < h.nstr && !sch_rd(e, h.str + off + n, b + n, 1u) && b[n])
            n++;
    b[n] = 0;
    return n;
}

/* ---- a value in a patch */
static int32_t sch_field_get(const uint8_t *patch, const sch_param_t *p)
{
    uint32_t w = 0, nb = (p->shift + p->width + 7u) / 8u, i;
    for (i = 0; i < nb; i++)
        w |= (uint32_t)patch[p->off + i] << (8u * i);
    w = (w >> p->shift) & ((1u << p->width) - 1u);
    if ((p->flags & MOD3_SF_SIGNED) && (w >> (p->width - 1u)))
        return (int32_t)w - (int32_t)(1u << p->width);
    return (int32_t)w;
}
static void sch_field_put(uint8_t *patch, const sch_param_t *p, int32_t v)
{
    uint32_t nb = (p->shift + p->width + 7u) / 8u, w = 0, m = ((1u << p->width) - 1u) << p->shift, i;
    for (i = 0; i < nb; i++)
        w |= (uint32_t)patch[p->off + i] << (8u * i);
    w = (w & ~m) | (((uint32_t)v << p->shift) & m);
    for (i = 0; i < nb; i++)
        patch[p->off + i] = (uint8_t)(w >> (8u * i));
}
/* p's value on part t whose patch for engine e is patch */
static int32_t sch_get(const track_t *t, uint32_t e, const uint8_t *patch, const sch_param_t *p)
{
    const engine_t *g = ENGINES[e % NENGINES];
    switch (p->kind) {
    case MOD3_SK_FIELD: return sch_field_get(patch, p);
    case MOD3_SK_TRACK: return p->off < P_COUNT ? t->p[p->off] : 0;
    default: return g->pget && !mod_idle(e % NENGINES) ? MOD_MAIN(e % NENGINES, g->pget(t, patch, p->id)) : 0;
    }
}
/* .. set to v (its range): 1 when it changed. The engine is not told (patch_note) */
static int sch_put(track_t *t, uint32_t e, uint8_t *patch, const sch_param_t *p, int32_t v)
{
    const engine_t *g = ENGINES[e % NENGINES];
    int32_t old, lo = p->min, hi = p->max;
    if ((p->flags & MOD3_SF_DESC) && g->desc && !mod_idle(e % NENGINES)) {   /* (its range as the synth shows it now:
                                                                             * the Prophet's FREQ B in semitones) */
        const param_desc_t *d = MOD_MAIN(e % NENGINES, g->desc(t, MOD3_DESC_ID + p->id));
        if (d) {
            lo = d->min;
            hi = d->max;
        }
    }
    v = clamp(v, lo, hi);
    switch (p->kind) {
    case MOD3_SK_FIELD:
        old = sch_field_get(patch, p);
        sch_field_put(patch, p, v);
        return old != v;
    case MOD3_SK_TRACK:
        if (p->off >= P_COUNT || t->p[p->off] == v)
            return 0;
        t->p[p->off] = (int16_t)v;
        return 1;
    default:
        return g->pput && !mod_idle(e % NENGINES) ? MOD_MAIN(e % NENGINES, g->pput(t, patch, p->id, v)) : 0;
    }
}

/* e's widget data: n bytes at off (a page's wid, the head's wdef) into dst; 0 ok */
static int sch_wid(uint32_t e, uint32_t off, void *dst, uint32_t n)
{
    mod_sch_head h;
    return off == MOD_SCH_NONE || sch_head(e, &h) || sch_rd(e, h.wid + off, dst, n);
}
/* the value of e's parameter id on part t (its patch: patch_of), its highest into *max; none: 0, *max 0 */
static int32_t sch_val(const track_t *t, uint32_t e, uint32_t id, int32_t *max)
{
    sch_param_t p;
    if (sch_param(e, id, &p)) {
        if (max)
            *max = 0;
        return 0;
    }
    if (max)
        *max = p.max;
    return sch_get(t, e, patch_of(trk_ix(t)), &p);
}

/* ---- roles (mod_schema.h MOD3_R_*): the n-th record of role in e's schema (its id, flags, label: MOD_SCH_NONE none) */
static int sch_role(uint32_t e, uint32_t role, uint32_t n, uint32_t *id, uint32_t *flags, uint32_t *label)
{
    mod_sch_head h;
    uint8_t b[MOD_SCH_RREC];
    if (sch_head(e, &h))
        return 1;
    for (uint32_t i = 0; i < h.nrole; i++) {
        if (sch_rd(e, h.role + i * MOD_SCH_RREC, b, sizeof b))
            return 1;
        if (b[0] == role && !n--) {
            *id = sch_u16(b + 2);
            *flags = b[1];
            *label = sch_u16(b + 4);
            return 0;
        }
    }
    return 1;
}
/* HOME's four knobs of e: its schema's (ids into id[], MOD_SCH_NONE none): 1, else 0 (the engine's knob[]) */
static int sch_home(uint32_t e, uint32_t *id)
{
    uint32_t k, f, l, any = 0;
    for (k = 0; k < 4u; k++) {
        id[k] = MOD_SCH_NONE;
        if (!sch_role(e, MOD3_R_HOME1 + k, 0, &id[k], &f, &l))
            any = 1;
    }
    return (int)any;
}
/* the picture HOME shows while knob id of e turns (a HOME role naming it: MOD3_RF_PIC_FILTER / _ENV), 0 none */
static uint32_t sch_pic(uint32_t e, uint32_t id)
{
    uint32_t k, n, i, f, l;
    for (k = 0; k < 4u; k++)
        for (n = 0; !sch_role(e, MOD3_R_HOME1 + k, n, &i, &f, &l); n++)
            if (i == id && (f & (MOD3_RF_PIC_FILTER | MOD3_RF_PIC_ENV)))
                return f & (MOD3_RF_PIC_FILTER | MOD3_RF_PIC_ENV);
    return 0;
}
/* the value of e's role on part t (its first record), -1 none */
static int32_t sch_role_val(const track_t *t, uint32_t e, uint32_t role)
{
    uint32_t i, f, l;
    return sch_role(e, role, 0, &i, &f, &l) ? -1 : sch_val(t, e, i, 0);
}
/* e's user presets' tag letter in the lists ('U' when its schema names none) */
static char sch_utag(uint32_t e) { mod_sch_head h; return !sch_head(e, &h) && h.utag ? (char)h.utag : 'U'; }
/* e has a picture for its pages without one and for EDIT 1 / 2 (an algorithm chart: MOD3_W_ALG) */
static int sch_wdef(uint32_t e) { mod_sch_head h; return !sch_head(e, &h) && h.wdef != MOD_SCH_NONE; }
static int sch_flag(uint32_t e, uint32_t f) { mod_sch_head h; return !sch_head(e, &h) && (h.flags & f); }
static int sch_noedit(uint32_t e) { return sch_flag(e, MOD_SCH_F_NOEDIT); }
static uint32_t sch_first(uint32_t e) { mod_sch_head h; return sch_head(e, &h) ? 0u : h.first; }
/* the legacy native user preset namespaces (favorites, the browser: Melodee 1.x's native banks) by the format a synth's
 * patch was stored in: USER_NATIVE_* of engine e, USER_GENERAL none; and back */
static uint32_t user_native_ns(uint32_t e)
{
    uint32_t s = patch_store_tag(e);
    return s == MOD_SCH_STORE_PROGRAM ? USER_NATIVE_P5 : s == MOD_SCH_STORE_VOICE ? USER_NATIVE_FM :
           s == MOD_SCH_STORE_TONE ? USER_NATIVE_CZ : USER_GENERAL;
}
static uint32_t user_native_eng(uint32_t ns)
{
    for (uint32_t e = 0; e < NENGINES; e++)
        if (user_native_ns(e) == ns)
            return e;
    return ENGI_MIDI;
}

/* bytes by parameter ids: out[i] = parameter i's value (FIELD; else 0), i < n (song sounds' VOICE form); and back */
static void patch_ids_get(uint32_t e, const uint8_t *patch, uint8_t *out, uint32_t n)
{
    sch_param_t p;
    for (uint32_t i = 0; i < n; i++)
        out[i] = (uint8_t)(!sch_param(e, i, &p) && p.kind == MOD3_SK_FIELD ? sch_field_get(patch, &p) : 0);
}
static void patch_ids_set(uint32_t e, uint8_t *patch, const uint8_t *in, uint32_t n)
{
    sch_param_t p;
    for (uint32_t i = 0; i < n; i++)
        if (!sch_param(e, i, &p) && p.kind == MOD3_SK_FIELD)
            sch_field_put(patch, &p, in[i]);
}

static int patch_motion_map(uint32_t e, uint8_t *mx, uint8_t *mz, uint32_t n);
static void sch_info_sync(void);
/* ---- each part's synth in RAM, for the audio (MIDI controls, Motion: never the flash there): which Motion slots it
 * records, the parameters MIDI CC 73 / 75 / 72 and 74 / 71 set (its roles). Made in the main loop (mod_service, a
 * patch set there; host builds: when asked) for the engine each part asks for */
typedef struct {
    uint16_t id, off;
    int16_t min, max;
    uint8_t kind, shift, width, flags;           /* (the parameter's; flags: MOD3_SF_*) */
    uint8_t role, rflags;                        /* MOD3_R_CC_*, MOD3_RF_* */
} sch_cct_t;
#define SCH_CCT 20u
static struct {
    uint8_t eng, gen, ok, ncc;
    uint8_t mmask[MOD3_MOTION_IDS / 8u];
    sch_cct_t cc[SCH_CCT];
} sch_part[NPART] __attribute__((section(".pool")));
static void sch_part_make(uint32_t k, uint32_t e)
{
    static const uint8_t ROLES[] = {MOD3_R_CC_ATK, MOD3_R_CC_DEC, MOD3_R_CC_REL, MOD3_R_CC_BRIGHT, MOD3_R_CC_RESO,
                                    MOD3_R_CC_GLIDE};
    uint8_t mx[MOD3_MOTION_IDS], mz[MOD3_MOTION_IDS];
    uint32_t r, n, id, f, l;
    memset(&sch_part[k], 0, sizeof sch_part[k]);
    sch_part[k].eng = (uint8_t)e;
    sch_part[k].gen = sch_gen;
    sch_part[k].ok = 1;
    if (!patch_motion_map(e, mx, mz, MOD3_MOTION_IDS))
        for (uint32_t i = 0; i < MOD3_MOTION_IDS; i++)
            if (mx[i])
                sch_part[k].mmask[i / 8u] |= (uint8_t)(1u << (i % 8u));
    for (r = 0; r < sizeof ROLES; r++)
        for (n = 0; sch_part[k].ncc < SCH_CCT && !sch_role(e, ROLES[r], n, &id, &f, &l); n++) {
            sch_cct_t *c = &sch_part[k].cc[sch_part[k].ncc];
            sch_param_t p;
            c->role = ROLES[r];
            c->rflags = (uint8_t)f;
            c->id = (uint16_t)id;
            if (f & MOD3_RF_TRACK) {
                c->kind = MOD3_SK_TRACK;
                c->off = (uint16_t)id;
            } else if (!sch_param(e, id, &p)) {
                c->kind = p.kind; c->off = p.off; c->shift = p.shift; c->width = p.width; c->flags = p.flags;
                c->min = p.min; c->max = p.max;
            } else {
                continue;
            }
            sch_part[k].ncc++;
        }
}
static void sch_part_sync(void)                  /* (main loop) */
{
    sch_info_sync();
    for (uint32_t k = 0; k < NPART; k++)
        if (!sch_part[k].ok || sch_part[k].eng != trk[k].eng_req || sch_part[k].gen != sch_gen)
            sch_part_make(k, trk[k].eng_req);
}
/* part t's summary while it is for the engine it asks for (0: not yet) */
static const typeof(sch_part[0]) *sch_part_of(const track_t *t)
{
    uint32_t k = trk_ix(t);
    if (k >= NPART)
        return 0;
#ifndef FM1_IRQ_TARGET
    sch_part_sync();                             /* (host builds: no flash, made when asked) */
#endif
    return sch_part[k].ok && sch_part[k].eng == t->eng_req && sch_part[k].gen == sch_gen ? &sch_part[k] : 0;
}
/* MIDI CC: the parameters part t's synth names for role (its RAM summary: no flash, the audio may ask) set from value
 * 0..127: as sent and clamped (MOD3_RF_RAW), else over the parameter's range (MOD3_RF_INVERT: down); the engine told
 * when its patch is the part's. 1 when the synth has the role */
static void patch_note(uint32_t k, uint32_t how, uint32_t id);
static int sch_cc_role(track_t *t, uint32_t role, uint32_t value)
{
    const typeof(sch_part[0]) *c = sch_part_of(t);
    const engine_t *g = ENGINES[eng_idx(t->eng_req)];
    uint32_t k = trk_ix(t), any = 0;
    if (!c || patch_eng[k] != t->eng_req)
        return 0;
    for (uint32_t i = 0; i < c->ncc; i++) {
        const sch_cct_t *x = &c->cc[i];
        int32_t v;
        if (x->role != role || x->kind == MOD3_SK_TRACK)
            continue;
        v = x->rflags & MOD3_RF_RAW ? (int32_t)value : x->rflags & MOD3_RF_INVERT ?
            x->max - (int32_t)(value * (uint32_t)(x->max - x->min) / 127u) : x->min + (int32_t)(value * (uint32_t)(x->max - x->min) / 127u);
        v = clamp(v, x->min, x->max);
        if (x->kind == MOD3_SK_FIELD) {
            sch_param_t p = {0};
            p.off = x->off;
            p.shift = x->shift;
            p.width = x->width;
            sch_field_put(patch_of(k), &p, v);
        } else if (g->pput && !mod_idle(eng_idx(t->eng_req))) {
            (void)g->pput(t, patch_of(k), x->id, v);
        }
        any = 1;
    }
    if (any)
        patch_note(k, MOD3_PATCH_EDIT, 0xFFFFu);
    return (int)any;
}
/* the part's own parameter (a P_* number: MOD3_RF_TRACK) part t's synth names for role, P_COUNT none (RAM summary) */
static __attribute__((noinline)) uint32_t sch_role_track(const track_t *t, uint32_t role)
{
    const typeof(sch_part[0]) *c = sch_part_of(t);
    for (uint32_t i = 0; c && i < c->ncc; i++)
        if (c->cc[i].role == role && c->cc[i].kind == MOD3_SK_TRACK)
            return c->cc[i].off;
    return P_COUNT;
}

/* ---- the patches */
/* every engine's patch length, sound, store format and Motion tag in RAM (the audio's song sections read them: never
 * the flash), made again in the main loop when sch_gen moved */
static struct { uint16_t plen, sound, mmap; uint8_t tag, mtag, mbytes, pads; } sch_info[NENGINES];   /* (pads: its
                                                  * pads.c pads_c + 1, 0 none) */
static void msx_routes_sync(void);
static void pads_sync(void);                     /* (pads.c) */
static uint8_t sch_info_gen;
static void sch_info_sync(void)
{
    mod_sch_head h;
    if (sch_info_gen == sch_gen)
        return;
    for (uint32_t e = 0; e < NENGINES; e++) {
        int none = sch_head(e, &h) || h.patch > MOD3_PATCH_MAX;
        sch_info[e].plen = none ? 0u : h.patch;
        sch_info[e].sound = none ? 0u : h.sound && h.sound < h.patch ? h.sound : h.patch;
        sch_info[e].tag = none ? 0u : h.store;
        sch_info[e].mtag = none || h.mmap == MOD_SCH_NONE ? 0u : h.mtag;
        sch_info[e].mmap = none ? MOD_SCH_NONE : h.mmap;
        sch_info[e].mbytes = !none && (h.flags & MOD_SCH_F_MBYTES);
    }
    sch_info_gen = sch_gen;
    pads_sync();
    msx_routes_sync();
}
#ifdef FM1_IRQ_TARGET
#define SCH_INFO() ((void)0)                     /* (the main loop keeps it: mod_service, a listing, power-on) */
#else
#define SCH_INFO() (sch_info_gen == sch_gen ? (void)0 : sch_info_sync())   /* (host builds: when asked) */
#endif
static uint32_t patch_len(uint32_t e) { SCH_INFO(); return sch_info[e % NENGINES].plen; }
static uint32_t patch_store_tag(uint32_t e) { SCH_INFO(); return sch_info[e % NENGINES].tag; }
static uint32_t patch_mtag(uint32_t e) { SCH_INFO(); return sch_info[e % NENGINES].mtag; }
/* Motion's slots of engine e (main loop): each one's highest value into mx (MO_NATIVE_N bytes: 0 not recorded) and the
 * bits it keeps 0 into mz; 0 ok */
static int patch_motion_map(uint32_t e, uint8_t *mx, uint8_t *mz, uint32_t n)
{
    mod_sch_head h;
    uint32_t m;
    memset(mx, 0, n);
    memset(mz, 0, n);
    if (sch_head(e, &h) || h.mmap == MOD_SCH_NONE)
        return 1;
    m = h.patch < n ? h.patch : n;
    return sch_rd(e, h.wid + h.mmap, mx, m) || sch_rd(e, h.wid + h.mmap + h.patch, mz, m);
}
/* e's patch's bytes that are the sound (a preset's, a user preset's, INIT's: the rest are settings kept) */
static uint32_t patch_sound_len(uint32_t e) { SCH_INFO(); return sch_info[e % NENGINES].sound; }
/* where e's patch keeps its own name: its length (0 none), its place into *off */
static uint32_t patch_name_place(uint32_t e, uint32_t *off)
{
    mod_sch_head h;
    if (sch_head(e, &h) || !h.nlen || h.name + h.nlen > h.patch)
        return 0;
    *off = h.name;
    return h.nlen;
}
/* ---- what a module says of its stored bytes, read from its schema (the module's code loaded or not): its patch's
 * checks (MOD3_R_CHECK), the SysEx it answers (MOD3_R_SYSEX) */
/* n bytes b of e's patch from byte at, as e's checks want them: 1 (none, or no schema: 1), 0 refused */
static int patch_check(uint32_t e, const uint8_t *b, uint32_t at, uint32_t n)
{
    mod_sch_head h;
    uint32_t id, nr, l, i, j;
    uint8_t r[MOD_SCH_CREC];
    if (sch_head(e, &h) || sch_role(e, MOD3_R_CHECK, 0, &id, &nr, &l))
        return 1;
    for (i = 0; i < nr; i++) {
        uint32_t off, st, sh = 0;
        if (sch_rd(e, h.wid + id + i * MOD_SCH_CREC, r, sizeof r) || !r[4])
            return 1;
        off = r[0] | (uint32_t)r[1] << 8;
        st = r[3] ? r[3] : 1u;
        while (!(r[4] >> sh & 1u))
            sh++;
        for (j = 0; j < r[2]; j++, off += st) {
            int32_t v, lo = r[6], hi = r[7];
            if (off < at || off >= at + n)
                continue;
            v = (int32_t)((b[off - at] & r[4]) >> sh);
            if (r[5] & MOD3_CK_SIGNED) {
                v = (int8_t)b[off - at];
                lo = (int8_t)r[6];
                hi = (int8_t)r[7];
            }
            if (r[5] & MOD3_CK_PAIR ? v != lo && v != hi : v < lo || v > hi)
                return 0;
        }
    }
    return 1;
}
static uint32_t patch_store_engine(uint32_t tag);
/* ---- a stored patch's packing (MOD3_R_PACK: how Melodee 1.x packed a KIT patch in projects and user presets) */
#define PACK_MAX 64u                             /* (the packed bytes, at most) */
typedef struct { uint8_t nf, count, stride; uint8_t w[8]; int8_t bias[8]; } pack_t;
static int patch_pack_of(uint32_t e, pack_t *k)  /* e's packing: 0 ok */
{
    mod_sch_head h;
    uint32_t id, nf, l, i, bits = 0;
    uint8_t b[18];
    if (sch_head(e, &h) || sch_role(e, MOD3_R_PACK, 0, &id, &nf, &l) || !nf || nf > 8u ||
        sch_rd(e, h.wid + id, b, 2u * nf + 2u))
        return 1;
    k->nf = (uint8_t)nf;
    k->count = b[2u * nf];
    k->stride = b[2u * nf + 1u];
    for (i = 0; i < nf; i++) {
        k->w[i] = b[2u * i];
        k->bias[i] = (int8_t)b[2u * i + 1u];
        bits += k->w[i];
    }
    return !k->count || (bits * k->count + 7u) / 8u > PACK_MAX || (uint32_t)(k->count - 1u) * k->stride + nf > h.patch;
}
static uint32_t pack_len(const pack_t *k)
{
    uint32_t bits = 0;
    for (uint32_t i = 0; i < k->nf; i++)
        bits += k->w[i];
    return (bits * k->count + 7u) / 8u;
}
/* the stored form of tag's patches (MOD_SCH_STORE_*): its packed bytes (n: the bytes a store keeps, the form's own
 * when its synth packs it; without its synth's schema the stored bytes pass through as they are) */
static uint32_t patch_pack(uint32_t tag, uint8_t *b, const uint8_t *patch, uint32_t n)
{
    uint32_t e = patch_store_engine(tag), pos = 0, g, i;
    pack_t k;
    memset(b, 0, n);
    if (e >= NENGINES || patch_pack_of(e, &k) || pack_len(&k) != n) {
        memcpy(b, patch, n);                     /* (no synth to ask: the bytes as they came, patch_unpack's) */
        return n;
    }
    for (g = 0; g < k.count; g++)
        for (i = 0; i < k.nf; i++)
            wire_bits_put(b, &pos, (uint32_t)((int8_t)patch[g * k.stride + i] + k.bias[i]), k.w[i]);
    return n;
}
static void patch_unpack(uint32_t tag, uint8_t *patch, const uint8_t *b, uint32_t n)
{
    uint32_t e = patch_store_engine(tag), pos = 0, g, i;
    pack_t k;
    if (e >= NENGINES || patch_pack_of(e, &k) || pack_len(&k) != n) {
        memcpy(patch, b, n);
        return;
    }
    for (g = 0; g < k.count; g++)
        for (i = 0; i < k.nf; i++)
            patch[g * k.stride + i] = (uint8_t)(int8_t)((int32_t)wire_bits_get(b, &pos, k.w[i]) - k.bias[i]);
}
/* the engine whose patch Melodee 1.x stored as tag (MOD_SCH_STORE_*), NENGINES none */
static uint32_t patch_store_engine(uint32_t tag)
{
    for (uint32_t e = 0; tag && e < NENGINES; e++)
        if (patch_store_tag(e) == tag)
            return e;
    return NENGINES;
}
/* a patch as a store of format tag holds it (MOD_SCH_STORE_BYTES[tag] bytes): as its synth accepts it (its synth not
 * here: as it is; its synth checks it when it loads) */
static int patch_stored_ok(uint32_t tag, const uint8_t *b)
{
    uint32_t e = patch_store_engine(tag);
    return e >= NENGINES || patch_check(e, b, 0, MOD_SCH_STORE_BYTES[tag % sizeof MOD_SCH_STORE_BYTES]);
}
/* the settings a project keeps of a patch of format tag (MOD_SCH_STORE_FN bytes after its sound) */
static int patch_settings_ok(uint32_t tag, const uint8_t *f)
{
    uint32_t e = patch_store_engine(tag);
    return e >= NENGINES || patch_check(e, f, patch_sound_len(e), MOD_SCH_STORE_FN);
}
/* e's patch as n parameter values by their ids 0..n-1 (a song sound's VOICE): each in its parameter's range */
static int patch_ids_ok(uint32_t e, const uint8_t *v, uint32_t n)
{
    sch_param_t p;
    for (uint32_t i = 0; i < n; i++)
        if (!sch_param(e, i, &p) && ((int32_t)v[i] < p.min || (int32_t)v[i] > p.max))
            return 0;
    return 1;
}
/* the SysEx patterns of every synth into the interrupts' table (sysex_rx.c), main loop */
static void msx_routes_sync(void)
{
    mod_sch_head h;
    uint32_t e, id, n, l, i, m = 0;
    uint8_t q[4];
    msx_nroute = 0;
    RING_PUBLISH();
    for (e = 0; e < NENGINES; e++) {
        if (ENGINES[e] == &ENG_RETIRED || sch_head(e, &h) || sch_role(e, MOD3_R_SYSEX, 0, &id, &n, &l))
            continue;
        for (i = 0; i < n && m < MSX_ROUTES; i++) {
            if (sch_rd(e, h.wid + id + 4u * i, q, sizeof q) || !q[0] || q[0] > 3u)
                break;
            msx_route[m].engine = (uint8_t)e;
            msx_route[m].n = q[0];
            memcpy(msx_route[m].b, q + 1, 3);
            m++;
        }
    }
    RING_PUBLISH();
    msx_nroute = (uint8_t)m;
}
/* e's starting patch into dst (MOD3_PATCH_MAX bytes: zeros after it) */
static void patch_default(uint32_t e, uint8_t *dst)
{
    mod_sch_head h;
    memset(dst, 0, MOD3_PATCH_MAX);
    if (!sch_head(e, &h) && h.patch <= MOD3_PATCH_MAX)
        (void)sch_rd(e, h.def, dst, h.patch);
}
/* the engine whose patch track k holds told it was written (how, id: mod_abi3.h MOD3_PATCH_*), when its code is in
 * SRAM. Main loop */
static void patch_compare_drop(uint32_t tr);     /* (sch_pages.c: COMPARE's copy, a whole patch loaded) */
static void patch_note(uint32_t k, uint32_t how, uint32_t id)
{
    uint32_t e = k < NPART ? patch_eng[k] : NENGINES;
    const engine_t *g = e < NENGINES ? ENGINES[e] : 0;
    if (g && g->patch_changed && !mod_idle(e))
        MOD_MAIN_V(e, g->patch_changed(&trk[k], patch_of(k), how, id));
    if (k < NTRK && how != MOD3_PATCH_EDIT && how != MOD3_PATCH_MOTION)
        patch_compare_drop(k);
}
/* track k's patch becomes engine e's: n bytes of src, the engine told how. The bytes past n: kept while the patch was
 * already e's (a preset's patch is the sound, a synth's own settings may follow it: FM6's functions), else e's starting
 * bytes. When the engine changes the audio reads zeros from the moment the patch is another's (patch_play) */
static void patch_set(uint32_t k, uint32_t e, const uint8_t *src, uint32_t n, uint32_t how)
{
    uint8_t *p = patch_of(k);
    uint32_t len = patch_len(e), i = k < NPART ? k : NPART;
    patch_keep[i] = 0;
    if (src && n > len && !len && e < NENGINES && ENGINES[e] == &ENG_RETIRED && eng_map_own(e)) {
        len = n < MOD3_PATCH_MAX ? n : MOD3_PATCH_MAX;   /* (a module not here: its bytes kept whole, patch_keep) */
        patch_keep[i] = (uint16_t)len;
    }
    if (n > len)
        n = len;
    if (patch_eng[i] != e || !src) {             /* (a whole patch: no starting bytes read, the audio's song sections) */
        patch_eng[i] = (uint8_t)e;
        RING_PUBLISH();
        if (!src || n < len)
            patch_default(e, p);
        else
            memset(p + len, 0, MOD3_PATCH_MAX - len);   /* (past the patch: nothing of the synth before) */
    }
    if (src && n)
        memcpy(p, src, n);
    patch_note(k, how, 0xFFFFu);
}

/* ---- a module's own widget (MOD3_W_DRAW): the host's drawing, into the canvas the pages give it (ui_graph.c sets
 * sch_gfx while the module's draw runs; else nothing is drawn) */
static struct {
    uint16_t (*color)(uint32_t role);
    void (*fill)(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c);
    void (*rrect)(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c);
    void (*text)(int32_t x, int32_t y, const char *s, uint32_t font, uint16_t c);
} sch_gfx;
static uint16_t mod_h_color(uint32_t role) { return sch_gfx.color ? sch_gfx.color(role) : 0u; }
static void mod_h_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { if (sch_gfx.fill) sch_gfx.fill(x, y, w, h, c); }
static void mod_h_rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    if (sch_gfx.rrect)
        sch_gfx.rrect(x, y, w, h, r, c);
}
static void mod_h_text(int32_t x, int32_t y, const char *s, uint32_t font, uint16_t c)
{
    if (sch_gfx.text)
        sch_gfx.text(x, y, s, font, c);
}
