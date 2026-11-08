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

enum { MS_EMPTY, MS_LOADED, MS_STORED, MS_BAD, MS_NORAM, MS_CLASH, MS_SKIPPED,
       MS_STALE,                                   /* an older ABI's module, or another firmware's legacy data (key) */
       MS_NEWER };                                 /* a module that needs a later host table than this firmware's */
typedef struct {
    uint8_t state, engine;                          /* MS_*; its engine number here (eng_map.c: the device's map) */
    uint8_t want;                                   /* the number its file asks for */
    char tag[4];                                    /* its four-letter tag (its IDNT chunk's), 0-padded */
    uint32_t uid;                                   /* who it is (its IDNT chunk's; mod_abi3.h mod_ident_v3) */
    uint16_t version;
    uint32_t len;                                   /* the file's length (0: empty) */
    char name[MOD3_NAME_MAX + 1u];
    uint8_t npresets;                               /* (its head: the browser's list without loading it; its PRST
                                                     * chunk's, 0 without one) */
    uint8_t abi;                                    /* its file's ABI (MOD3_ABI; 1, 2: an older firmware's) */
    uint16_t host;                                  /* the host table version it needs */
    uint32_t crc, key;                              /* its head's CRC and legacy key: what the installer compares with
                                                     * its own files (editor_module.c) */
    uint16_t copy[MOD_RECS];                        /* where its file is: each record's current copy (mod_open; every
                                                     * record but the last ST_PROJECT_MAX bytes) */
    uint8_t parts;                                  /* .. its records */
    uint8_t sig;                                    /* its signature, as checked when it was installed (mod_sign.c): 0 not
                                                     * checked this run, 1 Melodee's, 2 none or not Melodee's */
    uint32_t pst, pst_len, pst_dat;                 /* its factory presets (module_presets.h): the PRST payload's place
                                                     * in the file, its length (0: none), its dictionary's place in it */
    uint16_t pst_init, pst_dict, pst_ulen;          /* .. its INIT, dictionary and entry bytes */
    uint16_t cat_at;                                /* .. their categories in mod_cat (MOD_PST_NONE: in the file) */
    uint32_t sch, sch_len;                          /* its schema (module_schema.h SCHM): its payload's place, length
                                                     * (0: none) */
    const mod_desc_v3 *d;                           /* MS_LOADED: its descriptor in the block */
} mod_slot_t;
static mod_slot_t mods[NMOD] __attribute__((section(".pool")));
static uint8_t mod_skipped;                         /* this boot did not load modules (mod_guard: last boot crashed) */
static uint8_t mod_blamed;                          /* the slot + 1 this boot left out: the last run crashed in its code
                                                     * (module_store.c mod_blame), 0 none */
static uint32_t mod_last[NMOD];                     /* fm1_ms: when something last used slot k's engine */
static int (*mod_fetch)(uint32_t k);                /* module_store.c mod_bring: slot k into SRAM, MS_* */
#define MOD_IDLE_MS 3000u
/* the module whose code the main loop calls (mod_run.loop: its slot + 1, for the crash blame): around each call */
static uint32_t mod_main_enter(uint32_t e)
{
    uint32_t was = mod_run.loop;
    mod_run.loop = e < NENGINES ? mod_at[e] : 0u;
    return was;
}
static void mod_main_leave(uint32_t was) { mod_run.loop = (uint8_t)was; }
#define MOD_MAIN(e, x) ({ uint32_t mm_ = mod_main_enter(e); __typeof__(x) mr_ = (x); mod_main_leave(mm_); mr_; })
#define MOD_MAIN_V(e, x) do { uint32_t mm_ = mod_main_enter(e); x; mod_main_leave(mm_); } while (0)
/* the module whose main-loop call this is (its UID; 0: none: the firmware's own) */
static uint32_t mod_running_uid(void)
{
    uint32_t k = mod_run.loop;
    return k && k <= NMOD ? mods[k - 1u].uid : 0u;
}
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
               MOD3_SAME_E(bend_range) && MOD3_SAME_E(patch_changed) && MOD3_SAME_E(pget) && MOD3_SAME_E(pput) &&
               MOD3_SAME_E(pformat) && MOD3_SAME_E(pactive) && MOD3_SAME_E(draw) && MOD3_SAME_E(sysex) &&
               MOD3_SAME_E(sysex_dump) && MOD3_SAME_E(rec_check) && MOD3_SAME_E(rec_take) && MOD3_SAME_E(rec_pad) &&
               sizeof(engine_t) == sizeof(mod_engine_v3),
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
#define MOD_TAB(i, t) case i: return t;
    switch (id) {
    SHARED_TABLES(MOD_TAB)                          /* (build/gen/melodee_tables.h: the generator's tables by id) */
#undef MOD_TAB
    default: return 0;
    }
}
static int mod_h_file_read(uint32_t self, uint32_t off, void *dst, uint32_t n);   /* eng_module_main.c */
/* the host's drawing for a module's own widget (schema.c sch_draw sets the canvas it draws into) */
static uint16_t mod_h_color(uint32_t role);
static void mod_h_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c);
static void mod_h_rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c);
static void mod_h_text(int32_t x, int32_t y, const char *s, uint32_t font, uint16_t c);
/* host 3's main-loop members: the main loop's code (sysex_route.c) sets the hooks at power-on (msx_reset); a build
 * without it (a host test of a part of the firmware) has none, and its members answer nothing */
