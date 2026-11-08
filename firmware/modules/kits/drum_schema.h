/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The kits' schemas (firmware/modules/sdk/mod_schema.h): their patch (drum_patch.h: the kit's offsets, zero the factory
 * kit; stored as Melodee 1.x's KIT format, its packing MOD3_R_PACK), MIDI CC 74 on TUNE (E3), its pads (MOD3_R_PADS). The
 * module file carries it as its SCHM chunk; host builds compile it in (eng_drum.c) */
static const mod_sch_role_src DRUM_SCH_ROLE[] = {
    {MOD3_R_CC_BRIGHT, MOD3_RF_TRACK, MOD3_P_E0 + 2, 0},
};
#define DRUMS_CK(off, lo, hi) (off), 0, 16, 4, 0xFF, MOD3_CK_SIGNED, (uint8_t)(int8_t)(lo), (uint8_t)(int8_t)(hi)
static const uint8_t DRUMS_CHECK[] = {         /* (a kit's offsets as Melodee 1.x took them: TUNE, DECAY, the third, LEVEL) */
    DRUMS_CK(0, -24, 24), DRUMS_CK(1, -64, 63), DRUMS_CK(2, -64, 63), DRUMS_CK(3, 0, 127),
};
#undef DRUMS_CK
/* the kits' pads (mod_schema.h MOD3_R_PADS): each kit synth's one map (the 808's, FM KIT's, SID KIT's, the 909's,
 * CR78's, SAMPLER's: its recorded kits). Tables as drum_map.c plays them (tests/pads_test.c holds them to it) */
#define DRUM_PD_LANES "KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM"
static const uint8_t DRUM_PD_LANE_NOTE[MOD3_NLANE] = {36, 38, 39, 42, 46, 45, 37, 56};
static const char *const DRUM_PD_LANE_NAME[MOD3_NLANE] = {DRUM_PD_LANES, "BELL"};
static const char *const DRUM_PD_LANE_ABBR[MOD3_NLANE] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
static const char *const DRUM_PD_LANE_NAME11[MOD3_NLANE] = {DRUM_PD_LANES, "RIDE"};
static const char *const DRUM_PD_LANE_ABBR11[MOD3_NLANE] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "RD"};
static const char *const DRUM_PD_LANE_NAME_CR[MOD3_NLANE] = {"KICK", "SNARE", "TAMB", "HATCL", "HATOP", "TOM", "RIM", "BELL"};
static const char *const DRUM_PD_LANE_ABBR_CR[MOD3_NLANE] = {"BD", "SD", "TB", "CH", "OH", "TM", "RS", "CB"};
#undef DRUM_PD_LANES
static const uint8_t DRUM_PD_NOTE_LANE[128] = {
    0, 6, 1, 2, 1, 5, 3, 5, 3, 5, 4, 5, 0, 6, 1, 2, 1, 5, 3, 5, 3, 5, 4, 5, 0, 6, 1, 2, 1, 5, 3, 5,
    3, 5, 4, 0, 0, 6, 1, 2, 1, 5, 3, 5, 3, 5, 4, 5, 5, 7, 5, 7, 7, 7, 3, 7, 7, 7, 6, 7, 5, 5, 5, 5,
    5, 5, 5, 7, 7, 3, 3, 6, 6, 3, 3, 6, 6, 6, 5, 5, 7, 7, 4, 5, 0, 6, 1, 2, 1, 5, 3, 5, 3, 5, 4, 5,
    0, 6, 1, 2, 1, 5, 3, 5, 3, 5, 4, 5, 0, 6, 1, 2, 1, 5, 3, 5, 3, 5, 4, 5, 0, 6, 1, 2, 1, 5, 3, 5,
};
/* the 808's sixteen (the SID's, CR78's and FM's too) */
static const uint8_t DRUM_PD_NOTES16[16] = {36, 38, 41, 45, 50, 37, 39, 42, 46, 49, 56, 64, 61, 60, 75, 70};
static const uint8_t DRUM_PD_MPC16[16] = {0, 1, 6, 5, 7, 8, 10, 9, 2, 3, 4, 15, 11, 12, 13, 14};
static const uint8_t DRUM_PD_ORDER16[16] = {0, 1, 6, 7, 15, 8, 2, 3, 4, 11, 12, 13, 5, 14, 10, 9};
static const uint8_t DRUM_PD_NOTE_PAD16[128] = {
    0, 5, 1, 6, 1, 2, 7, 2, 7, 3, 8, 3, 0, 5, 1, 6, 1, 2, 7, 2, 7, 3, 8, 3, 0, 5, 1, 6, 1, 2, 7, 2,
    7, 3, 8, 0, 0, 5, 1, 6, 1, 2, 7, 2, 7, 3, 8, 3, 4, 9, 4, 9, 9, 9, 15, 9, 10, 9, 14, 9, 13, 12, 13, 12,
    11, 3, 3, 10, 10, 15, 15, 14, 14, 7, 7, 14, 14, 14, 12, 12, 10, 10, 8, 3, 0, 5, 1, 6, 1, 2, 7, 2, 7, 3, 8, 3,
    0, 5, 1, 6, 1, 2, 7, 2, 7, 3, 8, 3, 0, 5, 1, 6, 1, 2, 7, 2, 7, 3, 8, 3, 0, 5, 1, 6, 1, 2, 7, 2,
};
#define DRUM_PD_FIVE "KICK", "SNARE", "TOM L", "TOM M", "TOM H", "RIM"
static const char *const DRUM_PD_NAME16[16] = {DRUM_PD_FIVE, "CLAP", "HAT C", "HAT O", "CYMB", "BELL", "CONGL", "CONGM",
                                               "CONGH", "CLAVE", "MARAC"};
