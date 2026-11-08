/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Pads (mod_schema.h MOD3_R_PADS): a synth whose notes strike pads, as a drum kit's. Its pads block is read from its
 * schema, the module's code loaded or not. What the audio and the keys ask (is the part a kit's, its lanes' notes, the
 * lane a note strikes, the MPC pads' notes, the keys' map) is in RAM: pads_c, made for every synth with pads when the
 * schemas change (schema.c sch_info_sync: the main loop; host builds when asked). What the pages show (the pads' names,
 * notes, order, the lanes' names, the parameters, the words) is the selected map decoded once (pads_view, the main
 * loop). A part of a synth without pads is never a kit's: its steps' hits play the pattern format's lane notes
 * (core.h STEP_LANE_NOTE). Included after schema.c */
#define PADS_NSEL 24u                            /* the selector's values a part's map follows (the rest: map 0) */
#define PADS_NMAP 4u                             /* a synth's maps (a kit synth has one) */
#define PADS_NSYN 8u                             /* the synths with pads held (the six kits, two more): in .pool, the
                                                  * arena's room */
typedef struct {
    uint8_t eng, flags, sel, nsel, nrec, rec0, npar, pstride;
    uint8_t seltab[PADS_NSEL];
    uint8_t note_lane[64];                       /* a note's lane, two a byte (low first) */
    struct { uint8_t flags, npads; uint8_t mpc_note[16]; } map[PADS_NMAP];
} pads_c_t;
static pads_c_t pads_c[PADS_NSYN] __attribute__((section(".pool")));
static uint8_t pads_nc;
static uint32_t pads_u16(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 8; }
/* e's pads block: its head into b (MOD_SCH_PDHEAD bytes), its widget data offset; 0 ok */
static int pads_head(uint32_t e, uint8_t *b, uint32_t *wid)
{
    mod_sch_head h;
    uint32_t id, f, l;
    if (sch_head(e, &h) || sch_role(e, MOD3_R_PADS, 0, &id, &f, &l) || f != 1u)
        return 1;
    *wid = h.wid;
    return sch_rd(e, h.wid + id, b, MOD_SCH_PDHEAD) || b[0] != 1u || !b[2] || b[7] != NLANE;
}
static void pads_make(pads_c_t *c, uint32_t e)
{
    uint8_t b[MOD_SCH_PDHEAD], m[MOD_SCH_PDMAP], lanes[NLANE + 128u], mpc[16], notes[16];
    uint32_t wid, k, j;
    memset(c, 0, sizeof *c);
    if (pads_head(e, b, &wid))
        return;
    c->flags = b[1];
    c->sel = b[3];
    c->nsel = (uint8_t)(b[4] < PADS_NSEL ? b[4] : PADS_NSEL);
    c->npar = b[5];
    c->pstride = b[6];
    c->rec0 = b[22];
    c->nrec = (uint8_t)(b[23] < MOD3_NREC ? b[23] : MOD3_NREC);
    if (sch_rd(e, wid + pads_u16(b + 8), c->seltab, c->nsel) || sch_rd(e, wid + pads_u16(b + 10), lanes, sizeof lanes))
        return;
    if (memcmp(lanes, STEP_LANE_NOTE, NLANE))
        return;                                  /* (its lanes play the pattern format's notes, or it has no pads) */
    for (k = 0; k < 128u; k++)
        c->note_lane[k / 2u] |= (uint8_t)((lanes[NLANE + k] % NLANE) << (4u * (k & 1u)));
    for (k = 0; k < b[2] && k < PADS_NMAP; k++) {
        if (sch_rd(e, wid + pads_u16(b + 12) + k * MOD_SCH_PDMAP, m, sizeof m) || !m[0] || m[0] > 16u ||
            sch_rd(e, wid + pads_u16(m + 8), mpc, 16) || sch_rd(e, wid + pads_u16(m + 6), notes, m[0]))
            return;
        c->map[k].flags = m[1];
        c->map[k].npads = m[0];
        for (j = 0; j < 16u; j++)
            c->map[k].mpc_note[j] = notes[mpc[j] % m[0]];
    }
    for (k = 0; k < c->nsel; k++)
        if (c->seltab[k] >= b[2] || c->seltab[k] >= PADS_NMAP)
            c->seltab[k] = 0;
    c->eng = (uint8_t)e;
}
/* every synth's pads into RAM (schema.c sch_info_sync, when the schemas may have changed) */
static void pads_sync(void)
{
    uint8_t b[MOD_SCH_PDHEAD];
    uint32_t e, wid;
    pads_nc = 0;
    for (e = 0; e < NENGINES; e++) {
        sch_info[e].pads = 0;
        if (pads_nc >= PADS_NSYN || ENGINES[e] == &ENG_NONE || pads_head(e, b, &wid))
            continue;
        pads_make(&pads_c[pads_nc], e);
        if (pads_c[pads_nc].map[0].npads)
            sch_info[e].pads = (uint8_t)++pads_nc;
    }
}
/* ---- what the audio asks (RAM only) */
/* part t's synth's pads, 0: it plays none */
static const pads_c_t *pads_of(const track_t *t)
{
    SCH_INFO();
    uint32_t i = sch_info[eng_idx(t->eng_req)].pads;
    return i ? &pads_c[i - 1u] : 0;
}
static int pads_on(const track_t *t) { return pads_of(t) != 0; }
static int pads_eng(uint32_t e) { SCH_INFO(); return e < NENGINES && sch_info[e].pads; }   /* engine e plays pads */
static uint32_t pads_first(void) { SCH_INFO(); return pads_nc ? pads_c[0].eng : NENGINES; }   /* the first synth with pads
                                                                                   * (the manifest's order: the 808) */
