/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The engines' factory presets for the picks, the lists and the stores (included by engines.c; built for size:
 * tools/size_fns.py). A factory preset as a pick needs it: its values and name (pr; pr.name), the synth's own patch
 * (plen bytes: the Prophet's program as p5_patch_t, the CZ-1's tone, FM6's packed voice; 0 none). Built in (host
 * builds) from the engine's rows or its maker and the makers of the patches; a synth module's from its file
 * (module_presets.h: tools/mod_build.py wrote the makers' bytes there), its code loaded or not */
#define PST_PATCH_MAX 160u                          /* the largest patch a preset carries (the CZ-1's tone) */
typedef struct {
    preset_t pr;
    char name[MOD_PST_NAME + 1u];
    uint16_t plen;
    uint8_t patch[PST_PATCH_MAX];
} preset_entry_t;
static uint32_t eng_npresets(uint32_t e);
#if MELODEE_MODULES
static uint32_t mod_pst_of(uint32_t e);             /* eng_module_main.c: the slot whose file has e's presets */
static int mod_pst_get(uint32_t s, uint32_t k, preset_t *pr, char *name, uint8_t *patch, uint32_t cap);   /* .. */
static int mod_pst_name(uint32_t s, uint32_t k, char *nm, uint32_t cap);                                   /* .. */
#endif
/* the patch built-in engine e's preset k carries (its makers: host builds, engines.c), its length */
static uint32_t preset_patch_make(uint32_t e, uint32_t k, uint8_t *p)
{
#if MELODEE_HOST_ENGINES && HOST_MAKERS
    uint8_t q[MOD3_PATCH_MAX];
    uint32_t n = preset_builtin_patch(e, k, q);
    if (n > PST_PATCH_MAX)
        n = 0;
    memcpy(p, q, n);
    return n;
#else
    (void)e; (void)k; (void)p;
    return 0;
#endif
}
/* engine e's preset k (< eng_npresets(e)) into x; patch: with its patch (else x->plen 0) */
static void preset_fetch_into(uint32_t e, uint32_t k, preset_entry_t *x, int patch)
{
    const engine_t *en;
    memset(x, 0, sizeof *x);
    x->pr.name = x->name;
    e %= NENGINES;
#if MELODEE_MODULES
    uint32_t s = mod_pst_of(e);
    if (s < NMOD) {
        int n = mod_pst_get(s, k, &x->pr, x->name, patch ? x->patch : 0, patch ? PST_PATCH_MAX : 0);
        x->plen = (uint16_t)(n > 0 ? n : 0);
        return;
    }
#endif
    en = ENGINES[e];
    if (k >= en->npresets)
        return;
    if (en->presets)
        x->pr = en->presets[k];
    else if (en->preset_make)
        en->preset_make(k, &x->pr, x->name);
    if (patch)
        x->plen = (uint16_t)preset_patch_make(e, k, x->patch);
}
static preset_entry_t pe_tmp __attribute__((section(".pool")));   /* (preset_patch's, eng_preset's: main loop) */
/* engine e's factory preset k's patch into dst when it is n bytes (the Prophet's, CZ-1's, FM6's factory sounds): 1 */
static int preset_patch(uint32_t e, uint32_t k, void *dst, uint32_t n)
{
    preset_fetch_into(e, k, &pe_tmp, 1);
    if (pe_tmp.plen != n || n > PST_PATCH_MAX)
        return 0;
    memcpy(dst, pe_tmp.patch, n);
    return 1;
}
/* preset k (< npresets) of e: its row, or one made or read from its module's file on demand. Made presets take turns
 * in two slots: two can be held at once (main loop only) */
static const preset_t *eng_preset(const engine_t *e, uint32_t k)
{
    static struct { preset_t pr; char name[MOD_PST_NAME + 1u]; } made[2];
    static uint8_t turn;
    if (e->presets)
        return &e->presets[k];
    turn ^= 1u;
    memset(&made[turn], 0, sizeof made[turn]);
    made[turn].pr.name = made[turn].name;
    for (uint32_t n = 0; n < NENGINES && k < e->npresets; n++)   /* (none: a retired engine, a module not in SRAM) */
        if (ENGINES[n] == e) {                      /* (an engine made on demand, a synth module) */
            preset_fetch_into(n, k, &pe_tmp, 0);
            made[turn].pr = pe_tmp.pr;
            memcpy(made[turn].name, pe_tmp.name, sizeof made[turn].name);
            made[turn].pr.name = made[turn].name;
            break;
        }
    return &made[turn].pr;
}
/* engine e's preset k's name into nm (cap bytes; "-": none), read from its module's file when it has one (the lists,
 * the cards, the editor: the module's code loaded or not) */
static void eng_preset_name(uint32_t e, uint32_t k, char *nm, uint32_t cap)
{
    e %= NENGINES;
#if MELODEE_MODULES
    uint32_t s = mod_pst_of(e);
    if (s < NMOD) {
        if (mod_pst_name(s, k, nm, cap))
            str_cpy(nm, "-", cap);
        return;
    }
#endif
    str_cpy(nm, k < ENGINES[e]->npresets ? eng_preset(ENGINES[e], k)->name : "-", cap);
}
/* a factory preset's patch onto track t (its synth's patch: the preset's bytes, the rest of the patch kept; a preset
 * without one: the synth's starting patch), a new sound */
static void preset_patch_put(track_t *t, const preset_entry_t *x)
{
    uint32_t tr = (uint32_t)(t - trk);
    patch_set(tr, t->eng_req % NENGINES, x->plen ? x->patch : 0, x->plen, MOD3_PATCH_NEW);
}
