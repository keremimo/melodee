/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* HYDRA's schema (mod_schema.h): its patch (hydra_patch.h, 144 bytes), every panel value by its byte's place (Saadet's
 * hydra_edit.h HY_PANEL: the ids its editor and Motion used), the macros' destinations and depths past the patch's bytes
 * (HYV_DEST / HYV_DEPTH, the module's pget / pput) and their values (the part's E1..E8), its pages (Saadet's EDIT, ENV 1
 * and LFO > MACRO stops), roles, the sound designer's recipes, its SysEx, the checks of a stored patch and Motion's
 * bytes. The builder writes the file's SCHM chunk from it (the module's source built for the host: MOD_HOSTED). */
static const char *const HYS_OMODE[] = {"SINGLE", "SCAN"};
static const char *const HYS_MUT[] = {"OFF", "FM", "STACK", "SYNC", "PW", "SQZ", "WARP", "HARM"};
static const char *const HYS_SRC[] = {"OSC 1", "OSC 2", "OSC 3", "RING", "NOISE"};
static const char *const HYS_OSC[] = {"OSC 1", "OSC 2", "OSC 3"};
static const char *const HYS_NCOLOR[] = {"WHITE", "PINK", "BROWN"};
static const char *const HYS_ROUTE[] = {"SERIES", "PARLLEL"};
static const char *const HYS_F1[] = {"LAD12", "LAD24", "FAT12", "FAT24", "MS LP", "MS HP", "LPG", "3P LP", "3P BP",
                                     "3P HP", "VOWEL"};
static const char *const HYS_F2[] = {"BAND", "NOTCH"};
static const char *const HYS_DPOS[] = {"PRE", "POST"};
_Static_assert(sizeof HYS_F1 / sizeof *HYS_F1 == F1_N && sizeof HYS_MUT / sizeof *HYS_MUT == MU_N, "HYDRA's names");
/* The waves (56, 57 with EMPTY), the macros' names (48) and destinations (32) are more names than a page's column holds
 * (the firmware's: 33, in 100 bytes): those are numbers the module words (MOD3_SF_FORMAT, hydra.c hy_pformat) */
/* a byte of the patch, raw (an offset value shows signed: MOD3_F_OFS, 0 at the middle of its range; an enum's names whole
 * where they fit) */
#define HYS_P(id, fmt, hi, def, label, name, names, unit, fl) \
    {id, MOD3_SK_FIELD, fmt, 0, hi, def, id, 0, 8, MOD3_SF_DIM | (fl), label, name, names, unit, 0, \
     (fmt) == MOD3_F_ENUM ? MOD3_SF2_WHOLE : 0}
