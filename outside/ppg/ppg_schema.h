/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The PPG's schema (mod_schema.h): its program (ppg_patch.h, 115 bytes), every panel value by the id Saadet's pages
 * gave it (ppg_patch.h PG_*, PGX_*; each a MOD3_SK_CALL parameter: a group's fields are the group GROUP selects), its
 * pages as Saadet's (docs/ppg/mocks.html: PROGRAM, WAVES, TUNE, FILTER, ENV 1, LOUD, ENV 3, LFO, PLAY), roles, the
 * sound designer's recipes, the SysEx it answers (a V8 OS bank), the checks of a stored program and Motion's bytes.
 * The builder writes the file's SCHM chunk from it (the module's source built for the host: MOD_HOSTED). */
static const char *const PGS_TABLE[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14",
                                        "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27",
                                        "28", "29", "UPPER", "SAMPLE"};
static const char *const PGS_SW[] = {"PARA", "FIXED", "ENV 3", "OFF"};
static const char *const PGS_DETU[] = {"OFF", "1", "2", "3", "4", "5TH", "OCT", "2 OCT"};
static const char *const PGS_BI[] = {"2ND", "3RD", "5TH", "OCT"};
static const char *const PGS_BD[] = {"OFF", "PITCH", "FILTER", "WAVES", "SUB", "P+F", "P+W", "F+W"};
static const char *const PGS_TO[] = {"OFF", "OSC", "SUB", "BOTH"};
static const char *const PGS_SHAPE[] = {"1", "2", "3", "4"};
static const char *const PGS_GROUP[] = {"A", "B"};
static const char *const PGS_ONOFF[] = {"OFF", "ON"};
_Static_assert(sizeof PGS_TABLE / sizeof *PGS_TABLE == PG_SAMPLE + 1u, "PPG tables");
#define PGS_P(id, fmt, lo, hi, def, label, name, names) \
    {id, MOD3_SK_CALL, fmt, lo, hi, def, 0, 0, 0, MOD3_SF_DIM, label, name, names, 0, 0, 0}