static uint32_t pads_flag(const track_t *t, uint32_t f) { const pads_c_t *c = pads_of(t); return c ? c->flags & f : 0u; }
/* the map part t's own parameter picks */
static uint32_t pads_map_c(const pads_c_t *c, const track_t *t)
{
    int32_t v = c->sel < P_COUNT ? t->p[c->sel] : 0;
    return v >= 0 && v < (int32_t)c->nsel ? c->seltab[v] : 0u;
}
/* the lane note strikes on part t (a synth with pads; else 0) */
static uint32_t pads_lane(const track_t *t, uint32_t note)
{
    const pads_c_t *c = pads_of(t);
    return c ? (c->note_lane[(note & 127u) / 2u] >> (4u * (note & 1u))) & 15u : 0u;
}
/* the note lane l plays on part t: the pattern format's (a synth's pads block names the same: pads_make refuses
 * others; its note map makes them its pads) */
static uint32_t lane_note(const track_t *t, uint32_t l) { (void)t; return STEP_LANE_NOTE[l % NLANE]; }
/* a note part t's engine starts (voice.c, the audio): its lane lit for Stage when the engine plays pads */
static void pads_flash_note(const track_t *t, uint32_t note)
{
    uint32_t e = eng_idx(t->engine), i = sch_info[e].pads;
    if (i && !mod_idle(e))
        pads_flash[trk_ix(t) % NTRK] |= (uint8_t)(1u << ((pads_c[i - 1u].note_lane[(note & 127u) / 2u] >> (4u * (note & 1u))) & 15u));
}
/* the note MPC pad k (0..15) plays on part t (a synth with pads) */
static uint32_t pads_mpc_note(const track_t *t, uint32_t k)
{
    const pads_c_t *c = pads_of(t);
    return c ? c->map[pads_map_c(c, t)].mpc_note[k & 15u] : 0u;
}
/* 1: part t's keys are its synth's own map, every key (the SMK-37's outer keys too: keys() with k signed) */
static int pads_keymap(const track_t *t)
{
    const pads_c_t *c = pads_of(t);
    return c && (c->map[pads_map_c(c, t)].flags & MOD3_PM_KEYMAP);
}
/* the data record part t plays (its selector at rec0 + slot), -1 none */
static int32_t pads_rec(const track_t *t)
{
    const pads_c_t *c = pads_of(t);
    int32_t v = c && c->nrec && c->sel < P_COUNT ? t->p[c->sel] - c->rec0 : -1;
    return c && v >= 0 && v < (int32_t)c->nrec ? v : -1;
}
/* the pads synth whose records value v of descriptor d names (its selector's names: the KIT card), its record slot,
 * else -1 */
static int32_t pads_rec_value(const param_desc_t *d, int32_t v)
{
    SCH_INFO();
    for (uint32_t i = 0; i < pads_nc; i++) {
        const pads_c_t *c = &pads_c[i];
        const engine_t *g = ENGINES[c->eng];
        if (c->nrec && c->sel >= P_E0 && c->sel < P_E0 + 8u && d->names && d->names == g->edit[c->sel - P_E0].names)
            return v >= c->rec0 && v < c->rec0 + (int32_t)c->nrec ? v - c->rec0 : -1;
    }
    return -1;
}
static int pads_rec_names(const param_desc_t *d)  /* d names its synth's records from some value on */
{
    SCH_INFO();
    for (uint32_t i = 0; i < pads_nc; i++) {
        const pads_c_t *c = &pads_c[i];
        if (c->nrec && c->sel >= P_E0 && c->sel < P_E0 + 8u && d->names && d->names == ENGINES[c->eng]->edit[c->sel - P_E0].names)
            return c->rec0;
    }
    return -1;
}

