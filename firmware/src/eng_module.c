/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Synth modules (ABI 3: firmware/modules/sdk/mod_abi3.h): engines that live outside the image. A module file is read
 * from wherever it is kept (module_store.c: pool records; later an SD card) into a block of the audio working arena,
 * its address words moved to the block, checked, bound to the host table below, and from then on it is engine
 * `engine`: ENGINES[engine] points at a copy of its mod_engine_v3 (in the block, after the module's own bytes). Its
 * callbacks get the firmware's own track_t and voice_t, read through the frozen heads they begin with (asserted here),
 * so a module built once plays on every firmware that keeps this contract.
 *
 * NMOD slots, loaded lazily: at power-on only the files' heads are read (module_store.c mod_catalog), and a stored
 * module's engine is ENG_MOD_IDLE: the lists show its name (eng_name), its sounds keep their values (retired
 * semantics), and a part asked to switch to it keeps its old engine until the module is in SRAM (mod_waiting).
 * Picking it (mod_want) or a part, a song section or a pattern using it (module_store.c mod_service) loads it; one
 * nothing has used for MOD_IDLE_MS goes back to the flash. Unloading points its engine at ENG_MOD_IDLE first, so no
 * audio block can be inside the module when its block is released. */
#include "module_legacy.h"   /* TEMPORARY: the five shipped synths' data, until the patch blob */

enum { MS_EMPTY, MS_LOADED, MS_STORED, MS_BAD, MS_NORAM, MS_CLASH, MS_SKIPPED,
       MS_STALE,                                   /* an older ABI's module, or another firmware's legacy data (key) */
       MS_NEWER };                                 /* a module that needs a later host table than this firmware's */
typedef struct {
    uint8_t state, engine;                          /* MS_*; the engine number its file names */
    uint16_t version;
    uint32_t len;                                   /* the file's length (0: empty) */
    char name[MOD3_NAME_MAX + 1u];
    uint8_t npresets;                               /* (its head: the browser's list without loading it; its PRST
                                                     * chunk's, 0 without one) */
    uint8_t abi;                                    /* its file's ABI (MOD3_ABI; 1, 2: an older firmware's) */
    uint16_t host;                                  /* the host table version it needs */
    uint32_t crc, key;                              /* its head's CRC and legacy key: what the installer compares with
                                                     * its own files (editor_module.c) */
    uint32_t copy, xcopy, cut;                      /* where its file is (st_current's copies, the first record's
                                                     * length: mod_open) */
    uint32_t pst, pst_len, pst_dat;                 /* its factory presets (module_presets.h): the PRST payload's place
                                                     * in the file, its length (0: none), its dictionary's place in it */
    uint16_t pst_init, pst_dict, pst_ulen;          /* .. its INIT, dictionary and entry bytes */
    uint16_t cat_at;                                /* .. their categories in mod_cat (MOD_PST_NONE: in the file) */
    const mod_desc_v3 *d;                           /* MS_LOADED: its descriptor in the block */
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

/* ---- the contract: the firmware's structs begin with ABI 3's views, its numbers are the frozen ones */
#define MOD3_SAME(T, f, V, g) (__builtin_offsetof(T, f) == __builtin_offsetof(V, g) && \
                               sizeof(((T *)0)->f) == sizeof(((V *)0)->g))
_Static_assert(MOD3_SAME(track_t, p, mod_part_v3, p) && MOD3_SAME(track_t, size, mod_part_v3, size) &&
               MOD3_SAME(track_t, index, mod_part_v3, index) && MOD3_SAME(track_t, engine, mod_part_v3, engine) &&
               MOD3_SAME(track_t, v, mod_part_v3, voices) && MOD3_SAME(track_t, vsize, mod_part_v3, vsize) &&
               MOD3_SAME(track_t, nmono, mod_part_v3, nmono) && MOD3_SAME(track_t, mono_stack, mod_part_v3, mono_stack) &&
               MOD3_SAME(track_t, mw, mod_part_v3, mw) && MOD3_SAME(track_t, at, mod_part_v3, at) &&
               MOD3_SAME(track_t, foot, mod_part_v3, foot) && MOD3_SAME(track_t, breath, mod_part_v3, breath) &&
               MOD3_SAME(track_t, porta, mod_part_v3, porta) && MOD3_SAME(track_t, bend_raw, mod_part_v3, bend_raw) &&
               MOD3_SAME(track_t, rsv, mod_part_v3, rsv) && sizeof(track_t) >= sizeof(mod_part_v3),
               "track_t begins with mod_part_v3 (mod_abi3.h)");
