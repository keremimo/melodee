/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Synth modules, the main loop's side (eng_module.c has the audio's): the names and preset counts the lists show,
 * loading on demand, and registering a module as its engine number. Built for size (tools/size_fns.py). */
/* e's name in the lists (a module not in SRAM: its file's) */
static const char *eng_map_name(uint32_t e);      /* eng_map.c */
static const char *eng_name(uint32_t e)
{
    uint32_t k = ENGINES[e % NENGINES] == &ENG_MOD_IDLE ? mod_slot_of(e % NENGINES) : NMOD;
    const char *m;
    if (k < NMOD)
        return mods[k].name;
    if (ENGINES[e % NENGINES] == &ENG_NONE && (m = eng_map_name(e % NENGINES)) != 0)
        return m;                                   /* (a module the device has not got: the map's name for it) */
    return ENGINES[e % NENGINES]->name;
}
/* the slot whose file holds e's factory presets (its PRST chunk) while e plays from it or waits for it, NMOD: none (a
 * built-in engine: its rows or maker) */
static uint32_t mod_pst_of(uint32_t e)
{
    uint32_t k = e < NENGINES ? mod_slot_of(e) : NMOD;
    return k < NMOD && mods[k].pst && (mod_at[e] == k + 1u || ENGINES[e] == &ENG_MOD_IDLE) ? k : NMOD;
}
/* e's preset count (a module not in SRAM: its file's) */
static uint32_t eng_npresets(uint32_t e)
{
    uint32_t k = mod_pst_of(e % NENGINES);
    if (k >= NMOD && ENGINES[e % NENGINES] == &ENG_MOD_IDLE)
        k = mod_slot_of(e % NENGINES);
    return k < NMOD ? mods[k].npresets : ENGINES[e % NENGINES]->npresets;
}
/* ---- a file's factory presets (module_presets.h), read where the file is (module_store.c checked them when it listed
 * the file): two windows of its PRST payload (the lists read records and names in order), the categories of every
 * listed file in RAM (mod_cat: the LIST filters read them on every pass), one preset unpacked into the main loop's
 * scratch (scratch.h dec: filled and consumed inside mod_pst_get) */
