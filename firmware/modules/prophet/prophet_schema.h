/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The Prophet's schema (firmware/modules/sdk/mod_schema.h): its patch (p5_patch.h), every panel value by its byte's
 * place in the program (the ids Melodee 1.x's Motion and editor used), its pages, roles and Motion's bytes. The module
 * file carries it as its SCHM chunk (tools/mod_build.py); host builds compile it in (p5_ui.c) */
static const char *const P5S_ONOFF[] = {"OFF", "ON"};
static const char *const P5S_TRACK[] = {"OFF", "HALF", "FULL"};
static const char *const P5S_FILTER[] = {"SSI", "CURTIS"};
static const char *const P5S_ASSIGN[] = {"LOW", "LOW RET", "LAST", "LAST RET"};
static const mod_sch_param_src P5_SCH_PARAM[] = {
    {0, MOD3_SK_CALL, MOD3_F_SEMI, -12, 36, 0, 0, 0, 0, MOD3_SF_DIM, "FREQ A", 0, 0, 0},   /* P5_FREQ_A */
    {1, MOD3_SK_CALL, MOD3_F_INT, 0, 127, 0, 0, 0, 0, MOD3_SF_DIM | MOD3_SF_DESC, "FREQ B", 0, 0, 0},   /* P5_FREQ_B */
    {2, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 2, 0, 8, MOD3_SF_DIM, "FINE B", 0, 0, 0},
    {3, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 3, 0, 8, MOD3_SF_DIM, "SAW A", 0, P5S_ONOFF, 0},
    {4, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 4, 0, 8, MOD3_SF_DIM, "PULSE A", 0, P5S_ONOFF, 0},
    {5, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 5, 0, 8, MOD3_SF_DIM, "SAW B", 0, P5S_ONOFF, 0},
    {6, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 6, 0, 8, MOD3_SF_DIM, "TRI B", 0, P5S_ONOFF, 0},
    {7, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 7, 0, 8, MOD3_SF_DIM, "PULSE B", 0, P5S_ONOFF, 0},
    {8, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 8, 0, 8, MOD3_SF_DIM, "WIDTH A", 0, 0, 0},
    {9, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 9, 0, 8, MOD3_SF_DIM, "WIDTH B", 0, 0, 0},
    {10, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 10, 0, 8, MOD3_SF_DIM, "SYNC", 0, P5S_ONOFF, 0},
    {11, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 11, 0, 8, MOD3_SF_DIM, "LOW B", 0, P5S_ONOFF, 0},
    {12, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 12, 0, 8, MOD3_SF_DIM, "KEY B", 0, P5S_ONOFF, 0},
    {13, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 13, 0, 8, MOD3_SF_DIM, "GLIDE", 0, 0, 0},
    {14, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 14, 0, 8, MOD3_SF_DIM, "OSC A", 0, 0, 0},
    {15, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 15, 0, 8, MOD3_SF_DIM, "OSC B", 0, 0, 0},
    {16, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 16, 0, 8, MOD3_SF_DIM, "NOISE", 0, 0, 0},
    {17, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 17, 0, 8, MOD3_SF_DIM, "CUTOFF", 0, 0, 0},
    {18, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 18, 0, 8, MOD3_SF_DIM, "RES", 0, 0, 0},
    {19, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 2, 0, 19, 0, 8, MOD3_SF_DIM, "KEY", 0, P5S_TRACK, 0},
    {20, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 1, 20, 0, 8, MOD3_SF_DIM, "MODEL", 0, P5S_FILTER, 0},
    {21, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 21, 0, 8, MOD3_SF_DIM, "RATE", 0, 0, 0},
    {22, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 22, 0, 8, MOD3_SF_DIM, "INITIAL", 0, 0, 0},
    {23, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 23, 0, 8, MOD3_SF_DIM, "SAW", 0, P5S_ONOFF, 0},
    {24, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 24, 0, 8, MOD3_SF_DIM, "TRI", 0, P5S_ONOFF, 0},
    {25, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 25, 0, 8, MOD3_SF_DIM, "SQUARE", 0, P5S_ONOFF, 0},
    {26, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 26, 0, 8, MOD3_SF_DIM, "N MIX", 0, 0, 0},
    {27, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 27, 0, 8, MOD3_SF_DIM, "FREQ A", 0, P5S_ONOFF, 0},
    {28, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 28, 0, 8, MOD3_SF_DIM, "FREQ B", 0, P5S_ONOFF, 0},
    {29, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 29, 0, 8, MOD3_SF_DIM, "PW A", 0, P5S_ONOFF, 0},
    {30, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 30, 0, 8, MOD3_SF_DIM, "PW B", 0, P5S_ONOFF, 0},
    {31, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 31, 0, 8, MOD3_SF_DIM, "FILTER", 0, P5S_ONOFF, 0},
    {32, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 32, 0, 8, MOD3_SF_DIM, "ENV AMT", 0, 0, 0},
    {33, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 33, 0, 8, MOD3_SF_DIM, "OSC B", 0, 0, 0},
    {34, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 34, 0, 8, MOD3_SF_DIM, "FREQ A", 0, P5S_ONOFF, 0},
    {35, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 35, 0, 8, MOD3_SF_DIM, "PW A", 0, P5S_ONOFF, 0},
    {36, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 36, 0, 8, MOD3_SF_DIM, "FILTER", 0, P5S_ONOFF, 0},
    {37, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 37, 0, 8, MOD3_SF_DIM, "VINTAGE", 0, 0, 0},
    {38, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 38, 0, 8, MOD3_SF_DIM, "AT FLT", 0, P5S_ONOFF, 0},
    {39, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 39, 0, 8, MOD3_SF_DIM, "AT LFO", 0, P5S_ONOFF, 0},
    {40, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 40, 0, 8, MOD3_SF_DIM, "ENV AMT", 0, 0, 0},
    {41, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 41, 0, 8, MOD3_SF_DIM, "VEL FLT", 0, P5S_ONOFF, 0},
    {42, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 42, 0, 8, MOD3_SF_DIM, "VEL AMP", 0, P5S_ONOFF, 0},
    {43, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 43, 0, 8, MOD3_SF_DIM, "ATK", 0, 0, 0},
    {44, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 44, 0, 8, MOD3_SF_DIM, "ATK", 0, 0, 0},
    {45, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 45, 0, 8, MOD3_SF_DIM, "DEC", 0, 0, 0},
    {46, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 46, 0, 8, MOD3_SF_DIM, "DEC", 0, 0, 0},
    {47, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 47, 0, 8, MOD3_SF_DIM, "SUS", 0, 0, 0},
    {48, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 48, 0, 8, MOD3_SF_DIM, "SUS", 0, 0, 0},
    {49, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 49, 0, 8, MOD3_SF_DIM, "REL", 0, 0, 0},
    {50, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 50, 0, 8, MOD3_SF_DIM, "REL", 0, 0, 0},
    {51, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 51, 0, 8, MOD3_SF_DIM, "RELEASE", 0, P5S_ONOFF, 0},
    {52, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 52, 0, 8, MOD3_SF_DIM | MOD3_SF_NOTIFY, "UNISON", 0, P5S_ONOFF, 0},
    {53, MOD3_SK_CALL, MOD3_F_INT, 1, 5, 5, 0, 0, 0, MOD3_SF_DIM | MOD3_SF_NOTIFY, "VOICES", 0, 0, 0},   /* P5_UNISON_COUNT */
    {54, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 54, 0, 8, MOD3_SF_DIM | MOD3_SF_NOTIFY, "DETUNE", 0, 0, 0},
    {86, MOD3_SK_CALL, MOD3_F_INT, 1, 12, 1, 0, 0, 0, MOD3_SF_DIM, "BEND", 0, 0, 0},   /* P5_BEND */
    {87, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 3, 0, 87, 0, 8, MOD3_SF_DIM | MOD3_SF_NOTIFY, "PRIORITY", 0, P5S_ASSIGN, 0},
};
#define P5S_PG(t, a, b, c, d, sec, fl) {t, MOD3_W_NONE, fl, {a, b, c, d}, 0, {0}, 0, 0, sec}
static const char *const P5S_INIT[] = {"PROPHET INIT"};
static const mod_sch_page_src P5_SCH_PAGE[] = {
    {"P5 STORE", MOD3_W_STORE, MOD3_PG_LAST, {0, 1, 2, 3}, 0, {0}, P5S_INIT, 0, "Store"},
    P5S_PG("P5 OSC A", 0, 3, 4, 8, "Osc", 0),
    P5S_PG("P5 OSC B", 1, 2, 9, 12, "Osc", 0),
    P5S_PG("P5 B WAVES", 5, 6, 7, 11, "Osc", 0),
    P5S_PG("P5 MIXER", 14, 15, 16, 10, "Mixer", 0),
    P5S_PG("P5 FILTER", 17, 18, 19, 20, "Filter", 0),
    P5S_PG("P5 FLT ENV", 43, 45, 47, 49, "Filter", MOD3_PG_ENV),
    P5S_PG("P5 AMP ENV", 44, 46, 48, 50, "Amp", MOD3_PG_ENV),
    P5S_PG("P5 ENV MOD", 40, 41, 42, 51, "Mod", MOD3_PG_ENV),
    P5S_PG("P5 POLYMOD", 32, 33, 34, 35, "Mod", 0),
    P5S_PG("P5 MOD FLT", 36, 37, 38, 39, "Mod", 0),
    P5S_PG("P5 LFO", 21, 23, 24, 25, "LFO", MOD3_PG_LFO),
    P5S_PG("P5 WHEEL", 22, 26, 27, 28, "Wheel", MOD3_PG_LFO),
    P5S_PG("P5 WHEEL 2", 29, 30, 31, 13, "Wheel", MOD3_PG_LFO),
    P5S_PG("P5 UNISON", 52, 53, 54, 87, "Voice", 0),
    P5S_PG("P5 BEND", 86, MOD_SCH_NONE, MOD_SCH_NONE, MOD_SCH_NONE, "Voice", 0),
    P5S_PG("FILTER", 17, 18, 40, 45, 0, MOD3_PG_BANK),                    /* (the SMK-37's FILTER and ENV banks) */
    P5S_PG("ENV", 44, 46, 48, 50, 0, MOD3_PG_BANK),
};
#undef P5S_PG
static const mod_sch_role_src P5_SCH_ROLE[] = {
    {MOD3_R_HOME1, MOD3_RF_PIC_FILTER, 17, 0}, {MOD3_R_HOME2, MOD3_RF_PIC_FILTER, 18, 0},
    {MOD3_R_HOME3, MOD3_RF_PIC_FILTER, 40, 0}, {MOD3_R_HOME4, MOD3_RF_PIC_ENV, 50, 0},
    {MOD3_R_CUTOFF, 0, 17, 0}, {MOD3_R_RESO, 0, 18, 0},
    {MOD3_R_ENV_A, 0, 44, 0}, {MOD3_R_ENV_D, 0, 46, 0}, {MOD3_R_ENV_S, 0, 48, 0}, {MOD3_R_ENV_R, 0, 50, 0},
    {MOD3_R_CC_ATK, 0, 44, 0}, {MOD3_R_CC_DEC, 0, 46, 0}, {MOD3_R_CC_REL, 0, 50, 0},
    {MOD3_R_CC_BRIGHT, MOD3_RF_TRACK, MOD3_P_E0, 0}, {MOD3_R_CC_RESO, MOD3_RF_TRACK, MOD3_P_E0 + 1, 0},
    {MOD3_R_BANK_FILTER, 0, 16, 0}, {MOD3_R_BANK_ENV, 0, 17, 0}, {MOD3_R_BANK_LFO, 0, 11, 0},
};
/* Motion's bytes (MOD_SCH_F_MBYTES): each panel value but the unison's, up to its highest value; no bits forced 0 */
static const uint8_t P5_SCH_WID[] = {
    127, 127, 127, 1, 1, 1, 1, 1, 127, 127, 1, 1, 1, 127, 127, 127, 127, 127, 127, 2, 1, 127, 127, 1,
    1, 1, 127, 1, 1, 1, 1, 1, 127, 127, 1, 1, 1, 127, 1, 1, 127, 1, 1, 127, 127, 127, 127, 127,
    127, 127, 127, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static void p5_sch_def(uint8_t *b) { p5_patch_init((p5_patch_t *)(void *)b); }
static const char *const P5S_STRIP[] = {"P5 ", 0};   /* (the pages' short names: the title past these) */
/* The sound designer (mod_schema.h): FEATURES (the mixer's paths, Poly-Mod, the wheel's LFO, the envelopes' switches,
 * unison), what a dim control needs, the build that cannot sound, the pages' stages; the goals' recipes (records) */
#define P5S_E6 (MOD3_D_TRACK | (MOD3_P_E0 + 6u))        /* (the part's OSC A / B levels: EDIT's mixer offsets) */
#define P5S_E7 (MOD3_D_TRACK | (MOD3_P_E0 + 7u))
#define P5S_WAVE_B D_NZ(P5_SAW_B), D_NZ(P5_TRI_B), MOD3_D_OR, D_NZ(P5_PULSE_B), MOD3_D_OR
#define P5S_B_ON P5S_WAVE_B, D_VAL(P5_LEVEL_B), D_VAL(P5S_E7), MOD3_D_ADD, D_CONST(0), MOD3_D_GT, MOD3_D_AND
#define P5S_A_ON(...) __VA_ARGS__, D_VAL(P5_LEVEL_A), D_VAL(P5S_E6), MOD3_D_ADD, D_CONST(0), MOD3_D_GT, MOD3_D_AND
#define P5S_B_SET D_AMT(P5_LEVEL_B, 80), D_IF(1), D_VAL(P5_LEVEL_B), D_VAL(P5S_E7), MOD3_D_ADD, D_CONST(0), MOD3_D_LE, \
    MOD3_D_END, D_SET(P5S_E7, 0), D_IF(1), P5S_WAVE_B, MOD3_D_NOT, MOD3_D_END, D_SET(P5_SAW_B, 1)
static const uint8_t P5S_D_OSCB[] = {P5S_B_ON, MOD3_D_END};
static const uint8_t P5S_D_OSCB_ON[] = {P5S_B_SET, MOD3_D_END};
static const uint8_t P5S_D_OSCB_OFF[] = {D_SET(P5_LEVEL_B, 0), D_IF(1), D_VAL(P5S_E7), D_CONST(0), MOD3_D_GT, MOD3_D_END,
                                         D_SET(P5S_E7, 0), MOD3_D_END};
static const uint8_t P5S_D_PULSEA[] = {D_NZ(P5_PULSE_A), MOD3_D_END};
static const uint8_t P5S_D_PULSEA_ON[] = {D_SETS(P5_PULSE_A), D_AMT(P5_LEVEL_A, 100), D_IF(1), D_VAL(P5_LEVEL_A),
                                          D_VAL(P5S_E6), MOD3_D_ADD, D_CONST(0), MOD3_D_LE, MOD3_D_END, D_SET(P5S_E6, 0),
                                          MOD3_D_END};
static const uint8_t P5S_D_PULSEA_OFF[] = {D_SETS(P5_PULSE_A), MOD3_D_END};
static const uint8_t P5S_D_PULSEB[] = {D_NZ(P5_PULSE_B), MOD3_D_END};
static const uint8_t P5S_D_PULSEB_ON[] = {D_SETS(P5_PULSE_B), P5S_B_SET, MOD3_D_END};   /* (B into the mix) */
static const uint8_t P5S_D_PULSEB_OFF[] = {D_SETS(P5_PULSE_B), MOD3_D_END};
static const uint8_t P5S_D_NOISE[] = {D_NZ(P5_NOISE), MOD3_D_END};
static const uint8_t P5S_D_NOISE_ON[] = {D_AMT(P5_NOISE, 60), MOD3_D_END};
static const uint8_t P5S_D_NOISE_OFF[] = {D_SET(P5_NOISE, 0), MOD3_D_END};
static const uint8_t P5S_D_SYNC[] = {D_NZ(P5_SYNC), MOD3_D_END};
/* (A heard: a wave and its level; else a level, then a saw: the level first, while A's waves are as they were) */
static const uint8_t P5S_D_SYNC_ON[] = {D_SETS(P5_SYNC), D_IF(1), D_NZ(P5_LEVEL_A), MOD3_D_NOT, D_NZ(P5_SAW_A),
                                        D_NZ(P5_PULSE_A), MOD3_D_OR, D_VAL(P5S_E6), D_CONST(0), MOD3_D_GT, MOD3_D_AND,
                                        MOD3_D_NOT, MOD3_D_AND, MOD3_D_END, D_SET(P5_LEVEL_A, 100), D_IF(1),
                                        D_VAL(P5_LEVEL_A), D_VAL(P5S_E6), MOD3_D_ADD, D_CONST(0), MOD3_D_LE, MOD3_D_END,
                                        D_SET(P5S_E6, 0), D_IF(1), D_NZ(P5_SAW_A), D_NZ(P5_PULSE_A), MOD3_D_OR,
                                        MOD3_D_NOT, MOD3_D_END, D_SET(P5_SAW_A, 1), MOD3_D_END};
static const uint8_t P5S_D_SYNC_OFF[] = {D_SETS(P5_SYNC), MOD3_D_END};
static const uint8_t P5S_D_POLY[] = {D_NZ(P5_POLY_FREQ), D_NZ(P5_POLY_PW), MOD3_D_OR, D_NZ(P5_POLY_FILTER), MOD3_D_OR,
                                     D_NZ(P5_POLY_ENV), D_NZ(P5_POLY_B), MOD3_D_OR, MOD3_D_AND, MOD3_D_END};
static const uint8_t P5S_D_POLY_ON[] = {D_IF(1), D_NZ(P5_POLY_FREQ), D_NZ(P5_POLY_PW), MOD3_D_OR, D_NZ(P5_POLY_FILTER),
                                        MOD3_D_OR, MOD3_D_NOT, MOD3_D_END, D_SET(P5_POLY_FILTER, 1), D_IF(1),
                                        D_NZ(P5_POLY_ENV), D_NZ(P5_POLY_B), MOD3_D_OR, MOD3_D_NOT, MOD3_D_END,
                                        D_SET(P5_POLY_ENV, 35), D_IF(1), P5S_WAVE_B, MOD3_D_NOT, MOD3_D_END,
                                        D_SET(P5_SAW_B, 1), MOD3_D_END};
static const uint8_t P5S_D_POLY_OFF[] = {D_SET(P5_POLY_FREQ, 0), D_SET(P5_POLY_PW, 0), D_SET(P5_POLY_FILTER, 0), MOD3_D_END};
#define P5S_WHEEL D_NZ(P5_WHEEL_FREQ_A), D_NZ(P5_WHEEL_FREQ_B), MOD3_D_OR, D_NZ(P5_WHEEL_PW_A), MOD3_D_OR, \
    D_NZ(P5_WHEEL_PW_B), MOD3_D_OR, D_NZ(P5_WHEEL_FILTER), MOD3_D_OR
#define P5S_LFO_WAVE D_NZ(P5_LFO_SAW), D_NZ(P5_LFO_TRI), MOD3_D_OR, D_NZ(P5_LFO_PULSE), MOD3_D_OR
static const uint8_t P5S_D_LFO[] = {P5S_WHEEL, D_NZ(P5_LFO_INITIAL), MOD3_D_AND, P5S_LFO_WAVE, D_NZ(P5_WHEEL_MIX),
                                    MOD3_D_OR, MOD3_D_AND, MOD3_D_END};
static const uint8_t P5S_D_LFO_ON[] = {D_IF(1), P5S_LFO_WAVE, MOD3_D_NOT, MOD3_D_END, D_SET(P5_LFO_TRI, 1), D_IF(1),
                                       P5S_WHEEL, MOD3_D_NOT, MOD3_D_END, D_SET(P5_WHEEL_FILTER, 1),
                                       D_AMT(P5_LFO_INITIAL, 25), D_AMT(P5_LFO_RATE, 50), D_IF(1), D_VAL(P5_WHEEL_MIX),
                                       D_CONST(127), MOD3_D_EQ, MOD3_D_END, D_SET(P5_WHEEL_MIX, 0), MOD3_D_END};
static const uint8_t P5S_D_LFO_OFF[] = {D_SET(P5_WHEEL_FREQ_A, 0), D_SET(P5_WHEEL_FREQ_B, 0), D_SET(P5_WHEEL_PW_A, 0),
                                        D_SET(P5_WHEEL_PW_B, 0), D_SET(P5_WHEEL_FILTER, 0), MOD3_D_END};
static const uint8_t P5S_D_FENV[] = {D_NZ(P5_ENV_FILTER), MOD3_D_END};
static const uint8_t P5S_D_FENV_ON[] = {D_AMT(P5_ENV_FILTER, 40), MOD3_D_END};
static const uint8_t P5S_D_FENV_OFF[] = {D_SET(P5_ENV_FILTER, 0), MOD3_D_END};
static const uint8_t P5S_D_UNISON[] = {D_NZ(P5_UNISON), MOD3_D_END};
static const uint8_t P5S_D_UNISON_ON[] = {D_SETS(P5_UNISON), D_IF(1), D_VAL(P5_UNISON_COUNT), D_CONST(1), MOD3_D_EQ,
                                          MOD3_D_END, D_SET(P5_UNISON_COUNT, 3), D_NOTE(P5_UNISON), MOD3_D_END};
static const uint8_t P5S_D_UNISON_OFF[] = {D_SETS(P5_UNISON), D_NOTE(P5_UNISON), MOD3_D_END};   /* (its voice settings:
                                                                                                * the part's) */
static const uint8_t P5S_D_RELEASE[] = {D_NZ(P5_RELEASE_ON), MOD3_D_END};
static const uint8_t P5S_D_RELEASE_SET[] = {D_SETS(P5_RELEASE_ON), MOD3_D_END};
static const uint8_t P5S_D_VEL[] = {D_NZ(P5_VEL_AMP), D_NZ(P5_VEL_FILTER), MOD3_D_OR, MOD3_D_END};
static const uint8_t P5S_D_VEL_SET[] = {D_SETS(P5_VEL_AMP), D_SETS(P5_VEL_FILTER), MOD3_D_END};
static const uint8_t P5S_D_GLIDE[] = {D_NZ(P5_GLIDE), MOD3_D_END};
static const uint8_t P5S_D_GLIDE_ON[] = {D_AMT(P5_GLIDE, 30), MOD3_D_END};
static const uint8_t P5S_D_GLIDE_OFF[] = {D_SET(P5_GLIDE, 0), MOD3_D_END};
enum { P5S_F_OSCB, P5S_F_PULSEA, P5S_F_PULSEB, P5S_F_NOISE, P5S_F_SYNC, P5S_F_POLY, P5S_F_LFO, P5S_F_FENV, P5S_F_UNISON,
       P5S_F_RELEASE, P5S_F_VEL, P5S_F_GLIDE };
static const mod_sch_feat_src P5S_DFEAT[] = {
    {"Osc B", 0, 0, 2, 0, 0, 0, P5S_D_OSCB, P5S_D_OSCB_ON, P5S_D_OSCB_OFF},
    {"Pulse A", 0, 0, 2, 0, 0, 0, P5S_D_PULSEA, P5S_D_PULSEA_ON, P5S_D_PULSEA_OFF},
    {"Pulse B", 0, 0, 2, 0, 0, 0, P5S_D_PULSEB, P5S_D_PULSEB_ON, P5S_D_PULSEB_OFF},
    {"Noise", 0, 0, 2, 0, 0, 0, P5S_D_NOISE, P5S_D_NOISE_ON, P5S_D_NOISE_OFF},
    {"Sync", 0, 0, 2, 0, 0, 0, P5S_D_SYNC, P5S_D_SYNC_ON, P5S_D_SYNC_OFF},
    {"Poly-Mod", 0, 0, 2, 0, 0, 0, P5S_D_POLY, P5S_D_POLY_ON, P5S_D_POLY_OFF},
    {"LFO", 0, 0, 2, 0, 0, 0, P5S_D_LFO, P5S_D_LFO_ON, P5S_D_LFO_OFF},
    {"Filter env", 0, 0, 2, 0, 0, 0, P5S_D_FENV, P5S_D_FENV_ON, P5S_D_FENV_OFF},
    {"Unison", 0, 0, 2, 0, 0, 0, P5S_D_UNISON, P5S_D_UNISON_ON, P5S_D_UNISON_OFF},
    {"Release", 0, 0, 2, 0, 0, 0, P5S_D_RELEASE, P5S_D_RELEASE_SET, P5S_D_RELEASE_SET},
    {"Velocity", 0, 0, 2, 0, 0, 0, P5S_D_VEL, P5S_D_VEL_SET, P5S_D_VEL_SET},
    {"Glide", 0, 0, 2, 0, 0, 0, P5S_D_GLIDE, P5S_D_GLIDE_ON, P5S_D_GLIDE_OFF},
};
static const uint8_t P5S_D_PW_POLY[] = {P5S_A_ON(D_NZ(P5_PULSE_A)), MOD3_D_END};   /* (pulse A heard: Poly-Mod's) */
static const uint8_t P5S_D_PW_B[] = {D_NZ(P5_PULSE_B), MOD3_D_END};
static const uint8_t P5S_D_REL_ON[] = {D_NZ(P5_RELEASE_ON), MOD3_D_END};
#define P5S_N(lo, hi, f) {lo, hi, 0, MOD3_DN_DIM, f, 0, 0}
static const mod_sch_need_src P5S_DNEED[] = {     /* (a dim control of the panel: else the LFO's, rate, waves, mix) */
    P5S_N(P5_PW_A, P5_PW_A, P5S_F_PULSEA), P5S_N(P5_WHEEL_PW_A, P5_WHEEL_PW_A, P5S_F_PULSEA),
    {P5_POLY_PW, P5_POLY_PW, 0, MOD3_DN_DIM, P5S_F_POLY, P5S_F_PULSEA, P5S_D_PW_POLY},
    {P5_PW_B, P5_PW_B, 0, MOD3_DN_DIM, P5S_F_OSCB, P5S_F_PULSEB, P5S_D_PW_B},
    {P5_WHEEL_PW_B, P5_WHEEL_PW_B, 0, MOD3_DN_DIM, P5S_F_OSCB, P5S_F_PULSEB, P5S_D_PW_B},
    P5S_N(P5_FREQ_B, P5_FINE_B, P5S_F_OSCB), P5S_N(P5_LOW_B, P5_KEY_B, P5S_F_OSCB),
    P5S_N(P5_UNISON_COUNT, P5_UNISON_DETUNE, P5S_F_UNISON), P5S_N(P5_RETRIGGER, P5_RETRIGGER, P5S_F_UNISON),
    P5S_N(P5_POLY_ENV, P5_POLY_FREQ, P5S_F_POLY), P5S_N(P5_POLY_FILTER, P5_POLY_FILTER, P5S_F_POLY),
    P5S_N(P5_RELEASE_AMP, P5_RELEASE_AMP, P5S_F_RELEASE),
    {P5_RELEASE_FILTER, P5_RELEASE_FILTER, 0, MOD3_DN_DIM, P5S_F_FENV, P5S_F_RELEASE, P5S_D_REL_ON},
    P5S_N(P5_ATTACK_FILTER, P5_ATTACK_FILTER, P5S_F_FENV), P5S_N(P5_DECAY_FILTER, P5_DECAY_FILTER, P5S_F_FENV),
    P5S_N(P5_SUSTAIN_FILTER, P5_SUSTAIN_FILTER, P5S_F_FENV), P5S_N(P5_VEL_FILTER, P5_VEL_FILTER, P5S_F_FENV),
    P5S_N(0, 0xFFFFu, P5S_F_LFO),
};
#undef P5S_N
/* (osc A, osc B and the noise all out of the mix) */
static const uint8_t P5S_D_SILENT[] = {P5S_A_ON(D_NZ(P5_SAW_A), D_NZ(P5_PULSE_A), MOD3_D_OR), P5S_B_ON, MOD3_D_OR,
                                       D_NZ(P5_NOISE), MOD3_D_OR, MOD3_D_NOT, MOD3_D_END};
/* the pages' stages (P5_SCH_PAGE's order) */
static const uint8_t P5S_DPAGE[] = {
    MOD3_DS_SOURCE, 0, 0, MOD3_DS_SOURCE, 0, 0, MOD3_DS_SOURCE, 0, 0, MOD3_DS_SOURCE, 0, 0,   /* STORE, OSC A, OSC B, B WAVES */
    MOD3_DS_SOURCE, 0, 0, MOD3_DS_TONE, 0, 0, MOD3_DS_ENV, 0, 0, MOD3_DS_ENV, 0, 0,          /* MIXER, FILTER, FLT ENV, AMP ENV */
    MOD3_DS_ENV, 0, 0, MOD3_DS_MOVE, 0, 0, MOD3_DS_MOVE, 0, 0, MOD3_DS_MOVE, 0, 0,           /* ENV MOD, POLYMOD, MOD FLT, LFO */
    MOD3_DS_MOVE, 0, 0, MOD3_DS_MOVE, 0, 0, MOD3_DS_PLAY, 0, 0, MOD3_DS_PLAY | MOD3_DS_SKIP, 0, 0,   /* WHEEL, WHEEL 2, UNISON,
                                                                                               * BEND */
    MOD3_DS_SOURCE, 0, 0, MOD3_DS_SOURCE, 0, 0,                                               /* (the SMK-37's banks) */
};
_Static_assert(sizeof P5S_DPAGE == 3u * sizeof P5_SCH_PAGE / sizeof *P5_SCH_PAGE, "a stage a page");
static const mod_sch_design_src P5S_DESIGN = {
    MOD3_DSF_NO_VOICE | MOD3_DSF_NO_EDIT | MOD3_DSF_OWN_LFO, P5S_DFEAT, sizeof P5S_DFEAT / sizeof *P5S_DFEAT,
    P5S_DNEED, sizeof P5S_DNEED / sizeof *P5S_DNEED, P5S_D_SILENT, "MIXER SILENT", P5S_DPAGE,
};
/* the goals' recipes (START: a goal from INIT): the envelopes, the cutoff, a pad's detuned B, a lead's unison, FX's
 * Poly-Mod on A's frequency from a low B */
static const mod_sch_rec_src P5S_REC[] = {
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), P5_ATTACK_AMP, 0},
    {MOD3_Q_GOAL, MOD3_GM(PAD), P5_ATTACK_AMP, 70},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), P5_DECAY_AMP, 45},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), P5_DECAY_AMP, 60},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), P5_SUSTAIN_AMP, 110},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), P5_SUSTAIN_AMP, 0},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(LEAD) | MOD3_GM(FX), P5_RELEASE_AMP, 20},
    {MOD3_Q_GOAL, MOD3_GM(PAD), P5_RELEASE_AMP, 85},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), P5_RELEASE_AMP, 45},
    {MOD3_Q_GOAL, MOD3_GM_ALL, P5_RELEASE_ON, 1},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), P5_ATTACK_FILTER, 0},
    {MOD3_Q_GOAL, MOD3_GM(PAD), P5_ATTACK_FILTER, 55},
    {MOD3_Q_GOAL, MOD3_GM_ALL, P5_DECAY_FILTER, 65},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), P5_SUSTAIN_FILTER, 0},
    {MOD3_Q_GOAL, MOD3_GM(PAD), P5_SUSTAIN_FILTER, 90},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), P5_RELEASE_FILTER, 30},
    {MOD3_Q_GOAL, MOD3_GM(PAD), P5_RELEASE_FILTER, 70},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(KEYS) | MOD3_GM(PLUCK), P5_ENV_FILTER, 55},
    {MOD3_Q_GOAL, MOD3_GM(PAD) | MOD3_GM(LEAD) | MOD3_GM(FX), P5_ENV_FILTER, 20},
    {MOD3_Q_GOAL, MOD3_GM(BASS), P5_CUTOFF, 55},
    {MOD3_Q_GOAL, MOD3_GM(PAD), P5_CUTOFF, 80},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(BASS) | MOD3_GM(PAD)), P5_CUTOFF, 100},
    {MOD3_Q_GOAL, MOD3_GM(PAD), P5_LEVEL_B, 80},
    {MOD3_Q_GOAL, MOD3_GM(PAD) | MOD3_GM(FX), P5_SAW_B, 1},
    {MOD3_Q_GOAL, MOD3_GM(PAD), P5_FINE_B, 68},
    {MOD3_Q_GOAL, MOD3_GM(LEAD), P5_UNISON, 1},
    {MOD3_Q_GOAL, MOD3_GM(LEAD), P5_UNISON_COUNT, 3},
    {MOD3_Q_GOAL, MOD3_GM(LEAD), P5_UNISON_DETUNE, 2},
    {MOD3_Q_GOAL, MOD3_GM(FX), P5_POLY_B, 25},
    {MOD3_Q_GOAL, MOD3_GM(FX), P5_POLY_FREQ, 1},
    {MOD3_Q_GOAL, MOD3_GM(FX), P5_LOW_B, 1},
    {MOD3_Q_GOAL, MOD3_GM(FX), P5_KEY_B, 0},
    {MOD3_Q_GOAL | MOD3_RQ_NOTE, MOD3_GM_ALL, P5_UNISON, 0},   /* (its voice settings: the part's) */
};
static const mod_sch_src P5_SCHEMA = {
    .patch = sizeof(p5_patch_t), .def_make = p5_sch_def, .store = MOD_SCH_STORE_PROGRAM, .utag = 'P', .mtag = 1,
    .flags = MOD_SCH_F_MBYTES | MOD_SCH_F_PCT127 | MOD_SCH_F_SNAME, .abbr = "P5", .wdef = MOD_SCH_NONE, .mmap = 0,
    .name = P5_NAME, .nlen = P5_NAME_LEN, .first = 1,   /* (picked: Sequential's first program, not INIT) */
    .param = P5_SCH_PARAM, .nparam = sizeof P5_SCH_PARAM / sizeof *P5_SCH_PARAM,
    .page = P5_SCH_PAGE, .npage = sizeof P5_SCH_PAGE / sizeof *P5_SCH_PAGE,
    .role = P5_SCH_ROLE, .nrole = sizeof P5_SCH_ROLE / sizeof *P5_SCH_ROLE,
    .wid = P5_SCH_WID, .nwid = sizeof P5_SCH_WID,
    .rec = P5S_REC, .nrec = sizeof P5S_REC / sizeof *P5S_REC,
    .strip = P5S_STRIP, .design = &P5S_DESIGN,
};
