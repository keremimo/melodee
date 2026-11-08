/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table (order = PRESETS browsing order and the engine numbers of the editor protocol) and the parts'
 * sounds at power-on. */
#include "dsp.c"
#include "audio_batch.h"
#include "../modules/shipped.h" /* the synths Melodee makes: the default engine map, their names before their files */
#include "module_presets.h"     /* a synth module's factory presets, in its file (module_store.c reads them) */
#include "module_schema.h"      /* .. its patch and parameters (schema.c reads them) */
#include "patch.c"              /* the parts' patches: their engines' bytes */
#include "sysex_rx.c"           /* the synths' SysEx in: their schemas' patterns, collected for their modules */
static int preset_patch(uint32_t e, uint32_t k, void *dst, uint32_t n);   /* (below: factory preset k's patch) */
#if MELODEE_HOST_ENGINES
#define HOST_STAGE 1
#include "../modules/host/host_engines.c"   /* host builds: the synths Melodee ships, built in as their reference
                                             * (the device's image has none: eng_module.c plays their modules) */
#endif
#if MELODEE_LEGACY_EXTRAS
#include "eng_formant.c"
#endif
#if MELODEE_HOST_ENGINES
#undef HOST_STAGE
#define HOST_STAGE 2
#include "../modules/host/host_engines.c"
#endif
#include "eng_phys.c"           /* PHYS: DaisySP physical models (phys_dsp.c, MIT) */
#include "pads.h"               /* a synth's pads and data records: the firmware's side (pads.c, rec_store.c) */
#if MELODEE_HOST_ENGINES
#undef HOST_STAGE
#define HOST_STAGE 3
#include "../modules/host/host_engines.c"
#endif
#if MELODEE_LEGACY_EXTRAS
#include "eng_noise.c"
#endif
#if MELODEE_HOST_ENGINES
#undef HOST_STAGE
#define HOST_STAGE 4
#include "../modules/host/host_engines.c"
#endif
#include "eng_midi.c"
#if MELODEE_SLICE
#include "eng_slice.c"
#else
static void slice_gone_note_on(struct track *t, voice_t *v) { (void)t; (void)v; }
static void slice_gone_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
static const engine_t ENG_RETIRED = {         /* 13 without MELODEE_SLICE: reserved, never offered (eng_ok) */
    .name = "-",
    .page_title = {"-", "-"},
    .edit = {{"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}},
    .note_on = slice_gone_note_on,
    .render = slice_gone_render,
    .knob = {P_E4, P_E5, P_E6, P_REL},
};
#endif

/* Each track owns only its selected engine's working state. Simple engines
 * use voice_t alone. State is released after the old engine's switch fade. */
#include "resources.c"
#if MELODEE_MODULES
static uint8_t mod_at[NENGINES];                    /* engine -> its module's slot + 1, 0: none (eng_module.c) */
#endif
static int mod_idle(uint32_t e);                    /* e: a stored module not in SRAM (eng_module.c) */
static const engine_t *eng_of(uint32_t e);
static uint32_t eng_state_size(uint32_t e)
{
    const engine_t *g = eng_of(e);
    if (g->state)                                   /* (the engine's own: a synth module's) */
        return g->state(0);
    switch (e) {
#if MELODEE_LEGACY_EXTRAS
    case ENGI_PHYS: return sizeof(phys_slot_t) * PHYS_POLY;
#endif
#if MELODEE_HOST_ENGINES
    HOST_STATE_SIZES                                /* (host_engines.c: the retired engines reference builds play) */
#endif
#if MELODEE_SLICE
    case ENGI_SLICE: return sizeof(slc_rb_t);
#endif
    default: return 0;
    }
}
static void eng_state_clear(uint32_t part)
{
    if (part < NPART) resource_release(RES_ENGINE0 + part);
}
static void eng_state_reset(void) { for (uint32_t k = 0; k < NPART; k++) eng_state_clear(k); }
static int eng_state_prepare(const track_t *t)
{
    const engine_t *g = eng_of(t->engine);
    uint32_t size = g->state ? g->state(t) : eng_state_size(t->engine);
    return !size || resource_get(RES_ENGINE0 + (uint32_t)(t - trk), size) != 0;
}
static phys_slot_t *phys_slots(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(phys_slot_t) * PHYS_POLY); }
#if MELODEE_HOST_ENGINES
#undef HOST_STAGE
#define HOST_STAGE 5
#include "../modules/host/host_engines.c"   /* (the built-in engines' part memory, table places, list order, names) */
#endif
#if MELODEE_SLICE
static int16_t (*slc_rbuf(uint32_t part))[SLC_RB] { return resource_get(RES_ENGINE0 + part % NPART, sizeof(slc_rb_t)); }
#endif

/* the editor protocol, user presets and projects store these indices: append, never reorder. A loaded synth module
 * takes its number's entry (eng_module.c mod_register), so the table is in RAM with modules */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winitializer-overrides"
#if MELODEE_MODULES
static const engine_t *ENGINES[NENGINES] = {
#else
static const engine_t *const ENGINES[NENGINES] = {
#endif
    [0 ... NENGINES - 1] = &ENG_RETIRED,        /* retired and reserved numbers (0 ANALOG, 1 DIGITAL, 4 SAMPLE, 8 GRAIN, 14
                                                 * OBXF; 16..18, 20: user-preset namespaces; 13: SLICE), and every synth
                                                 * module's number while none is loaded: stored IDs kept, silent */
#if MELODEE_LEGACY_EXTRAS
    [5] = &ENG_FORMANT,                         /* VOICE (eng_formant.c: "voice" is a sounding note in voice.c) */
    [ENGI_PHYS] = &ENG_PHYS,
    [11] = &ENG_NOISE,
#endif
#if MELODEE_SLICE
    [ENGI_SLICE] = &ENG_SLICE,
#endif
    [ENGI_MIDI] = &ENG_MIDI,                    /* 21: USB MIDI Track */
#if MELODEE_HOST_ENGINES
    HOST_ENGINES                                /* (host builds: the shipped synths at their numbers) */
#endif
};
#pragma clang diagnostic pop

#if MELODEE_HOST_ENGINES
#undef HOST_STAGE
#define HOST_STAGE 6
#include "../modules/host/host_engines.c"   /* (sch_builtin_src, preset_builtin_patch: their schemas, their makers) */
#endif
/* a track's engine number as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }
static const engine_t *eng_of(uint32_t e) { return ENGINES[eng_idx(e)]; }

#include "presets.c"            /* the factory presets: fetched, named, their patches put on a part (a synth
                                 * module's from its file) */

/* the order the engines are shown in (PRESETS browsing and its ENG knob, the editor's list):
 * engine indices, never a retired one. The indices stay as they are (the
 * stores and the protocol hold them); only this table orders them */
#if MELODEE_MODULES
static uint8_t eng_nshown = NENG_SHOWN;             /* the built-in engines, then the loaded modules (mod_register) */
static uint8_t ENGINE_ORDER[NENG_SHOWN_MAX] = {
#else
#define eng_nshown NENG_SHOWN
static const uint8_t ENGINE_ORDER[NENG_SHOWN] = {
#endif
#if MELODEE_HOST_ENGINES
    HOST_ORDER                                      /* (host builds: the built-in engines in their manifest order) */
#endif
    ENGI_MIDI,
};
/* the manifest's rows (../modules/shipped.h): the synths Melodee makes, by number */
typedef struct { uint8_t num, wclass, store, ns, flags; uint64_t uid; const char *name, *tag; } ship_t;
#define SHIP_ROW(n, u, nm, tg, wc, st, ns, f) {n, wc, st, ns, f, u, nm, tg},
static const ship_t SHIP[] = {SHIP_ROWS(SHIP_ROW)};
#undef SHIP_ROW
#define NSHIP (sizeof SHIP / sizeof SHIP[0])
static const ship_t *ship_of(uint32_t e)            /* e's row, 0: none */
{
    for (uint32_t k = 0; k < NSHIP; k++)
        if (SHIP[k].num == e)
            return &SHIP[k];
    return 0;
}
static uint32_t ship_flag(uint32_t f)               /* the first row with flag f: its number (NENGINES: none) */
{
    for (uint32_t k = 0; k < NSHIP; k++)
        if (SHIP[k].flags & f)
            return SHIP[k].num;
    return NENGINES;
}
static uint32_t ship_store(uint32_t store)          /* the synth Melodee 1.x kept in store format store (NENGINES: none) */
{
    for (uint32_t k = 0; k < NSHIP; k++)
        if (store && SHIP[k].store == store)
            return SHIP[k].num;
    return NENGINES;
}
/* where an engine that comes as a module goes among them (eng_module_main.c mod_order_add): the shipped synths in the
 * manifest's order, the MIDI Track after them, any other after them all */
static uint32_t eng_order_rank(uint32_t e)
{
    uint32_t r = 0;
    for (uint32_t k = 0; k < NSHIP; k++)
        if (SHIP[k].flags & SHIP_F_MODS) {
            if (SHIP[k].num == e)
                return r;
            r++;
        }
    return e == ENGI_MIDI ? r : 255u;
}
/* the engine a sound on a reserved number becomes (its own while it is retired or one can pick it) */
#define ENG_FALLBACK ship_flag(SHIP_F_FALLBACK)

/* Retired synth records (ANALOG and DIGITAL since 2.0 too) retain their stored identity and render silence.
 * Never relabel retired records FM6. FM6 or the Prophet without its module (MELODEE_FM6_BUILTIN /
 * MELODEE_P5_BUILTIN 0): retired as the others, its sounds keep the number, silent, its programs the firmware's
 * (edited, stored, sent: never heard), and the start asks for setup (SHIPPED_OK) */
static int eng_extra_retired(uint32_t e)
{
#if MELODEE_MODULES
    if (e < NENGINES && mod_at[e])
        return 0;                                   /* a module plays it */
    if (e >= ENGI_MOD0 && e < NENGINES)
        return 1;                                   /* a module's number with no module: its sounds keep it, silent */
#endif
#if !MELODEE_PROPHET_PROTOTYPE
    if (e == ENGI_ANALOG) return 1;
#endif
    /* DIGITAL, a retired engine's number (PHASE VOICE TRIO WHEEL PHYS NOISE outside reference builds), a shipped synth
     * with no engine (no module for it): retired */
    return e == ENGI_DIGITAL || ((!MELODEE_LEGACY_EXTRAS && (e == 2u || e == 5u || e == 6u || e == 7u || e == 9u || e == 11u)) ||
                                 (ship_of(e) && (ship_of(e)->flags & SHIP_F_MODS) && !HOST_BUILTIN(e)));
}
/* the engines one can pick, in ENGINE_ORDER: eng_ok(e), the n-th of them
 * eng_vis(n), e's place among them eng_rank(e), the next / previous one eng_step(e, dir) (wraps) */
static int eng_ok(uint32_t e)
{
#if MELODEE_MODULES
    if (e < NENGINES && mod_idle(e))
        return 1;                                   /* picking it loads it (mod_want) */
#endif
    return e < NENGINES && e != USER_NATIVE_P5 && !(e >= 16u && e <= 18u) && !eng_extra_retired(e) && e != 4u && e != 8u && e != 14u && (MELODEE_SLICE || e != ENGI_SLICE);
}
/* an engine number for a sound: its own while it is retired or one can pick it, else FM6's (retired too while FM6
 * has no module: then such a sound is silent, as any retired one) */
static uint32_t eng_sound_idx(uint32_t e)
{
    return eng_extra_retired(e) || eng_ok(e) ? e : ENG_FALLBACK;
}
static uint32_t eng_vis(uint32_t n) { return ENGINE_ORDER[n % eng_nshown]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n;
    if (!eng_ok(e))
        e = ENG_FALLBACK;                        /* (a retired or reserved engine: FM6's place; without its module
                                                  * none: the first engine's, the Prophet's while it is there) */
    for (n = 0; n < eng_nshown && ENGINE_ORDER[n] != e; n++)
        ;
    return n < eng_nshown ? n : 0u;
}
static uint32_t eng_step(uint32_t e, int32_t dir)
{
    return eng_vis((eng_rank(e % NENGINES) + (dir > 0 ? 1u : eng_nshown - 1u)) % eng_nshown);
}

/* the parts at power-on and in a new song (engine, preset; the steps empty): the manifest's (../modules/shipped.h) */
static const uint8_t TRK_DEF[NPART][2] = SHIP_PARTS;
/* track k's power-on engine and preset: TRK_DEF's, a MIDI-only track the MIDI Track's */
static uint32_t trk_def_engine(uint32_t k) { return k < NPART ? TRK_DEF[k][0] : ENGI_MIDI; }
static uint32_t trk_def_preset(uint32_t k) { return k < NPART ? TRK_DEF[k][1] : 0u; }
#if MELODEE_MODULES
#include "eng_module.c"         /* synth modules: engines loaded into SRAM (module_store.c keeps them) */
#include "eng_module_main.c"    /* (their lists, loading, registration: main loop) */
#include "eng_map.c"            /* the device's engine map: number <-> UID, the stores' entries */
#include "schema.c"             /* a synth's schema (its file's or built in), the patches by it */
#include "pads.c"               /* a synth's pads (its schema's MOD3_R_PADS) */
#define SHIPPED_OK() mod_shipped_ok()  /* the synth modules the image needs are on the device (else: setup) */
#else
#define SHIPPED_OK() 1
static int mod_idle(uint32_t e) { (void)e; return 0; }
static uint32_t mod_le32(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
#define MOD_MAIN(e, x) (x)
#define MOD_MAIN_V(e, x) x
static uint32_t mod_running_uid(void) { return 0; }
#include "eng_map.c"
#include "schema.c"
#include "pads.c"
static const char *eng_name(uint32_t e) { return ENGINES[e % NENGINES]->name; }
static uint32_t eng_npresets(uint32_t e) { return ENGINES[e % NENGINES]->npresets; }
static void mod_want(uint32_t e) { (void)e; }
static int mod_waiting(uint32_t e) { (void)e; return 0; }
#endif