#include "scratch.h"
static int (*mod_pread)(uint32_t k, uint32_t off, void *dst, uint32_t n);   /* module_store.c mod_rd: slot k's file */
/* the host table's file_read: n bytes of slot self's file at off (a module reads its own: bind's self). 0 ok */
static int mod_h_file_read(uint32_t self, uint32_t off, void *dst, uint32_t n)
{
    self &= 0xFFu;                                  /* (bind's self: the slot, the engine number above it) */
    return self >= NMOD || !mod_pread || off > mods[self].len || n > mods[self].len - off || mod_pread(self, off, dst, n);
}
#define MOD_WIN 128u
#define MOD_CATS 512u                               /* the factory presets whose categories RAM holds, every slot's */
static uint8_t mod_win[2][MOD_WIN] __attribute__((section(".pool")));
static uint8_t mod_cat[MOD_CATS / 2u] __attribute__((section(".pool")));
static struct { uint8_t slot; uint16_t n; uint32_t at; } mod_winq[2] = {{0xFFu, 0, 0}, {0xFFu, 0, 0}};
static uint8_t mod_win_last;
static uint16_t mod_cat_n;                          /* mod_cat's nibbles in use */
static void mod_win_drop(uint32_t k)
{
    for (uint32_t w = 0; w < 2u; w++)
        if (mod_winq[w].slot == k)
            mod_winq[w].slot = 0xFFu;
}
static uint32_t mod_le32(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
/* n bytes of slot k's PRST payload at off (more than a window: straight from the flash) */
static int mod_pst_rd(uint32_t k, uint32_t off, void *dst, uint32_t n)
{
    const mod_slot_t *s = &mods[k];
    uint32_t w;
    if (!s->pst || off > s->pst_len || n > s->pst_len - off)
        return 1;
    if (n > MOD_WIN)
        return mod_pread(k, s->pst + off, dst, n);
    for (w = 0; w < 2u; w++)
        if (mod_winq[w].slot == k && off >= mod_winq[w].at && off + n <= mod_winq[w].at + mod_winq[w].n)
            break;
    if (w == 2u) {                                  /* (the window read longest ago, from off on) */
        uint32_t m = s->pst_len - off < MOD_WIN ? s->pst_len - off : MOD_WIN;
        w = mod_win_last ^ 1u;
        mod_winq[w].slot = 0xFFu;
        if (mod_pread(k, s->pst + off, mod_win[w], m))
            return 1;
        mod_winq[w].slot = (uint8_t)k;
        mod_winq[w].at = off;
        mod_winq[w].n = (uint16_t)m;
    }
    mod_win_last = (uint8_t)w;
    memcpy(dst, mod_win[w] + (off - mod_winq[w].at), n);
    return 0;
}
/* preset i's record and the next one (its ends) */
static int mod_pst_recs(uint32_t k, uint32_t i, uint8_t r[2u * MOD_PST_REC])
{
    return i >= mods[k].npresets || mod_pst_rd(k, MOD_PST_HEAD + MOD_PST_REC * i, r, 2u * MOD_PST_REC);
}
/* preset i's name into nm (cap bytes, cut as str_cpy cuts): 0, or 1 (none) */
static int mod_pst_name(uint32_t k, uint32_t i, char *nm, uint32_t cap)
{
    uint8_t r[2u * MOD_PST_REC];
    char b[MOD_PST_NAME];
    if (!cap || mod_pst_recs(k, i, r))
        return 1;
    uint32_t no = r[4] | (uint32_t)r[5] << 8, n = (r[12] | (uint32_t)r[13] << 8) - no;
    if (n > MOD_PST_NAME || (n && mod_pst_rd(k, MOD_PST_HEAD + MOD_PST_REC * (mods[k].npresets + 1u) + no, b, n)))
        return 1;
    n = n < cap - 1u ? n : cap - 1u;
    memcpy(nm, b, n);
    nm[n] = 0;
    return 0;
}
/* preset i's category (the browser's CAT_*; 0 none) */
static uint32_t mod_pst_cat(uint32_t k, uint32_t i)
{
    const mod_slot_t *s = &mods[k];
    uint8_t r[2u * MOD_PST_REC];
    if (i >= s->npresets)
        return 0;
    if (s->cat_at != MOD_PST_NONE) {
        uint32_t c = s->cat_at + i;
        return (uint32_t)(mod_cat[c / 2u] >> (c % 2u * 4u)) & 15u;
    }
    return mod_pst_recs(k, i, r) ? 0u : r[6] & 15u;
}
/* a packed entry as dsp.c lz_unpack reads it: nb bytes making exactly n, no copy reaching before the dictionary */
static int mod_lz_ok(const uint8_t *b, uint32_t nb, uint32_t n, uint32_t dict)
{
    uint32_t i = 0, o = 0;
    while (o < n) {
        if (i >= nb)
            return 0;
        uint32_t c = b[i++];
        if (c < 0x80u) {
            c++;
            if (c > nb - i || c > n - o)
                return 0;
            i += c;
        } else {
            if (nb - i < 2u || (b[i] | (uint32_t)b[i + 1u] << 8) + 1u > dict + o || (c = (c & 0x7Fu) + 3u) > n - o)
                return 0;
            i += 2u;
        }
        o += c;
    }
    return i == nb;
}
/* preset i of slot k's file unpacked: its values into pr (pr->name is the caller's), its name into name
 * (MOD_PST_NAME + 1 bytes), its patch into patch (cap bytes; 0: not wanted). Its patch's length, or -1 */
static int mod_pst_get(uint32_t k, uint32_t i, preset_t *pr, char *name, uint8_t *patch, uint32_t cap)
{
    const mod_slot_t *s = &mods[k];
    uint8_t r[2u * MOD_PST_REC], *b = main_scratch.dec;
    if (mod_pst_name(k, i, name, MOD_PST_NAME + 1u) || mod_pst_recs(k, i, r))
        return -1;
    uint32_t at = mod_le32(r), pn = mod_le32(r + MOD_PST_REC) - at, d = s->pst_dict, u = s->pst_ulen, j;
    if (pn > MOD_PST_WIN || d + u + pn > MOD_PST_BUF || (d && mod_pst_rd(k, s->pst_dat, b, d)) || mod_pst_rd(k, at, b + MOD_PST_BUF - pn, pn) ||
        !mod_lz_ok(b + MOD_PST_BUF - pn, pn, u, d))
        return -1;
    lz_unpack(b + d, b + MOD_PST_BUF - pn, u);
    b += d;
    for (j = 0; j < 8u; j++)
        pr->e[j] = (int8_t)b[j];
    for (j = 0; j < 4u; j++)
        pr->env[j] = b[8u + j];
    pr->fenv = (int8_t)b[12];
    pr->mono = b[13];
    if (patch && u - MOD_PST_COMMON > cap)
        return -1;
    if (patch)
        memcpy(patch, b + MOD_PST_COMMON, u - MOD_PST_COMMON);
    return (int)(u - MOD_PST_COMMON);
}
/* every synth Melodee ships as a module (../modules/shipped.h SHIP_F_MODS) is on the device, or built in (host
 * builds): a fresh install's setup wrote them (the web installer: web/fm1mods.js). Else the start asks for setup
 * (main.c, ui_boot.c), as for the resources */
static int mod_have(uint32_t e)
{
    for (uint32_t k = 0; k < NMOD; k++)
        if (mods[k].engine == e && (mod_listed(k) || mods[k].state == MS_SKIPPED))
            return 1;
    return 0;
}
static int mod_shipped_ok(void)
{
    for (uint32_t k = 0; k < NSHIP; k++)
        if ((SHIP[k].flags & SHIP_F_MODS) && !mod_have(ENG_H_SHIP0 + k) && !HOST_BUILTIN(ENG_H_SHIP0 + k))
            return 0;
    return 1;
}
/* main loop: e is about to be used (picked, its presets listed, a sound or project on it loaded): into SRAM now */
static void mod_want(uint32_t e)
{
    uint32_t k = e < NENGINES ? mod_slot_of(e) : NMOD;
    if (k >= NMOD)
        return;
    mod_last[k] = fm1_ms;
    if (mods[k].state != MS_LOADED && mod_fetch)
        (void)mod_fetch(k);
}

/* ---- registration (main loop). The arena and the tables the audio reads change with the audio interrupt held off
 * (resources.c: the arena is the audio's; the main loop changes it guarded) */
static uint32_t mod_guard(void)                    /* (the interrupt state restored as it was: also at power-on) */
{
#ifdef FM1_IRQ_TARGET
    uint32_t f = fm1_icfg();
    fm1_irq_off();
    return f;
#else
    return 0;
#endif
}
static void mod_unguard(uint32_t f)
{
#ifdef FM1_IRQ_TARGET
    fm1_icfg_set(f);
#else
    (void)f;
#endif
}
/* e into the engines shown, at its place (engines.c eng_order_rank: the manifest's order) */
static void mod_order_add(uint32_t e)
{
    uint32_t r = eng_order_rank(e), at = eng_nshown;
    for (uint32_t n = 0; n < eng_nshown; n++) {
        if (ENGINE_ORDER[n] == e)
            return;
        if (at == eng_nshown && eng_order_rank(ENGINE_ORDER[n]) > r)
            at = n;
    }
    if (eng_nshown >= NENG_SHOWN_MAX)
        return;
    for (uint32_t n = eng_nshown; n > at; n--)
        ENGINE_ORDER[n] = ENGINE_ORDER[n - 1u];
    ENGINE_ORDER[at] = (uint8_t)e;
    eng_nshown++;
}
static void mod_order_remove(uint32_t e)
{
    uint32_t j = 0;
    for (uint32_t n = 0; n < eng_nshown; n++)
        if (ENGINE_ORDER[n] != e)
            ENGINE_ORDER[j++] = ENGINE_ORDER[n];
    eng_nshown = (uint8_t)j;
}
/* slot k's module into its engine number: its engine copied after its bytes (a field its ABI 3 minor has not: 0), the
 * number, then the table entry the audio reads (each step a single store: a block in between sees the engine retired) */
static void mod_register(uint32_t k, const mod_desc_v3 *d, engine_t *e)
{
    uint32_t n = mods[k].engine, g;
    memset(e, 0, sizeof *e);
    memcpy(e, d->eng, d->eng_size < sizeof *e ? d->eng_size : sizeof *e);
    e->name = d->name;                              /* (its file's: the lists' while it is not loaded) */
    if (e->poly > NVOICE)
        e->poly = NVOICE;
    g = mod_guard();
    mods[k].d = d;
    mod_at[n] = (uint8_t)(k + 1u);
    __asm__ volatile("" ::: "memory");
    ENGINES[n] = e;
    mods[k].state = MS_LOADED;
    mod_order_add(n);
    mod_unguard(g);
    if (rec_hook.recheck)                           /* (its data records, found at boot, checked now: rec_store.c) */
        rec_hook.recheck();
}
/* slot k's module out of SRAM; its engine stays listed (ENG_MOD_IDLE) when idle, else retired (the slot is going) */
static void mod_unregister(uint32_t k, int idle)
{
    uint32_t n = mods[k].engine, g = mod_guard();
    if (n < NENGINES && (mod_at[n] == k + 1u || (ENGINES[n] == &ENG_MOD_IDLE && mod_listed(k)))) {
        if (!idle)
            mod_order_remove(n);
        ENGINES[n] = idle ? &ENG_MOD_IDLE : &ENG_NONE;
        __asm__ volatile("" ::: "memory");
        mod_at[n] = 0;
        for (uint32_t p = 0; p < NPART; p++)        /* its parts: voices ended, state gone (another module may come) */
            if (trk[p].engine == n || trk[p].eng_req == n) {
                for (uint32_t i = 0; i < NVOICE; i++)
                    trk[p].v[i].active = trk[p].v[i].gate = 0;
                eng_state_clear(p);
            }
    }
    mods[k].d = 0;
    resource_release(RES_MODULE0 + k);
    mod_unguard(g);
}
/* slot k listed as its engine without being loaded (mod_catalog) */
static void mod_list_idle(uint32_t k)
{
    uint32_t n = mods[k].engine, g = mod_guard();
    ENGINES[n] = &ENG_MOD_IDLE;
    mods[k].state = MS_STORED;
    mod_order_add(n);
    mod_unguard(g);
}
