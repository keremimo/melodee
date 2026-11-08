/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A synth's own pages, from its schema (module_schema.h, schema.c): PAGE_TAB holds SCH_PG_MAX places for them (scope
 * SC_MOD, id[0] the page's index in the schema), which page_at fills with the selected track's synth's pages: their
 * titles and columns in RAM (sch_pg: what the lists of pages read on every pass), the rest read from the schema when a
 * page is drawn. A column's value is a copy (sch_cell, as the Prophet's, the CZ-1's and FM6's pages had it); an edit
 * writes the patch (sch_page_put) and tells the engine. An instanced page (FM6's operator pages) shows instance
 * sch_inst (its tabs: the schema's instance names). Included by params.c */
static page_t sch_pg[SCH_PG_MAX] __attribute__((section(".pool")));
static char sch_pgt[SCH_PG_MAX][MOD_SCH_NAME + 1u] __attribute__((section(".pool")));
static struct { uint8_t eng, n, ok, gen; } sch_pgs;
static uint8_t sch_inst;                         /* the instance an instanced page shows (FM6: OP1 = 0) */
static uint16_t sch_store_slot[NENGINES];        /* a STORE page's slot, its synth's (0..UP_SLOTS - 1) */
static const page_t SCH_PG_NONE = {"", FAM_EDIT, SC_MOD, GR_NONE, {0xFF, 0xFF, 0xFF, 0xFF}};
typedef struct {                                 /* a schema page record as the pages read it */
    uint8_t widget, flags, ninst, chip;          /* (chip: the title's characters its short name leaves out) */
    int8_t istride[4];
    uint16_t id[4], inames, wid, section;
} sch_page_t;
static int sch_page_rec(uint32_t e, uint32_t j, sch_page_t *g)
{
    mod_sch_head h;
    uint8_t b[MOD_SCH_GREC];
    if (sch_head(e, &h) || j >= h.npage || sch_rd(e, h.page + j * MOD_SCH_GREC, b, sizeof b))
        return 1;
    g->widget = b[2];
    g->flags = b[3];
    for (uint32_t k = 0; k < 4u; k++) {
        g->id[k] = (uint16_t)sch_u16(b + 4 + 2 * k);
        g->istride[k] = (int8_t)b[14 + k];
    }
    g->ninst = b[12];
    g->chip = b[13];
    g->inames = (uint16_t)sch_u16(b + 18);
    g->wid = (uint16_t)sch_u16(b + 20);
    g->section = (uint16_t)sch_u16(b + 22);
    return 0;
}
static uint32_t sch_graph(uint32_t widget)
{
    switch (widget) {
    case MOD3_W_ENV8: return GR_ENV8;
    case MOD3_W_STORE: return GR_STORE;
    case MOD3_W_TOOLS: return GR_TOOLS;
    case MOD3_W_DRAW: return GR_WDRAW;
    default: return GR_NONE;
    }
}
/* the selected track's synth's pages into sch_pg (when its engine or a schema changed) */
static void sch_pages_sync(void)
{
    uint32_t e = eng_idx(TSEL->eng_req), j, n;
    mod_sch_head h;
    if (sch_pgs.ok && sch_pgs.eng == e && sch_pgs.gen == sch_gen)
        return;
    sch_pgs.ok = 1;
    sch_pgs.eng = (uint8_t)e;
    sch_pgs.gen = sch_gen;
    n = sch_head(e, &h) ? 0u : h.npage < SCH_PG_MAX ? h.npage : SCH_PG_MAX;
    for (j = 0; j < n; j++) {
        uint8_t b[MOD_SCH_GREC];
        page_t *p = &sch_pg[j];
        if (sch_rd(e, h.page + j * MOD_SCH_GREC, b, sizeof b))
            break;
        (void)sch_str(e, sch_u16(b), sch_pgt[j], sizeof sch_pgt[j]);
        p->title = sch_pgt[j];
        p->fam = FAM_EDIT;
        p->scope = SC_MOD;
        p->graph = (uint8_t)sch_graph(b[2]);
        for (uint32_t k = 0; k < 4u; k++) {
            uint32_t id = sch_u16(b + 4 + 2 * k);
            p->id[k] = (uint8_t)(id < 0xFFu ? id : 0xFFu);
        }
    }
    sch_pgs.n = (uint8_t)j;
}
/* a kit's pages (SC_PADS, SC_PADHIT: the selected track's synth's pads block titles them) */
static page_t pads_pg[3] __attribute__((section(".pool")));
static const page_t *pads_page(const page_t *p)
{
    uint32_t w = p->scope == SC_PADHIT ? MOD3_PW_HIT : p->id[1] == 4u ? MOD3_PW_PAGE2 : MOD3_PW_PAGE1;
    pads_pg[w] = *p;
    pads_pg[w].title = pads_word(TSEL, w);
    return &pads_pg[w];
}
static const page_t *page_at(uint32_t i)
{
    const page_t *p = &PAGE_TAB[i < NPAGES ? i : 0u];
    if (p->scope == SC_PADS || p->scope == SC_PADHIT)
        return pads_page(p);
    if (p->scope != SC_MOD)
        return p;
    sch_pages_sync();
    return p->id[0] < sch_pgs.n ? &sch_pg[p->id[0]] : &SCH_PG_NONE;
}
/* page pg's place among the synth's pages (sch_pg), SCH_PG_MAX: none (Stage's, a knob bank's own) */
static uint32_t sch_pg_index(const page_t *pg)
{
    return pg >= sch_pg && pg < sch_pg + sch_pgs.n ? (uint32_t)(pg - sch_pg) : SCH_PG_MAX;
}
static uint32_t sch_pg_flags(const page_t *pg)
{
    sch_page_t g;
    uint32_t j = sch_pg_index(pg);
    return j < SCH_PG_MAX && !sch_page_rec(sch_pgs.eng, j, &g) ? g.flags : 0u;
}
/* PAGE_TAB's place i: one of the selected synth's pages the pages show (not a knob bank's, not one gathered into a
 * list) */
static int sch_page_shown(uint32_t i)
{
    const page_t *pg = page_at(i);
    return pg != &SCH_PG_NONE && !(sch_pg_flags(pg) & (MOD3_PG_BANK | MOD3_PG_INLIST));
}
/* the PAGE_TAB place of the synth's page j (NPAGES: none) */
static uint32_t sch_page_place(uint32_t j)
{
    for (uint32_t i = 0; i < NPAGES; i++)
        if (PAGE_TAB[i].scope == SC_MOD && PAGE_TAB[i].id[0] == j)
            return i;
    return NPAGES;
}
/* an instanced page: its instances (0: not instanced) */
static uint32_t sch_pg_ninst(const page_t *pg)
{
    sch_page_t g;
    uint32_t j = sch_pg_index(pg);
    return j < SCH_PG_MAX && !sch_page_rec(sch_pgs.eng, j, &g) && (g.flags & MOD3_PG_INST) ? g.ninst : 0u;
}
/* instance k's name of the synth's instanced pages into b (cap bytes): its length, 0 none */
static uint32_t sch_inst_name(uint32_t k, char *b, uint32_t cap)
{
    sch_page_t g;
    for (uint32_t j = 0; j < sch_pgs.n; j++)
        if (!sch_page_rec(sch_pgs.eng, j, &g) && (g.flags & MOD3_PG_INST) && k < g.ninst) {
            uint32_t off = g.inames, i;
            char t[MOD_SCH_NAME + 1u];
            for (i = 0; i < k; i++)                  /* (the names one after another) */
                off += sch_str(sch_pgs.eng, off, t, sizeof t) + 1u;
            return sch_str(sch_pgs.eng, off, b, cap);
        }
    if (cap)
        b[0] = 0;
    return 0;
}
/* page pg's section (its name into b, cap bytes): 0 none (a page of the firmware's) */
static uint32_t sch_pg_section(const page_t *pg, char *b, uint32_t cap)
{
    sch_page_t g;
    uint32_t j = sch_pg_index(pg);
    if (j >= SCH_PG_MAX || sch_page_rec(sch_pgs.eng, j, &g) || g.section == MOD_SCH_NONE)
        return 0;
    return sch_str(sch_pgs.eng, g.section, b, cap);
}

/* ---- a column's parameter and its value */
#define SCH_NAMES 33u                            /* an enum's names a column holds (FM6's 32 algorithms) */
static struct {
    param_desc_t d;
    char label[MOD_SCH_NAME + 1u], unit[8];
    const char *names[SCH_NAMES + 1u];
    char nbuf[100];
    int16_t cell;
    uint16_t id;
    uint8_t eng, flags, flags2, ok;
} sch_col[5] __attribute__((section(".pool")));  /* (the four columns; [4] SEQ > MOTION's lanes: sch_param_desc) */
static const param_desc_t SCH_STORE_D[4] = {
    {"SLOT", F_INT, 1, UP_SLOTS, 1, 0, 0}, {"STORE", F_INT, 0, 1, 0, 0, 0}, {"SEND", F_INT, 0, 1, 0, 0, 0},
    {"INIT", F_INT, 0, 1, 0, 0, 0}};
/* engine e's parameter p as a column's descriptor in sch_col[c] */
static const param_desc_t *sch_desc_fill(uint32_t c, uint32_t e, const sch_param_t *p)
{
    typeof(sch_col[0]) *s = &sch_col[c < 5u ? c : 4u];
    s->ok = 0;
    s->d.label = s->label;
    s->d.fmt = p->fmt;
    s->d.min = p->min;
    s->d.max = p->max;
    s->d.def = p->def;
    s->d.names = 0;
    s->d.unit = 0;
    (void)sch_str(e, p->label, s->label, sizeof s->label);
    if (p->unit != MOD_SCH_NONE) {
        (void)sch_str(e, p->unit, s->unit, sizeof s->unit);
        s->d.unit = s->unit;
    }
    if (p->fmt == F_ENUM && p->names != MOD_SCH_NONE) {   /* the names one after another */
        uint32_t n = (uint32_t)(p->max - p->min + 1), k, off = p->names, at = 0;
        for (k = 0; k < n && k < SCH_NAMES && at < sizeof s->nbuf; k++) {
            uint32_t m = sch_str(e, off, s->nbuf + at, sizeof s->nbuf - at);
            s->names[k] = s->nbuf + at;
            at += m + 1u;
            off += m + 1u;
        }
        for (; k <= SCH_NAMES; k++)
            s->names[k] = "";
        s->d.names = s->names;                   /* (a schema's enums start at 0: mod_schema.h) */
    }
    s->id = p->id;
    s->eng = (uint8_t)e;
    s->flags = p->flags;
    s->flags2 = p->flags2;
    s->ok = 1;
    return &s->d;
}
/* engine e's parameter p as a descriptor (until the next call: SEQ > MOTION's lanes) */
static const param_desc_t *sch_param_desc(uint32_t e, const sch_param_t *p) { return sch_desc_fill(4u, e, p); }
/* parameter id of e is an instanced page's column: its instance into *k (FM6's OP3 OUT: 2), 1; else 0 */
static int sch_inst_of(uint32_t e, uint32_t id, uint32_t *k)
{
    mod_sch_head h;
    sch_page_t g;
    if (sch_head(e, &h))
        return 0;
    for (uint32_t j = 0; j < h.npage; j++) {
        if (sch_page_rec(e, j, &g) || !(g.flags & MOD3_PG_INST))
            continue;
        for (uint32_t c = 0; c < 4u; c++) {
            int32_t d = (int32_t)id - (int32_t)g.id[c], st = g.istride[c];
            if (g.id[c] == MOD_SCH_NONE || !st || d % st || d / st < 0 || d / st >= (int32_t)g.ninst)
                continue;
            *k = (uint32_t)(d / st);
            return 1;
        }
    }
    return 0;
}
/* a column of a synth's page: its parameter id (instanced: sch_inst's), MOD_SCH_NONE none */
static uint32_t sch_col_id(const page_t *pg, uint32_t slot)
{
    sch_page_t g;
    uint32_t j = sch_pg_index(pg), id;
    if (j >= SCH_PG_MAX)                             /* (Stage's or a knob bank's own: its ids as they are) */
        return pg->id[slot & 3u] == 0xFFu ? MOD_SCH_NONE : pg->id[slot & 3u];
    if (sch_page_rec(sch_pgs.eng, j, &g) || g.id[slot & 3u] == MOD_SCH_NONE)
        return MOD_SCH_NONE;
    id = g.id[slot & 3u];
    if (g.flags & MOD3_PG_INST)
        id = (uint32_t)((int32_t)id + (int32_t)(sch_inst % (g.ninst ? g.ninst : 1u)) * g.istride[slot & 3u]);
    return id;
}
static const param_desc_t *sch_page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t e = eng_idx(TSEL->eng_req), id;
    sch_param_t p;
    const param_desc_t *d;
    *valp = 0;
    sch_col[slot & 3u].ok = 0;
    if (pg->graph == GR_STORE && sch_pg_index(pg) < SCH_PG_MAX) {   /* the slot, then its actions (OCT+) */
        sch_col[slot & 3u].cell = (int16_t)(slot & 3u ? 0 : sch_store_slot[e] % UP_SLOTS + 1u);
        *valp = &sch_col[slot & 3u].cell;
        return &SCH_STORE_D[slot & 3u];
    }
    id = sch_col_id(pg, slot);
    if (id == MOD_SCH_NONE || sch_param(e, id, &p) || (p.flags & MOD3_SF_HIDDEN))
        return 0;
    d = sch_desc_fill(slot, e, &p);
    if ((p.flags & MOD3_SF_DESC) && ENGINES[e]->desc && !mod_idle(e)) {   /* (its descriptor now: the module's) */
        const param_desc_t *m = ENGINES[e]->desc(TSEL, MOD3_DESC_ID + id);
        if (m) {
            sch_col[slot & 3u].d = *m;
            d = &sch_col[slot & 3u].d;
        }
    }
    sch_col[slot & 3u].cell = (int16_t)(pg->graph == GR_TOOLS ? 0 : sch_get(TSEL, e, patch_of(song.sel), &p));
    *valp = &sch_col[slot & 3u].cell;
    return d;
}
/* the module's text of a column's value (MOD3_SF_FORMAT): 1, else 0 */
static int sch_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    for (uint32_t c = 0; c < 4u; c++) {
        const engine_t *g;
        if (d != &sch_col[c].d || !sch_col[c].ok || !(sch_col[c].flags & MOD3_SF_FORMAT))
            continue;
        g = ENGINES[sch_col[c].eng % NENGINES];
        if (!g->pformat || mod_idle(sch_col[c].eng))
            return 0;
        g->pformat(TSEL, patch_of(song.sel), sch_col[c].id, v, val, unit);
        return 1;
    }
    return 0;
}
/* a column whose names its cell shows whole where they fit (MOD3_SF2_WHOLE) */
static int sch_whole(const param_desc_t *d)
{
    for (uint32_t c = 0; c < 4u; c++)
        if (d == &sch_col[c].d && sch_col[c].ok)
            return (sch_col[c].flags2 & MOD3_SF2_WHOLE) != 0;
    return 0;
}
/* a column of a synth's page drawn dim: the module says its value has no effect now (MOD3_SF_DIM) */
static int sch_col_dim(const page_t *pg, uint32_t slot)
{
    uint32_t e = eng_idx(TSEL->eng_req), id = pg->scope == SC_MOD ? sch_col_id(pg, slot) : MOD_SCH_NONE;
    sch_param_t p;
    if (id == MOD_SCH_NONE || sch_param(e, id, &p) || !(p.flags & MOD3_SF_DIM) || !ENGINES[e]->pactive || mod_idle(e))
        return 0;
    return !ENGINES[e]->pactive(TSEL, patch_of(song.sel), id);
}
static const char *eng_abbr_of(uint32_t e)
{
    static char b[8];
    mod_sch_head h;
    if (e < NENGINES && !sch_head(e, &h) && h.abbr != MOD_SCH_NONE && sch_str(e, h.abbr, b, sizeof b))
        return b;
    return N_ENGNAME[e % NENGINES];
}

