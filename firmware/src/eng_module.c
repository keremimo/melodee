/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Synth modules (module_abi.h): engines that live outside the image. A module file is read from wherever it is kept
 * (module_store.c: pool records; later an SD card) into a block of the audio working arena, its address words moved
 * to the block, checked, and from then on it is engine `engine`: ENGINES[engine] points at an engine_t made for it
 * (in the block, after the module's own bytes) whose callbacks are the ones below. They hand the module its part,
 * a copy of the voice and the voice's modulation, and copy the voice back.
 *
 * NMOD slots, loaded lazily: at power-on only the files' heads are read (module_store.c mod_catalog), and a stored
 * module's engine is ENG_MOD_IDLE: the lists show its name (eng_name), its sounds keep their values (retired
 * semantics), and a part asked to switch to it keeps its old engine until the module is in SRAM (mod_waiting).
 * Picking it (mod_want) or a part, a song section or a pattern using it (module_store.c mod_service) loads it; one
 * nothing has used for MOD_IDLE_MS goes back to the flash. Unloading points its engine at ENG_MOD_IDLE first, so no
 * audio block can be inside the module when its block is released. */
#include "module_abi.h"
#include "module_native.h"   /* native modules: an engine's own source, its own engine_t (SID, DRUM, CZ-1, FM6, the
                              * Prophet) */

enum { MS_EMPTY, MS_LOADED, MS_STORED, MS_BAD, MS_NORAM, MS_CLASH, MS_SKIPPED,
       MS_STALE };                                 /* mod_slot_t.state; STALE: a native module of another firmware */
typedef struct {
    uint8_t state, engine;                          /* MS_*; the engine number its file names */
    uint16_t version;
    uint32_t len;                                   /* the file's length (0: empty) */
    char name[MOD_NAME_MAX + 1u];
    uint8_t npresets;                               /* (its head: the browser's list without loading it; a native
                                                     * module's: its PRST chunk's, 0 without one) */
    uint8_t abi;                                    /* MOD_ABI or MOD_ABI_NATIVE */
    uint32_t crc, key;                              /* its head's CRC and (native) layout key: what the installer
                                                     * compares with its own files (editor_module.c) */
    uint32_t copy, xcopy, cut;                      /* where its file is (st_current's copies, the first record's
                                                     * length: mod_open) */
    uint32_t pst, pst_len, pst_dat;                 /* its factory presets (module_presets.h): the PRST payload's place
                                                     * in the file, its length (0: none), its dictionary's place in it */
    uint16_t pst_init, pst_dict, pst_ulen;          /* .. its INIT, dictionary and entry bytes */
    uint16_t cat_at;                                /* .. their categories in mod_cat (MOD_PST_NONE: in the file) */
    const mod_desc_t *d;                            /* MS_LOADED, ABI 1: the descriptor in the block */
    const mod_native_t *nd;                         /* MS_LOADED, native: its descriptor */
} mod_slot_t;
static mod_slot_t mods[NMOD];
static uint8_t mod_skipped;                         /* this boot did not load modules (mod_guard: last boot crashed) */
static uint32_t mod_last[NMOD];                     /* fm1_ms: when something last used slot k's engine */
static int (*mod_fetch)(uint32_t k);                /* module_store.c mod_bring: slot k into SRAM, MS_* */
#define MOD_IDLE_MS 3000u
/* a catalogued slot: its engine number points at its module (MS_LOADED) or at ENG_MOD_IDLE */
static int mod_listed(uint32_t k) { return mods[k].state == MS_LOADED || mods[k].state == MS_STORED || mods[k].state == MS_NORAM; }
static uint32_t mod_slot_of(uint32_t e)            /* the slot playing as engine e, NMOD: none */
{
    for (uint32_t k = 0; k < NMOD; k++)
        if (mod_listed(k) && mods[k].engine == e)
            return k;
    return NMOD;
}

