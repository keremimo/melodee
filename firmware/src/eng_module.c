/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Synth modules (module_abi.h): engines that live outside the image. A module file is read from wherever it is kept
 * (module_store.c: pool records; later an SD card) into a block of the audio working arena, its address words moved
 * to the block, checked, and from then on it is engine `engine`: ENGINES[engine] points at an engine_t made for it
 * (in the block, after the module's own bytes) whose callbacks are the ones below. They hand the module its part,
 * a copy of the voice and the voice's modulation, and copy the voice back.
 *
 * NMOD slots. A slot's module stays in SRAM while it is loaded (modules are small: TRIO is 3.6 KB); unloading
 * points its engine back at ENG_RETIRED first (a track on it goes silent, keeps its number and values), so no audio
 * block can be inside the module when its block is released. */
#include "module_abi.h"

enum { MS_EMPTY, MS_LOADED, MS_STORED, MS_BAD, MS_NORAM, MS_CLASH, MS_SKIPPED };   /* mod_slot_t.state */
typedef struct {
    uint8_t state, engine;                          /* MS_*; the engine number its file names */
    uint16_t version;
    uint32_t len;                                   /* the file's length (0: empty) */
    char name[MOD_NAME_MAX + 1u];
    const mod_desc_t *d;                            /* MS_LOADED: the descriptor in the block */
} mod_slot_t;
static mod_slot_t mods[NMOD];
static uint8_t mod_skipped;                         /* this boot did not load modules (mod_guard: last boot crashed) */

/* ---- the host side of the contract */
static uint32_t mod_pitch_inc(uint32_t p) { return tuned_pitch_inc(p > 2047u ? 2047u : p); }
static void mod_mem_set(void *d, int c, uint32_t n) { memset(d, c, n); }
static void mod_mem_copy(void *d, const void *s, uint32_t n) { memcpy(d, s, n); }
static const mod_host_t MOD_HOST = {
    MOD_ABI, sizeof(mod_host_t), FS, CTL, VOICE_FS, SINE, TANH_Q15, SVF_G, dith,
    mod_pitch_inc, mod_mem_set, mod_mem_copy,
};
_Static_assert(CTL == 32 && VOICE_FS == 24000, "ABI 1 freezes the block length and the voice level");
_Static_assert(F_INT == MF_INT && F_PCT == MF_PCT && F_CUTOFF == MF_CUTOFF && F_SEMI == MF_SEMI && F_ENUM == MF_ENUM &&
               F_ONOFF == MF_ONOFF, "ABI 1 freezes the parameter formats");
_Static_assert(sizeof(mod_param_t) == sizeof(param_desc_t) &&
               __builtin_offsetof(mod_param_t, def) == __builtin_offsetof(param_desc_t, def) &&
               __builtin_offsetof(mod_param_t, names) == __builtin_offsetof(param_desc_t, names) &&
               __builtin_offsetof(mod_param_t, unit) == __builtin_offsetof(param_desc_t, unit), "mod_param_t");
_Static_assert(sizeof(mod_preset_t) == sizeof(preset_t) &&
               __builtin_offsetof(mod_preset_t, env) == __builtin_offsetof(preset_t, env) &&
               __builtin_offsetof(mod_preset_t, mono) == __builtin_offsetof(preset_t, mono), "mod_preset_t");
_Static_assert(sizeof(mod_vmod_t) == sizeof(vmod_t) &&
               __builtin_offsetof(mod_vmod_t, amp1) == __builtin_offsetof(vmod_t, amp1) &&
               __builtin_offsetof(mod_vmod_t, plog) == __builtin_offsetof(vmod_t, plog), "mod_vmod_t");
#ifdef FM1_IRQ_TARGET
_Static_assert(sizeof(mod_desc_t) == 216u && sizeof(mod_file_t) == 52u, "ABI 1 layouts (tools/mod_build.py)");
#endif

static const mod_desc_t *mod_desc_of(uint32_t e)
{
    uint32_t s = e < NENGINES ? mod_at[e] : 0u;
    return s ? mods[s - 1u].d : 0;
}
static uint32_t mod_part_bytes(uint32_t e)
{
    const mod_desc_t *d = mod_desc_of(e);
    return d ? d->part_bytes : 0u;
}

/* ---- the engine callbacks (audio; one part at a time) */
static mod_ctx_t mod_ctx;
static mod_ctx_t *modg_ctx(track_t *t, const mod_desc_t *d)
{
    uint32_t k = (uint32_t)(t - trk) % NPART;
    mod_ctx.e = t->p + P_E0;
    mod_ctx.part = d->part_bytes ? resource_get(RES_ENGINE0 + k, d->part_bytes) : 0;
    mod_ctx.part_index = (uint8_t)k;
    mod_ctx.voice_mode = (uint8_t)t->p[P_VOICE];
    mod_ctx.held = 0;
    return d->part_bytes && !mod_ctx.part ? 0 : &mod_ctx;
}
static void modg_vin(mod_voice_t *w, const voice_t *v, const track_t *t)
{
    w->note = v->note; w->vel = v->vel; w->gate = v->gate; w->index = (uint8_t)(v - t->v);
    w->was = voice_was; w->age = v->age;
    for (uint32_t k = 0; k < 3u; k++) w->ph[k] = v->ph[k];
    for (uint32_t k = 0; k < 6u; k++) w->s[k] = v->s[k];
}
static void modg_vout(voice_t *v, const mod_voice_t *w)
{
    for (uint32_t k = 0; k < 3u; k++) v->ph[k] = w->ph[k];
    for (uint32_t k = 0; k < 6u; k++) v->s[k] = w->s[k];
}
static void modg_note_on(track_t *t, voice_t *v)
{
    const mod_desc_t *d = mod_desc_of(t->engine);
    mod_ctx_t *c;
    mod_voice_t w;
    if (!d || !(c = modg_ctx(t, d)))
        return;
    for (uint32_t k = 0; k < NVOICE; k++)          /* the keys down (WHEEL's single-trigger percussion) */
        if (t->v[k].active && t->v[k].gate && t->v[k].stage != 4u)
            c->held |= (uint16_t)(1u << k);
    modg_vin(&w, v, t);
    d->note_on(c, &w);
    modg_vout(v, &w);
}
static void modg_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const mod_desc_t *d = mod_desc_of(t->engine);
    mod_ctx_t *c;
    mod_voice_t w;
    if (!d || !(c = modg_ctx(t, d)))
        return;
    modg_vin(&w, v, t);
    d->render(c, &w, out, n > CTL ? CTL : n, (const mod_vmod_t *)m);
    modg_vout(v, &w);
}
static void modg_block(track_t *t)
{
    const mod_desc_t *d = mod_desc_of(t->engine);
    mod_ctx_t *c;
    if (d && d->block && (c = modg_ctx(t, d)))
        d->block(c);
}
/* what a module's engine_t starts from (tools/audio_ram.py roots these callbacks in SRAM) */
static const engine_t ENG_MODULE = {
    .name = "-",
    .page_title = {"-", "-"},
    .note_on = modg_note_on,
    .render = modg_render,
    .block = modg_block,
    .knob = {P_E4, P_E5, P_E6, P_REL},
};

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
static void mod_unregister(uint32_t k)
{
    uint32_t n = mods[k].engine, g = mod_guard();
    if (mods[k].state == MS_LOADED && n < NENGINES && mod_at[n] == k + 1u) {
        mod_order_remove(n);
        ENGINES[n] = &ENG_RETIRED;
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
