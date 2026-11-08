/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table (order = PRESETS browsing order and the engine numbers of the editor protocol) and the parts'
 * sounds at power-on. */
#include "dsp.c"
#include "audio_batch.h"
#include "module_presets.h"     /* a synth module's factory presets, in its file (module_store.c reads them) */
static int preset_patch(uint32_t e, uint32_t k, void *dst, uint32_t n);   /* (below: factory preset k's patch) */
#if MELODEE_PROPHET
#include "p5_parts.h"           /* the parts' Prophet programs (the firmware's: the engine reads them), the voice count */
#include "prophet_programs.c"   /* Sequential's programs, the presets made from them, a program accepted on a part */
#if MELODEE_P5_BUILTIN
#include "eng_prophet_test.c"   /* PROPHET: Sequential's Rev 4 (a synth module on the device: firmware/modules/prophet) */
#elif MELODEE_PROPHET_PROTOTYPE
#error "MELODEE_PROPHET_PROTOTYPE plays the built-in Prophet as engine 0: MELODEE_P5_BUILTIN=1"
#endif
#endif
#include "cz_parts.h"           /* the parts' CZ-1 tones (the firmware's: the engine reads them) */
#if MELODEE_CZ_BUILTIN || MELODEE_LEGACY_EXTRAS
#include "eng_phase.c"          /* PHASE (retired), the core CZ-1 keeps */
#endif
#include "cz_tones.c"           /* Casio's tones, the presets made from them, COMP, the SysEx bytes */
#if MELODEE_CZ_BUILTIN
#include "eng_cz.c"             /* CZ-1 (a synth module on the device: firmware/modules/cz) */
#endif
#if MELODEE_SID_BUILTIN || MELODEE_DRUM_BUILTIN
#include "eng_sid.c"                /* SID (a synth module on the device: firmware/modules/sid); KIT SID's chip */
#endif
#if MELODEE_LEGACY_EXTRAS
#include "eng_formant.c"
#endif
#include "eng_trio.c"
#include "eng_wheel.c"
#include "eng_phys.c"           /* PHYS: DaisySP physical models (phys_dsp.c, MIT) */
#include "drum_map.c"           /* what the firmware knows of DRUM: its lanes, the GM map, the kits' numbers, the pads */
#include "drum_kit.h"           /* the user kits' records (kit_store.c) */
#if MELODEE_DRUM_BUILTIN
#include "eng_drum.c"           /* DRUM: synthesized 808 circuits (drum_808.c), the 909, a C64's SID (drum_sid.c), CR78, FM
                                 * (a synth module on the device: firmware/modules/drum) */
#endif
#if MELODEE_LEGACY_EXTRAS
#include "eng_noise.c"
#endif
#include "fm6_parts.h"          /* the parts' FM6 voices (the firmware's: the engine reads them), the 32 algorithms */
#include "fm6_voices.c"         /* the function settings, factory voices, patch formats and writes, DX7 SysEx in */
#if MELODEE_FM6_BUILTIN
#include "eng_fm6.c"            /* FM6: 6-operator FM rendered as Dexed renders it (fm6_core.c; a synth module on the
                                 * device: firmware/modules/fm6) */
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
static int mod_idle(uint32_t e);                    /* e: a stored module not in SRAM (eng_module.c) */
#endif
static const engine_t *eng_of(uint32_t e);
static uint32_t eng_state_size(uint32_t e)
{
    const engine_t *g = eng_of(e);
    if (g->state)                                   /* (the engine's own: a synth module's) */
        return g->state(0);
    switch (e) {
#if MELODEE_LEGACY_EXTRAS
    case ENGI_PHYS: return sizeof(phys_slot_t) * PHYS_POLY;
    case 7: return sizeof(drw_part_t);
#endif
#if MELODEE_LEGACY_EXTRAS
    case 2: return sizeof(cz_part_t);               /* (PHASE: CZ-1's core; CZ-1's own: engine_t.state) */
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
#if MELODEE_DRUM_BUILTIN
static drum909_part_t *drum909_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(drum909_part_t)); }
static drum_lane_t *drum_kit_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(drum_lane_t) * DV_NLANE); }
static dsk_part_t *dsk_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(dsk_part_t)); }
static dlk_part_t *dlk_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(dlk_part_t)); }
#if MELODEE_USER_KITS
static drk_part_t *drk_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(drk_part_t)); }
#endif
#endif
static drw_part_t *drw_of(const track_t *t) { return resource_get(RES_ENGINE0 + (uint32_t)(t - trk), sizeof(drw_part_t)); }
#if MELODEE_FM6_BUILTIN
static fm6_part_t *fm6_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(fm6_part_t)); }
#endif
#if MELODEE_CZ_BUILTIN || MELODEE_LEGACY_EXTRAS
static cz_part_t *cz_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(cz_part_t)); }
#endif
#if MELODEE_SID_BUILTIN || MELODEE_DRUM_BUILTIN
static sid_part_t *sid_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(sid_part_t)); }
#endif
#if MELODEE_PROPHET && MELODEE_P5_BUILTIN
static p5_part_t *p5_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(p5_part_t)); }
#endif
#if MELODEE_SLICE
static int16_t (*slc_rbuf(uint32_t part))[SLC_RB] { return resource_get(RES_ENGINE0 + part % NPART, sizeof(slc_rb_t)); }
#endif