#define PGS_K(id, label, name, def) PGS_P(id, MOD3_F_INT, 0, 63, def, label, name, 0)
#define PGS_C(id, label, name, def) PGS_P(id, MOD3_F_INT, 0, 7, def, label, name, 0)
#define PGS_E(id, label, name, names, hi, def) PGS_P(id, MOD3_F_ENUM, 0, hi, def, label, name, names)
#define PGS_S(id, label, name) PGS_P(id, MOD3_F_INT, -24, 24, 0, label, name, 0)
static const mod_sch_param_src PPG_SCH_PARAM[] = {   /* (ids ascending: PG_* then PGX_*) */
    PGS_E(PG_TABLE, "TABLE", "WAVETABLE", PGS_TABLE, 31, 0),
    PGS_K(PG_OSC, "OSC", "WAVES OSC", 63), PGS_K(PG_SUB, "SUB", "WAVES SUB", 0),
    PGS_E(PG_SW, "SUB", "SUB MODE", PGS_SW, 3, 3),
    PGS_K(PG_E1W, "ENV 1", "ENV 1 WAVES", 0), PGS_C(PG_KW, "KEYS", "KEYS WAVES", 0),
    PGS_E(PG_UW, "UPPER", "UPPER WAVES", PGS_ONOFF, 1, 0),
    PGS_S(PG_SEMI, "SEMI", "SEMITONES"), PGS_E(PG_DETU, "DETUNE", "SUB DETUNE", PGS_DETU, 7, 0),
    PGS_E(PG_BI, "BEND", "BEND RANGE", PGS_BI, 3, 0), PGS_E(PG_BD, "BEND TO", 0, PGS_BD, 7, 1),
    PGS_K(PG_CUT, "CUTOFF", "VCF CUTOFF", 63), PGS_K(PG_EMPH, "EMPH", "EMPHASIS", 0),
    PGS_K(PG_E1F, "ENV 1", "ENV 1 VCF", 0), PGS_C(PG_KF, "KEYS", "KEYS VCF", 3),
    PGS_K(PG_A1, "ATTACK", "ENV 1 ATTACK", 0), PGS_K(PG_D1, "DECAY", "ENV 1 DECAY", 30),
    PGS_K(PG_S1, "SUSTAIN", "ENV 1 SUSTAIN", 63), PGS_K(PG_R1, "RELEASE", "ENV 1 RELEASE", 20),
    PGS_K(PG_A2, "ATTACK", "ENV 2 ATTACK", 0), PGS_K(PG_D2, "DECAY", "ENV 2 DECAY", 30),
    PGS_K(PG_S2, "SUSTAIN", "ENV 2 SUSTAIN", 63), PGS_K(PG_R2, "RELEASE", "ENV 2 RELEASE", 16),
    PGS_K(PG_LOUD, "LEVEL", "LOUDNESS", 63), PGS_C(PG_KL, "KEYS", "KEYS LOUD", 4),
    PGS_K(PG_A3, "ATTACK", "ENV 3 ATTACK", 0), PGS_K(PG_D3, "DECAY", "ENV 3 DECAY", 20),
    PGS_P(PG_E3AMT, MOD3_F_INT, -32, 31, 0, "AMOUNT", "ENV 3 AMOUNT", 0), PGS_E(PG_E3TO, "TO", "ENV 3 TO", PGS_TO, 3, 0),
    PGS_K(PG_RATE, "RATE", "LFO RATE", 32), PGS_E(PG_SHAPE, "SHAPE", "LFO SHAPE", PGS_SHAPE, 3, 0),
    PGS_K(PG_DELAY, "DELAY", "LFO DELAY", 0), PGS_K(PG_WHEEL, "WHEEL", "LFO WHEEL", 0),
    PGS_E(PG_MP, "PITCH", "LFO PITCH", PGS_TO, 3, 0), PGS_E(PG_MW, "WAVES", "LFO WAVES", PGS_ONOFF, 1, 0),
    PGS_E(PG_MF, "FILTER", "LFO FILTER", PGS_ONOFF, 1, 0), PGS_E(PG_ML, "LOUD", "LFO LOUD", PGS_ONOFF, 1, 0),
    PGS_E(PG_TW, "WAVES", "TOUCH WAVES", PGS_ONOFF, 1, 0), PGS_E(PG_TF, "FILTER", "TOUCH FILTER", PGS_ONOFF, 1, 0),
    PGS_E(PG_TL, "LOUD", "TOUCH LOUD", PGS_ONOFF, 1, 0), PGS_E(PG_TM, "MOD", "TOUCH MOD", PGS_ONOFF, 1, 0),
    PGS_E(PG_VF, "FILTER", "VEL FILTER", PGS_ONOFF, 1, 0), PGS_E(PG_VL, "LOUD", "VEL LOUD", PGS_ONOFF, 1, 0),
    PGS_S(PG_SEMI2, "V2", "VOICE 2 SEMI"), PGS_S(PG_SEMI3, "V3", "VOICE 3 SEMI"), PGS_S(PG_SEMI4, "V4", "VOICE 4 SEMI"),
    PGS_S(PG_SEMI5, "V5", "VOICE 5 SEMI"), PGS_S(PG_SEMI6, "V6", "VOICE 6 SEMI"), PGS_S(PG_SEMI7, "V7", "VOICE 7 SEMI"),
    PGS_S(PG_SEMI8, "V8", "VOICE 8 SEMI"),
    PGS_E(PGX_SEL, "GROUP", 0, PGS_GROUP, 1, 0),
    PGS_P(PGX_KEYB, MOD3_F_INT, 0, 8, 0, "KEYB", "KEYB MODE", 0),
    PGS_P(PGX_SPLIT, MOD3_F_NOTE, PG_SPLIT0, PG_SPLIT0 + 60, PG_SPLIT0 + 24, "SPLIT", 0, 0),
    PGS_S(PGX_V1, "V1", "VOICE 1 SEMI"),
};
#undef PGS_P
#undef PGS_K
#undef PGS_C
#undef PGS_E
#undef PGS_S
#define PGS_PG(t, a, b, c, d, sec, fl) {t, MOD3_W_NONE, fl, {a, b, c, d}, 0, {0}, 0, 0, sec}
#define PGS_NO MOD_SCH_NONE
static const char *const PGS_INIT[] = {"PPG INIT"};
static const mod_sch_page_src PPG_SCH_PAGE[] = {
    {"PPG STORE", MOD3_W_STORE, MOD3_PG_LAST, {0, 1, 2, 3}, 0, {0}, PGS_INIT, 0, "Store"},
    PGS_PG("PPG PROGRAM", PGX_SEL, PGX_KEYB, PGX_SPLIT, PGS_NO, "PROGRAM", 0),
    PGS_PG("PPG TABLE", PG_TABLE, PG_OSC, PG_SUB, PG_SW, "WAVES", 0),
    PGS_PG("PPG SWEEP", PG_E1W, PG_KW, PG_UW, PGS_NO, "WAVES", 0),
    PGS_PG("PPG PITCH", PG_SEMI, PG_DETU, PG_BI, PG_BD, "TUNE", 0),
    PGS_PG("PPG SEMI 1-4", PGX_V1, PG_SEMI2, PG_SEMI3, PG_SEMI4, "TUNE", 0),
    PGS_PG("PPG SEMI 5-8", PG_SEMI5, PG_SEMI6, PG_SEMI7, PG_SEMI8, "TUNE", 0),
    PGS_PG("PPG FILTER", PG_CUT, PG_EMPH, PG_E1F, PG_KF, "FILTER", 0),
    PGS_PG("PPG ENV 1", PG_A1, PG_D1, PG_S1, PG_R1, "ENV 1", MOD3_PG_ENV),
    PGS_PG("PPG ENV 2", PG_A2, PG_D2, PG_S2, PG_R2, "LOUD", MOD3_PG_ENV),
    PGS_PG("PPG LEVEL", PG_LOUD, PG_KL, PGS_NO, PGS_NO, "LOUD", 0),
    PGS_PG("PPG ENV 3", PG_A3, PG_D3, PG_E3AMT, PG_E3TO, "ENV 3", MOD3_PG_ENV),
    PGS_PG("PPG LFO", PG_RATE, PG_SHAPE, PG_DELAY, PG_WHEEL, "LFO", MOD3_PG_LFO),
    PGS_PG("PPG LFO TO", PG_MP, PG_MW, PG_MF, PG_ML, "LFO", MOD3_PG_LFO),
    PGS_PG("PPG TOUCH", PG_TW, PG_TF, PG_TL, PG_TM, "PLAY", 0),
    PGS_PG("PPG VELOCITY", PG_VF, PG_VL, PGS_NO, PGS_NO, "PLAY", 0),
    PGS_PG("FILTER", PG_CUT, PG_EMPH, PG_E1F, PG_KF, 0, MOD3_PG_BANK),   /* (the SMK-37's FILTER, ENV, LFO banks) */
    PGS_PG("ENV", PG_A2, PG_D2, PG_S2, PG_R2, 0, MOD3_PG_BANK),
};
#undef PGS_PG
enum { PGS_PAGE_LFO = 12, PGS_PAGE_BFILTER = 16, PGS_PAGE_BENV = 17 };
_Static_assert(sizeof PPG_SCH_PAGE / sizeof *PPG_SCH_PAGE == PGS_PAGE_BENV + 1u, "the bank pages' indexes");
static const mod_sch_role_src PPG_SCH_ROLE[] = {   /* (HOME as Saadet's: the filter and the loudness's release) */
    {MOD3_R_HOME1, MOD3_RF_PIC_FILTER, PG_CUT, "CUTOFF"}, {MOD3_R_HOME2, MOD3_RF_PIC_FILTER, PG_EMPH, "EMPHASIS"},
    {MOD3_R_HOME3, MOD3_RF_PIC_FILTER, PG_E1F, "ENV 1 VCF"}, {MOD3_R_HOME4, MOD3_RF_PIC_ENV, PG_R2, "RELEASE"},
    {MOD3_R_CUTOFF, 0, PG_CUT, 0}, {MOD3_R_RESO, 0, PG_EMPH, 0},
    {MOD3_R_ENV_A, 0, PG_A2, 0}, {MOD3_R_ENV_D, 0, PG_D2, 0}, {MOD3_R_ENV_S, 0, PG_S2, 0}, {MOD3_R_ENV_R, 0, PG_R2, 0},
    {MOD3_R_CC_ATK, 0, PG_A2, 0}, {MOD3_R_CC_DEC, 0, PG_D2, 0}, {MOD3_R_CC_REL, 0, PG_R2, 0},
    {MOD3_R_CC_BRIGHT, 0, PG_CUT, 0}, {MOD3_R_CC_RESO, 0, PG_EMPH, 0},
    {MOD3_R_BANK_FILTER, 0, PGS_PAGE_BFILTER, 0}, {MOD3_R_BANK_ENV, 0, PGS_PAGE_BENV, 0},
    {MOD3_R_BANK_LFO, 0, PGS_PAGE_LFO, 0},
};
/* Motion's bytes (MOD_SCH_F_MBYTES): both groups' controls up to their largest values, the bits above them 0; KEYB,
 * SPLIT, GROUP and the name never */