#define HYS_K(id, label, name, def) HYS_P(id, MOD3_F_PCT, 127, def, label, name, 0, 0, 0)
#define HYS_OSC_PARAMS(b, n) \
    HYS_P(b + HO_MODE, MOD3_F_ENUM, 1, 0, "MODE", n " MODE", HYS_OMODE, 0, 0), \
    HYS_P(b + HO_WAVE, MOD3_F_INT, HY_NWAVE - 1, HYW_SAW, "WAVE", n " WAVE", 0, 0, MOD3_SF_FORMAT), \
    HYS_P(b + HO_L0, MOD3_F_INT, HY_NWAVE, 0, "SLOT 1", n " SLOT 1", 0, 0, MOD3_SF_FORMAT), \
    HYS_P(b + HO_L0 + 1, MOD3_F_INT, HY_NWAVE, 0, "SLOT 2", n " SLOT 2", 0, 0, MOD3_SF_FORMAT), \
    HYS_P(b + HO_L0 + 2, MOD3_F_INT, HY_NWAVE, 0, "SLOT 3", n " SLOT 3", 0, 0, MOD3_SF_FORMAT), \
    HYS_P(b + HO_L0 + 3, MOD3_F_INT, HY_NWAVE, 0, "SLOT 4", n " SLOT 4", 0, 0, MOD3_SF_FORMAT), \
    HYS_P(b + HO_L0 + 4, MOD3_F_INT, HY_NWAVE, 0, "SLOT 5", n " SLOT 5", 0, 0, MOD3_SF_FORMAT), \
    HYS_P(b + HO_L0 + 5, MOD3_F_INT, HY_NWAVE, 0, "SLOT 6", n " SLOT 6", 0, 0, MOD3_SF_FORMAT), \
    HYS_P(b + HO_L0 + 6, MOD3_F_INT, HY_NWAVE, 0, "SLOT 7", n " SLOT 7", 0, 0, MOD3_SF_FORMAT), \
    HYS_P(b + HO_L0 + 7, MOD3_F_INT, HY_NWAVE, 0, "SLOT 8", n " SLOT 8", 0, 0, MOD3_SF_FORMAT), \
    HYS_K(b + HO_SCAN, "SCAN", n " SCAN", 0), \
    HYS_P(b + HO_SEMI, MOD3_F_OFS, 2 * HY_SEMI0, HY_SEMI0, "SEMI", n " SEMI", 0, "st", 0), \
    HYS_P(b + HO_CENT, MOD3_F_OFS, 2 * HY_CENT0, HY_CENT0, "CENT", n " CENT", 0, "ct", 0), \
    HYS_P(b + HO_KEY, MOD3_F_INT, 200, 100, "KEYS", n " KEYS", 0, "%", 0)
#define HYS_MUT_PARAMS(b, n) \
    HYS_P(b + HM_MODE, MOD3_F_ENUM, MU_N - 1, MU_OFF, "MODE", n " MODE", HYS_MUT, 0, 0), \
    HYS_P(b + HM_SRC, MOD3_F_ENUM, HYS_N - 1, HYS_OSC3, "FROM", n " FROM", HYS_SRC, 0, 0), \
    HYS_P(b + HM_RATIO, MOD3_F_INT, 127, 32, "RATIO", n " RATIO", 0, 0, MOD3_SF_DESC), \
    HYS_K(b + HM_DEPTH, "DEPTH", n " DEPTH", 0), \
    HYS_P(b + HM_AUX, MOD3_F_PCT, 127, 0, "AUX", n " AUX", 0, 0, MOD3_SF_DESC), \
    HYS_K(b + HM_WET, "WET", n " WET", 127)
#define HYS_ENVAMT(id, label, name) HYS_P(id, MOD3_F_OFS, 2 * HY_ENV0, HY_ENV0, label, name, 0, 0, 0)
#define HYS_NAME(m) HYS_P(HY_MNAME + (m) - 1, MOD3_F_INT, HN_COUNT - 1, 0, "NAME", "MACRO " #m " NAME", 0, 0, \
                           MOD3_SF_FORMAT)                    /* (m: 1..8) */