/* the editor protocol, user presets and projects store these indices: append, never reorder. A loaded synth module
 * takes its number's entry (eng_module.c mod_register), so the table is in RAM with modules */
#if MELODEE_MODULES
static const engine_t *ENGINES[NENGINES] = {
#else
static const engine_t *const ENGINES[NENGINES] = {
#endif
#if MELODEE_PROPHET_PROTOTYPE
    &ENG_P5_TEST,                /* test firmware only: RAM patches, NEVER persist */
#else
    &ENG_RETIRED,                /* 0: retired ANALOG; stored ID reserved */
#endif
    &ENG_RETIRED,                /* 1: retired DIGITAL; stored ID reserved */
#if MELODEE_LEGACY_EXTRAS
    &ENG_PHASE,                  /* 2 */
#else
    &ENG_RETIRED,                /* 2: retired PHASE; stored ID reserved (its core: CZ-1's) */
#endif
#if MELODEE_SID_BUILTIN
    &ENG_SID,                    /* 3: SID replaces LOFI without reusing a stored ID */
#else
    &ENG_RETIRED,                /* 3: SID, a synth module (firmware/modules/sid) */
#endif
    &ENG_RETIRED,                /* 4: retired SAMPLE; reserved to preserve stored indices */
#if MELODEE_LEGACY_EXTRAS
    &ENG_FORMANT,                /* 5 VOICE (eng_formant.c: "voice" is a sounding note in voice.c) */
#else
    &ENG_RETIRED,                /* 5: retired VOICE; stored ID reserved */
#endif
#if MELODEE_LEGACY_EXTRAS
    &ENG_TRIO,
#else
    &ENG_RETIRED,                /* 6: retired TRIO; stored ID reserved */
#endif
#if MELODEE_LEGACY_EXTRAS
    &ENG_WHEEL,
#else
    &ENG_RETIRED,                /* 7: retired WHEEL; stored ID reserved */
#endif
    &ENG_RETIRED,                /* 8: retired GRAIN; stored index reserved */
#if MELODEE_LEGACY_EXTRAS
    &ENG_PHYS,
#else
    &ENG_RETIRED,                /* 9: retired PHYS; stored ID reserved */
#endif
#if MELODEE_DRUM_BUILTIN
    &ENG_DRUM,                   /* 10 (ENGI_DRUM) */
#else
    &ENG_RETIRED,                /* 10 (ENGI_DRUM): DRUM, a synth module (firmware/modules/drum) */
#endif
#if MELODEE_LEGACY_EXTRAS
    &ENG_NOISE,                  /* 11 */
#else
    &ENG_RETIRED,                /* 11: retired NOISE; stored ID reserved */
#endif
#if MELODEE_FM6_BUILTIN
    &ENG_FM6,                    /* 12 (ENGI_FM6) */
#else
    &ENG_RETIRED,                /* 12 (ENGI_FM6): FM6, a synth module (firmware/modules/fm6) */
#endif
#if MELODEE_SLICE
    &ENG_SLICE,                  /* 13 (ENGI_SLICE) */
#else
    &ENG_RETIRED,             /* 13: reserved (MELODEE_SLICE=0 builds without SLICE) */
#endif
    &ENG_RETIRED,                /* 14: retired OBXF; stored index reserved */
#if MELODEE_CZ_BUILTIN
    &ENG_CZ,                     /* 15: native Casio CZ-1 */
#else
    &ENG_RETIRED,                /* 15: CZ-1, a synth module (firmware/modules/cz) */
#endif
    &ENG_RETIRED, &ENG_RETIRED, &ENG_RETIRED, /* 16..18 reserved */
#if MELODEE_P5_BUILTIN
    &ENG_P5_TEST,                /* 19 (ENGI_PROPHET): Prophet (ANALOG's ID 0 stays reserved) */
#else
    &ENG_RETIRED,                /* 19 (ENGI_PROPHET): Prophet, a synth module (firmware/modules/prophet) */
#endif
    &ENG_RETIRED,                /* 20: Prophet user-preset namespace */
    &ENG_MIDI,                   /* 21: USB MIDI Track */
    &ENG_RETIRED, &ENG_RETIRED, &ENG_RETIRED, &ENG_RETIRED,   /* 22..29: synth modules (ENGI_MOD0), silent while */
    &ENG_RETIRED, &ENG_RETIRED, &ENG_RETIRED, &ENG_RETIRED,   /* none is loaded */
};

/* a track's engine number as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }
static const engine_t *eng_of(uint32_t e) { return ENGINES[eng_idx(e)]; }
/* the KIT parameter's value names (DRUM's E1): a user kit's value shows the kit's own name (params.c, ui_draw.c) */
static int drum_kit_names(const param_desc_t *d) { return d->names && d->names == ENGINES[ENGI_DRUM]->edit[0].names; }

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
#if MELODEE_P5_BUILTIN
    ENGI_PROPHET,                /* Prophet replaces ANALOG in new-sound browsing (else a module: listed when there,
                                  * first) */
#endif
#if MELODEE_FM6_BUILTIN
    12,                          /* FM6 (else a module: listed when there, at its place below) */
#endif
#if MELODEE_LEGACY_EXTRAS
    2, ENGI_CZ, 3, 5,             /* PHASE CZ-1 SID VOICE */
    6, 7, 9,
    11,                          /* NOISE */
#else
#if MELODEE_CZ_BUILTIN
    ENGI_CZ,                     /* CZ-1 (else a module: listed when there, at its place below) */
#endif
#if MELODEE_SID_BUILTIN
    3,                           /* SID (else a module: listed when there, at its place below) */
#endif
#endif
#if MELODEE_SLICE
    13,                          /* SLICE */
#endif
#if MELODEE_DRUM_BUILTIN
    10,                          /* DRUM */
#endif
    ENGI_MIDI,
};
/* where an engine that comes as a module goes among them (eng_module_main.c mod_order_add): the Prophet first, FM6
 * after it, CZ-1 after FM6, SID after CZ-1, DRUM before the MIDI Track as built in; any other after them all */