#define PGS_MAXES 31, 63, 63, 3, 63, 7, 1, 48, 7, 3, 7, 63, 63, 63, 7, 63, 63, 63, 63, 63, 63, 63, 63, 63, 7, 63, 63, 63, \
                  3, 63, 3, 63, 63, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 48, 48, 48, 48, 48, 48, 48
#define PGS_ZEROS 0xE0, 0xC0, 0xC0, 0xFC, 0xC0, 0xF8, 0xFE, 0xC0, 0xF8, 0xFC, 0xF8, 0xC0, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, \
                  0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xC0, 0xFC, 0xC0, 0xFC, 0xC0, 0xC0, 0xFC,   \
                  0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0
static const uint8_t PPG_SCH_WID[2u * PPG_BLOB] = {PGS_MAXES, PGS_MAXES, [PPG_BLOB] = PGS_ZEROS, PGS_ZEROS};
static void ppg_sch_def(uint8_t *b) { ppg_patch_init((ppg_patch_t *)(void *)b); }
static const char *const PGS_STRIP[] = {"PPG ", 0};
/* the sound designer's recipes (START: a goal from INIT): the loudness's envelope, the filter and its envelope; the
 * rest is the firmware's from the roles */
#define PGS_ALL MOD3_GM_ALL
#define PGS_KP (MOD3_GM(KEYS) | MOD3_GM(PLUCK))
static const mod_sch_rec_src PGS_REC[] = {
    {MOD3_Q_GOAL, PGS_ALL & ~MOD3_GM(PAD), PG_A2, 0}, {MOD3_Q_GOAL, MOD3_GM(PAD), PG_A2, 36},
    {MOD3_Q_GOAL, PGS_ALL & ~PGS_KP, PG_D2, 30}, {MOD3_Q_GOAL, PGS_KP, PG_D2, 34},
    {MOD3_Q_GOAL, PGS_ALL & ~PGS_KP, PG_S2, 63}, {MOD3_Q_GOAL, PGS_KP, PG_S2, 0},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(LEAD) | MOD3_GM(FX), PG_R2, 12},
    {MOD3_Q_GOAL, MOD3_GM(PAD), PG_R2, 40}, {MOD3_Q_GOAL, PGS_KP, PG_R2, 26},
    {MOD3_Q_GOAL, MOD3_GM(BASS), PG_CUT, 20}, {MOD3_Q_GOAL, MOD3_GM(PAD), PG_CUT, 34},
    {MOD3_Q_GOAL, PGS_ALL & ~(MOD3_GM(BASS) | MOD3_GM(PAD)), PG_CUT, 40},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | PGS_KP, PG_E1F, 30}, {MOD3_Q_GOAL, MOD3_GM(BASS) | PGS_KP, PG_D1, 30},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | PGS_KP, PG_S1, 0},
    {MOD3_Q_GOAL, MOD3_GM(PAD) | MOD3_GM(LEAD) | MOD3_GM(FX), PG_E1F, 12},
    {MOD3_Q_GOAL, MOD3_GM(FX), PG_EMPH, 44}, {MOD3_Q_GOAL, MOD3_GM(PAD), PG_E1W, 40},
    {MOD3_Q_GOAL, MOD3_GM(BASS), PG_SEMI, -12},
};
#undef PGS_ALL
#undef PGS_KP
/* a stored program's checks: every byte of each group up to its largest value (one record a run of equal ones, both
 * groups), KEYB, SPLIT and GROUP in range, a printable name */
