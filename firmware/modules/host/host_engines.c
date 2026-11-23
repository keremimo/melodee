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
#endif
#endif
#if MELODEE_CZ_BUILTIN
#include "../cz/eng_phase.c"          /* the phase-distortion core CZ-1 plays */
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
#include "../kits/eng_drum.c"         /* the kits (one implementation behind six synths): 808 circuits (drum_808.c), the
                                       * 909, a C64's SID (drum_sid.c), CR78, FM, the user's (drum_real.c) */
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
#if MELODEE_CZ_BUILTIN
static cz_part_t *cz_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(cz_part_t)); }
#endif
#if MELODEE_SID_BUILTIN || MELODEE_DRUM_BUILTIN
static sid_part_t *sid_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(sid_part_t)); }
#endif
#if MELODEE_PROPHET && MELODEE_P5_BUILTIN
static p5_part_t *p5_part(uint32_t part) { return resource_get(RES_ENGINE0 + part % NPART, sizeof(p5_part_t)); }
#endif
static drw_part_t *drw_of(const track_t *t) { return resource_get(RES_ENGINE0 + (uint32_t)(t - trk), sizeof(drw_part_t)); }

/* their places in the engine table (engines.c ENGINES: their manifest rows' handles) */
#if MELODEE_SID_BUILTIN
#define HOST_ESID [ENGI_SID] = &ENG_SID,
#else
#define HOST_ESID
#endif
#if MELODEE_DRUM_BUILTIN
#define HOST_EDRUM [ENGI_808] = &ENG_KIT808, [ENGI_909] = &ENG_KIT909, [ENGI_SIDKIT] = &ENG_KITSID, [ENGI_CR78] = &ENG_KITCR, \
                   [ENGI_FMKIT] = &ENG_KITFM, [ENGI_SAMPLER] = &ENG_SAMPLER,
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
#define HOST_ENGINES HOST_ESID HOST_EDRUM HOST_EFM6 HOST_ECZ HOST_EP5
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
#if MELODEE_DRUM_BUILTIN
#define HOST_ODRUM ENGI_808, ENGI_909, ENGI_SIDKIT, ENGI_CR78, ENGI_FMKIT, ENGI_SAMPLER,
#else
#define HOST_ODRUM
#endif
#define HOST_ORDER HOST_OP5 HOST_OFM6 HOST_OMID HOST_ODRUM
/* their factory presets' categories (browse.c PRESET_CAT; a module's are its file's) */
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
#define HOST_CATS HOST_CSID HOST_CFM6 HOST_CCZ HOST_CP5
#elif HOST_STAGE == 6
/* the built-in engines' schemas (the descriptions their modules' files are built from; schema.c writes the same SCHM
 * bytes from them) */
#if MELODEE_SID_BUILTIN
#include "../sid/sid_schema.h"
#endif
#if MELODEE_DRUM_BUILTIN
#include "../kits/drum_schema.h"
#endif
static const mod_sch_src *sch_builtin_src(uint32_t e)
{
    if (e >= NENGINES || ENGINES[e] == &ENG_NONE)
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
    if (ENGINES[e] == &ENG_KIT808 || ENGINES[e] == &ENG_KITFM) return &KIT808_SCHEMA;   /* (FM KIT: the 808's pads) */
    if (ENGINES[e] == &ENG_KIT909) return &KIT909_SCHEMA;
    if (ENGINES[e] == &ENG_KITSID) return &KITSID_SCHEMA;
    if (ENGINES[e] == &ENG_KITCR) return &KITCR_SCHEMA;
    if (ENGINES[e] == &ENG_SAMPLER) return &SAMPLER_SCHEMA;
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