/* ---- edits */
/* COMPARE (a synth's TOOLS page with MOD3_A_COMPARE: the CZ-1's COMP): one track's patch as it was before its first edit,
 * swapped with the edited one. One copy: the first edit takes it, a whole patch loaded on that track drops it */
static uint8_t patch_cmp[MOD3_PATCH_MAX] __attribute__((section(".pool")));
static uint8_t patch_cmp_tr;                     /* the track + 1 whose patch patch_cmp holds; 0 none */
static int sch_has_compare(uint32_t e)
{
    mod_sch_head h;
    sch_page_t g;
    if (sch_head(e, &h))
        return 0;
    for (uint32_t j = 0; j < h.npage; j++)
        if (!sch_page_rec(e, j, &g) && g.widget == MOD3_W_TOOLS)
            for (uint32_t c = 0; c < 4u; c++)
                if (g.id[c] == MOD3_A_COMPARE)
                    return 1;
    return 0;
}
static void patch_compare_drop(uint32_t tr) { if (patch_cmp_tr == tr % NTRK + 1u) patch_cmp_tr = 0; }
static void patch_compare_from(uint32_t tr, const uint8_t *before)
{
    if (patch_cmp_tr == tr % NTRK + 1u || !sch_has_compare(eng_idx(trk[tr % NTRK].eng_req)))
        return;
    memcpy(patch_cmp, before, MOD3_PATCH_MAX);
    patch_cmp_tr = (uint8_t)(tr % NTRK + 1u);
}
static void patch_compare_take(uint32_t tr) { patch_compare_from(tr, patch_of(tr % NTRK)); }
static int patch_compare_swap(uint32_t tr)       /* 0 = nothing edited */
{
    uint8_t x[MOD3_PATCH_MAX];
    if (patch_cmp_tr != tr % NTRK + 1u)
        return 0;
    memcpy(x, patch_of(tr % NTRK), MOD3_PATCH_MAX);
    memcpy(patch_of(tr % NTRK), patch_cmp, MOD3_PATCH_MAX);   /* (the caller holds the audio off) */
    memcpy(patch_cmp, x, MOD3_PATCH_MAX);
    return 1;
}
/* a column of a synth's page to v: its patch written, the engine told (the main loop, under motion_guard). cmp: the
 * first edit takes COMPARE's copy. 1 when it changed */