/* ---- the data records' synth (rec_store.c keeps the records; pads.h rec_tab lists the ones it checked) */
/* the synth whose record slot u is (its owner's ID in the device's map, eng_map.c; slot MOD3_NREC: the default
 * owner's), its pads: the one whose pads name records; 0 none */
static const pads_c_t *rec_owner(uint32_t u)
{
    uint64_t uid = rec_owner_uid(u);
    SCH_INFO();
    for (uint32_t i = 0; i < pads_nc; i++)
        if (pads_c[i].nrec && pads_c[i].eng < NENGINES && eng_map[pads_c[i].eng].uid == uid)
            return &pads_c[i];
    return 0;
}
/* a listed record's name (its pads' rname, rnlen) into b (cap bytes): its length, 0 none */
static uint32_t rec_name(uint32_t u, char *b, uint32_t cap)
{
    const pads_c_t *c = rec_owner(u);
    uint8_t h[MOD_SCH_PDHEAD];
    uint32_t wid, g = rec_gen, n = 0, off, len;
    if (cap)
        b[0] = 0;
    if (!c || u >= MOD3_NREC || (g & 1u) || !rec_tab[u].n || pads_head(c->eng, h, &wid) || !cap)
        return 0;
    off = h[24] | (uint32_t)h[25] << 8;
    len = h[26];
    if (off + len > MOD3_REC_RUN0)
        return 0;
    const uint8_t *r = REC_BASE + rec_tab[u].sec[0] * 4096u + MOD3_REC_OFF + off;
    for (; n < len && n + 1u < cap && r[n] >= ' ' && r[n] <= '~'; n++)
        b[n] = (char)r[n];
    b[n] = 0;
    if (rec_gen != g)
        b[n = 0] = 0;
    return n;
}

