/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Host builds' reference engines (host_engines.h): the shipped synths' engine sources built in, and what engines.c,
 * browse.c, presets.c and schema.c take from them: their places in the engine table and in the list order, their
 * schemas, their makers' patches, their presets' categories. Included by engines.c at the place the engines' sources
 * were, in their old order (some share a core: CZ-1 keeps PHASE's, DRUM's KIT SID plays SID's chip): engines.c includes
 * it once per stage, HOST_STAGE 1..6 */
#if HOST_STAGE == 1
#if MELODEE_PROPHET
#if MELODEE_P5_BUILTIN
#include "../prophet/eng_prophet_test.c"   /* PROPHET: Sequential's Rev 4 */
#elif MELODEE_PROPHET_PROTOTYPE
#error "MELODEE_PROPHET_PROTOTYPE plays the built-in Prophet as engine 0: MELODEE_P5_BUILTIN=1"
#endif
#endif
#if MELODEE_CZ_BUILTIN || MELODEE_LEGACY_EXTRAS
#include "../cz/eng_phase.c"          /* PHASE (retired), the core CZ-1 keeps */
#endif
#if MELODEE_CZ_BUILTIN
#include "../cz/eng_cz.c"             /* CZ-1 */
#endif
#if MELODEE_SID_BUILTIN || MELODEE_DRUM_BUILTIN
#include "../sid/eng_sid.c"           /* SID; KIT SID's chip */
#endif
#elif HOST_STAGE == 2
#include "../trio/eng_trio.c"         /* the retired built-in TRIO and WHEEL: the references the SDK's modules are held to */
#include "../wheel/eng_wheel.c"
#elif HOST_STAGE == 3
#if MELODEE_DRUM_BUILTIN
#include "../drum/eng_drum.c"         /* DRUM: synthesized 808 circuits (drum_808.c), the 909, a C64's SID (drum_sid.c), CR78, FM */
#endif
#elif HOST_STAGE == 4
#if MELODEE_FM6_BUILTIN
#include "../fm6/eng_fm6.c"           /* FM6: 6-operator FM rendered as Dexed renders it (fm6_core.c) */
#endif
#elif HOST_STAGE == 5
/* the engines' part memory (resources.c: part k's engine block) */
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
static drw_part_t *drw_of(const track_t *t) { return resource_get(RES_ENGINE0 + (uint32_t)(t - trk), sizeof(drw_part_t)); }

/* their places in the engine table (engines.c ENGINES: the numbers the stores keep) */
#if MELODEE_PROPHET_PROTOTYPE
#define HOST_E0 [0] = &ENG_P5_TEST,       /* test firmware only: RAM patches, NEVER persist */
#else
#define HOST_E0
#endif
#if MELODEE_LEGACY_EXTRAS
#define HOST_ELEGACY [2] = &ENG_PHASE, [ENGI_TRIO] = &ENG_TRIO, [ENGI_WHEEL] = &ENG_WHEEL,
#else
#define HOST_ELEGACY
#endif
#if MELODEE_SID_BUILTIN
#define HOST_ESID [ENGI_SID] = &ENG_SID,
#else
#define HOST_ESID
#endif
#if MELODEE_DRUM_BUILTIN
#define HOST_EDRUM [ENGI_DRUM] = &ENG_DRUM,
#else
#define HOST_EDRUM
#endif
#if MELODEE_FM6_BUILTIN
#define HOST_EFM6 [ENGI_FM6] = &ENG_FM6,
#else
#define HOST_EFM6
#endif
#if MELODEE_CZ_BUILTIN
#define HOST_ECZ [ENGI_CZ] = &ENG_CZ,
#else
#define HOST_ECZ
#endif
#if MELODEE_P5_BUILTIN
#define HOST_EP5 [ENGI_PROPHET] = &ENG_P5_TEST,
#else
#define HOST_EP5
#endif
#define HOST_ENGINES HOST_E0 HOST_ELEGACY HOST_ESID HOST_EDRUM HOST_EFM6 HOST_ECZ HOST_EP5
/* their place in the list (engines.c ENGINE_ORDER; a module comes in at its manifest row's place) */
#if MELODEE_P5_BUILTIN
#define HOST_OP5 ENGI_PROPHET,
#else
#define HOST_OP5
#endif
#if MELODEE_FM6_BUILTIN
#define HOST_OFM6 ENGI_FM6,
#else
#define HOST_OFM6
#endif
#if MELODEE_LEGACY_EXTRAS
#define HOST_OMID 2, ENGI_CZ, ENGI_SID, 5, ENGI_TRIO, ENGI_WHEEL, 9, 11,   /* PHASE CZ-1 SID VOICE TRIO WHEEL PHYS NOISE */
#else
#if MELODEE_CZ_BUILTIN
#define HOST_OCZ ENGI_CZ,
#else
#define HOST_OCZ
#endif
#if MELODEE_SID_BUILTIN
#define HOST_OSID ENGI_SID,
#else
#define HOST_OSID
#endif
#define HOST_OMID HOST_OCZ HOST_OSID
#endif
#if MELODEE_DRUM_BUILTIN
#define HOST_ODRUM ENGI_DRUM,
#else
#define HOST_ODRUM
#endif
#define HOST_ORDER HOST_OP5 HOST_OFM6 HOST_OMID HOST_ODRUM
/* the names of the retired engines reference builds play (params.c N_ENGNAME) */
#if MELODEE_LEGACY_EXTRAS
#define HOST_ENGNAMES [2] = "PHASE", [ENGI_TRIO] = "TRIO", [ENGI_WHEEL] = "WHEEL",
#else
#define HOST_ENGNAMES
#endif
/* their factory presets' categories (browse.c PRESET_CAT; a module's are its file's) */
#if MELODEE_LEGACY_EXTRAS
#define HOST_CLEGACY [2] = "RGSLEU",                  /* PHASE: BRASS ORGAN STRING RESO BELL WIRE */
#else
#define HOST_CLEGACY
#endif
#if MELODEE_SID_BUILTIN
#define HOST_CSID [ENGI_SID] = "OLBBBBBBLLLLLLLLPSPPGGKKUUERFFFF",
#else
#define HOST_CSID
#endif
#if MELODEE_FM6_BUILTIN
#define HOST_CFM6 [ENGI_FM6] = "KEBRPEGUKRBEEKGSPLUEWEBEO",          /* INIT VOICE last */
#else
#define HOST_CFM6
#endif
#if MELODEE_CZ_BUILTIN
#define HOST_CCZ [ENGI_CZ] = "ORRRSSSSSUUUBBBBURWSWWWWUKKKKKKKKGGGGGPPPEEEEEDEESSULLLLFDDDEFFFF",   /* INIT TONE, Casio's 64 */
#else
#define HOST_CCZ
#endif
#if MELODEE_P5_BUILTIN
#define HOST_CP5 [ENGI_PROPHET] =                                    /* INIT, Sequential's 200 */ \
        "OOOKBPROOOGKLPWPOLOUUBEOEGFPOOBOEOOODDDEOOGKSGBPEPPLBROOOOPOKPKOFOSOOEPRODFLFOOEOULWOPBOLOPKFORGLPOBULROOSKOO" \
        "PSBUEFDEEEWEUOUEOOOOSERPUOBOOOBOFLGUGRLOOOBUFOOFEUFORSKKWKLGLGGKROGBGLKSLOOFOFOFLUUEFFEOFEPF",
#else
#define HOST_CP5
#endif
#define HOST_CATS HOST_CLEGACY HOST_CSID HOST_CFM6 HOST_CCZ HOST_CP5
#elif HOST_STAGE == 6
/* the built-in engines' schemas (the descriptions their modules' files are built from; schema.c writes the same SCHM
 * bytes from them) */
#if MELODEE_SID_BUILTIN
#include "../sid/sid_schema.h"
#endif
#if MELODEE_DRUM_BUILTIN
#include "../drum/drum_schema.h"
#endif
static const mod_sch_src *sch_builtin_src(uint32_t e)
{
    if (e >= NENGINES || ENGINES[e] == &ENG_RETIRED)
        return 0;
#if MELODEE_P5_BUILTIN
    if (ENGINES[e] == &ENG_P5_TEST) return &P5_SCHEMA;
#endif
#if MELODEE_CZ_BUILTIN
    if (ENGINES[e] == &ENG_CZ) return &CZ_SCHEMA;
#endif
#if MELODEE_FM6_BUILTIN
    if (ENGINES[e] == &ENG_FM6) return &FM6_SCHEMA;
#endif
#if MELODEE_SID_BUILTIN
    if (ENGINES[e] == &ENG_SID) return &SID_SCHEMA;
#endif
#if MELODEE_DRUM_BUILTIN
    if (ENGINES[e] == &ENG_DRUM) return &DRUM_SCHEMA;
#endif
    return 0;
}
#if HOST_MAKERS
/* the patch of built-in engine e's factory preset k (its makers), its length; 0 none */
static uint32_t preset_builtin_patch(uint32_t e, uint32_t k, uint8_t *p)
{
#if MELODEE_P5_BUILTIN
    if (ENGINES[e % NENGINES] == &ENG_P5_TEST) return p5_preset_patch(k, p);
#endif
#if MELODEE_CZ_BUILTIN
    if (ENGINES[e % NENGINES] == &ENG_CZ) return cz_preset_patch(k, p);
#endif
#if MELODEE_FM6_BUILTIN
    if (ENGINES[e % NENGINES] == &ENG_FM6) return fm6_preset_patch(k, p);
#endif
    return 0;
}
#endif
#endif