#define HYS_DESTS(m) HYS_DEST(m, 1), HYS_DEST(m, 2), HYS_DEST(m, 3)
#define HYS_VALUE(m) \
    {HYV_VALUE + (m) - 1u, MOD3_SK_TRACK, MOD3_F_BIPCT, -64, 63, 0, MOD3_P_E0 + (m) - 1u, 0, 0, MOD3_SF_DIM, "VALUE", \
     "MACRO " #m " VALUE", 0, 0}                 /* (m: 1..8; E1..E8 of the part) */
/* macro m's (0..7) destination j (1..3) and its depth (the module's: hydra.c HYV_DEST, HYV_DEPTH) */
#define HYS_DEST(m, j) \
    {HY_BYTES + (m) * 6u + ((j) - 1u) * 2u, MOD3_SK_CALL, MOD3_F_INT, 0, HY_NTGT, 0, 0, 0, 0, \
     MOD3_SF_DIM | MOD3_SF_FORMAT, "DEST " #j, 0, 0, 0}, \
    {HY_BYTES + (m) * 6u + ((j) - 1u) * 2u + 1u, MOD3_SK_CALL, MOD3_F_INT, -HY_DEPTH0, HY_DEPTH0, 0, 0, 0, 0, MOD3_SF_DIM, \
     "DEPTH " #j, 0, 0, 0}
static const mod_sch_param_src HY_SCH_PARAM[] = {
    HYS_OSC_PARAMS(HY_O1, "OSC 1"), HYS_OSC_PARAMS(HY_O2, "OSC 2"),
    HYS_P(HY_O3_WAVE, MOD3_F_INT, HY_NWAVE - 1, HYW_SINE, "WAVE", "OSC 3 WAVE", 0, 0, MOD3_SF_FORMAT),
    HYS_P(HY_O3_SEMI, MOD3_F_OFS, 2 * HY_SEMI0, HY_SEMI0, "SEMI", "OSC 3 SEMI", 0, "st", 0),
    HYS_P(HY_O3_CENT, MOD3_F_OFS, 2 * HY_CENT0, HY_CENT0, "CENT", "OSC 3 CENT", 0, "ct", 0),
    HYS_P(HY_O3_KEY, MOD3_F_INT, 200, 100, "KEYS", "OSC 3 KEYS", 0, "%", 0),
    HYS_MUT_PARAMS(HY_M1, "MUTANT 1"), HYS_MUT_PARAMS(HY_M2, "MUTANT 2"), HYS_MUT_PARAMS(HY_M3, "MUTANT 3"),
    HYS_MUT_PARAMS(HY_M4, "MUTANT 4"),
    HYS_P(HY_RING_A, MOD3_F_ENUM, 2, HYS_OSC1, "RING A", 0, HYS_OSC, 0, 0),
    HYS_P(HY_RING_B, MOD3_F_ENUM, 2, HYS_OSC2, "RING B", 0, HYS_OSC, 0, 0),
    HYS_P(HY_NOISE, MOD3_F_ENUM, 2, 0, "COLOR", "NOISE COLOR", HYS_NCOLOR, 0, 0),
    HYS_K(HY_LVL, "OSC 1", "OSC 1 LEVEL", 100), HYS_K(HY_LVL + 1, "OSC 2", "OSC 2 LEVEL", 0),
    HYS_K(HY_LVL + 2, "OSC 3", "OSC 3 LEVEL", 0), HYS_K(HY_LVL + 3, "RING", "RING LEVEL", 0),
    HYS_K(HY_LVL + 4, "NOISE", "NOISE LEVEL", 0),
    HYS_K(HY_BAL, "OSC 1>F2", 0, 0), HYS_K(HY_BAL + 1, "OSC 2>F2", 0, 0), HYS_K(HY_BAL + 2, "OSC 3>F2", 0, 0),
    HYS_K(HY_BAL + 3, "RING>F2", 0, 0), HYS_K(HY_BAL + 4, "NOISE>F2", 0, 0),
    HYS_P(HY_ROUTE, MOD3_F_ENUM, 1, 0, "ROUTE", 0, HYS_ROUTE, 0, 0),
    HYS_P(HY_F1_TYPE, MOD3_F_ENUM, F1_N - 1, F1_LAD24, "TYPE", "FILTER 1 TYPE", HYS_F1, 0, 0),
    HYS_P(HY_F1_CUT, MOD3_F_CUTOFF, 127, 127, "CUTOFF", "FILTER 1 CUTOFF", 0, 0, 0),
    HYS_K(HY_F1_RES, "RES", "FILTER 1 RES", 0), HYS_K(HY_F1_DRIVE, "DRIVE", "FILTER 1 DRIVE", 0),
    HYS_P(HY_F1_DPOS, MOD3_F_ENUM, 1, 0, "ORDER", "DRIVE ORDER", HYS_DPOS, 0, 0),
    HYS_ENVAMT(HY_F1_ENV, "ENV 1", "FILTER 1 ENV 1"),
    HYS_P(HY_F1_KEY, MOD3_F_INT, 200, 0, "KEYS", "FILTER 1 KEYS", 0, "%", 0),
    HYS_K(HY_F1_VEL, "VEL", "FILTER 1 VEL", 0),
    HYS_P(HY_F2_TYPE, MOD3_F_ENUM, 1, 0, "TYPE", "FILTER 2 TYPE", HYS_F2, 0, 0),
    HYS_K(HY_F2_MORPH, "PASS", "FILTER 2 PASS", 0),
    HYS_P(HY_F2_CUT, MOD3_F_CUTOFF, 127, 127, "CUTOFF", "FILTER 2 CUTOFF", 0, 0, 0),
    HYS_K(HY_F2_RES, "RES", "FILTER 2 RES", 0),
    HYS_ENVAMT(HY_F2_ENV, "ENV 1", "FILTER 2 ENV 1"),
    HYS_P(HY_F2_KEY, MOD3_F_INT, 200, 0, "KEYS", "FILTER 2 KEYS", 0, "%", 0),
    HYS_K(HY_AMP, "LEVEL", 0, 100),
    HYS_ENVAMT(HY_E1_SC1, "SCAN 1", "ENV 1 > SCAN 1"), HYS_ENVAMT(HY_E1_SC2, "SCAN 2", "ENV 1 > SCAN 2"),
    HYS_ENVAMT(HY_E1_MU1, "MUTANT 1", "ENV 1 > MUTANT 1"),
    HYS_P(HY_E1_DLY, MOD3_F_TIME, 127, 0, "DELAY", "ENV 1 DELAY", 0, 0, 0),
    HYS_P(HY_E1_ATK, MOD3_F_TIME, 127, 0, "ATTACK", "ENV 1 ATTACK", 0, 0, 0),
    HYS_P(HY_E1_HLD, MOD3_F_TIME, 127, 0, "HOLD", "ENV 1 HOLD", 0, 0, 0),
    HYS_P(HY_E1_DEC, MOD3_F_TIME, 127, 64, "DECAY", "ENV 1 DECAY", 0, 0, 0),
    HYS_K(HY_E1_SUS, "SUSTAIN", "ENV 1 SUSTAIN", 0),
    HYS_P(HY_E1_REL, MOD3_F_TIME, 127, 50, "RELEASE", "ENV 1 RELEASE", 0, 0, 0),
    HYS_NAME(1), HYS_NAME(2), HYS_NAME(3), HYS_NAME(4), HYS_NAME(5), HYS_NAME(6), HYS_NAME(7), HYS_NAME(8),
    HYS_DESTS(0), HYS_DESTS(1), HYS_DESTS(2), HYS_DESTS(3), HYS_DESTS(4), HYS_DESTS(5), HYS_DESTS(6), HYS_DESTS(7),
    HYS_VALUE(1), HYS_VALUE(2), HYS_VALUE(3), HYS_VALUE(4), HYS_VALUE(5), HYS_VALUE(6), HYS_VALUE(7), HYS_VALUE(8),
};
#undef HYS_P
#undef HYS_K
#undef HYS_OSC_PARAMS
#undef HYS_MUT_PARAMS
#undef HYS_ENVAMT
#undef HYS_NAME
#undef HYS_DEST
#undef HYS_DESTS
#undef HYS_VALUE
#define HYS_PG(t, a, b, c, d, sec, fl) {t, MOD3_W_NONE, fl, {a, b, c, d}, 0, {0}, 0, 0, sec}
#define HYS_NO MOD_SCH_NONE
#define HYS_OSC_PAGES(t, t2, t3, t4, b, sec) \
    HYS_PG(t, b + HO_MODE, b + HO_WAVE, b + HO_SCAN, b + HO_SEMI, sec, 0), \
    HYS_PG(t2, b + HO_CENT, b + HO_KEY, HYS_NO, HYS_NO, sec, 0), \
    HYS_PG(t3, b + HO_L0, b + HO_L0 + 1, b + HO_L0 + 2, b + HO_L0 + 3, sec, 0), \
    HYS_PG(t4, b + HO_L0 + 4, b + HO_L0 + 5, b + HO_L0 + 6, b + HO_L0 + 7, sec, 0)
#define HYS_MUT_PAGES(t, t2, b, sec) \
    HYS_PG(t, b + HM_MODE, b + HM_DEPTH, b + HM_RATIO, b + HM_WET, sec, 0), \
    HYS_PG(t2, b + HM_SRC, b + HM_AUX, HYS_NO, HYS_NO, sec, 0)
#define HYS_MACRO_PAGES(t, t2, m) \
    HYS_PG(t, HY_MNAME + (m), HYV_VALUE + (m), HY_BYTES + (m) * 6u, HY_BYTES + (m) * 6u + 1u, "Macros", 0), \
    HYS_PG(t2, HY_BYTES + (m) * 6u + 2u, HY_BYTES + (m) * 6u + 3u, HY_BYTES + (m) * 6u + 4u, HY_BYTES + (m) * 6u + 5u, \
           "Macros", 0)
static const char *const HYS_INIT[] = {"HYDRA INIT"};
static const mod_sch_page_src HY_SCH_PAGE[] = {
    {"HY STORE", MOD3_W_STORE, MOD3_PG_LAST, {0, 1, 2, 3}, 0, {0}, HYS_INIT, 0, "Store"},
    HYS_OSC_PAGES("HY OSC 1", "HY O1 TUNE", "HY O1 SLOTS", "HY O1 SLOTS2", HY_O1, "Osc 1"),
    HYS_MUT_PAGES("HY MUTANT 1", "HY MUTANT 1B", HY_M1, "Mutant 1"),
    HYS_MUT_PAGES("HY MUTANT 2", "HY MUTANT 2B", HY_M2, "Mutant 2"),
    HYS_OSC_PAGES("HY OSC 2", "HY O2 TUNE", "HY O2 SLOTS", "HY O2 SLOTS2", HY_O2, "Osc 2"),
    HYS_MUT_PAGES("HY MUTANT 3", "HY MUTANT 3B", HY_M3, "Mutant 3"),
    HYS_MUT_PAGES("HY MUTANT 4", "HY MUTANT 4B", HY_M4, "Mutant 4"),
    HYS_PG("HY OSC 3", HY_O3_WAVE, HY_O3_SEMI, HY_O3_CENT, HY_O3_KEY, "Osc 3", 0),
    HYS_PG("HY MIXER", HY_LVL, HY_LVL + 1, HY_LVL + 2, HY_AMP, "Mixer", 0),
    HYS_PG("HY RING", HY_LVL + 3, HY_RING_A, HY_RING_B, HYS_NO, "Mixer", 0),
    HYS_PG("HY NOISE", HY_LVL + 4, HY_NOISE, HYS_NO, HYS_NO, "Mixer", 0),
    HYS_PG("HY TO F2", HY_BAL, HY_BAL + 1, HY_BAL + 2, HY_BAL + 3, "Mixer", 0),
    HYS_PG("HY ROUTE", HY_BAL + 4, HY_ROUTE, HYS_NO, HYS_NO, "Mixer", 0),
    HYS_PG("HY FILTER 1", HY_F1_TYPE, HY_F1_CUT, HY_F1_RES, HY_F1_ENV, "Filter 1", 0),
    HYS_PG("HY F1 DRIVE", HY_F1_DRIVE, HY_F1_DPOS, HY_F1_KEY, HY_F1_VEL, "Filter 1", 0),
    HYS_PG("HY FILTER 2", HY_F2_TYPE, HY_F2_MORPH, HY_F2_CUT, HY_F2_RES, "Filter 2", 0),
    HYS_PG("HY F2 MOD", HY_F2_ENV, HY_F2_KEY, HYS_NO, HYS_NO, "Filter 2", 0),
    /* (ENV 1 moves the filters and the scans; the amplitude is the part's ADSR: these are not the ENV button's) */
    HYS_PG("HY ENV 1", HY_E1_ATK, HY_E1_DEC, HY_E1_SUS, HY_E1_REL, "Env 1", 0),
    HYS_PG("HY E1 TIMING", HY_E1_DLY, HY_E1_HLD, HYS_NO, HYS_NO, "Env 1", 0),
    HYS_PG("HY E1 TARGET", HY_E1_SC1, HY_E1_SC2, HY_E1_MU1, HYS_NO, "Env 1", 0),
    HYS_MACRO_PAGES("HY MACRO 1", "HY MACRO 1B", 0), HYS_MACRO_PAGES("HY MACRO 2", "HY MACRO 2B", 1),
    HYS_MACRO_PAGES("HY MACRO 3", "HY MACRO 3B", 2), HYS_MACRO_PAGES("HY MACRO 4", "HY MACRO 4B", 3),
    HYS_MACRO_PAGES("HY MACRO 5", "HY MACRO 5B", 4), HYS_MACRO_PAGES("HY MACRO 6", "HY MACRO 6B", 5),
    HYS_MACRO_PAGES("HY MACRO 7", "HY MACRO 7B", 6), HYS_MACRO_PAGES("HY MACRO 8", "HY MACRO 8B", 7),
    HYS_PG("FILTER", HY_F1_CUT, HY_F1_RES, HY_F1_ENV, HY_F1_DRIVE, 0, MOD3_PG_BANK),   /* (the SMK-37's FILTER bank) */
};
#undef HYS_PG
#undef HYS_OSC_PAGES
#undef HYS_MUT_PAGES
#undef HYS_MACRO_PAGES
enum { HYS_PAGE_BFILTER = 46 };
_Static_assert(sizeof HY_SCH_PAGE / sizeof *HY_SCH_PAGE == HYS_PAGE_BFILTER + 1u, "the FILTER bank is the last page");
/* HOME's knobs are the engine's (E1..E4: macros 1..4); CC 74 / 71 the macros the factory sounds keep for BRIGHT and
 * the resonance (3 and 4); the amplitude's envelope is the part's ADSR (no ENV roles: the firmware's own) */
static const mod_sch_role_src HY_SCH_ROLE[] = {
    {MOD3_R_CUTOFF, 0, HY_F1_CUT, 0}, {MOD3_R_RESO, 0, HY_F1_RES, 0},
    {MOD3_R_CC_BRIGHT, MOD3_RF_TRACK, MOD3_P_E0 + 2, 0}, {MOD3_R_CC_RESO, MOD3_RF_TRACK, MOD3_P_E0 + 3, 0},
    {MOD3_R_BANK_FILTER, 0, HYS_PAGE_BFILTER, 0},
};
/* Motion's bytes (MOD_SCH_F_MBYTES): every panel value up to its highest, but what a voice is made of (an oscillator's
 * MODE, a mutant's MODE, FILTER 1's TYPE: Saadet's macros may not reach them either), the macros' names and pool, the
 * name; no bits forced 0 */
#define HYS_W_OSC 0, 120, 121, 121, 121, 121, 121, 121, 121, 121, 127, 2 * HY_SEMI0, 2 * HY_CENT0, 200
#define HYS_W_MUT 0, HYS_N - 1, 127, 127, 127, 127
static const uint8_t HY_SCH_WID[2u * HY_BYTES] = {
    HYS_W_OSC, HYS_W_OSC, 120, 2 * HY_SEMI0, 2 * HY_CENT0, 200,
    HYS_W_MUT, HYS_W_MUT, HYS_W_MUT, HYS_W_MUT,
    2, 2, 2, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 1,
    0, 127, 127, 127, 1, 2 * HY_ENV0, 200, 127, 0,
    1, 127, 127, 127, 2 * HY_ENV0, 200,
    127, 2 * HY_ENV0, 2 * HY_ENV0, 2 * HY_ENV0, 127, 127, 127, 127, 127, 127,
};
#undef HYS_W_OSC
#undef HYS_W_MUT
_Static_assert(HY_E1_REL == 94, "HY_SCH_WID lists the bytes up to ENV 1's RELEASE");
static void hy_sch_def(uint8_t *b) { hy_patch_init((hy_patch_t *)(void *)b); }
static const char *const HYS_STRIP[] = {"HY ", 0};
/* the sound designer's recipes (START: a goal from INIT): the part's ADSR (the amplitude), FILTER 1 and ENV 1, OSC 2
 * beside OSC 1 for a pad, a stack for a lead; the rest is the firmware's */
#define HYS_ALL MOD3_GM_ALL
#define HYS_KP (MOD3_GM(KEYS) | MOD3_GM(PLUCK))
#define HYS_T(p) (MOD3_D_TRACK | (p))
static const mod_sch_rec_src HYS_REC[] = {
    {MOD3_Q_GOAL, HYS_ALL & ~MOD3_GM(PAD), HYS_T(MOD3_P_ATK), 0}, {MOD3_Q_GOAL, MOD3_GM(PAD), HYS_T(MOD3_P_ATK), 60},
    {MOD3_Q_GOAL, HYS_ALL & ~HYS_KP, HYS_T(MOD3_P_DEC), 60}, {MOD3_Q_GOAL, HYS_KP, HYS_T(MOD3_P_DEC), 55},
    {MOD3_Q_GOAL, HYS_ALL & ~HYS_KP, HYS_T(MOD3_P_SUS), 110}, {MOD3_Q_GOAL, HYS_KP, HYS_T(MOD3_P_SUS), 0},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(LEAD) | MOD3_GM(FX), HYS_T(MOD3_P_REL), 30},
    {MOD3_Q_GOAL, MOD3_GM(PAD), HYS_T(MOD3_P_REL), 80}, {MOD3_Q_GOAL, HYS_KP, HYS_T(MOD3_P_REL), 50},
    {MOD3_Q_GOAL, MOD3_GM(BASS), HY_F1_CUT, 60}, {MOD3_Q_GOAL, MOD3_GM(PAD), HY_F1_CUT, 96},
    {MOD3_Q_GOAL, HYS_ALL & ~(MOD3_GM(BASS) | MOD3_GM(PAD)), HY_F1_CUT, 100},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | HYS_KP, HY_F1_ENV, HY_ENV0 + 25},
    {MOD3_Q_GOAL, MOD3_GM(PAD) | MOD3_GM(LEAD) | MOD3_GM(FX), HY_F1_ENV, HY_ENV0 + 10},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | HYS_KP, HY_E1_DEC, 70}, {MOD3_Q_GOAL, MOD3_GM(PAD), HY_E1_ATK, 60},
    {MOD3_Q_GOAL, MOD3_GM(FX), HY_F1_RES, 90}, {MOD3_Q_GOAL, MOD3_GM(BASS), HY_O2 + HO_SEMI, HY_SEMI0 - 12},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(PAD), HY_LVL + 1, 70},
    {MOD3_Q_GOAL, MOD3_GM(LEAD), HY_M1 + HM_MODE, MU_STACK}, {MOD3_Q_GOAL, MOD3_GM(LEAD), HY_M1 + HM_DEPTH, 50},
};
#undef HYS_ALL
#undef HYS_KP
#undef HYS_T
static const uint8_t HYS_SYSEX[] = {3, 0x7D, 0x48, 0x59};   /* (the native message: F0 7D 48 59 01 ..) */
/* a stored patch: hydra_patch.h hy_patch_valid's rules (a user wave's id passes: it plays silence here) */
#define HYS_CK(off, n, st, hi) (uint8_t)(off), 0, (n), (st), 0xFF, 0, 0, (hi)
#define HYS_CK_OSC(b) HYS_CK(b + HO_MODE, 1, 1, 1), HYS_CK(b + HO_WAVE, 1, 1, HY_ID_LAST - 1u), \
    HYS_CK(b + HO_L0, 8, 1, HY_ID_LAST), HYS_CK(b + HO_SCAN, 1, 1, 127), HYS_CK(b + HO_SEMI, 1, 1, 2 * HY_SEMI0), \
    HYS_CK(b + HO_CENT, 1, 1, 2 * HY_CENT0), HYS_CK(b + HO_KEY, 1, 1, 200)