static const char *const DRUM_PD_ABBR16[16] = {"BD", "SD", "LT", "MT", "HT", "RS", "CP", "CH", "OH", "CY", "CB", "LC", "MC",
                                               "HC", "CL", "MA"};
static const char *const DRUM_PD_NAME_CR[16] = {DRUM_PD_FIVE, "TAMB", "HAT C", "HAT O", "CYMB", "BELL", "CONGA", "BONGL",
                                                "BONGH", "CLAVE", "MARAC"};
static const char *const DRUM_PD_ABBR_CR[16] = {"BD", "SD", "LT", "MT", "HT", "RS", "TB", "CH", "OH", "CY", "CB", "CG", "BL",
                                                "BH", "CL", "MA"};
/* the 909's eleven (the user's kits' too) */
static const uint8_t DRUM_PD_NOTES11[11] = {36, 38, 41, 45, 50, 37, 39, 42, 46, 49, 51};
static const uint8_t DRUM_PD_MPC11[16] = {0, 1, 6, 5, 7, 8, 10, 9, 2, 3, 4, 10, 0, 1, 7, 8};
static const uint8_t DRUM_PD_ORDER11[11] = {0, 1, 6, 7, 8, 2, 3, 4, 5, 10, 9};
static const uint8_t DRUM_PD_NOTE_PAD11[128] = {
    0, 5, 1, 6, 1, 2, 7, 2, 7, 2, 8, 2, 0, 5, 1, 6, 1, 2, 7, 2, 7, 2, 8, 2, 0, 5, 1, 6, 1, 2, 7, 2,
    7, 2, 8, 0, 0, 5, 1, 6, 1, 2, 7, 2, 7, 3, 8, 3, 4, 9, 4, 10, 9, 10, 7, 9, 10, 9, 5, 10, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 7, 7, 5, 5, 7, 7, 5, 5, 5, 4, 4, 4, 4, 8, 4, 0, 5, 1, 6, 1, 4, 7, 4, 7, 4, 8, 4,
    0, 5, 1, 6, 1, 4, 7, 4, 7, 4, 8, 4, 0, 5, 1, 6, 1, 4, 7, 4, 7, 4, 8, 4, 0, 5, 1, 6, 1, 4, 7, 4,
};
static const char *const DRUM_PD_NAME11[11] = {DRUM_PD_FIVE, "CLAP", "HAT C", "HAT O", "CRASH", "RIDE"};
static const char *const DRUM_PD_ABBR11[11] = {"BD", "SD", "LT", "MT", "HT", "RS", "CP", "CH", "OH", "CR", "RD"};
#undef DRUM_PD_FIVE
/* the third parameter's label by the pad (CHARACTER: SNAPPY on the snare, the 909's CLICK (its ATTACK) on its kick and toms, the
 * SID's SNAP), pad * 4 + parameter */