#define PGS_CK(off, n, hi) (uint8_t)(off), 0, (n), 1, 0xFF, 0, 0, (hi), (uint8_t)((off) + PG_RAW), 0, (n), 1, 0xFF, 0, 0, (hi)
#define PGS_CK1(off, n, lo, hi) (uint8_t)(off), 0, (n), 1, 0xFF, 0, (lo), (hi)
static const uint8_t PGS_CHECK[] = {
    PGS_CK(PG_TABLE, 1, 31), PGS_CK(PG_OSC, 2, 63), PGS_CK(PG_SW, 1, 3), PGS_CK(PG_E1W, 1, 63), PGS_CK(PG_KW, 1, 7),
    PGS_CK(PG_UW, 1, 1), PGS_CK(PG_SEMI, 1, 48), PGS_CK(PG_DETU, 1, 7), PGS_CK(PG_BI, 1, 3), PGS_CK(PG_BD, 1, 7),
    PGS_CK(PG_CUT, 3, 63), PGS_CK(PG_KF, 1, 7), PGS_CK(PG_A1, 9, 63), PGS_CK(PG_KL, 1, 7), PGS_CK(PG_A3, 3, 63),
    PGS_CK(PG_E3TO, 1, 3), PGS_CK(PG_RATE, 1, 63), PGS_CK(PG_SHAPE, 1, 3), PGS_CK(PG_DELAY, 2, 63),
    PGS_CK(PG_MP, 1, 3), PGS_CK(PG_MW, 9, 1), PGS_CK(PG_SEMI2, 7, 48),
    PGS_CK1(PPG_KEYB, 1, 0, 8), PGS_CK1(PPG_SPLIT, 1, 0, 60), PGS_CK1(PPG_SEL, 1, 0, 1),
    PGS_CK1(PPG_NAME, PG_NAME_LEN, 32, 126),
};
#undef PGS_CK
#undef PGS_CK1
static const uint8_t PGS_SYSEX[] = {3, 0x29, 0x01, 0x0D};   /* (a V8 OS bank: F0 29 01 0D ..; ppg.c ppg_sysex) */
static const mod_sch_src PPG_SCHEMA = {
    .patch = PPG_BLOB, .def_make = ppg_sch_def, .store = MOD_SCH_STORE_NONE, .utag = 'W', .mtag = 22,
    .flags = MOD_SCH_F_MBYTES | MOD_SCH_F_SNAME | MOD_SCH_F_NOEDIT, .abbr = "PPG", .wdef = MOD_SCH_NONE, .mmap = 0,
    .name = PPG_NAME, .nlen = PG_NAME_LEN, .first = PPG_NPRESETS > 1u ? 1u : 0u, .pname = "PPG PROGRAM",
    .param = PPG_SCH_PARAM, .nparam = sizeof PPG_SCH_PARAM / sizeof *PPG_SCH_PARAM,
    .page = PPG_SCH_PAGE, .npage = sizeof PPG_SCH_PAGE / sizeof *PPG_SCH_PAGE,
    .role = PPG_SCH_ROLE, .nrole = sizeof PPG_SCH_ROLE / sizeof *PPG_SCH_ROLE,
    .wid = PPG_SCH_WID, .nwid = sizeof PPG_SCH_WID,
    .rec = PGS_REC, .nrec = sizeof PGS_REC / sizeof *PGS_REC,
    .strip = PGS_STRIP,
    .sysex = PGS_SYSEX, .nsysex = 1,
    .check = PGS_CHECK, .ncheck = sizeof PGS_CHECK / MOD_SCH_CREC,
};