static int sch_page_put(const page_t *pg, uint32_t slot, int32_t v, int cmp)
{
    uint32_t e = eng_idx(TSEL->eng_req), id = sch_col_id(pg, slot), k = song.sel % NTRK;
    uint8_t before[MOD3_PATCH_MAX];
    sch_param_t p;
    int changed;
    if (id == MOD_SCH_NONE || sch_param(e, id, &p))
        return 0;
    if (cmp)
        memcpy(before, patch_of(k), MOD3_PATCH_MAX);
    changed = sch_put(TSEL, e, patch_of(k), &p, v);
    if (changed && cmp)
        patch_compare_from(k, before);
    if (changed || (p.flags & MOD3_SF_NOTIFY))
        patch_note(k, MOD3_PATCH_EDIT, id);
    return changed;
}
/* a TOOLS page's column slot done (mod_schema.h MOD3_W_TOOLS: the module's own action, pput with v 1; its range,
 * 0..0, keeps a turn from doing it): 1 when the patch changed. The caller holds the audio off */
static int sch_page_act(const page_t *pg, uint32_t slot)
{
    uint32_t e = eng_idx(TSEL->eng_req), id = sch_col_id(pg, slot), k = song.sel % NTRK;
    const engine_t *g = ENGINES[e];
    if (id == MOD_SCH_NONE || !g->pput || mod_idle(e) || !g->pput(TSEL, patch_of(k), id, 1))
        return 0;
    patch_note(k, MOD3_PATCH_EDIT, id);
    return 1;
}
/* engine e's parameter id's long name (its label without one) into b */
static void sch_param_name(uint32_t e, uint32_t id, char *b, uint32_t cap)
{
    sch_param_t p;
    if (sch_param(e, id, &p)) {
        if (cap) b[0] = 0;
        return;
    }
    (void)sch_str(e, p.name != MOD_SCH_NONE ? p.name : p.label, b, cap);
}
/* a STORE page's ways (MOD3_ST_*), its INIT's message */
static uint32_t sch_store_how(const page_t *pg)
{
    sch_page_t g;
    uint32_t j = sch_pg_index(pg);
    return j < SCH_PG_MAX && !sch_page_rec(sch_pgs.eng, j, &g) ? g.wid : 0u;
}
static void sch_store_init_msg(const page_t *pg, char *b, uint32_t cap)
{
    sch_page_t g;
    uint32_t j = sch_pg_index(pg);
    if (cap) b[0] = 0;
    if (j < SCH_PG_MAX && !sch_page_rec(sch_pgs.eng, j, &g) && g.inames != MOD_SCH_NONE)
        (void)sch_str(sch_pgs.eng, g.inames, b, cap);
}
/* track k's patch's own name (its schema's place: ASCII, a character outside 32..126 a space, trailing spaces cut) into
 * b (cap bytes): its length, 0 none */