static struct {
    mod_part_v3 *(*part)(uint32_t k);
    uint8_t *(*part_patch)(uint32_t k, uint32_t engine);
    int (*patch_put)(uint32_t k, uint32_t engine, const uint8_t *src, uint32_t n, uint32_t how);
    int (*sx_part)(uint32_t engine, uint32_t ch);
    int (*sx_send)(const uint8_t *f, uint32_t n);
    uint8_t *(*sx_scratch)(void);
    int (*sx_stage)(uint32_t engine, const uint8_t *patches, uint32_t count, uint32_t stride);
    uint32_t (*slot_get)(uint32_t engine, uint32_t slot, uint8_t *patch);
    uint32_t (*store_slot)(uint32_t engine);
    void (*message)(const char *s);
} msx_hook;
static mod_part_v3 *mod_h3_part(uint32_t k) { return msx_hook.part ? msx_hook.part(k) : (mod_part_v3 *)(void *)&trk[k % NTRK]; }
static uint8_t *mod_h3_part_patch(uint32_t k, uint32_t e) { return msx_hook.part_patch ? msx_hook.part_patch(k, e) : 0; }
static int mod_h3_patch_put(uint32_t k, uint32_t e, const uint8_t *src, uint32_t n, uint32_t how)
{
    return msx_hook.patch_put ? msx_hook.patch_put(k, e, src, n, how) : 1;
}
static int mod_h3_sx_part(uint32_t e, uint32_t ch) { return msx_hook.sx_part ? msx_hook.sx_part(e, ch) : -1; }
static int mod_h3_sx_send(const uint8_t *f, uint32_t n) { return msx_hook.sx_send ? msx_hook.sx_send(f, n) : -1; }
static uint8_t *mod_h3_sx_scratch(void) { return msx_hook.sx_scratch ? msx_hook.sx_scratch() : 0; }
static int mod_h3_sx_stage(uint32_t e, const uint8_t *p, uint32_t count, uint32_t stride)
{
    return msx_hook.sx_stage ? msx_hook.sx_stage(e, p, count, stride) : 0;
}
static uint32_t mod_h3_slot_get(uint32_t e, uint32_t k, uint8_t *p) { return msx_hook.slot_get ? msx_hook.slot_get(e, k, p) : 0; }
static uint32_t mod_h3_store_slot(uint32_t e) { return msx_hook.store_slot ? msx_hook.store_slot(e) : 0; }
static void mod_h3_message(const char *s) { if (msx_hook.message) msx_hook.message(s); }
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
    .patch = MOD3_CALL(uint8_t *(*)(const mod_part_v3 *), patch_play),
    .color = mod_h_color, .fill = mod_h_fill, .rrect = mod_h_rrect, .text = mod_h_text, .paired = &eng_paired,
    .part = mod_h3_part, .part_patch = mod_h3_part_patch, .patch_put = mod_h3_patch_put, .sx_part = mod_h3_sx_part,
    .sx_send = mod_h3_sx_send, .sx_scratch = mod_h3_sx_scratch, .sx_stage = mod_h3_sx_stage, .slot_get = mod_h3_slot_get,
    .store_slot = mod_h3_store_slot, .message = mod_h3_message, .ms = &fm1_ms,
    .note_pitch = &note_hit_pitch, .note_len = &note_hit_len, .rec = rec_tab, .rec_gen = &rec_gen, .rec_base = REC_BASE,
    .rec_begin = mod_h4_rec_begin, .rec_write = mod_h4_rec_write, .rec_commit = mod_h4_rec_commit,
    .rec_delete = mod_h4_rec_delete, .wdt = mod_h4_wdt,
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