_Static_assert(MOD3_SAME(voice_t, note, mod_voice_v3, note) && MOD3_SAME(voice_t, vel, mod_voice_v3, vel) &&
               MOD3_SAME(voice_t, gate, mod_voice_v3, gate) && MOD3_SAME(voice_t, active, mod_voice_v3, active) &&
               MOD3_SAME(voice_t, stage, mod_voice_v3, stage) && MOD3_SAME(voice_t, mvel, mod_voice_v3, mvel) &&
               MOD3_SAME(voice_t, index, mod_voice_v3, index) && MOD3_SAME(voice_t, env, mod_voice_v3, env) &&
               MOD3_SAME(voice_t, env_out, mod_voice_v3, env_out) && MOD3_SAME(voice_t, pitch16, mod_voice_v3, pitch16) &&
               MOD3_SAME(voice_t, pitch_cur, mod_voice_v3, pitch_cur) && MOD3_SAME(voice_t, ph, mod_voice_v3, ph) &&
               MOD3_SAME(voice_t, age, mod_voice_v3, age) &&
               __builtin_offsetof(voice_t, s) == __builtin_offsetof(mod_voice_v3, s) &&
               sizeof(voice_t) >= sizeof(mod_voice_v3) && sizeof(voice_t) % 4u == 0, "voice_t begins with mod_voice_v3");
#if !MELODEE_LEGACY_EXTRAS
_Static_assert(MOD3_SAME(voice_t, s, mod_voice_v3, s) && MOD3_SAME(voice_t, rsv, mod_voice_v3, rsv), "voice_t s, rsv");
#endif
#define MOD3_SAME_E(f) MOD3_SAME(engine_t, f, mod_engine_v3, f)
_Static_assert(MOD3_SAME_E(name) && MOD3_SAME_E(page_title) && MOD3_SAME_E(edit) && MOD3_SAME_E(presets) &&
               MOD3_SAME_E(npresets) && MOD3_SAME_E(knob) && MOD3_SAME_E(poly) && MOD3_SAME_E(sampled) &&
               MOD3_SAME_E(keep) && MOD3_SAME_E(oneshot) && MOD3_SAME_E(note_on) && MOD3_SAME_E(render) &&
               MOD3_SAME_E(amp) && MOD3_SAME_E(desc) && MOD3_SAME_E(block) && MOD3_SAME_E(keys) &&
               MOD3_SAME_E(ownenv) && MOD3_SAME_E(done) && MOD3_SAME_E(alloc) && MOD3_SAME_E(legato) &&
               MOD3_SAME_E(mono_key) && MOD3_SAME_E(post) && MOD3_SAME_E(cap) && MOD3_SAME_E(units) &&
               MOD3_SAME_E(preset_make) && MOD3_SAME_E(state) && MOD3_SAME_E(env_q15) && MOD3_SAME_E(chord) &&
               MOD3_SAME_E(reuse) && MOD3_SAME_E(batch) && MOD3_SAME_E(render_p) && MOD3_SAME_E(wclass) &&
               MOD3_SAME_E(menv) && MOD3_SAME_E(join) && MOD3_SAME_E(reserve) && MOD3_SAME_E(caps) &&
               MOD3_SAME_E(bend_range) && sizeof(engine_t) == sizeof(mod_engine_v3),
               "engine_t is mod_engine_v3 (mod_abi3.h)");
_Static_assert(P_COUNT == MOD3_P_COUNT && P_E0 == MOD3_P_E0 && NVOICE == MOD3_NVOICE && NPART == MOD3_NPART &&
               FS == MOD3_RATE && CTL == MOD3_CTL && VOICE_FS == MOD3_VOICE_FS && DITH_N == MOD3_DITH_N &&
               P_ATK == MOD3_P_ATK && P_DEC == MOD3_P_DEC && P_SUS == MOD3_P_SUS && P_REL == MOD3_P_REL &&
               P_VOICE == MOD3_P_VOICE && G_TUNE == MOD3_G_TUNE && AP_COUNT == MOD3_AP_COUNT && F_ALIAS + 1 == MOD3_F_COUNT &&
               F_ENUM == MOD3_F_ENUM && F_ONOFF == MOD3_F_ONOFF && F_FMFRQ == MOD3_F_FMFRQ,
               "the numbers ABI 3 freezes (mod_abi3.h): another ABI");
#ifdef FM1_IRQ_TARGET
_Static_assert(sizeof(mod_desc_v3) == 32u && sizeof(mod_file3_t) == MOD3_HEAD && sizeof(mod_part_v3) == 300u &&
               sizeof(mod_voice_v3) == 68u && sizeof(mod_vmod_v3) == 36u && sizeof(mod_param_v3) == 20u &&
               __builtin_offsetof(mod_engine_v3, note_on) == 188u && __builtin_offsetof(mod_engine_v3, render) == 192u &&
               MOD3_BATCH_BYTES <= AUDIO_BATCH_BYTES,
               "ABI 3 layouts on the device (tools/mod_build.py)");
#endif