static uint32_t patch_name_of(uint32_t e, const uint8_t *p, char *b, uint32_t cap);
static uint32_t patch_name(uint32_t k, char *b, uint32_t cap) { return patch_name_of(patch_eng_of(k), patch_of(k), b, cap); }
/* a patch of engine e's own name: where its schema keeps it, at most (0: none); from bytes p into b */
static uint32_t patch_name_len(uint32_t e) { mod_sch_head h; return sch_head(e, &h) || h.name + h.nlen > MOD3_PATCH_MAX ? 0u : h.nlen; }
static uint32_t patch_name_of(uint32_t e, const uint8_t *p, char *b, uint32_t cap)
{
    mod_sch_head h;
    uint32_t n = 0, i;
    if (!cap)
        return 0;
    b[0] = 0;
    if (sch_head(e, &h) || !h.nlen || h.name + h.nlen > MOD3_PATCH_MAX)
        return 0;
    for (i = 0; i < h.nlen && i + 1u < cap; i++) {
        b[i] = (char)(p[h.name + i] < 32u || p[h.name + i] > 126u ? ' ' : p[h.name + i]);
        if (b[i] != ' ')
            n = i + 1u;
    }
    b[n] = 0;
    return n;
}
/* what engine e's patch is called ("CZ TONE"), into b */
static void patch_kind_name(uint32_t e, char *b, uint32_t cap)
{
    mod_sch_head h;
    if (sch_head(e, &h) || !sch_str(e, h.pname, b, cap))
        str_cpy(b, "SOUND", cap);
}
/* .. set to s (spaces after it), the engine told */
static void patch_name_set(uint32_t k, const char *s)
{
    mod_sch_head h;
    uint32_t e = patch_eng_of(k), i;
    uint8_t *p = patch_of(k);
    if (sch_head(e, &h) || !h.nlen || h.name + h.nlen > MOD3_PATCH_MAX)
        return;
    patch_compare_take(k);
    for (i = 0; i < h.nlen; i++)
        p[h.name + i] = (uint8_t)(*s ? *s++ : ' ');
    patch_note(k, MOD3_PATCH_EDIT, 0xFFFFu);
}
static void up_ui_named(uint32_t kind, uint32_t k, const char *name);   /* upreset.c */
/* STORE by the patch's own name: the selected track's sound into user slot k */
static void patch_store_named(uint32_t k)
{
    char nm[MOD_SCH_NAME + 1u];
    if (k >= UP_SLOTS)
        return;
    patch_name(song.sel % NTRK, nm, sizeof nm);
    up_ui_named(2, k, nm);
}
/* INIT: track k's sound the synth's starting one (its sound's bytes: the rest of the patch kept), a new sound */
static void patch_init_sound(uint32_t k, uint32_t e)
{
    mod_sch_head h;
    uint8_t d[MOD3_PATCH_MAX];
    patch_default(e, d);
    patch_set(k, e, d, sch_head(e, &h) || !h.sound ? patch_len(e) : h.sound, MOD3_PATCH_NEW);
}