/* ---- what the pages show (the main loop): part t's map decoded */
#define PADS_NAME 8u
#define PADS_ABBR 4u
typedef struct {
    char label[MOD_SCH_NAME + 1u], unit[4], icon[MOD_SCH_NAME + 1u];
    uint8_t off, flags;
    int8_t min, max, add;
} pads_par_t;
typedef struct {
    uint8_t eng, map, gen, ok;
    uint8_t npads, npar, pstride, rnlen, rec0, nrec;
    uint16_t rname, labels;                      /* (labels: the map's per-pad labels' widget data offset) */
    char name[16][PADS_NAME], abbr[16][PADS_ABBR];
    uint8_t note[16], order[16], note_pad[64];
    char lname[NLANE][PADS_NAME], labbr[NLANE][PADS_ABBR];
    char word[MOD3_PW_COUNT][MOD_SCH_NAME + 5u];
    pads_par_t par[4];
} pads_v_t;
static pads_v_t padv __attribute__((section(".pool")));
static uint32_t pads_strs(uint32_t e, uint32_t off, char *b, uint32_t n, uint32_t cap)   /* n strings one after another */
{
    char t[MOD_SCH_NAME + 8u];
    for (uint32_t i = 0; i < n; i++) {
        uint32_t len = sch_str(e, off, t, sizeof t);
        str_cpy(b + i * cap, t, cap);
        off += len + 1u;
    }
    return off;
}
static int pads_view_make(uint32_t e, uint32_t map)
{
    uint8_t b[MOD_SCH_PDHEAD], m[MOD_SCH_PDMAP], np[128], q[MOD_SCH_PDPAR];
    uint32_t wid, k, w;
    memset(&padv, 0, sizeof padv);
    if (pads_head(e, b, &wid) || map >= b[2] ||
        sch_rd(e, wid + pads_u16(b + 12) + map * MOD_SCH_PDMAP, m, sizeof m) || !m[0] || m[0] > 16u)
        return 0;
    padv.npads = m[0];
    padv.npar = (uint8_t)(b[5] < 4u ? b[5] : 4u);
    padv.pstride = b[6];
    padv.rec0 = b[22];
    padv.nrec = b[23];
    padv.rname = (uint16_t)pads_u16(b + 24);
    padv.rnlen = b[26];
    padv.labels = (uint16_t)pads_u16(m + 18);
    pads_strs(e, pads_u16(m + 2), padv.name[0], padv.npads, PADS_NAME);
    pads_strs(e, pads_u16(m + 4), padv.abbr[0], padv.npads, PADS_ABBR);
    w = pads_u16(m + 14);
    pads_strs(e, w != MOD_SCH_NONE ? w : pads_u16(b + 18), padv.lname[0], NLANE, PADS_NAME);
    w = pads_u16(m + 16);
    pads_strs(e, w != MOD_SCH_NONE ? w : pads_u16(b + 20), padv.labbr[0], NLANE, PADS_ABBR);
    pads_strs(e, pads_u16(b + 16), padv.word[0], MOD3_PW_COUNT, sizeof padv.word[0]);
    if (sch_rd(e, wid + pads_u16(m + 6), padv.note, padv.npads) || sch_rd(e, wid + pads_u16(m + 10), padv.order, padv.npads) ||
        sch_rd(e, wid + pads_u16(m + 12), np, sizeof np))
        return 0;
    for (k = 0; k < 128u; k++)
        padv.note_pad[k / 2u] |= (uint8_t)((np[k] % padv.npads) << (4u * (k & 1u)));
    for (k = 0; k < padv.npads; k++)
        padv.order[k] = (uint8_t)(padv.order[k] % padv.npads);
    for (k = 0; k < padv.npar; k++) {
        pads_par_t *p = &padv.par[k];
        if (sch_rd(e, wid + pads_u16(b + 14) + k * MOD_SCH_PDPAR, q, sizeof q))
            return 0;
        p->off = q[0];
        p->flags = q[1];
        p->min = (int8_t)q[2];
        p->max = (int8_t)q[3];
        p->add = (int8_t)q[4];
        (void)sch_str(e, pads_u16(q + 6), p->label, sizeof p->label);
        (void)sch_str(e, pads_u16(q + 8), p->unit, sizeof p->unit);
        (void)sch_str(e, pads_u16(q + 10), p->icon, sizeof p->icon);
    }
    return 1;
}
/* part t's pads as the pages show them (its synth's, the map its parameter picks), 0: it plays none */
static const pads_v_t *pads_view(const track_t *t)
{
    const pads_c_t *c = pads_of(t);
    uint32_t e = eng_idx(t->eng_req), map;
    if (!c)
        return 0;
    map = pads_map_c(c, t);
    if (!padv.ok || padv.eng != e || padv.map != map || padv.gen != sch_gen) {
        padv.ok = (uint8_t)pads_view_make(e, map);
        padv.eng = (uint8_t)e;
        padv.map = (uint8_t)map;
        padv.gen = sch_gen;
    }
    return padv.ok ? &padv : 0;
}
static uint32_t pads_npar(const track_t *t) { const pads_v_t *v = pads_view(t); return v ? v->npar : 0u; }
static uint32_t pads_count(const track_t *t) { const pads_v_t *v = pads_view(t); return v ? v->npads : 1u; }
static uint32_t pads_note(const track_t *t, uint32_t pad) { const pads_v_t *v = pads_view(t); return v ? v->note[pad % v->npads] : 0u; }
/* the pad note strikes on part t */
static uint32_t pads_pad_of(const track_t *t, uint32_t note)
{
    const pads_v_t *v = pads_view(t);
    return v ? (v->note_pad[(note & 127u) / 2u] >> (4u * (note & 1u))) & 15u : 0u;
}
static const char *pads_name(const track_t *t, uint32_t pad) { const pads_v_t *v = pads_view(t); return v ? v->name[pad % v->npads] : ""; }
static const char *pads_abbr(const track_t *t, uint32_t pad) { const pads_v_t *v = pads_view(t); return v ? v->abbr[pad % v->npads] : ""; }
static const char *lane_name(const track_t *t, uint32_t l) { const pads_v_t *v = pads_view(t); return v ? v->lname[l % NLANE] : ""; }
static const char *lane_abbr(const track_t *t, uint32_t l) { const pads_v_t *v = pads_view(t); return v ? v->labbr[l % NLANE] : ""; }
static const char *pads_word(const track_t *t, uint32_t w) { const pads_v_t *v = pads_view(t); return v ? v->word[w % MOD3_PW_COUNT] : ""; }
/* the pad delta places from pad in the order a knob steps through them (clamped at the ends) */
static uint32_t pads_step(const track_t *t, uint32_t pad, int32_t delta)
{
    const pads_v_t *v = pads_view(t);
    int32_t n, r = 0;
    if (!v)
        return 0;
    n = v->npads;
    while (r < n - 1 && v->order[r] != pad % (uint32_t)n)
        r++;
    return v->order[clamp(r + delta, 0, n - 1)];
}
/* parameter k's label for pad (the map's own for it, else the parameter's) into b (MOD_SCH_NAME + 1 bytes) */
static const char *pads_par_label(const track_t *t, uint32_t pad, uint32_t k, char *b)
{
    const pads_v_t *v = pads_view(t);
    uint8_t o[2];
    mod_sch_head h;
    uint32_t e = eng_idx(t->eng_req);
    if (!v || k >= v->npar)
        return "";
    str_cpy(b, v->par[k].label, MOD_SCH_NAME + 1u);
    if (v->labels != MOD_SCH_NONE && !sch_head(e, &h) &&
        !sch_rd(e, h.wid + v->labels + 2u * ((pad % v->npads) * v->npar + k), o, 2) && pads_u16(o) != MOD_SCH_NONE)
        (void)sch_str(e, pads_u16(o), b, MOD_SCH_NAME + 1u);
    return b;
}
/* pad's parameter k's byte in part k's patch (0: none) */
static int8_t *pads_par_at(uint32_t tr, uint32_t pad, uint32_t k)
{
    const pads_v_t *v = pads_view(&trk[tr % NTRK]);
    if (!v || k >= v->npar || tr >= NPART || pad >= v->npads || patch_eng_of(tr) != trk[tr].eng_req ||
        (pad + 1u) * v->pstride > patch_len(patch_eng_of(tr)))
        return 0;
    return (int8_t *)(void *)(patch_of(tr) + pad * v->pstride + v->par[k].off);
}
/* .. its value as shown (v + add, or max - v), and the knob's edit by delta of it */
static int32_t pads_par_shown(const pads_v_t *v, uint32_t k, const int8_t *p)
{
    int32_t x = v->par[k].flags & MOD3_PP_SIGNED ? *p : (int32_t)(uint8_t)*p;
    return v->par[k].flags & MOD3_PP_INVERT ? v->par[k].max - x : x + v->par[k].add;
}
static void pads_par_turn(const pads_v_t *v, uint32_t k, int8_t *p, int32_t delta)
{
    int32_t x = v->par[k].flags & MOD3_PP_SIGNED ? *p : (int32_t)(uint8_t)*p;
    *p = (int8_t)clamp(v->par[k].flags & MOD3_PP_INVERT ? x - delta : x + delta, v->par[k].min, v->par[k].max);
}

