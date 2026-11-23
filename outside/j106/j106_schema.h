/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The JUNO-106's schema (mod_schema.h): its patch (j106_patch.h, 31 bytes), every panel value by the id Saadet's
 * editor gave it (the sixteen sliders = their bytes, SW1 / SW2, then the switches), its pages, roles, the sound
 * designer's recipes, its SysEx, the checks of a stored patch and Motion's bytes. The builder writes the file's SCHM
 * chunk from it (the module's source built for the host: MOD_HOSTED). */
static const char *const J106S_RANGE[] = {"16'", "8'", "4'"};
static const char *const J106S_ONOFF[] = {"OFF", "ON"};
static const char *const J106S_CHORUS[] = {"OFF", "I", "II"};
static const char *const J106S_PWM[] = {"LFO", "MAN"};
static const char *const J106S_POL[] = {"NORMAL", "INVERT"};
static const char *const J106S_VCA[] = {"ENV", "GATE"};
#define J106S_K(id, label, name, fl) {id, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, id, 0, 8, MOD3_SF_DIM | (fl), label, name, 0, 0}
#define J106S_B(id, byte, bit, label, names, def, f2) \
    {id, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, def, byte, bit, 1, MOD3_SF_DIM, label, 0, names, 0, 0, f2}
static const mod_sch_param_src J106_SCH_PARAM[] = {
    J106S_K(J106_LFO_RATE, "RATE", "LFO RATE", 0), J106S_K(J106_LFO_DELAY, "DELAY", "LFO DELAY", 0),
    J106S_K(J106_DCO_LFO, "DCO LFO", 0, 0), J106S_K(J106_DCO_PWM, "PWM", 0, 0), J106S_K(J106_NOISE, "NOISE", 0, 0),
    J106S_K(J106_VCF_FREQ, "FREQ", "VCF FREQ", 0), J106S_K(J106_VCF_RES, "RES", "VCF RES", 0),
    J106S_K(J106_VCF_ENV, "ENV", "VCF ENV", 0), J106S_K(J106_VCF_LFO, "VCF LFO", 0, 0),
    J106S_K(J106_VCF_KYBD, "KYBD", "VCF KYBD", 0), J106S_K(J106_VCA_LEVEL, "LEVEL", "VCA LEVEL", 0),
    J106S_K(J106_ENV_A, "ATTACK", 0, 0), J106S_K(J106_ENV_D, "DECAY", 0, 0), J106S_K(J106_ENV_S, "SUSTAIN", 0, 0),
    J106S_K(J106_ENV_R, "RELEASE", 0, 0), J106S_K(J106_SUB, "SUB", "SUB OSC", 0),
    J106S_K(J106_SW1, "SW1", 0, MOD3_SF_HIDDEN), J106S_K(J106_SW2, "SW2", 0, MOD3_SF_HIDDEN),
    {J106_RANGE, MOD3_SK_CALL, MOD3_F_ENUM, 0, 2, 1, 0, 0, 0, MOD3_SF_DIM, "RANGE", 0, J106S_RANGE, 0},
    J106S_B(J106_PULSE, J106_SW1, 3, "PULSE", J106S_ONOFF, 0, 0), J106S_B(J106_SAW, J106_SW1, 4, "SAW", J106S_ONOFF, 1, 0),
    {J106_CHORUS, MOD3_SK_CALL, MOD3_F_ENUM, 0, 2, 0, 0, 0, 0, MOD3_SF_DIM, "CHORUS", 0, J106S_CHORUS, 0},
    J106S_B(J106_PWM_MODE, J106_SW2, 0, "PWM BY", J106S_PWM, 0, 0),
    J106S_B(J106_ENV_POL, J106_SW2, 1, "ENV POL", J106S_POL, 0, MOD3_SF2_WHOLE),   /* (NORMAL / INVERT whole) */
    J106S_B(J106_VCA_MODE, J106_SW2, 2, "VCA", J106S_VCA, 0, 0),
    {J106_HPF, MOD3_SK_CALL, MOD3_F_INT, 0, 3, 1, 0, 0, 0, MOD3_SF_DIM, "HPF", 0, 0, 0},
};
#undef J106S_K
#undef J106S_B
#define J106S_PG(t, a, b, c, d, sec, fl) {t, MOD3_W_NONE, fl, {a, b, c, d}, 0, {0}, 0, 0, sec}
#define J106S_NO MOD_SCH_NONE
static const char *const J106S_INIT[] = {"JUNO INIT"};
static const mod_sch_page_src J106_SCH_PAGE[] = {
    {"J106 STORE", MOD3_W_STORE, MOD3_PG_LAST, {0, 1, 2, 3}, 0, {0}, J106S_INIT, 0, "Store"},
    J106S_PG("J106 LFO", J106_LFO_RATE, J106_LFO_DELAY, J106_DCO_LFO, J106_VCF_LFO, "LFO", MOD3_PG_LFO),
    J106S_PG("J106 DCO", J106_RANGE, J106_SAW, J106_PULSE, J106_SUB, "DCO", 0),
    J106S_PG("J106 PWM", J106_DCO_PWM, J106_PWM_MODE, J106_NOISE, J106S_NO, "DCO", 0),
    J106S_PG("J106 VCF", J106_VCF_FREQ, J106_VCF_RES, J106_VCF_ENV, J106_VCF_KYBD, "VCF", 0),
    J106S_PG("J106 VCF 2", J106_ENV_POL, J106_HPF, J106S_NO, J106S_NO, "VCF", 0),
    J106S_PG("J106 ENV", J106_ENV_A, J106_ENV_D, J106_ENV_S, J106_ENV_R, "Env", MOD3_PG_ENV),
    J106S_PG("J106 VCA", J106_VCA_MODE, J106_VCA_LEVEL, J106_CHORUS, J106S_NO, "VCA", 0),
    J106S_PG("FILTER", J106_VCF_FREQ, J106_VCF_RES, J106_VCF_ENV, J106_VCF_KYBD, 0, MOD3_PG_BANK),   /* (the SMK-37's */
    J106S_PG("ENV", J106_ENV_A, J106_ENV_D, J106_ENV_S, J106_ENV_R, 0, MOD3_PG_BANK),                 /* FILTER, ENV) */
};
#undef J106S_PG
enum { J106S_PAGE_LFO = 1, J106S_PAGE_BFILTER = 8, J106S_PAGE_BENV = 9 };
static const mod_sch_role_src J106_SCH_ROLE[] = {
    {MOD3_R_HOME1, MOD3_RF_PIC_FILTER, J106_VCF_FREQ, 0}, {MOD3_R_HOME2, MOD3_RF_PIC_FILTER, J106_VCF_RES, 0},
    {MOD3_R_HOME3, MOD3_RF_PIC_FILTER, J106_VCF_ENV, 0}, {MOD3_R_HOME4, MOD3_RF_PIC_ENV, J106_ENV_R, 0},
    {MOD3_R_CUTOFF, 0, J106_VCF_FREQ, 0}, {MOD3_R_RESO, 0, J106_VCF_RES, 0},
    {MOD3_R_ENV_A, 0, J106_ENV_A, 0}, {MOD3_R_ENV_D, 0, J106_ENV_D, 0}, {MOD3_R_ENV_S, 0, J106_ENV_S, 0},
    {MOD3_R_ENV_R, 0, J106_ENV_R, 0},
    {MOD3_R_CC_ATK, 0, J106_ENV_A, 0}, {MOD3_R_CC_DEC, 0, J106_ENV_D, 0}, {MOD3_R_CC_REL, 0, J106_ENV_R, 0},
    {MOD3_R_CC_BRIGHT, MOD3_RF_TRACK, MOD3_P_E0, 0}, {MOD3_R_CC_RESO, MOD3_RF_TRACK, MOD3_P_E0 + 1, 0},
    {MOD3_R_BANK_FILTER, 0, J106S_PAGE_BFILTER, 0}, {MOD3_R_BANK_ENV, 0, J106S_PAGE_BENV, 0},
    {MOD3_R_BANK_LFO, 0, J106S_PAGE_LFO, 0},
};
/* Motion's bytes (MOD_SCH_F_MBYTES): the sliders and the two switch bytes, seven bits each; the patch number and the
 * name never */
static const uint8_t J106_SCH_WID[2u * J106_BLOB] = {
    127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127,
    [J106_BLOB] = 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
};
static void j106_sch_def(uint8_t *b) { j106_patch_init((j106_patch_t *)(void *)b); }
static const char *const J106S_STRIP[] = {"J106 ", 0};
/* the sound designer's recipes (START: a goal from INIT): the envelope, the filter, the sub for a bass, the chorus for
 * a pad and keys; the rest is the firmware's from the roles */
#define J106S_ALL MOD3_GM_ALL
#define J106S_KP (MOD3_GM(KEYS) | MOD3_GM(PLUCK))
static const mod_sch_rec_src J106S_REC[] = {
    {MOD3_Q_GOAL, J106S_ALL & ~MOD3_GM(PAD), J106_ENV_A, 0}, {MOD3_Q_GOAL, MOD3_GM(PAD), J106_ENV_A, 60},
    {MOD3_Q_GOAL, J106S_ALL & ~J106S_KP, J106_ENV_D, 60}, {MOD3_Q_GOAL, J106S_KP, J106_ENV_D, 55},
    {MOD3_Q_GOAL, J106S_ALL & ~J106S_KP, J106_ENV_S, 100}, {MOD3_Q_GOAL, J106S_KP, J106_ENV_S, 0},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(LEAD) | MOD3_GM(FX), J106_ENV_R, 20},
    {MOD3_Q_GOAL, MOD3_GM(PAD), J106_ENV_R, 70}, {MOD3_Q_GOAL, J106S_KP, J106_ENV_R, 40},
    {MOD3_Q_GOAL, MOD3_GM(BASS), J106_VCF_FREQ, 40}, {MOD3_Q_GOAL, MOD3_GM(PAD), J106_VCF_FREQ, 70},
    {MOD3_Q_GOAL, J106S_ALL & ~(MOD3_GM(BASS) | MOD3_GM(PAD)), J106_VCF_FREQ, 80},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | J106S_KP, J106_VCF_ENV, 50},
    {MOD3_Q_GOAL, MOD3_GM(PAD) | MOD3_GM(LEAD) | MOD3_GM(FX), J106_VCF_ENV, 20},
    {MOD3_Q_GOAL, MOD3_GM(FX), J106_VCF_RES, 90}, {MOD3_Q_GOAL, MOD3_GM(BASS), J106_SUB, 80},
    {MOD3_Q_GOAL, MOD3_GM(BASS), J106_RANGE, 0}, {MOD3_Q_GOAL, MOD3_GM(LEAD), J106_PULSE, 1},
    {MOD3_Q_GOAL, MOD3_GM(PAD), J106_CHORUS, 2}, {MOD3_Q_GOAL, MOD3_GM(KEYS), J106_CHORUS, 1},
};
#undef J106S_ALL
#undef J106S_KP
static const uint8_t J106S_SYSEX[] = {2, 0x41, 0x30, 0, 2, 0x41, 0x31, 0, 2, 0x41, 0x32, 0};   /* (Roland: patch,
                                                                                               * manual, control) */