static uint32_t eng_order_rank(uint32_t e)
{
    static const uint8_t R[] = {ENGI_PROPHET, ENGI_FM6, ENGI_CZ, 3, ENGI_DRUM, ENGI_MIDI};
    for (uint32_t k = 0; k < sizeof R; k++)
        if (R[k] == e)
            return k;
    return 255u;
}

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
    return e == ENGI_DIGITAL || (!MELODEE_LEGACY_EXTRAS && (e == 2u || e == 5u || e == 6u || e == 7u || e == 9u || e == 11u)) ||
           (!MELODEE_SID_BUILTIN && e == 3u) || (!MELODEE_DRUM_BUILTIN && e == ENGI_DRUM) ||
           (!MELODEE_CZ_BUILTIN && e == ENGI_CZ) ||
           (!MELODEE_FM6_BUILTIN && e == ENGI_FM6) ||
           (!MELODEE_P5_BUILTIN && e == ENGI_PROPHET);   /* (no module for them) */
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
    return eng_extra_retired(e) || eng_ok(e) ? e : ENGI_FM6;
}
static uint32_t eng_vis(uint32_t n) { return ENGINE_ORDER[n % eng_nshown]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n;
    if (!eng_ok(e))
        e = ENGI_FM6;                            /* (a retired or reserved engine: FM6's place; without its module
                                                  * none: the first engine's, the Prophet's while it is there) */
    for (n = 0; n < eng_nshown && ENGINE_ORDER[n] != e; n++)
        ;
    return n < eng_nshown ? n : 0u;
}
static uint32_t eng_step(uint32_t e, int32_t dir)
{
    return eng_vis((eng_rank(e % NENGINES) + (dir > 0 ? 1u : eng_nshown - 1u)) % eng_nshown);
}

/* the parts at power-on and in a new song (engine, preset; the steps empty): poly, bass, lead, drums. IT'S A
 * PROPHET 5, FM6 SOLID BASS, CZ-1 SYNTH.LEAD 1, 808 KIT (until 2026-10-16: PROPHET FUNK BASS II, FM6 PAD, SID INIT
 * SID, 808 KIT) */
static const uint8_t TRK_DEF[NPART][2] = {{ENGI_PROPHET, 1}, {ENGI_FM6, 10}, {ENGI_CZ, 52}, {ENGI_DRUM, 0}};
/* track k's power-on engine and preset: TRK_DEF's, a MIDI-only track the MIDI Track's */
static uint32_t trk_def_engine(uint32_t k) { return k < NPART ? TRK_DEF[k][0] : ENGI_MIDI; }
static uint32_t trk_def_preset(uint32_t k) { return k < NPART ? TRK_DEF[k][1] : 0u; }
#if MELODEE_MODULES
#include "eng_module.c"         /* synth modules: engines loaded into SRAM (module_store.c keeps them) */
#include "eng_module_main.c"    /* (their lists, loading, registration: main loop) */
#define SHIPPED_OK() mod_shipped_ok()  /* the synth modules the image needs are on the device (else: setup) */
#else
#define SHIPPED_OK() 1
static const char *eng_name(uint32_t e) { return ENGINES[e % NENGINES]->name; }
static uint32_t eng_npresets(uint32_t e) { return ENGINES[e % NENGINES]->npresets; }
static void mod_want(uint32_t e) { (void)e; }
static int mod_waiting(uint32_t e) { (void)e; return 0; }
#endif
