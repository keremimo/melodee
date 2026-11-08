/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Synth modules, the main loop's side (eng_module.c has the audio's): the names and preset counts the lists show,
 * loading on demand, and registering a module as its engine number. Built for size (tools/size_fns.py). */
/* e's name in the lists (a module not in SRAM: its file's) */
static const char *eng_name(uint32_t e)
{
    uint32_t k = ENGINES[e % NENGINES] == &ENG_MOD_IDLE ? mod_slot_of(e % NENGINES) : NMOD;
    return k < NMOD ? mods[k].name : ENGINES[e % NENGINES]->name;
}
/* e's preset count (a module not in SRAM: its file's) */
static uint32_t eng_npresets(uint32_t e)
{
    uint32_t k = ENGINES[e % NENGINES] == &ENG_MOD_IDLE ? mod_slot_of(e % NENGINES) : NMOD;
    return k < NMOD ? mods[k].npresets : ENGINES[e % NENGINES]->npresets;
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
static int mod_engine_ok(uint32_t e)               /* a number a module may play as */
{
    return (e >= ENGI_MOD0 && e < NENGINES) || e == 2u || e == 5u || e == 6u || e == 7u || e == 9u || e == 11u;
}
static void mod_order_add(uint32_t e)
{
    for (uint32_t n = 0; n < eng_nshown; n++)
        if (ENGINE_ORDER[n] == e)
            return;
    if (eng_nshown < NENG_SHOWN_MAX)
        ENGINE_ORDER[eng_nshown++] = (uint8_t)e;
}
static void mod_order_remove(uint32_t e)
{
    uint32_t j = 0;
    for (uint32_t n = 0; n < eng_nshown; n++)
        if (ENGINE_ORDER[n] != e)
            ENGINE_ORDER[j++] = ENGINE_ORDER[n];
    eng_nshown = (uint8_t)j;
}
static void mod_engine_make(engine_t *e, const mod_desc_t *d)
{
    static const uint8_t KNOB[12] = {P_E0, P_E0 + 1, P_E0 + 2, P_E0 + 3, P_E0 + 4, P_E0 + 5, P_E0 + 6, P_E0 + 7,
                                     P_ATK, P_DEC, P_SUS, P_REL};
    memcpy(e, &ENG_MODULE, sizeof *e);
    e->name = d->name;
    e->page_title[0] = d->page_title[0];
    e->page_title[1] = d->page_title[1];
    memcpy(e->edit, d->edit, sizeof e->edit);
    e->presets = (const preset_t *)d->presets;
    e->npresets = d->npresets;
    for (uint32_t k = 0; k < 4u; k++)
        e->knob[k] = KNOB[d->knob[k] % 12u];
    e->poly = d->poly > NVOICE ? NVOICE : d->poly;
    e->keep = d->keep;
    if (!d->block)
        e->block = 0;                               /* (no block: idle parts need no state) */
}
/* slot k's module into its engine number: the descriptor first, then the number, then the table entry the audio
 * reads (each step a single store: a block in between sees the engine retired) */
static void mod_register(uint32_t k, const mod_desc_t *d, engine_t *e)
{
    uint32_t n = mods[k].engine;
    mod_engine_make(e, d);
    uint32_t g = mod_guard();
    mods[k].d = d;
    mod_at[n] = (uint8_t)(k + 1u);
    __asm__ volatile("" ::: "memory");
    ENGINES[n] = e;
    mods[k].state = MS_LOADED;
    mod_order_add(n);
    mod_unguard(g);
}
/* slot k's module out of SRAM; its engine stays listed (ENG_MOD_IDLE) when idle, else retired (the slot is going) */
static void mod_unregister(uint32_t k, int idle)
{
    uint32_t n = mods[k].engine, g = mod_guard();
    if (n < NENGINES && (mod_at[n] == k + 1u || (ENGINES[n] == &ENG_MOD_IDLE && mod_listed(k)))) {
        if (!idle)
            mod_order_remove(n);
        ENGINES[n] = idle ? &ENG_MOD_IDLE : &ENG_RETIRED;
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