#define J106S_CK(off, n, lo, hi) (uint8_t)(off), 0, (n), 1, 0xFF, 0, (lo), (hi)
static const uint8_t J106S_CHECK[] = {           /* (a stored patch: seven-bit bytes, a printable name) */
    J106S_CK(0, J106_RAW + 1u, 0, 127), J106S_CK(J106_NAME, J106_NAME_LEN, 32, 126),
};
#undef J106S_CK
static const mod_sch_src J106_SCHEMA = {
    .patch = J106_BLOB, .def_make = j106_sch_def, .store = MOD_SCH_STORE_NONE, .utag = 'J', .mtag = 106,
    .flags = MOD_SCH_F_MBYTES | MOD_SCH_F_PCT127 | MOD_SCH_F_SNAME, .abbr = "J106", .wdef = MOD_SCH_NONE, .mmap = 0,
    .name = J106_NAME, .nlen = J106_NAME_LEN, .first = 1, .pname = "JUNO PATCH",   /* (picked: A11, not INIT) */
    .param = J106_SCH_PARAM, .nparam = sizeof J106_SCH_PARAM / sizeof *J106_SCH_PARAM,
    .page = J106_SCH_PAGE, .npage = sizeof J106_SCH_PAGE / sizeof *J106_SCH_PAGE,
    .role = J106_SCH_ROLE, .nrole = sizeof J106_SCH_ROLE / sizeof *J106_SCH_ROLE,
    .wid = J106_SCH_WID, .nwid = sizeof J106_SCH_WID,
    .rec = J106S_REC, .nrec = sizeof J106S_REC / sizeof *J106S_REC,
    .strip = J106S_STRIP,
    .sysex = J106S_SYSEX, .nsysex = 3, .check = J106S_CHECK, .ncheck = sizeof J106S_CHECK / MOD_SCH_CREC,
};