/* ---- a step's lanes on part t (the grid: SEQ > NOTES on a part with pads) */
/* the lanes step s strikes: its hits, and its notes on their lanes (a NOTE step only) */
static uint32_t step_lanes(const track_t *t, const step_t *s)
{
    uint32_t m = 0, i;
    if (s->time != ST_NOTE)
        return 0;
    for (i = 0; i < s->n && i < 4u; i++)
        m |= 1u << pads_lane(t, s->note[i]);
    return m | s->hit;
}
/* .. and which of them are accented */
static uint32_t step_accents(const track_t *t, const step_t *s)
{
    return s->flags & SF_ACCENT ? step_lanes(t, s) : s->hit ? s->acc & step_lanes(t, s) : 0u;
}
/* the note step s strikes pad with (DRUM HIT): one of its notes, else its lane's hit when the lane's own note is that
 * pad's (the 909 ride's lane note is not its pad's note); -1: none */
static int32_t step_pad_note(const track_t *t, const step_t *s, uint32_t pad)
{
    uint32_t i, l = pads_lane(t, pads_note(t, pad));
    for (i = 0; i < s->n && i < 4u; i++)
        if (pads_pad_of(t, s->note[i]) == pad)
            return s->note[i];
    return (s->hit >> l) & 1u && pads_pad_of(t, lane_note(t, l)) == pad ? (int32_t)lane_note(t, l) : -1;
}
/* a step's notes that are a lane's note become that lane's hits (the same note, the same velocity: nothing sounds
 * different). Other notes (a low tom 41, a crash 49) stay notes, shown on their lane. A step accent of a step left
 * with hits only becomes the hits' accents. The grid's edits (ui.c grid_own) */
static void step_to_grid(const track_t *t, step_t *s)
{
    if (!s->hit) s->acc = 0;                       /* recorded synth gate is not a drum accent */
    uint32_t i, k = 0;
    if (s->time != ST_NOTE)
        return;
    for (i = 0; i < s->n && i < 4u; i++) {
        uint32_t l = pads_lane(t, s->note[i]);
        if (s->note[i] == lane_note(t, l))
            s->hit |= (uint8_t)(1u << l);
        else
            s->note[k++] = s->note[i];
    }
    for (i = k; i < 4u; i++)
        s->note[i] = 0;
    s->n = (uint8_t)k;
    if (!k && (s->flags & SF_ACCENT)) {
        s->acc |= s->hit;
        s->flags &= (uint8_t)~SF_ACCENT;
    }
}