#define DRUM_PD_SN 0, 0, 0, 0, 0, 0, "SNAPPY", 0
static const char *const DRUM_PD_LABEL16[64] = {DRUM_PD_SN};
static const char *const DRUM_PD_LABEL11[44] = {DRUM_PD_SN};
#undef DRUM_PD_SN
#define DRUM_PD_AT 0, 0, "CLICK", 0
static const char *const DRUM_PD_LABEL909[44] = {DRUM_PD_AT, 0, 0, "SNAPPY", 0, DRUM_PD_AT, DRUM_PD_AT, DRUM_PD_AT};
#undef DRUM_PD_AT
#define DRUM_PD_S 0, 0, "SNAP", 0
static const char *const DRUM_PD_LABEL_SID[64] = {DRUM_PD_S, DRUM_PD_S, DRUM_PD_S, DRUM_PD_S, DRUM_PD_S, DRUM_PD_S, DRUM_PD_S,
    DRUM_PD_S, DRUM_PD_S, DRUM_PD_S, DRUM_PD_S, DRUM_PD_S, DRUM_PD_S, DRUM_PD_S, DRUM_PD_S, DRUM_PD_S};
#undef DRUM_PD_S
#define DRUM_PD_MAP_808 {16, 0, DRUM_PD_NAME16, DRUM_PD_ABBR16, DRUM_PD_NOTES16, DRUM_PD_MPC16, DRUM_PD_ORDER16,      \
                         DRUM_PD_NOTE_PAD16, 0, 0, DRUM_PD_LABEL16}   /* the 808, FM KIT */
#define DRUM_PD_MAP_SID {16, 0, DRUM_PD_NAME16, DRUM_PD_ABBR16, DRUM_PD_NOTES16, DRUM_PD_MPC16, DRUM_PD_ORDER16,      \
                         DRUM_PD_NOTE_PAD16, 0, 0, DRUM_PD_LABEL_SID}
#define DRUM_PD_MAP_909 {11, 0, DRUM_PD_NAME11, DRUM_PD_ABBR11, DRUM_PD_NOTES11, DRUM_PD_MPC11, DRUM_PD_ORDER11,      \
                         DRUM_PD_NOTE_PAD11, DRUM_PD_LANE_NAME11, DRUM_PD_LANE_ABBR11, DRUM_PD_LABEL909}
#define DRUM_PD_MAP_CR {16, 0, DRUM_PD_NAME_CR, DRUM_PD_ABBR_CR, DRUM_PD_NOTES16, DRUM_PD_MPC16, DRUM_PD_ORDER16,      \
                        DRUM_PD_NOTE_PAD16, DRUM_PD_LANE_NAME_CR, DRUM_PD_LANE_ABBR_CR, DRUM_PD_LABEL16}
#define DRUM_PD_MAP_USER {11, MOD3_PM_KEYMAP, DRUM_PD_NAME11, DRUM_PD_ABBR11, DRUM_PD_NOTES11, DRUM_PD_MPC11,          \
                          DRUM_PD_ORDER11, DRUM_PD_NOTE_PAD11, DRUM_PD_LANE_NAME11, DRUM_PD_LANE_ABBR11,              \
                          DRUM_PD_LABEL11}                     /* KIT 1..8, white keys */