static const uint8_t HYS_CHECK[] = {
    HYS_CK_OSC(HY_O1), HYS_CK_OSC(HY_O2),
    HYS_CK(HY_O3_WAVE, 1, 1, HY_ID_LAST - 1u), HYS_CK(HY_O3_SEMI, 1, 1, 2 * HY_SEMI0),
    HYS_CK(HY_O3_CENT, 1, 1, 2 * HY_CENT0), HYS_CK(HY_O3_KEY, 1, 1, 200),
    HYS_CK(HY_M1 + HM_MODE, 4, HM_N, MU_N - 1), HYS_CK(HY_M1 + HM_SRC, 4, HM_N, HYS_N - 1),
    HYS_CK(HY_M1 + HM_RATIO, 4, HM_N, 127), HYS_CK(HY_M1 + HM_DEPTH, 4, HM_N, 127), HYS_CK(HY_M1 + HM_AUX, 4, HM_N, 127),
    HYS_CK(HY_M1 + HM_WET, 4, HM_N, 127),
    HYS_CK(HY_RING_A, 3, 1, 2), HYS_CK(HY_LVL, 10, 1, 127), HYS_CK(HY_ROUTE, 1, 1, 1),
    HYS_CK(HY_F1_TYPE, 1, 1, F1_N - 1), HYS_CK(HY_F1_CUT, 3, 1, 127), HYS_CK(HY_F1_DPOS, 1, 1, 1),
    HYS_CK(HY_F1_ENV, 1, 1, 2 * HY_ENV0), HYS_CK(HY_F1_KEY, 1, 1, 200), HYS_CK(HY_F1_VEL, 1, 1, 127),
    HYS_CK(HY_F2_TYPE, 1, 1, 1), HYS_CK(HY_F2_MORPH, 3, 1, 127), HYS_CK(HY_F2_ENV, 1, 1, 2 * HY_ENV0),
    HYS_CK(HY_F2_KEY, 1, 1, 200), HYS_CK(HY_AMP, 1, 1, 127), HYS_CK(HY_E1_SC1, 3, 1, 2 * HY_ENV0),
    HYS_CK(HY_E1_DLY, 6, 1, 127), HYS_CK(HY_MNAME, HY_MACROS, 1, HN_COUNT - 1),
    HYS_CK(HY_MA + 1, HY_MSLOTS, 2, 2 * HY_DEPTH0),
    (uint8_t)HY_NAME, 0, HY_NAME_LEN, 1, 0xFF, 0, 32, 126,
};
#undef HYS_CK
#undef HYS_CK_OSC
static const mod_sch_src HY_SCHEMA = {
    .patch = HY_BYTES, .def_make = hy_sch_def, .store = MOD_SCH_STORE_NONE, .utag = 'H', .mtag = 25,
    .flags = MOD_SCH_F_MBYTES | MOD_SCH_F_SNAME, .abbr = "HYDRA", .wdef = MOD_SCH_NONE, .mmap = 0,
    .name = HY_NAME, .nlen = HY_NAME_LEN, .first = 1, .pname = "HYDRA PATCH",   /* (picked: WAVESCAN, not INIT) */
    .param = HY_SCH_PARAM, .nparam = sizeof HY_SCH_PARAM / sizeof *HY_SCH_PARAM,
    .page = HY_SCH_PAGE, .npage = sizeof HY_SCH_PAGE / sizeof *HY_SCH_PAGE,
    .role = HY_SCH_ROLE, .nrole = sizeof HY_SCH_ROLE / sizeof *HY_SCH_ROLE,
    .wid = HY_SCH_WID, .nwid = sizeof HY_SCH_WID,
    .rec = HYS_REC, .nrec = sizeof HYS_REC / sizeof *HYS_REC,
    .strip = HYS_STRIP,
    .sysex = HYS_SYSEX, .nsysex = 1, .check = HYS_CHECK, .ncheck = sizeof HYS_CHECK / MOD_SCH_CREC,
};
