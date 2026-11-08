/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* HYDRA's patch (144 bytes, Saadet's hy_patch_t byte for byte: Saadet d4ae975 firmware/src/hydra_patch.h, its INIT from
 * eng_hydra.c): the same bytes in a part, a user slot, a project and the native message, so a sound moves between
 * Saadet and Melodee whole. Signed fields are stored raw = value + offset (HY_SEMI0, HY_CENT0, HY_ENV0).
 *
 * The macros, after the Hydrasynth's: the sound names its eight (HY_MNAME, a word of HY_MACRO_NAMES each, AUTO: its
 * first destination's) and routes them through a pool of HY_MSLOTS assignments, two bytes each: the macro (high three
 * bits) and its destination (low five, a HY_TGT entry, 0: the slot is free), then the depth, raw = depth + HY_DEPTH0
 * (-127..127: the destination's whole range at a full turn). The macros' values are the part's E1..E8 (hydra.c),
 * -64..63, 0 the sound as stored. HY_SPARE and byte 78 are held; a stored patch may hold anything there. */
#pragma once
#include <stdint.h>
enum { HO_MODE, HO_WAVE, HO_L0, HO_SCAN = HO_L0 + 8, HO_SEMI, HO_CENT, HO_KEY, HO_N };   /* OSC 1 / OSC 2 */
enum { HM_MODE, HM_SRC, HM_RATIO, HM_DEPTH, HM_AUX, HM_WET, HM_N };                     /* a MUTANT */
enum { HY_MACROS = 8, HY_MSLOTS = 14, HY_DEPTH0 = 127 };
enum {
    HY_O1 = 0, HY_O2 = HY_O1 + HO_N,
    HY_O3 = HY_O2 + HO_N, HY_O3_WAVE = HY_O3, HY_O3_SEMI, HY_O3_CENT, HY_O3_KEY,
    HY_M1, HY_M2 = HY_M1 + HM_N, HY_M3 = HY_M2 + HM_N, HY_M4 = HY_M3 + HM_N,
    HY_RING_A = HY_M4 + HM_N, HY_RING_B, HY_NOISE,
    HY_LVL, HY_BAL = HY_LVL + 5, HY_ROUTE = HY_BAL + 5,          /* O1 O2 O3 RING NOISE */
    HY_F1_TYPE, HY_F1_CUT, HY_F1_RES, HY_F1_DRIVE, HY_F1_DPOS, HY_F1_ENV, HY_F1_KEY, HY_F1_VEL,
    HY_F2_TYPE = HY_F1_VEL + 2, HY_F2_MORPH, HY_F2_CUT, HY_F2_RES, HY_F2_ENV, HY_F2_KEY,
    HY_AMP, HY_E1_SC1, HY_E1_SC2, HY_E1_MU1,                     /* ENV 1 -> SCAN 1, SCAN 2, MUTANT 1 DEPTH */
    HY_E1, HY_E1_DLY = HY_E1, HY_E1_ATK, HY_E1_HLD, HY_E1_DEC, HY_E1_SUS, HY_E1_REL,
    HY_MNAME, HY_MA = HY_MNAME + HY_MACROS, HY_SPARE = HY_MA + 2 * HY_MSLOTS,
    HY_NAME = 132, HY_NAME_LEN = 12, HY_BYTES = 144
};
_Static_assert(HY_SPARE < HY_NAME && HY_NAME + HY_NAME_LEN == HY_BYTES, "HYDRA patch layout");
#define HY_SLOT(macro, tgt) ((uint8_t)((macro) << 5 | (tgt)))
#define HY_SLOT_MACRO(a) ((uint32_t)(a) >> 5)
#define HY_SLOT_TGT(a) ((uint32_t)(a) & 31u)
/* where a macro may go: what changes the sound, not what a voice is made of (an oscillator's mode or wave, a mutant's
 * mode, a filter's type); a level or FILTER 2 a macro can open is costed as opened (eng_hydra.c hy_worst). {id, the
 * byte, its name on the pages} */
#define HY_TARGETS                                                                                                       \
    X(NONE, 0, "NONE") X(SCAN1, HY_O1 + HO_SCAN, "SCAN 1") X(SCAN2, HY_O2 + HO_SCAN, "SCAN 2")                          \
    X(FINE2, HY_O2 + HO_CENT, "O2 FINE") X(MUT1, HY_M1 + HM_DEPTH, "MUTANT 1") X(MUT2, HY_M2 + HM_DEPTH, "MUTANT 2")     \
    X(MUT3, HY_M3 + HM_DEPTH, "MUTANT 3") X(MUT4, HY_M4 + HM_DEPTH, "MUTANT 4") X(RATIO1, HY_M1 + HM_RATIO, "M1 RATIO")  \
    X(RATIO3, HY_M3 + HM_RATIO, "M3 RATIO") X(AUX1, HY_M1 + HM_AUX, "M1 AUX") X(WET1, HY_M1 + HM_WET, "M1 WET")          \
    X(WET3, HY_M3 + HM_WET, "M3 WET") X(OSC1, HY_LVL, "OSC 1") X(OSC2, HY_LVL + 1, "OSC 2") X(OSC3, HY_LVL + 2, "OSC 3") \
    X(RING, HY_LVL + 3, "RING") X(NOISE, HY_LVL + 4, "NOISE") X(CUT, HY_F1_CUT, "CUTOFF") X(RES, HY_F1_RES, "RES")      \
    X(DRIVE, HY_F1_DRIVE, "DRIVE") X(F1ENV, HY_F1_ENV, "F1 ENV") X(PASS, HY_F2_MORPH, "PASS")                          \
    X(F2CUT, HY_F2_CUT, "F2 CUT") X(F2RES, HY_F2_RES, "F2 RES") X(F2ENV, HY_F2_ENV, "F2 ENV") X(LEVEL, HY_AMP, "LEVEL") \
    X(ENVSCAN, HY_E1_SC1, "ENV>SCAN") X(ENVMUT, HY_E1_MU1, "ENV>MUT") X(E1ATK, HY_E1_ATK, "E1 ATK")                    \
    X(E1DEC, HY_E1_DEC, "E1 DEC") X(E1REL, HY_E1_REL, "E1 REL")
#define X(n, b, l) HT_##n,
enum { HY_TARGETS HT_COUNT };
#undef X
#define X(n, b, l) (uint8_t)(b),
static const uint8_t HY_TGT[] = {HY_TARGETS};
#undef X
#define HY_NTGT 31u
_Static_assert(HT_COUNT == HY_NTGT + 1u, "a slot's five bits name every destination");
/* a destination's range in raw steps (its stored maximum) */
static inline uint32_t hy_span(uint32_t g)
{
    uint32_t b = HY_TGT[g % (HY_NTGT + 1u)];
    return b == HY_O2 + HO_CENT ? 100u : b == HY_F1_ENV || b == HY_F2_ENV || b == HY_E1_SC1 || b == HY_E1_MU1 ? 126u : 127u;
}
/* a macro's name: AUTO (its first destination's), else one of these */
#define HY_NAMES                                                                                                         \
    X(AUTO) X(BRIGHT) X(DARK) X(TONE) X(GRIT) X(BITE) X(EDGE) X(RES) X(SCAN) X(MORPH) X(MOTION) X(SWEEP) X(WARP) X(FM)   \
    X(SYNC) X(STACK) X(SQUEEZE) X(PW) X(HARMONIC) X(DETUNE) X(WIDTH) X(SUB) X(BODY) X(AIR) X(NOISE) X(RING) X(BELL)     \
    X(GLASS) X(METAL) X(VOWEL) X(CHOIR) X(BREATH) X(ATTACK) X(DECAY) X(RELEASE) X(PLUCK) X(SWELL) X(PASS) X(BLEND)      \
    X(COLOR) X(SHAPE) X(FAT) X(THIN) X(SPACE) X(LEVEL) X(FILTER) X(ENV) X(TALK)
#define X(n) HN_##n,
enum { HY_NAMES HN_COUNT };
#undef X
#define X(n) #n,
static const char *const HY_MACRO_NAMES[] = {HY_NAMES};
#undef X
#define HY_NNAME ((uint32_t)HN_COUNT)
enum { MU_OFF, MU_FM, MU_STACK, MU_SYNC, MU_PW, MU_SQZ, MU_WARP, MU_HARM, MU_N };
enum { HYS_OSC1, HYS_OSC2, HYS_OSC3, HYS_RING, HYS_NOISE, HYS_N };            /* FM sources */
enum { F1_LAD12, F1_LAD24, F1_FAT12, F1_FAT24, F1_MSLP, F1_MSHP, F1_LPG, F1_3LP, F1_3BP, F1_3HP, F1_VOWEL, F1_N };
#define HY_SEMI0 36
#define HY_CENT0 50
#define HY_ENV0 63
#define HY_WAVE_COUNT 56u                              /* tools/gen_hy_waves.py (eng_hydra.c checks it) */
/* A wave's id, as a slot holds it: 0 EMPTY, 1..HY_WAVE_COUNT the factory waves, then the user waves from HY_USER0
 * (hydra_bank.h). HY_UMAX ids are theirs, so the bank can grow without another patch format; one past the bank plays
 * silence. A single oscillator's WAVE and OSC 3's hold the id - 1 */
#define HY_USER0 (HY_WAVE_COUNT + 1u)
#define HY_UMAX 64u
#define HY_UWAVES 14u                                  /* the bank as built: two flash objects of seven */
#define HY_ID_LAST (HY_USER0 + HY_UMAX - 1u)
_Static_assert(HY_UWAVES <= HY_UMAX && HY_ID_LAST <= 255u, "a wave's id is a byte");
#define HY_FRAME 294u                                  /* a native message, F0 to F7: its header, two nibbles a byte */
typedef struct { uint8_t raw[HY_BYTES]; } hy_patch_t;

/* the bytes a stored or received patch may hold: {first, last, the largest value} */
static int hy_patch_valid(const hy_patch_t *p)
{
#define HY_OSC_RULES(b) {b + HO_MODE, b + HO_MODE, 1}, {b + HO_WAVE, b + HO_WAVE, HY_ID_LAST - 1u}, \
        {b + HO_L0, b + HO_L0 + 7, HY_ID_LAST}, {b + HO_SCAN, b + HO_SCAN, 127}, {b + HO_SEMI, b + HO_SEMI, 2 * HY_SEMI0}, \
        {b + HO_CENT, b + HO_CENT, 2 * HY_CENT0}, {b + HO_KEY, b + HO_KEY, 200}
#define HY_MUT_RULES(b) {b + HM_MODE, b + HM_MODE, MU_N - 1}, {b + HM_SRC, b + HM_SRC, HYS_N - 1}, {b + HM_RATIO, b + HM_WET, 127}
    static const uint8_t RULE[][3] = {
        HY_OSC_RULES(HY_O1), HY_OSC_RULES(HY_O2),
        {HY_O3_WAVE, HY_O3_WAVE, HY_ID_LAST - 1u}, {HY_O3_SEMI, HY_O3_SEMI, 2 * HY_SEMI0},
        {HY_O3_CENT, HY_O3_CENT, 2 * HY_CENT0}, {HY_O3_KEY, HY_O3_KEY, 200},
        HY_MUT_RULES(HY_M1), HY_MUT_RULES(HY_M2), HY_MUT_RULES(HY_M3), HY_MUT_RULES(HY_M4),
        {HY_RING_A, HY_NOISE, 2}, {HY_LVL, HY_BAL + 4, 127}, {HY_ROUTE, HY_ROUTE, 1},
        {HY_F1_TYPE, HY_F1_TYPE, F1_N - 1}, {HY_F1_CUT, HY_F1_DRIVE, 127}, {HY_F1_DPOS, HY_F1_DPOS, 1},
        {HY_F1_ENV, HY_F1_ENV, 2 * HY_ENV0}, {HY_F1_KEY, HY_F1_KEY, 200}, {HY_F1_VEL, HY_F1_VEL, 127},
        {HY_F2_TYPE, HY_F2_TYPE, 1}, {HY_F2_MORPH, HY_F2_RES, 127}, {HY_F2_ENV, HY_F2_ENV, 2 * HY_ENV0},
        {HY_F2_KEY, HY_F2_KEY, 200}, {HY_AMP, HY_AMP, 127}, {HY_E1_SC1, HY_E1_MU1, 2 * HY_ENV0},
        {HY_E1_DLY, HY_E1_REL, 127}, {HY_MNAME, HY_MNAME + HY_MACROS - 1, HY_NNAME - 1u},
    };
#undef HY_OSC_RULES
#undef HY_MUT_RULES
    for (uint32_t k = 0; k < sizeof RULE / sizeof RULE[0]; k++)
        for (uint32_t i = RULE[k][0]; i <= RULE[k][1]; i++)
            if (p->raw[i] > RULE[k][2])
                return 0;
    for (uint32_t k = 0; k < HY_MSLOTS; k++)           /* (a slot's first byte is whole: eight macros, 31 destinations) */
        if (p->raw[HY_MA + 2u * k + 1u] > 2u * HY_DEPTH0)
            return 0;
    for (uint32_t i = 0; i < HY_NAME_LEN; i++)
        if (p->raw[HY_NAME + i] < 32u || p->raw[HY_NAME + i] > 126u)
            return 0;
    return 1;
}
/* the native message: F0 7D 48 59 01, two nibbles a byte (low first), F7. 7D is the header the editor's frames carry too
 * ("non-commercial"): HYDRA is no instrument with a message of its own */
static uint32_t hy_patch_encode(const hy_patch_t *p, uint8_t *frame, uint32_t cap)
{
    if (cap < HY_FRAME)
        return 0;
    frame[0] = 0xF0u; frame[1] = 0x7Du; frame[2] = 0x48u; frame[3] = 0x59u; frame[4] = 1u;
    for (uint32_t i = 0; i < HY_BYTES; i++) {
        frame[5u + 2u * i] = p->raw[i] & 15u;
        frame[6u + 2u * i] = p->raw[i] >> 4;
    }
    frame[HY_FRAME - 1u] = 0xF7u;
    return HY_FRAME;
}
/* the bytes between F0 and F7. 1: p holds a valid patch */
static int hy_patch_decode(hy_patch_t *p, const uint8_t *body, uint32_t n)
{
    if (n != HY_FRAME - 2u || body[0] != 0x7Du || body[1] != 0x48u || body[2] != 0x59u || body[3] != 1u)
        return 0;
    for (uint32_t i = 0; i < HY_BYTES; i++) {
        if (body[4u + 2u * i] > 15u || body[5u + 2u * i] > 15u)
            return 0;
        p->raw[i] = (uint8_t)(body[4u + 2u * i] | body[5u + 2u * i] << 4);
    }
    return hy_patch_valid(p);
}

/* INIT: one saw through an open ladder (OSC 2 and 3 tuned and silent, the lists ready to scan); its macros SCAN 1,
 * MUTANT 1, CUTOFF, RES, then F1 ENV, DRIVE, ENV 1's attack and decay, each its own name (Saadet's HY_INIT) */
static const uint8_t HY_INIT[][2] = {
    {HY_O1 + HO_WAVE, 2 /* SAW */}, {HY_O1 + HO_SEMI, HY_SEMI0}, {HY_O1 + HO_CENT, HY_CENT0}, {HY_O1 + HO_KEY, 100},
    {HY_O1 + HO_L0, 2 + 1}, {HY_O1 + HO_L0 + 1, 32 + 1 /* SYNC 2 */}, {HY_O1 + HO_L0 + 2, 33 + 1 /* SYNC 3 */},
    {HY_O1 + HO_L0 + 3, 34 + 1 /* SYNC 5 */},
    {HY_O2 + HO_WAVE, 2}, {HY_O2 + HO_SEMI, HY_SEMI0}, {HY_O2 + HO_CENT, HY_CENT0 + 7}, {HY_O2 + HO_KEY, 100},
    {HY_O2 + HO_L0, 0 + 1 /* SINE */}, {HY_O2 + HO_L0 + 1, 1 + 1 /* TRI */}, {HY_O2 + HO_L0 + 2, 3 + 1 /* SQR */},
    {HY_O2 + HO_L0 + 3, 6 + 1 /* SOFTSW */},
    {HY_O3_WAVE, 0 /* SINE */}, {HY_O3_SEMI, HY_SEMI0}, {HY_O3_CENT, HY_CENT0}, {HY_O3_KEY, 100},
    {HY_M1 + HM_WET, 127}, {HY_M2 + HM_WET, 127}, {HY_M3 + HM_WET, 127}, {HY_M4 + HM_WET, 127},
    {HY_M1 + HM_RATIO, 32}, {HY_M2 + HM_RATIO, 32}, {HY_M3 + HM_RATIO, 32}, {HY_M4 + HM_RATIO, 32},
    {HY_M1 + HM_SRC, HYS_OSC3}, {HY_M2 + HM_SRC, HYS_OSC3}, {HY_M3 + HM_SRC, HYS_OSC3}, {HY_M4 + HM_SRC, HYS_OSC3},
    {HY_RING_A, HYS_OSC1}, {HY_RING_B, HYS_OSC2}, {HY_LVL, 100},
    {HY_F1_TYPE, F1_LAD24}, {HY_F1_CUT, 127}, {HY_F1_ENV, HY_ENV0}, {HY_F1_KEY, 0},
    {HY_F2_CUT, 127}, {HY_F2_ENV, HY_ENV0},
    {HY_AMP, 100}, {HY_E1_SC1, HY_ENV0}, {HY_E1_SC2, HY_ENV0}, {HY_E1_MU1, HY_ENV0},
    {HY_E1_DEC, 64}, {HY_E1_REL, 50},
    {HY_MA, HY_SLOT(0, HT_SCAN1)}, {HY_MA + 1, HY_DEPTH0 + 64}, {HY_MA + 2, HY_SLOT(1, HT_MUT1)}, {HY_MA + 3, HY_DEPTH0 + 64},
    {HY_MA + 4, HY_SLOT(2, HT_CUT)}, {HY_MA + 5, HY_DEPTH0 + 64}, {HY_MA + 6, HY_SLOT(3, HT_RES)}, {HY_MA + 7, HY_DEPTH0 + 64},
    {HY_MA + 8, HY_SLOT(4, HT_F1ENV)}, {HY_MA + 9, HY_DEPTH0 + 64}, {HY_MA + 10, HY_SLOT(5, HT_DRIVE)}, {HY_MA + 11, HY_DEPTH0 + 64},
    {HY_MA + 12, HY_SLOT(6, HT_E1ATK)}, {HY_MA + 13, HY_DEPTH0 + 64}, {HY_MA + 14, HY_SLOT(7, HT_E1DEC)}, {HY_MA + 15, HY_DEPTH0 + 64},
};
static void hy_name_set(hy_patch_t *p, const char *s)
{
    for (uint32_t i = 0; i < HY_NAME_LEN; i++)
        p->raw[HY_NAME + i] = (uint8_t)(*s ? *s++ : ' ');
}
static void hy_patch_init(hy_patch_t *p)
{
    for (uint32_t i = 0; i < HY_BYTES; i++)
        p->raw[i] = 0;
    for (uint32_t i = 0; i < sizeof HY_INIT / sizeof HY_INIT[0]; i++)
        p->raw[HY_INIT[i][0]] = HY_INIT[i][1];
    hy_name_set(p, "INIT HYDRA");
}