/* ---- the host table (mod_host_v3): what a module reaches of the firmware */
static void *mod_part_mem(uint32_t part, uint32_t bytes) { return resource_get(RES_ENGINE0 + part % NPART, bytes); }
static void voice_scale_pitch(const track_t *t, voice_t *v, uint32_t note);   /* voice.c */
static uint32_t trk_nvoice(const track_t *t);                                  /* voice.c */
static uint32_t mod_h_pitch_inc(uint32_t p) { return tuned_pitch_inc(p > 2047u ? 2047u : p); }
static int32_t mod_h_octave(const mod_part_v3 *t) { return input_octave((const track_t *)(const void *)t); }
#if !MELODEE_DUAL_CORE
static const int mod_no_worker;                     /* (no CPU1: a module's own pairing finds it never online) */
static uint32_t mod_no_pairs;
static int mod_h_no_submit(void (*fn)(void *), void *context) { (void)fn; (void)context; return 0; }
static int mod_h_no_join(void) { return 0; }
#endif
#if !MELODEE_AUDIO_PROFILE
static void mod_h_no_profile(uint32_t cls, uint32_t ticks) { (void)cls; (void)ticks; }
#endif
static const void *mod_h_table(uint32_t id)
{
    switch (id) {
    case MOD3_T_SINE: return SINE;
    case MOD3_T_TANH_Q15: return TANH_Q15;
    case MOD3_T_SVF_G: return SVF_G;
    case MOD3_T_PITCH_OCT: return PITCH_OCT;
    case MOD3_T_A4_RATIO: return A4_RATIO;
    case MOD3_T_A4_LOG: return A4_LOG;
    case MOD3_T_P5_TANH: return P5_TANH;
    case MOD3_T_FM6_SIN: return FM6_SIN;
    case MOD3_T_FM6_MKI_LOG: return FM6_MKI_LOG;
    case MOD3_T_FM6_MKI_EXP: return FM6_MKI_EXP;
    case MOD3_T_FM6_OPL_LOG: return FM6_OPL_LOG;
    case MOD3_T_FM6_OPL_EXP: return FM6_OPL_EXP;
    default: return 0;
    }
}
static int mod_h_file_read(uint32_t self, uint32_t off, void *dst, uint32_t n);   /* eng_module_main.c */
static const mod_legacy_v3 MOD_LEGACY = {          /* TEMPORARY (module_legacy.h) */
    drum_patch, &drum_patch_none, &drum_event_pitch, &drum_event_length, drum_flash, drk_user, &drk_user_gen,
    cz_patch, &cz_patch_none,
    fm6_patch, fm6_patch_none, fm6_pgen, fm6_lgen, fm6_on, fm6_fn,
#if MELODEE_DUAL_CORE
    &fm6_pairs,
#else
    &mod_no_pairs,
#endif
    p5_patch, p5_ready,
};
#define MOD3_CALL(T, f) ((T)(void (*)(void))(f))   /* (track_t, voice_t: read as their views) */
static const mod_host_v3 MOD_HOST3 = {
    .version = MOD3_HOST, .size = sizeof(mod_host_v3),
    .was = &voice_was, .a4 = &tuning_a4, .song_g = song.g, .midi_rel = midi_verb, .dither = dith, .dither_f = dith_f,
    .table = mod_h_table,
    .part_mem = mod_part_mem,
    .scale_pitch = MOD3_CALL(void (*)(const mod_part_v3 *, mod_voice_v3 *, uint32_t), voice_scale_pitch),
    .nvoice = MOD3_CALL(uint32_t (*)(const mod_part_v3 *), trk_nvoice),
    .pitch_inc = mod_h_pitch_inc,
    .octave = mod_h_octave,
    .batch_active = &audio_batch_active, .batch_push = audio_batch_push, .batch_push_cost = audio_batch_push_cost,
#if MELODEE_DUAL_CORE
    .worker_online = &audio_worker_online, .worker_submit = audio_worker_submit, .worker_join = audio_worker_join,
#else
    .worker_online = &mod_no_worker, .worker_submit = mod_h_no_submit, .worker_join = mod_h_no_join,
#endif
#if MELODEE_AUDIO_PROFILE
    .profile_add = audio_profile_add,
#else
    .profile_add = mod_h_no_profile,
#endif
    .lz_unpack = lz_unpack,
    .file_read = mod_h_file_read,
    .legacy = &MOD_LEGACY,
};

/* a stored module's engine while it is not in SRAM: its lists' names come from its file (eng_name), its sounds keep
 * their values and play silent (retired semantics); tools/audio_ram.py roots these callbacks in SRAM */
static const engine_t ENG_MOD_IDLE = {
    .name = "-",
    .page_title = {"-", "-"},
    .edit = {{"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}},
    .note_on = slice_gone_note_on,
    .render = slice_gone_render,
    .knob = {P_E4, P_E5, P_E6, P_REL},
};
static int mod_idle(uint32_t e) { return ENGINES[e] == &ENG_MOD_IDLE; }
/* the audio (voice.c engine_block): a part asked to switch to e keeps its engine while e's module is on its way */
static int mod_waiting(uint32_t e)
{
    uint32_t k = e < NENGINES && ENGINES[e] == &ENG_MOD_IDLE ? mod_slot_of(e) : NMOD;
    return k < NMOD && mods[k].state == MS_STORED;
}