/* ---- the host side of the contract */
static uint32_t mod_pitch_inc(uint32_t p) { return tuned_pitch_inc(p > 2047u ? 2047u : p); }
static void mod_mem_set(void *d, int c, uint32_t n) { memset(d, c, n); }
static void mod_mem_copy(void *d, const void *s, uint32_t n) { memcpy(d, s, n); }
static const mod_host_t MOD_HOST = {
    MOD_ABI, sizeof(mod_host_t), FS, CTL, VOICE_FS, SINE, TANH_Q15, SVF_G, dith,
    mod_pitch_inc, mod_mem_set, mod_mem_copy,
};
_Static_assert(CTL == 32 && VOICE_FS == 24000, "ABI 1 freezes the block length and the voice level");

/* ---- the firmware's side of a native module (module_native.h): its data and calls, handed over at load */
static void *mod_part_mem(uint32_t part, uint32_t bytes) { return resource_get(RES_ENGINE0 + part % NPART, bytes); }
static void voice_scale_pitch(const track_t *t, voice_t *v, uint32_t note);   /* voice.c */
static uint32_t trk_nvoice(const track_t *t);                                  /* voice.c */
static void p5_update_bend(track_t *t);                                        /* voice.c */
#if !MELODEE_DUAL_CORE
static int mod_no_worker;                           /* (no CPU1: a module's own pairing finds it never online) */
static uint32_t mod_no_pairs;
#endif
static const mod_imports_t MOD_IMPORTS = {
    trk, &song, &voice_was, &tuning_a4, dith, dith_f, midi_verb, drum_patch, &drum_patch_none,
    &drum_event_pitch, &drum_event_length, drum_flash, drk_user, &drk_user_gen, mod_part_mem, voice_scale_pitch,
    SINE, TANH_Q15, PITCH_OCT, A4_RATIO, P5_TANH,
    cz_patch, &cz_patch_none, FM6_SIN,
    fm6_patch, fm6_patch_none, fm6_pgen, fm6_lgen, fm6_on, fm6_fn,
#if MELODEE_DUAL_CORE
    &fm6_pairs,
#else
    &mod_no_pairs,
#endif
    trk_nvoice, &audio_batch_active, audio_batch_push, audio_batch_push_cost,
#if MELODEE_DUAL_CORE
    &audio_worker_online, audio_worker_submit, audio_worker_join,
#else
    &mod_no_worker, 0, 0,
#endif
#if MELODEE_AUDIO_PROFILE
    audio_profile_add,
#else
    0,
#endif
    FM6_MKI_LOG, FM6_MKI_EXP, FM6_OPL_LOG, FM6_OPL_EXP, A4_LOG,
    p5_patch, p5_ready, p5_update_bend,
};
#ifdef FM1_IRQ_TARGET
_Static_assert(sizeof(mod_native_t) == 28u && MOD_NHEAD == sizeof(mod_file_t) + 4u, "tools/mod_build.py ABI_NATIVE");
#endif
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
/* what a module's engine_t starts from (mod_engine_make), and a stored module's engine while it is not in SRAM
 * (ENG_MOD_IDLE: its callbacks find no descriptor, silent). tools/audio_ram.py roots these callbacks in SRAM */
static const engine_t ENG_MODULE = {
    .name = "-",
    .page_title = {"-", "-"},
    .edit = {{"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}},
    .note_on = modg_note_on,
    .render = modg_render,
    .block = modg_block,
    .knob = {P_E4, P_E5, P_E6, P_REL},
};
#define ENG_MOD_IDLE ENG_MODULE
static int mod_idle(uint32_t e) { return ENGINES[e] == &ENG_MOD_IDLE; }
/* the audio (voice.c engine_block): a part asked to switch to e keeps its engine while e's module is on its way */
static int mod_waiting(uint32_t e)
{
    uint32_t k = e < NENGINES && ENGINES[e] == &ENG_MOD_IDLE ? mod_slot_of(e) : NMOD;
    return k < NMOD && mods[k].state == MS_STORED;
}