static const uint8_t DRUM_PD_SEL[17] = {0};    /* (a kit synth's KIT is its own: one map, whatever KIT 0..16 holds) */
static const mod_sch_padpar_src DRUM_PD_PAR[] = {      /* a pad's four bytes (drum_patch.h): TUNE, DECAY, the third, LEVEL */
    {0, MOD3_PP_SIGNED, -24, 24, 0, "TUNE", "ST", "NOTE"},
    {1, MOD3_PP_SIGNED, -64, 63, 64, "DECAY", 0, "HIT"},
    {2, MOD3_PP_SIGNED, -64, 63, 64, "TONE", 0, 0},
    {3, MOD3_PP_INVERT, 0, 127, 0, "LEVEL", 0, 0},
};
static const char *const DRUM_PD_WORDS[MOD3_PW_COUNT] = {"DRUM SOUND", "DRUM MIX", "DRUM HIT", "DRUM", "Drum"};
/* a kit's pads (mod_schema.h MOD3_R_PADS): its one map; SAMPLER's records too (KIT 1..8 = values 9..16 = its data records
 * 0..7; a record's name: drk_kit_t.name, its sectors, its header: drum_kit.h DRK_KSEC, DRK_KIT_BYTES) */
#define KIT_PADS(sym, mapsym, flags_, ...)                                                                          \
    static const mod_sch_padmap_src mapsym##_MAP[] = {mapsym};                                                    \
    static const mod_sch_pads_src sym = {                                                                         \
        .flags = MOD3_PD_NOBEND | MOD3_PD_NOSUS | MOD3_PD_HITS | (flags_), .sel = MOD3_P_E0, .nsel = 17,           \
        .npar = 4, .pstride = 4, .seltab = DRUM_PD_SEL, .lane_note = DRUM_PD_LANE_NOTE,                           \
        .note_lane = DRUM_PD_NOTE_LANE, .lane_names = DRUM_PD_LANE_NAME, .lane_abbrs = DRUM_PD_LANE_ABBR,          \
        .map = mapsym##_MAP, .nmap = 1, .par = DRUM_PD_PAR, .words = DRUM_PD_WORDS, __VA_ARGS__};
KIT_PADS(KIT808_PADS, DRUM_PD_MAP_808, 0, .rec0 = 0)
KIT_PADS(KITSID_PADS, DRUM_PD_MAP_SID, 0, .rec0 = 0)
KIT_PADS(KIT909_PADS, DRUM_PD_MAP_909, 0, .rec0 = 0)
KIT_PADS(KITCR_PADS, DRUM_PD_MAP_CR, 0, .rec0 = 0)
KIT_PADS(SAMPLER_PADS, DRUM_PD_MAP_USER, MOD3_PD_RECORD, .rec0 = 9, .nrec = 8, .rname = 8, .rnlen = 5, .rsec = 7, .rmin = 616)
#undef KIT_PADS
/* Melodee 1.x's KIT store (projects, user presets): per pad TUNE in 6 bits (+ 24), DECAY, the third in 7 (+ 64), the
 * cut in 7 */
static const mod_sch_pack_field DRUM_PACK_F[] = {{6, 24}, {7, 64}, {7, 64}, {7, 0}};
static const mod_sch_pack_src DRUM_PACK = {DRUM_PACK_F, 4, 16, 4};
#define KIT_SCHEMA(sym, pads_)                                                                                       \
    static const mod_sch_src sym = {                                                                              \
        .patch = sizeof(drum_patch_t), .store = MOD_SCH_STORE_KIT, .wdef = MOD_SCH_NONE, .mmap = MOD_SCH_NONE,    \
        .role = DRUM_SCH_ROLE, .nrole = sizeof DRUM_SCH_ROLE / sizeof *DRUM_SCH_ROLE,                            \
        .check = DRUMS_CHECK, .ncheck = sizeof DRUMS_CHECK / MOD_SCH_CREC, .pads = &pads_, .pack = &DRUM_PACK,  \
    };
KIT_SCHEMA(KIT808_SCHEMA, KIT808_PADS)
KIT_SCHEMA(KIT909_SCHEMA, KIT909_PADS)
KIT_SCHEMA(KITSID_SCHEMA, KITSID_PADS)
KIT_SCHEMA(KITCR_SCHEMA, KITCR_PADS)
KIT_SCHEMA(SAMPLER_SCHEMA, SAMPLER_PADS)
#undef KIT_SCHEMA
