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
#if MELODEE_HOST_ENGINES
#undef HOST_STAGE
#define HOST_STAGE 2
#include "../modules/host/host_engines.c"
#endif
#include "pads.h"               /* a synth's pads and data records: the firmware's side (pads.c, rec_store.c) */
#if MELODEE_HOST_ENGINES
#undef HOST_STAGE
#define HOST_STAGE 3
#include "../modules/host/host_engines.c"
#endif
#if MELODEE_HOST_ENGINES
#undef HOST_STAGE
#define HOST_STAGE 4
#include "../modules/host/host_engines.c"
#endif
#include "eng_midi.c"
static void eng_none_note_on(struct track *t, voice_t *v) { (void)t; (void)v; }
static void eng_none_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
static const engine_t ENG_NONE = {             /* a handle no synth here plays (none, a synth not on this device): silent */
    .name = "-",
    .page_title = {"-", "-"},
    .edit = {{"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}},
    .note_on = eng_none_note_on,
    .render = eng_none_render,
    .knob = {P_E4, P_E5, P_E6, P_REL},
};

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
#if MELODEE_HOST_ENGINES
#undef HOST_STAGE
#define HOST_STAGE 5
#include "../modules/host/host_engines.c"   /* (the built-in engines' part memory, table places, list order, names) */
#endif

/* the engines by handle (core.h: RAM only, the stores name synths by ID: eng_map.c). A loaded synth module takes its
 * handle's entry (eng_module.c mod_register), so the table is in RAM with modules */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winitializer-overrides"
#if MELODEE_MODULES
static const engine_t *ENGINES[NENGINES] = {
#else
static const engine_t *const ENGINES[NENGINES] = {
#endif
    [0 ... NENGINES - 1] = &ENG_NONE,        /* retired and reserved numbers (0 ANALOG, 1 DIGITAL, 4 SAMPLE, 8 GRAIN, 14
                                                 * OBXF; 16..18, 20: user-preset namespaces; 13: SLICE), and every synth
                                                 * module's number while none is loaded: stored IDs kept, silent */
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

/* the order the engines are shown in (PRESETS browsing and its ENG knob, the editor's list): handles, never none */
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
/* the manifest's rows (../modules/shipped.h): the synths Melodee makes, row k at handle ENG_H_SHIP0 + k */
typedef struct { uint8_t wclass, store, ns, flags; uint64_t uid; const char *name, *tag; } ship_t;
#define SHIP_ROW(u, nm, tg, wc, st, ns, f) {wc, st, ns, f, u, nm, tg},
static const ship_t SHIP[] = {SHIP_ROWS(SHIP_ROW)};
#undef SHIP_ROW
#define NSHIP (sizeof SHIP / sizeof SHIP[0])
static const uint64_t SHIP_RETIRED[] = {SHIP_RETIRED_IDS};
_Static_assert(ENG_H_SHIP0 + NSHIP <= NENGINES, "the shipped synths' handles: their rows' places (tests/identity_test.c "
               "holds shipped.h's ENGI_* to them)");
static const ship_t *ship_of(uint32_t e)            /* handle e's row, 0: none */
{
    return e >= ENG_H_SHIP0 && e - ENG_H_SHIP0 < NSHIP ? &SHIP[e - ENG_H_SHIP0] : 0;
}
static uint32_t ship_flag(uint32_t f)               /* the first row with flag f: its handle (NENGINES: none) */
{
    for (uint32_t k = 0; k < NSHIP; k++)
        if (SHIP[k].flags & f)
            return ENG_H_SHIP0 + k;
    return NENGINES;
}
static uint32_t ship_store(uint32_t store)          /* the synth Melodee 1.x kept in store format store (NENGINES: none) */
{
    for (uint32_t k = 0; k < NSHIP; k++)
        if (store && SHIP[k].store == store)
            return ENG_H_SHIP0 + k;
    return NENGINES;
}
/* where an engine that comes as a module goes among them (eng_module_main.c mod_order_add): the shipped synths in the
 * manifest's order, the MIDI Track after them, any other after them all */
static uint32_t eng_order_rank(uint32_t e)
{
    const ship_t *r = ship_of(e);
    return r ? (uint32_t)(r - SHIP) : e == ENGI_MIDI ? (uint32_t)NSHIP : 255u;
}

/* e names no synth this device plays: none, or a synth whose module is not here (its sounds keep their bytes, silent).
 * A stored module not in SRAM is not silent: picking it loads it (mod_want) */
static int eng_silent(uint32_t e)
{
    if (e == ENGI_MIDI || HOST_BUILTIN(e))
        return 0;
#if MELODEE_MODULES
    return !(e < NENGINES && mod_at[e]);
#else
    return 1;
#endif
}
/* the engines one can pick, in ENGINE_ORDER: eng_ok(e), the n-th of them
 * eng_vis(n), e's place among them eng_rank(e), the next / previous one eng_step(e, dir) (wraps) */
static int eng_ok(uint32_t e)
{
#if MELODEE_MODULES
    if (e < NENGINES && mod_idle(e))
        return 1;                                   /* picking it loads it (mod_want) */
#endif
    return e < NENGINES && !eng_silent(e);
}
/* the handle for a sound: its own (a synth not here keeps its sound, silent) */
static uint32_t eng_sound_idx(uint32_t e) { return e < NENGINES ? e : ENG_H_NONE; }
static uint32_t eng_vis(uint32_t n) { return ENGINE_ORDER[n % eng_nshown]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n;
    for (n = 0; n < eng_nshown && ENGINE_ORDER[n] != e; n++)
        ;
    return n < eng_nshown ? n : 0u;                 /* (a synth one cannot pick: the first engine's place) */
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
