/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6's schema (firmware/modules/sdk/mod_schema.h): its patch (fm6_patch.h: the voice packed, the functions, the
 * switches). Every voice value has the id of its place in the 155-byte voice (VCED: a DX7's single voice, the sixth
 * operator first), which Melodee 1.x's Motion recorded, and lives at its bits in the packed voice; the functions are
 * 160 + FN_*, the operator switches 180 + k (the sixth operator first). The operator pages are one page each for OP1
 * .. OP6 (instanced: an operator's ids 21 apart, its switch 1 apart), their EG pages and the pitch EG draw the DX7
 * envelope, the others the algorithm (wdef). The module file carries it as its SCHM chunk (tools/mod_build.py); host
 * builds compile it in (fm6_ui.c) */
static const char *const FM6S_ALG[32] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
                                          "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28",
                                          "29", "30", "31", "32"};
static const char *const FM6S_CRV[] = {"-LIN", "-EXP", "+EXP", "+LIN"};
static const char *const FM6S_MODE[] = {"RATIO", "FIXED"};
static const char *const FM6S_LFW[] = {"TRI", "SAWDN", "SAWUP", "SQR", "SIN", "S&H"};
static const char *const FM6S_PM[] = {"PEDAL", "ON"};   /* portamento: while CC 65 is down (Dexed), always */
static const char *const FM6S_DEST[] = {"-", "P", "A", "PA", "E", "PE", "AE", "PAE"};   /* pitch, amp, EG bias */
static const char *const FM6S_ENG[] = {"MODRN", "MARK I", "OPL"};                  /* Dexed's engine resolutions */
static const char *const FM6S_ONOFF[] = {"OFF", "ON"};
static const mod_sch_param_src FM6_SCH_PARAM[] = {
    {0, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 0, 0, 7, MOD3_SF_MOTION, "R1", 0, 0, 0, "RATE"},
    {1, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 1, 0, 7, MOD3_SF_MOTION, "R2", 0, 0, 0, "RATE"},
    {2, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 2, 0, 7, MOD3_SF_MOTION, "R3", 0, 0, 0, "RATE"},
    {3, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 3, 0, 7, MOD3_SF_MOTION, "R4", 0, 0, 0, "RATE"},
    {4, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 4, 0, 7, MOD3_SF_MOTION, "L1", 0, 0, 0, "LVL"},
    {5, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 5, 0, 7, MOD3_SF_MOTION, "L2", 0, 0, 0, "LVL"},
    {6, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 6, 0, 7, MOD3_SF_MOTION, "L3", 0, 0, 0, "LVL"},
    {7, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 7, 0, 7, MOD3_SF_MOTION, "L4", 0, 0, 0, "LVL"},
    {8, MOD3_SK_FIELD, MOD3_F_FMNOTE, 0, 99, 39, 8, 0, 7, MOD3_SF_MOTION, "BRK", 0, 0, 0, "PIT"},
    {9, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 9, 0, 7, MOD3_SF_MOTION, "LDEP", 0, 0, 0, "KTR"},
    {10, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 10, 0, 7, MOD3_SF_MOTION, "RDEP", 0, 0, 0, "KTR"},
    {11, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 3, 0, 11, 0, 2, MOD3_SF_MOTION, "LCRV", 0, FM6S_CRV, 0, "SHP"},
    {12, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 3, 0, 11, 2, 2, MOD3_SF_MOTION, "RCRV", 0, FM6S_CRV, 0, "SHP"},
    {13, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 12, 0, 3, MOD3_SF_MOTION, "RSCL", 0, 0, 0, "KTR"},
    {14, MOD3_SK_FIELD, MOD3_F_INT, 0, 3, 0, 13, 0, 2, MOD3_SF_MOTION, "AMS", 0, 0, 0, "IDX"},
    {15, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 13, 2, 3, MOD3_SF_MOTION, "VEL", 0, 0, 0, "ACC"},
    {16, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 14, 0, 7, MOD3_SF_MOTION, "OUT", 0, 0, 0, "LVL"},
    {17, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 15, 0, 1, MOD3_SF_MOTION, "MODE", 0, FM6S_MODE, 0, "R2"},
    {18, MOD3_SK_FIELD, MOD3_F_FMFRQ, 0, 31, 1, 15, 1, 5, MOD3_SF_MOTION | MOD3_SF_FORMAT, "CRS", 0, 0, 0, "R2"},
    {19, MOD3_SK_FIELD, MOD3_F_FMFRQ, 0, 99, 0, 16, 0, 7, MOD3_SF_MOTION | MOD3_SF_FORMAT, "FINE", 0, 0, 0, "TUNE"},
    {20, MOD3_SK_FIELD, MOD3_F_OFS, 0, 14, 7, 12, 3, 4, MOD3_SF_MOTION, "DTN", 0, 0, 0, "DTN"},
    {21, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 17, 0, 7, MOD3_SF_MOTION, "R1", 0, 0, 0, "RATE"},
    {22, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 18, 0, 7, MOD3_SF_MOTION, "R2", 0, 0, 0, "RATE"},
    {23, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 19, 0, 7, MOD3_SF_MOTION, "R3", 0, 0, 0, "RATE"},
    {24, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 20, 0, 7, MOD3_SF_MOTION, "R4", 0, 0, 0, "RATE"},
    {25, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 21, 0, 7, MOD3_SF_MOTION, "L1", 0, 0, 0, "LVL"},
    {26, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 22, 0, 7, MOD3_SF_MOTION, "L2", 0, 0, 0, "LVL"},
    {27, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 23, 0, 7, MOD3_SF_MOTION, "L3", 0, 0, 0, "LVL"},
    {28, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 24, 0, 7, MOD3_SF_MOTION, "L4", 0, 0, 0, "LVL"},
    {29, MOD3_SK_FIELD, MOD3_F_FMNOTE, 0, 99, 39, 25, 0, 7, MOD3_SF_MOTION, "BRK", 0, 0, 0, "PIT"},
    {30, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 26, 0, 7, MOD3_SF_MOTION, "LDEP", 0, 0, 0, "KTR"},
    {31, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 27, 0, 7, MOD3_SF_MOTION, "RDEP", 0, 0, 0, "KTR"},
    {32, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 3, 0, 28, 0, 2, MOD3_SF_MOTION, "LCRV", 0, FM6S_CRV, 0, "SHP"},
    {33, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 3, 0, 28, 2, 2, MOD3_SF_MOTION, "RCRV", 0, FM6S_CRV, 0, "SHP"},
    {34, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 29, 0, 3, MOD3_SF_MOTION, "RSCL", 0, 0, 0, "KTR"},
    {35, MOD3_SK_FIELD, MOD3_F_INT, 0, 3, 0, 30, 0, 2, MOD3_SF_MOTION, "AMS", 0, 0, 0, "IDX"},
    {36, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 30, 2, 3, MOD3_SF_MOTION, "VEL", 0, 0, 0, "ACC"},
    {37, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 31, 0, 7, MOD3_SF_MOTION, "OUT", 0, 0, 0, "LVL"},
    {38, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 32, 0, 1, MOD3_SF_MOTION, "MODE", 0, FM6S_MODE, 0, "R2"},
    {39, MOD3_SK_FIELD, MOD3_F_FMFRQ, 0, 31, 1, 32, 1, 5, MOD3_SF_MOTION | MOD3_SF_FORMAT, "CRS", 0, 0, 0, "R2"},
    {40, MOD3_SK_FIELD, MOD3_F_FMFRQ, 0, 99, 0, 33, 0, 7, MOD3_SF_MOTION | MOD3_SF_FORMAT, "FINE", 0, 0, 0, "TUNE"},
    {41, MOD3_SK_FIELD, MOD3_F_OFS, 0, 14, 7, 29, 3, 4, MOD3_SF_MOTION, "DTN", 0, 0, 0, "DTN"},
    {42, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 34, 0, 7, MOD3_SF_MOTION, "R1", 0, 0, 0, "RATE"},
    {43, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 35, 0, 7, MOD3_SF_MOTION, "R2", 0, 0, 0, "RATE"},
    {44, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 36, 0, 7, MOD3_SF_MOTION, "R3", 0, 0, 0, "RATE"},
    {45, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 37, 0, 7, MOD3_SF_MOTION, "R4", 0, 0, 0, "RATE"},
    {46, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 38, 0, 7, MOD3_SF_MOTION, "L1", 0, 0, 0, "LVL"},
    {47, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 39, 0, 7, MOD3_SF_MOTION, "L2", 0, 0, 0, "LVL"},
    {48, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 40, 0, 7, MOD3_SF_MOTION, "L3", 0, 0, 0, "LVL"},
    {49, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 41, 0, 7, MOD3_SF_MOTION, "L4", 0, 0, 0, "LVL"},
    {50, MOD3_SK_FIELD, MOD3_F_FMNOTE, 0, 99, 39, 42, 0, 7, MOD3_SF_MOTION, "BRK", 0, 0, 0, "PIT"},
    {51, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 43, 0, 7, MOD3_SF_MOTION, "LDEP", 0, 0, 0, "KTR"},
    {52, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 44, 0, 7, MOD3_SF_MOTION, "RDEP", 0, 0, 0, "KTR"},
    {53, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 3, 0, 45, 0, 2, MOD3_SF_MOTION, "LCRV", 0, FM6S_CRV, 0, "SHP"},
    {54, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 3, 0, 45, 2, 2, MOD3_SF_MOTION, "RCRV", 0, FM6S_CRV, 0, "SHP"},
    {55, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 46, 0, 3, MOD3_SF_MOTION, "RSCL", 0, 0, 0, "KTR"},
    {56, MOD3_SK_FIELD, MOD3_F_INT, 0, 3, 0, 47, 0, 2, MOD3_SF_MOTION, "AMS", 0, 0, 0, "IDX"},
    {57, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 47, 2, 3, MOD3_SF_MOTION, "VEL", 0, 0, 0, "ACC"},
    {58, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 48, 0, 7, MOD3_SF_MOTION, "OUT", 0, 0, 0, "LVL"},
    {59, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 49, 0, 1, MOD3_SF_MOTION, "MODE", 0, FM6S_MODE, 0, "R2"},
    {60, MOD3_SK_FIELD, MOD3_F_FMFRQ, 0, 31, 1, 49, 1, 5, MOD3_SF_MOTION | MOD3_SF_FORMAT, "CRS", 0, 0, 0, "R2"},
    {61, MOD3_SK_FIELD, MOD3_F_FMFRQ, 0, 99, 0, 50, 0, 7, MOD3_SF_MOTION | MOD3_SF_FORMAT, "FINE", 0, 0, 0, "TUNE"},
    {62, MOD3_SK_FIELD, MOD3_F_OFS, 0, 14, 7, 46, 3, 4, MOD3_SF_MOTION, "DTN", 0, 0, 0, "DTN"},
    {63, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 51, 0, 7, MOD3_SF_MOTION, "R1", 0, 0, 0, "RATE"},
    {64, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 52, 0, 7, MOD3_SF_MOTION, "R2", 0, 0, 0, "RATE"},
    {65, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 53, 0, 7, MOD3_SF_MOTION, "R3", 0, 0, 0, "RATE"},
    {66, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 54, 0, 7, MOD3_SF_MOTION, "R4", 0, 0, 0, "RATE"},
    {67, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 55, 0, 7, MOD3_SF_MOTION, "L1", 0, 0, 0, "LVL"},
    {68, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 56, 0, 7, MOD3_SF_MOTION, "L2", 0, 0, 0, "LVL"},
    {69, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 57, 0, 7, MOD3_SF_MOTION, "L3", 0, 0, 0, "LVL"},
    {70, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 58, 0, 7, MOD3_SF_MOTION, "L4", 0, 0, 0, "LVL"},
    {71, MOD3_SK_FIELD, MOD3_F_FMNOTE, 0, 99, 39, 59, 0, 7, MOD3_SF_MOTION, "BRK", 0, 0, 0, "PIT"},
    {72, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 60, 0, 7, MOD3_SF_MOTION, "LDEP", 0, 0, 0, "KTR"},
    {73, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 61, 0, 7, MOD3_SF_MOTION, "RDEP", 0, 0, 0, "KTR"},
    {74, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 3, 0, 62, 0, 2, MOD3_SF_MOTION, "LCRV", 0, FM6S_CRV, 0, "SHP"},
    {75, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 3, 0, 62, 2, 2, MOD3_SF_MOTION, "RCRV", 0, FM6S_CRV, 0, "SHP"},
    {76, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 63, 0, 3, MOD3_SF_MOTION, "RSCL", 0, 0, 0, "KTR"},
    {77, MOD3_SK_FIELD, MOD3_F_INT, 0, 3, 0, 64, 0, 2, MOD3_SF_MOTION, "AMS", 0, 0, 0, "IDX"},
    {78, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 64, 2, 3, MOD3_SF_MOTION, "VEL", 0, 0, 0, "ACC"},
    {79, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 65, 0, 7, MOD3_SF_MOTION, "OUT", 0, 0, 0, "LVL"},
    {80, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 66, 0, 1, MOD3_SF_MOTION, "MODE", 0, FM6S_MODE, 0, "R2"},
    {81, MOD3_SK_FIELD, MOD3_F_FMFRQ, 0, 31, 1, 66, 1, 5, MOD3_SF_MOTION | MOD3_SF_FORMAT, "CRS", 0, 0, 0, "R2"},
    {82, MOD3_SK_FIELD, MOD3_F_FMFRQ, 0, 99, 0, 67, 0, 7, MOD3_SF_MOTION | MOD3_SF_FORMAT, "FINE", 0, 0, 0, "TUNE"},
    {83, MOD3_SK_FIELD, MOD3_F_OFS, 0, 14, 7, 63, 3, 4, MOD3_SF_MOTION, "DTN", 0, 0, 0, "DTN"},
    {84, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 68, 0, 7, MOD3_SF_MOTION, "R1", 0, 0, 0, "RATE"},
    {85, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 69, 0, 7, MOD3_SF_MOTION, "R2", 0, 0, 0, "RATE"},
    {86, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 70, 0, 7, MOD3_SF_MOTION, "R3", 0, 0, 0, "RATE"},
    {87, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 71, 0, 7, MOD3_SF_MOTION, "R4", 0, 0, 0, "RATE"},
    {88, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 72, 0, 7, MOD3_SF_MOTION, "L1", 0, 0, 0, "LVL"},
    {89, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 73, 0, 7, MOD3_SF_MOTION, "L2", 0, 0, 0, "LVL"},
    {90, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 74, 0, 7, MOD3_SF_MOTION, "L3", 0, 0, 0, "LVL"},
    {91, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 75, 0, 7, MOD3_SF_MOTION, "L4", 0, 0, 0, "LVL"},
    {92, MOD3_SK_FIELD, MOD3_F_FMNOTE, 0, 99, 39, 76, 0, 7, MOD3_SF_MOTION, "BRK", 0, 0, 0, "PIT"},
    {93, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 77, 0, 7, MOD3_SF_MOTION, "LDEP", 0, 0, 0, "KTR"},
    {94, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 78, 0, 7, MOD3_SF_MOTION, "RDEP", 0, 0, 0, "KTR"},
    {95, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 3, 0, 79, 0, 2, MOD3_SF_MOTION, "LCRV", 0, FM6S_CRV, 0, "SHP"},
    {96, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 3, 0, 79, 2, 2, MOD3_SF_MOTION, "RCRV", 0, FM6S_CRV, 0, "SHP"},
    {97, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 80, 0, 3, MOD3_SF_MOTION, "RSCL", 0, 0, 0, "KTR"},
    {98, MOD3_SK_FIELD, MOD3_F_INT, 0, 3, 0, 81, 0, 2, MOD3_SF_MOTION, "AMS", 0, 0, 0, "IDX"},
    {99, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 81, 2, 3, MOD3_SF_MOTION, "VEL", 0, 0, 0, "ACC"},
    {100, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 82, 0, 7, MOD3_SF_MOTION, "OUT", 0, 0, 0, "LVL"},
    {101, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 83, 0, 1, MOD3_SF_MOTION, "MODE", 0, FM6S_MODE, 0, "R2"},
    {102, MOD3_SK_FIELD, MOD3_F_FMFRQ, 0, 31, 1, 83, 1, 5, MOD3_SF_MOTION | MOD3_SF_FORMAT, "CRS", 0, 0, 0, "R2"},
    {103, MOD3_SK_FIELD, MOD3_F_FMFRQ, 0, 99, 0, 84, 0, 7, MOD3_SF_MOTION | MOD3_SF_FORMAT, "FINE", 0, 0, 0, "TUNE"},
    {104, MOD3_SK_FIELD, MOD3_F_OFS, 0, 14, 7, 80, 3, 4, MOD3_SF_MOTION, "DTN", 0, 0, 0, "DTN"},
    {105, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 85, 0, 7, MOD3_SF_MOTION, "R1", 0, 0, 0, "RATE"},
    {106, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 86, 0, 7, MOD3_SF_MOTION, "R2", 0, 0, 0, "RATE"},
    {107, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 87, 0, 7, MOD3_SF_MOTION, "R3", 0, 0, 0, "RATE"},
    {108, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 88, 0, 7, MOD3_SF_MOTION, "R4", 0, 0, 0, "RATE"},
    {109, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 89, 0, 7, MOD3_SF_MOTION, "L1", 0, 0, 0, "LVL"},
    {110, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 90, 0, 7, MOD3_SF_MOTION, "L2", 0, 0, 0, "LVL"},
    {111, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 91, 0, 7, MOD3_SF_MOTION, "L3", 0, 0, 0, "LVL"},
    {112, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 92, 0, 7, MOD3_SF_MOTION, "L4", 0, 0, 0, "LVL"},
    {113, MOD3_SK_FIELD, MOD3_F_FMNOTE, 0, 99, 39, 93, 0, 7, MOD3_SF_MOTION, "BRK", 0, 0, 0, "PIT"},
    {114, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 94, 0, 7, MOD3_SF_MOTION, "LDEP", 0, 0, 0, "KTR"},
    {115, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 95, 0, 7, MOD3_SF_MOTION, "RDEP", 0, 0, 0, "KTR"},
    {116, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 3, 0, 96, 0, 2, MOD3_SF_MOTION, "LCRV", 0, FM6S_CRV, 0, "SHP"},
    {117, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 3, 0, 96, 2, 2, MOD3_SF_MOTION, "RCRV", 0, FM6S_CRV, 0, "SHP"},
    {118, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 97, 0, 3, MOD3_SF_MOTION, "RSCL", 0, 0, 0, "KTR"},
    {119, MOD3_SK_FIELD, MOD3_F_INT, 0, 3, 0, 98, 0, 2, MOD3_SF_MOTION, "AMS", 0, 0, 0, "IDX"},
    {120, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 98, 2, 3, MOD3_SF_MOTION, "VEL", 0, 0, 0, "ACC"},
    {121, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 99, 0, 7, MOD3_SF_MOTION, "OUT", 0, 0, 0, "LVL"},
    {122, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 100, 0, 1, MOD3_SF_MOTION, "MODE", 0, FM6S_MODE, 0, "R2"},
    {123, MOD3_SK_FIELD, MOD3_F_FMFRQ, 0, 31, 1, 100, 1, 5, MOD3_SF_MOTION | MOD3_SF_FORMAT, "CRS", 0, 0, 0, "R2"},
    {124, MOD3_SK_FIELD, MOD3_F_FMFRQ, 0, 99, 0, 101, 0, 7, MOD3_SF_MOTION | MOD3_SF_FORMAT, "FINE", 0, 0, 0, "TUNE"},
    {125, MOD3_SK_FIELD, MOD3_F_OFS, 0, 14, 7, 97, 3, 4, MOD3_SF_MOTION, "DTN", 0, 0, 0, "DTN"},
    {126, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 102, 0, 7, MOD3_SF_MOTION, "R1", 0, 0, 0, "RATE"},
    {127, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 103, 0, 7, MOD3_SF_MOTION, "R2", 0, 0, 0, "RATE"},
    {128, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 104, 0, 7, MOD3_SF_MOTION, "R3", 0, 0, 0, "RATE"},
    {129, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 105, 0, 7, MOD3_SF_MOTION, "R4", 0, 0, 0, "RATE"},
    {130, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 50, 106, 0, 7, MOD3_SF_MOTION, "L1", 0, 0, 0, "PIT"},
    {131, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 50, 107, 0, 7, MOD3_SF_MOTION, "L2", 0, 0, 0, "PIT"},
    {132, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 50, 108, 0, 7, MOD3_SF_MOTION, "L3", 0, 0, 0, "PIT"},
    {133, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 50, 109, 0, 7, MOD3_SF_MOTION, "L4", 0, 0, 0, "PIT"},
    {134, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 31, 0, 110, 0, 5, MOD3_SF_MOTION, "ALG", 0, FM6S_ALG, 0, "ALG"},
    {135, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 0, 111, 0, 3, MOD3_SF_MOTION, "FB", 0, 0, 0, "FB"},
    {136, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 1, 111, 3, 1, MOD3_SF_MOTION, "SYNC", 0, FM6S_ONOFF, 0, "PHS"},
    {137, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 35, 112, 0, 7, MOD3_SF_MOTION, "SPEED", 0, 0, 0, "RATE"},
    {138, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 113, 0, 7, MOD3_SF_MOTION, "DELAY", 0, 0, 0, "FADE"},
    {139, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 114, 0, 7, MOD3_SF_MOTION, "PMD", 0, 0, 0, "VIB"},
    {140, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 115, 0, 7, MOD3_SF_MOTION, "AMD", 0, 0, 0, "IDX"},
    {141, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 1, 116, 0, 1, MOD3_SF_MOTION, "KSYNC", 0, FM6S_ONOFF, 0, "PHS"},
    {142, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 5, 0, 116, 1, 3, MOD3_SF_MOTION, "WAVE", 0, FM6S_LFW, 0, "LFOW"},
    {143, MOD3_SK_FIELD, MOD3_F_INT, 0, 7, 3, 116, 4, 3, MOD3_SF_MOTION, "PMS", 0, 0, 0, "VIB"},
    {144, MOD3_SK_FIELD, MOD3_F_OFS, 0, 48, 24, 117, 0, 7, 0, "TRNS", 0, 0, "st", "TRN"},
    {145, MOD3_SK_FIELD, MOD3_F_INT, 32, 126, 32, 118, 0, 7, MOD3_SF_HIDDEN, "NAME", 0, 0, 0, 0},
    {146, MOD3_SK_FIELD, MOD3_F_INT, 32, 126, 32, 119, 0, 7, MOD3_SF_HIDDEN, "NAME", 0, 0, 0, 0},
    {147, MOD3_SK_FIELD, MOD3_F_INT, 32, 126, 32, 120, 0, 7, MOD3_SF_HIDDEN, "NAME", 0, 0, 0, 0},
    {148, MOD3_SK_FIELD, MOD3_F_INT, 32, 126, 32, 121, 0, 7, MOD3_SF_HIDDEN, "NAME", 0, 0, 0, 0},
    {149, MOD3_SK_FIELD, MOD3_F_INT, 32, 126, 32, 122, 0, 7, MOD3_SF_HIDDEN, "NAME", 0, 0, 0, 0},
    {150, MOD3_SK_FIELD, MOD3_F_INT, 32, 126, 32, 123, 0, 7, MOD3_SF_HIDDEN, "NAME", 0, 0, 0, 0},
    {151, MOD3_SK_FIELD, MOD3_F_INT, 32, 126, 32, 124, 0, 7, MOD3_SF_HIDDEN, "NAME", 0, 0, 0, 0},
    {152, MOD3_SK_FIELD, MOD3_F_INT, 32, 126, 32, 125, 0, 7, MOD3_SF_HIDDEN, "NAME", 0, 0, 0, 0},
    {153, MOD3_SK_FIELD, MOD3_F_INT, 32, 126, 32, 126, 0, 7, MOD3_SF_HIDDEN, "NAME", 0, 0, 0, 0},
    {154, MOD3_SK_FIELD, MOD3_F_INT, 32, 126, 32, 127, 0, 7, MOD3_SF_HIDDEN, "NAME", 0, 0, 0, 0},
    {160, MOD3_SK_FIELD, MOD3_F_INT, 0, 12, 3, 128, 0, 8, 0, "BEND+", 0, 0, "st", "SWP"},
    {161, MOD3_SK_FIELD, MOD3_F_INT, 0, 12, 3, 129, 0, 8, 0, "BEND-", 0, 0, "st", "SWP"},
    {162, MOD3_SK_FIELD, MOD3_F_INT, 0, 12, 0, 130, 0, 8, 0, "STEP", 0, 0, 0, "SCL"},
    {163, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 131, 0, 8, 0, "PORTA", 0, FM6S_PM, 0, "GLD"},
    {164, MOD3_SK_FIELD, MOD3_F_INT, 0, 127, 0, 132, 0, 8, 0, "TIME", 0, 0, 0, "TIME"},
    {165, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 133, 0, 8, 0, "GLISS", 0, FM6S_ONOFF, 0, "SCL"},
    {166, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 99, 134, 0, 8, 0, "WHEEL", 0, 0, 0, "IDX"},
    {167, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 7, 1, 135, 0, 8, 0, "W.DST", 0, FM6S_DEST, 0, "MIX"},
    {168, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 136, 0, 8, 0, "FOOT", 0, 0, 0, "IDX"},
    {169, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 7, 0, 137, 0, 8, 0, "F.DST", 0, FM6S_DEST, 0, "MIX"},
    {170, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 138, 0, 8, 0, "BRTH", 0, 0, 0, "MOUTH"},
    {171, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 7, 0, 139, 0, 8, 0, "B.DST", 0, FM6S_DEST, 0, "MIX"},
    {172, MOD3_SK_FIELD, MOD3_F_INT, 0, 99, 0, 140, 0, 8, 0, "AFTER", 0, 0, 0, "ACC"},
    {173, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 7, 0, 141, 0, 8, 0, "A.DST", 0, FM6S_DEST, 0, "MIX"},
    {174, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 0, 142, 0, 8, 0, "DXVEL", 0, FM6S_ONOFF, 0, "ACC"},
    {175, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 2, 1, 143, 0, 8, 0, "ENGIN", 0, FM6S_ENG, 0, "CHIP"},
    {180, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 1, 144, 0, 1, 0, "ON", 0, FM6S_ONOFF, 0, "MUTE"},
    {181, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 1, 144, 1, 1, 0, "ON", 0, FM6S_ONOFF, 0, "MUTE"},
    {182, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 1, 144, 2, 1, 0, "ON", 0, FM6S_ONOFF, 0, "MUTE"},
    {183, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 1, 144, 3, 1, 0, "ON", 0, FM6S_ONOFF, 0, "MUTE"},
    {184, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 1, 144, 4, 1, 0, "ON", 0, FM6S_ONOFF, 0, "MUTE"},
    {185, MOD3_SK_FIELD, MOD3_F_ENUM, 0, 1, 1, 144, 5, 1, 0, "ON", 0, FM6S_ONOFF, 0, "MUTE"},
};
#define FM6S_OP 105u                             /* OP1's first id (its block: the sixth in the voice) */
#define FM6S_PG(t, a, b, c, d, sec, fl) {t, MOD3_W_NONE, fl, {a, b, c, d}, 0, {0}, 0, 0, sec}
#define FM6S_OPG(t, w, a, b, c, d, s4, wd, fl) {t, w, MOD3_PG_INST | (fl), {FM6S_OP + (a), FM6S_OP + (b), c, d}, 6, {-21, -21, \
    s4 ? -21 : 0, s4}, FM6S_OPN, wd, 0}
static const char *const FM6S_OPN[6] = {"OP1", "OP2", "OP3", "OP4", "OP5", "OP6"};
static const char *const FM6S_INIT[] = {"INIT VOICE", "VOICE SENT", "SYSEX SEND ERROR"};
static const mod_sch_page_src FM6_SCH_PAGE[] = {
    {"STORE", MOD3_W_STORE, MOD3_PG_LAST, {0, 1, 2, 3}, 0, {0}, FM6S_INIT, MOD3_ST_NAMED | MOD3_ST_NOUNDO | MOD3_ST_SENDMSG,
     "Store"},
    FM6S_PG("ALGO", 134, 135, 136, 144, "Algo", 0),
    FM6S_OPG("FREQ", MOD3_W_NONE, 18, 19, FM6S_OP + 20, FM6S_OP + 17, -21, 0, 0),
    FM6S_OPG("OUT", MOD3_W_NONE, 16, 15, FM6S_OP + 14, 185, -1, 0, 0),
    FM6S_OPG("EG RATE", MOD3_W_ENV8, 0, 1, FM6S_OP + 2, FM6S_OP + 3, -21, 0, MOD3_PG_ENV),
    FM6S_OPG("EG LVL", MOD3_W_ENV8, 4, 5, FM6S_OP + 6, FM6S_OP + 7, -21, 0, MOD3_PG_ENV),
    FM6S_OPG("SCALE", MOD3_W_NONE, 8, 9, FM6S_OP + 10, FM6S_OP + 13, -21, 0, 0),
    {"CURVE", MOD3_W_NONE, MOD3_PG_INST, {FM6S_OP + 11, FM6S_OP + 12, MOD_SCH_NONE, MOD_SCH_NONE}, 6, {-21, -21, 0, 0}, FM6S_OPN, 0, 0},
    {"PITCH EG", MOD3_W_ENV8, MOD3_PG_ENV, {126, 127, 128, 129}, 0, {0}, 0, 18, "Pitch"},
    {"PITCH LV", MOD3_W_ENV8, MOD3_PG_ENV, {130, 131, 132, 133}, 0, {0}, 0, 18, "Pitch"},
    FM6S_PG("FM LFO", 142, 137, 138, 141, "LFO", MOD3_PG_LFO | MOD3_PG_LIST),
    FM6S_PG("FM LFO 2", 139, 140, 143, MOD_SCH_NONE, "LFO", MOD3_PG_LFO | MOD3_PG_INLIST),
    FM6S_PG("FM BEND", 160, 161, 162, 174, "Ctrl", MOD3_PG_LIST),
    FM6S_PG("FM PORTA", 163, 164, 165, 175, "Ctrl", MOD3_PG_INLIST),
    FM6S_PG("FM WH/FT", 166, 167, 168, 169, "Ctrl", MOD3_PG_INLIST),
    FM6S_PG("FM BR/AT", 170, 171, 172, 173, "Ctrl", MOD3_PG_INLIST),
};
#undef FM6S_PG
#undef FM6S_OPG
static const mod_sch_role_src FM6_SCH_ROLE[] = {
    {MOD3_R_CC_ATK, MOD3_RF_INVERT, 0, 0}, {MOD3_R_CC_ATK, MOD3_RF_INVERT, 21, 0}, {MOD3_R_CC_ATK, MOD3_RF_INVERT, 42, 0},
    {MOD3_R_CC_ATK, MOD3_RF_INVERT, 63, 0}, {MOD3_R_CC_ATK, MOD3_RF_INVERT, 84, 0}, {MOD3_R_CC_ATK, MOD3_RF_INVERT, 105, 0},
    {MOD3_R_CC_DEC, MOD3_RF_INVERT, 1, 0}, {MOD3_R_CC_DEC, MOD3_RF_INVERT, 22, 0}, {MOD3_R_CC_DEC, MOD3_RF_INVERT, 43, 0},
    {MOD3_R_CC_DEC, MOD3_RF_INVERT, 64, 0}, {MOD3_R_CC_DEC, MOD3_RF_INVERT, 85, 0}, {MOD3_R_CC_DEC, MOD3_RF_INVERT, 106, 0},
    {MOD3_R_CC_REL, MOD3_RF_INVERT, 3, 0}, {MOD3_R_CC_REL, MOD3_RF_INVERT, 24, 0}, {MOD3_R_CC_REL, MOD3_RF_INVERT, 45, 0},
    {MOD3_R_CC_REL, MOD3_RF_INVERT, 66, 0}, {MOD3_R_CC_REL, MOD3_RF_INVERT, 87, 0}, {MOD3_R_CC_REL, MOD3_RF_INVERT, 108, 0},
    {MOD3_R_CC_BRIGHT, MOD3_RF_TRACK, MOD3_P_E0 + 2, 0},
    {MOD3_R_BANK_ENV, 0, 4, 0}, {MOD3_R_BANK_LFO, 0, 10, 0},
    {MOD3_R_CC_GLIDE, MOD3_RF_RAW, 160 + FN_PTIME, 0},   /* (CC 5: PORTA TIME, the value as sent) */
};
/* the widgets' data: OP1's envelope (EG RATE / LVL: its ids, 21 apart per operator), the pitch envelope, the algorithm
 * chart (W_ALG: ALG, FB, ENGINE and its MARK I, OP1's OUT 21 apart, the part's macros E1 (the algorithm when 1..32)
 * and E2 (added to FB), what EDIT 1 / 2's E1..E8 light, Dexed's 32 algorithms, each operator's cell), then Motion's
 * map (each id's highest value, ids 0..144, then the bits forced 0: none) */
static const uint8_t FM6_SCH_WID[] = {
    105, 0, 106, 0, 107, 0, 108, 0, 109, 0, 110, 0, 111, 0, 112, 0, 235, 0, 126, 0, 127, 0, 128, 0,
    129, 0, 130, 0, 131, 0, 132, 0, 133, 0, 0, 1, 134, 0, 135, 0, 175, 0, 121, 0, 235, 1, 116, 117,
    1, 2, 3, 4, 4, 4, 5, 1, 32, 6, 193, 17, 17, 20, 1, 20, 1, 17, 17, 20, 193, 20, 193, 17,
    20, 1, 17, 20, 193, 17, 148, 1, 17, 20, 193, 20, 1, 20, 1, 20, 193, 148, 1, 20, 1, 20, 193, 17,
    5, 20, 1, 20, 1, 17, 197, 20, 1, 20, 1, 17, 5, 20, 193, 20, 1, 5, 20, 193, 17, 20, 193, 5,
    20, 1, 17, 20, 1, 5, 5, 20, 193, 20, 193, 5, 5, 20, 1, 20, 193, 5, 17, 20, 1, 20, 1, 5,
    17, 20, 193, 20, 193, 17, 2, 37, 5, 20, 1, 17, 2, 37, 197, 20, 1, 17, 17, 197, 5, 20, 193, 20,
    20, 1, 17, 20, 1, 5, 20, 193, 20, 20, 1, 20, 20, 193, 20, 20, 193, 20, 20, 20, 1, 20, 193, 20,
    20, 1, 20, 4, 193, 20, 20, 20, 4, 4, 193, 20, 20, 4, 4, 4, 193, 5, 20, 1, 20, 4, 1, 5,
    20, 193, 20, 4, 4, 193, 17, 20, 1, 20, 193, 20, 1, 20, 4, 4, 4, 193, 17, 20, 4, 4, 193, 20,
    4, 4, 4, 4, 196, 4, 4, 4, 4, 4, 0, 16, 1, 17, 33, 49, 0, 16, 2, 18, 34, 50, 0, 16,
    32, 1, 17, 33, 0, 16, 32, 1, 17, 33, 0, 16, 1, 17, 2, 18, 0, 16, 1, 17, 2, 18, 0, 16,
    1, 17, 18, 34, 0, 16, 1, 18, 17, 33, 0, 16, 2, 18, 19, 35, 0, 16, 32, 1, 17, 18, 0, 16,
    32, 1, 17, 18, 0, 16, 3, 18, 19, 20, 0, 16, 2, 17, 18, 19, 0, 16, 1, 17, 32, 33, 0, 16,
    2, 18, 33, 34, 1, 16, 17, 33, 18, 34, 1, 18, 16, 32, 17, 33, 1, 16, 18, 17, 33, 49, 0, 16,
    32, 1, 2, 17, 0, 1, 16, 2, 18, 19, 0, 1, 16, 2, 3, 18, 0, 16, 1, 2, 3, 18, 0, 1,
    17, 2, 3, 18, 0, 1, 2, 3, 4, 19, 0, 1, 2, 3, 4, 19, 0, 1, 17, 2, 18, 19, 0, 1,
    17, 3, 19, 20, 0, 16, 1, 17, 33, 2, 0, 1, 2, 18, 3, 19, 0, 1, 2, 18, 34, 3, 0, 1,
    2, 3, 4, 20, 0, 1, 2, 3, 4, 5, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7,
    3, 7, 99, 1, 31, 99, 14, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99,
    1, 31, 99, 14, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99,
    14, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14, 99, 99, 99, 99, 99, 99, 99, 99,
    31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
/* The sound designer (mod_schema.h): FEATURES (the operators, feedback, the pitch EG, the LFO), what a dim control
 * needs (an operator's page: its operator), the build that cannot sound, the pages' stages; the goals' recipes */
#define FM6S_SW 180u                             /* (the switches: 180 + the operator's block, OP1's the sixth) */
#define FM6S_NOTE D_NOTE(MOD_SCH_NONE)           /* (the voice told: packed again as the DX7 SysEx makes it) */
static const uint8_t FM6S_D_OP1[] = {D_NZ(FM6S_SW + 5u), D_NZ(5 * FP_OP + FP_OL), MOD3_D_AND, MOD3_D_END};
static const uint8_t FM6S_D_OP1_ON[] = {D_IF(1), D_NZ(5 * FP_OP + FP_OL), MOD3_D_NOT, MOD3_D_END,
                                     D_SETCAR(5 * FP_OP + FP_OL, 0, 90, 60), D_SET(FM6S_SW + 5u, 1), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_OP1_OFF[] = {D_SET(FM6S_SW + 5u, 0), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_OP2[] = {D_NZ(FM6S_SW + 4u), D_NZ(4 * FP_OP + FP_OL), MOD3_D_AND, MOD3_D_END};
static const uint8_t FM6S_D_OP2_ON[] = {D_IF(1), D_NZ(4 * FP_OP + FP_OL), MOD3_D_NOT, MOD3_D_END,
                                     D_SETCAR(4 * FP_OP + FP_OL, 1, 90, 60), D_SET(FM6S_SW + 4u, 1), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_OP2_OFF[] = {D_SET(FM6S_SW + 4u, 0), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_OP3[] = {D_NZ(FM6S_SW + 3u), D_NZ(3 * FP_OP + FP_OL), MOD3_D_AND, MOD3_D_END};
static const uint8_t FM6S_D_OP3_ON[] = {D_IF(1), D_NZ(3 * FP_OP + FP_OL), MOD3_D_NOT, MOD3_D_END,
                                     D_SETCAR(3 * FP_OP + FP_OL, 2, 90, 60), D_SET(FM6S_SW + 3u, 1), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_OP3_OFF[] = {D_SET(FM6S_SW + 3u, 0), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_OP4[] = {D_NZ(FM6S_SW + 2u), D_NZ(2 * FP_OP + FP_OL), MOD3_D_AND, MOD3_D_END};
static const uint8_t FM6S_D_OP4_ON[] = {D_IF(1), D_NZ(2 * FP_OP + FP_OL), MOD3_D_NOT, MOD3_D_END,
                                     D_SETCAR(2 * FP_OP + FP_OL, 3, 90, 60), D_SET(FM6S_SW + 2u, 1), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_OP4_OFF[] = {D_SET(FM6S_SW + 2u, 0), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_OP5[] = {D_NZ(FM6S_SW + 1u), D_NZ(1 * FP_OP + FP_OL), MOD3_D_AND, MOD3_D_END};
static const uint8_t FM6S_D_OP5_ON[] = {D_IF(1), D_NZ(1 * FP_OP + FP_OL), MOD3_D_NOT, MOD3_D_END,
                                     D_SETCAR(1 * FP_OP + FP_OL, 4, 90, 60), D_SET(FM6S_SW + 1u, 1), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_OP5_OFF[] = {D_SET(FM6S_SW + 1u, 0), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_OP6[] = {D_NZ(FM6S_SW + 0u), D_NZ(0 * FP_OP + FP_OL), MOD3_D_AND, MOD3_D_END};
static const uint8_t FM6S_D_OP6_ON[] = {D_IF(1), D_NZ(0 * FP_OP + FP_OL), MOD3_D_NOT, MOD3_D_END,
                                     D_SETCAR(0 * FP_OP + FP_OL, 5, 90, 60), D_SET(FM6S_SW + 0u, 1), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_OP6_OFF[] = {D_SET(FM6S_SW + 0u, 0), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_FB[] = {D_NZ(FP_FB), MOD3_D_END};
static const uint8_t FM6S_D_FB_ON[] = {D_AMT(FP_FB, 5), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_FB_OFF[] = {D_SET(FP_FB, 0), FM6S_NOTE, MOD3_D_END};
/* (from L4 up to L1 at once, back by R2 to the centre: a blip as the note starts) */
static const uint8_t FM6S_D_PEG[] = {D_VAL(FP_PL1), D_CONST(50), MOD3_D_NE, D_VAL(FP_PL1 + 1), D_CONST(50), MOD3_D_NE,
                                     MOD3_D_OR, D_VAL(FP_PL1 + 2), D_CONST(50), MOD3_D_NE, MOD3_D_OR, D_VAL(FP_PL1 + 3),
                                     D_CONST(50), MOD3_D_NE, MOD3_D_OR, MOD3_D_END};
static const uint8_t FM6S_D_PEG_ON[] = {D_SET(FP_PL1, 58), D_SET(FP_PL1 + 1, 50), D_SET(FP_PL1 + 2, 50), D_SET(FP_PL1 + 3, 50),
                                        D_SET(FP_PR1, 99), D_SET(FP_PR1 + 1, 60), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_PEG_OFF[] = {D_SET(FP_PL1, 50), D_SET(FP_PL1 + 1, 50), D_SET(FP_PL1 + 2, 50),
                                         D_SET(FP_PL1 + 3, 50), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_LFO[] = {D_NZ(FP_LPMD), D_NZ(FP_LPMS), MOD3_D_AND, D_NZ(FP_LAMD), MOD3_D_OR, MOD3_D_END};
static const uint8_t FM6S_D_LFO_ON[] = {D_AMT(FP_LPMD, 10), D_AMT(FP_LPMS, 3), D_AMT(FP_LFS, 35), FM6S_NOTE, MOD3_D_END};
static const uint8_t FM6S_D_LFO_OFF[] = {D_SET(FP_LPMD, 0), D_SET(FP_LAMD, 0), FM6S_NOTE, MOD3_D_END};
#define FM6S_F(n) {"OP" #n, 0, 0, 2, MOD3_FF_INST | MOD3_FF_CARRIER, n - 1, 0, FM6S_D_OP##n, FM6S_D_OP##n##_ON, FM6S_D_OP##n##_OFF}
static const mod_sch_feat_src FM6S_DFEAT[] = {
    FM6S_F(1), FM6S_F(2), FM6S_F(3), FM6S_F(4), FM6S_F(5), FM6S_F(6),
    {"Feedback", 0, 0, 2, 0, 0, 0, FM6S_D_FB, FM6S_D_FB_ON, FM6S_D_FB_OFF},
    {"Pitch EG", 0, 0, 2, 0, 0, 0, FM6S_D_PEG, FM6S_D_PEG_ON, FM6S_D_PEG_OFF},
    {"LFO", 0, 0, 2, 0, 0, 0, FM6S_D_LFO, FM6S_D_LFO_ON, FM6S_D_LFO_OFF},
    {"Voice", 0, 0, 4, MOD3_FF_HOST, MOD3_HF_VOICE, 0, 0, 0, 0},
};
#undef FM6S_F
/* an operator's page: its operator switched on first (its own switch aside, OP1's column on every page); its level
 * turned down switches nothing on (the ids 21 apart from OL: OP2's switch too) */
static const mod_sch_need_src FM6S_DNEED[] = {
    {FM6S_SW + 5u, 0, (uint8_t)-1, MOD3_DN_INST, 0, 0, 0},
    {FP_OL, 0xFFFFu, FP_OP, MOD3_DN_DOWN | MOD3_DN_INST, 0, 0, 0},
};
/* (no carrier that is on with a level) */
static const uint8_t FM6S_D_SILENT[] = {MOD3_D_CAR, 0, D_NZ(FM6S_SW + 5u), MOD3_D_AND, D_NZ(5 * FP_OP + FP_OL), MOD3_D_AND, MOD3_D_CAR, 1, D_NZ(FM6S_SW + 4u), MOD3_D_AND, D_NZ(4 * FP_OP + FP_OL), MOD3_D_AND, MOD3_D_CAR, 2, D_NZ(FM6S_SW + 3u), MOD3_D_AND, D_NZ(3 * FP_OP + FP_OL), MOD3_D_AND, MOD3_D_CAR, 3, D_NZ(FM6S_SW + 2u), MOD3_D_AND, D_NZ(2 * FP_OP + FP_OL), MOD3_D_AND, MOD3_D_CAR, 4, D_NZ(FM6S_SW + 1u), MOD3_D_AND, D_NZ(1 * FP_OP + FP_OL), MOD3_D_AND, MOD3_D_CAR, 5, D_NZ(FM6S_SW + 0u), MOD3_D_AND, D_NZ(0 * FP_OP + FP_OL), MOD3_D_AND, MOD3_D_OR, MOD3_D_OR, MOD3_D_OR,
                                        MOD3_D_OR, MOD3_D_OR, MOD3_D_NOT, MOD3_D_END};
/* the pages' stages (FM6_SCH_PAGE's order): EDIT 1 / 2 in ALL only (its macros repeat its pages) */
static const uint8_t FM6S_DPAGE[] = {
    MOD3_DS_SOURCE, 0, 0, MOD3_DS_SOURCE, 0, 0, MOD3_DS_SOURCE, 0, 0, MOD3_DS_SOURCE, 0, 0,   /* STORE, ALGO, FREQ, OUT */
    MOD3_DS_ENV, 0, 0, MOD3_DS_ENV, 0, 0, MOD3_DS_TONE | MOD3_DS_SKIP, 0, 0, MOD3_DS_TONE | MOD3_DS_SKIP, 0, 0,   /* EG RATE,
                                                                                               * EG LVL, SCALE, CURVE */
    MOD3_DS_ENV | MOD3_DS_SKIP, 0, 0, MOD3_DS_ENV | MOD3_DS_SKIP, 0, 0, MOD3_DS_MOVE, 0, 0, MOD3_DS_MOVE, 0, 0,   /* PITCH EG,
                                                                                               * PITCH LV, FM LFO, 2 */
    MOD3_DS_PLAY | MOD3_DS_SKIP, 0, 0, MOD3_DS_PLAY | MOD3_DS_SKIP, 0, 0, MOD3_DS_PLAY | MOD3_DS_SKIP, 0, 0,   /* FM BEND, PORTA,
                                                                                               * WH/FT */
    MOD3_DS_PLAY | MOD3_DS_SKIP, 0, 0,                                                        /* FM BR/AT */
};
_Static_assert(sizeof FM6S_DPAGE == 3u * sizeof FM6_SCH_PAGE / sizeof *FM6_SCH_PAGE, "a stage a page");
static const mod_sch_design_src FM6S_DESIGN = {
    MOD3_DSF_EDIT_ALL | MOD3_DSF_OWN_LFO | MOD3_DSF_FIRST, FM6S_DFEAT, sizeof FM6S_DFEAT / sizeof *FM6S_DFEAT,
    FM6S_DNEED, sizeof FM6S_DNEED / sizeof *FM6S_DNEED, FM6S_D_SILENT, "NO CARRIER ON", FM6S_DPAGE,
};
/* the goals' recipes: algorithm 5 (three independent two-operator pairs), each operator's level, ratio and EG, every
 * operator on; FX: feedback and a bright fifth operator */
static const mod_sch_rec_src FM6S_REC[] = {
    {MOD3_Q_GOAL, MOD3_GM_ALL, FP_ALG, 4},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), 0 * FP_OP + FP_OL, 0},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 0 * FP_OP + FP_OL, 35},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 0 * FP_OP + FP_FC, 1},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 0 * FP_OP + FP_FC, 3},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), 0 * FP_OP + FP_R1, 99},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 0 * FP_OP + FP_R1, 45},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 0 * FP_OP + FP_R1 + 1, 75},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 0 * FP_OP + FP_R1 + 1, 60},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 0 * FP_OP + FP_R1 + 2, 65},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(LEAD) | MOD3_GM(FX), 0 * FP_OP + FP_R1 + 3, 80},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 0 * FP_OP + FP_R1 + 3, 42},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 0 * FP_OP + FP_R1 + 3, 60},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 0 * FP_OP + FP_L1, 99},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 0 * FP_OP + FP_L1 + 1, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 0 * FP_OP + FP_L1 + 1, 65},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 0 * FP_OP + FP_L1 + 2, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 0 * FP_OP + FP_L1 + 2, 0},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 0 * FP_OP + FP_L1 + 3, 0},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), 1 * FP_OP + FP_OL, 0},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 1 * FP_OP + FP_OL, 78},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 1 * FP_OP + FP_FC, 1},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), 1 * FP_OP + FP_R1, 99},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 1 * FP_OP + FP_R1, 45},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 1 * FP_OP + FP_R1 + 1, 75},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 1 * FP_OP + FP_R1 + 1, 60},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 1 * FP_OP + FP_R1 + 2, 65},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(LEAD) | MOD3_GM(FX), 1 * FP_OP + FP_R1 + 3, 80},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 1 * FP_OP + FP_R1 + 3, 42},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 1 * FP_OP + FP_R1 + 3, 60},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 1 * FP_OP + FP_L1, 99},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 1 * FP_OP + FP_L1 + 1, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 1 * FP_OP + FP_L1 + 1, 65},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 1 * FP_OP + FP_L1 + 2, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 1 * FP_OP + FP_L1 + 2, 0},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 1 * FP_OP + FP_L1 + 3, 0},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), 2 * FP_OP + FP_OL, 0},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 2 * FP_OP + FP_OL, 35},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 2 * FP_OP + FP_FC, 1},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 2 * FP_OP + FP_FC, 3},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), 2 * FP_OP + FP_R1, 99},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 2 * FP_OP + FP_R1, 45},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 2 * FP_OP + FP_R1 + 1, 75},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 2 * FP_OP + FP_R1 + 1, 60},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 2 * FP_OP + FP_R1 + 2, 65},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(LEAD) | MOD3_GM(FX), 2 * FP_OP + FP_R1 + 3, 80},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 2 * FP_OP + FP_R1 + 3, 42},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 2 * FP_OP + FP_R1 + 3, 60},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 2 * FP_OP + FP_L1, 99},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 2 * FP_OP + FP_L1 + 1, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 2 * FP_OP + FP_L1 + 1, 65},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 2 * FP_OP + FP_L1 + 2, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 2 * FP_OP + FP_L1 + 2, 0},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 2 * FP_OP + FP_L1 + 3, 0},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), 3 * FP_OP + FP_OL, 0},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 3 * FP_OP + FP_OL, 78},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 3 * FP_OP + FP_FC, 1},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), 3 * FP_OP + FP_R1, 99},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 3 * FP_OP + FP_R1, 45},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 3 * FP_OP + FP_R1 + 1, 75},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 3 * FP_OP + FP_R1 + 1, 60},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 3 * FP_OP + FP_R1 + 2, 65},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(LEAD) | MOD3_GM(FX), 3 * FP_OP + FP_R1 + 3, 80},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 3 * FP_OP + FP_R1 + 3, 42},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 3 * FP_OP + FP_R1 + 3, 60},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 3 * FP_OP + FP_L1, 99},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 3 * FP_OP + FP_L1 + 1, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 3 * FP_OP + FP_L1 + 1, 65},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 3 * FP_OP + FP_L1 + 2, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 3 * FP_OP + FP_L1 + 2, 0},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 3 * FP_OP + FP_L1 + 3, 0},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(PAD) | MOD3_GM(LEAD), 4 * FP_OP + FP_OL, 45},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 4 * FP_OP + FP_OL, 72},
    {MOD3_Q_GOAL, MOD3_GM(FX), 4 * FP_OP + FP_OL, 85},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(PAD) | MOD3_GM(LEAD), 4 * FP_OP + FP_FC, 1},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 4 * FP_OP + FP_FC, 3},
    {MOD3_Q_GOAL, MOD3_GM(FX), 4 * FP_OP + FP_FC, 7},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), 4 * FP_OP + FP_R1, 99},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 4 * FP_OP + FP_R1, 45},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 4 * FP_OP + FP_R1 + 1, 75},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 4 * FP_OP + FP_R1 + 1, 60},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 4 * FP_OP + FP_R1 + 2, 65},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(LEAD) | MOD3_GM(FX), 4 * FP_OP + FP_R1 + 3, 80},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 4 * FP_OP + FP_R1 + 3, 42},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 4 * FP_OP + FP_R1 + 3, 60},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 4 * FP_OP + FP_L1, 99},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 4 * FP_OP + FP_L1 + 1, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 4 * FP_OP + FP_L1 + 1, 65},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 4 * FP_OP + FP_L1 + 2, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 4 * FP_OP + FP_L1 + 2, 0},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 4 * FP_OP + FP_L1 + 3, 0},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 5 * FP_OP + FP_OL, 99},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 5 * FP_OP + FP_FC, 1},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), 5 * FP_OP + FP_R1, 99},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 5 * FP_OP + FP_R1, 45},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 5 * FP_OP + FP_R1 + 1, 75},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 5 * FP_OP + FP_R1 + 1, 60},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 5 * FP_OP + FP_R1 + 2, 65},
    {MOD3_Q_GOAL, MOD3_GM(BASS) | MOD3_GM(LEAD) | MOD3_GM(FX), 5 * FP_OP + FP_R1 + 3, 80},
    {MOD3_Q_GOAL, MOD3_GM(PAD), 5 * FP_OP + FP_R1 + 3, 42},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 5 * FP_OP + FP_R1 + 3, 60},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 5 * FP_OP + FP_L1, 99},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 5 * FP_OP + FP_L1 + 1, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 5 * FP_OP + FP_L1 + 1, 65},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), 5 * FP_OP + FP_L1 + 2, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), 5 * FP_OP + FP_L1 + 2, 0},
    {MOD3_Q_GOAL, MOD3_GM_ALL, 5 * FP_OP + FP_L1 + 3, 0},
    {MOD3_Q_GOAL, MOD3_GM_ALL, FM6S_SW + 0, 1},
    {MOD3_Q_GOAL, MOD3_GM_ALL, FM6S_SW + 1, 1},
    {MOD3_Q_GOAL, MOD3_GM_ALL, FM6S_SW + 2, 1},
    {MOD3_Q_GOAL, MOD3_GM_ALL, FM6S_SW + 3, 1},
    {MOD3_Q_GOAL, MOD3_GM_ALL, FM6S_SW + 4, 1},
    {MOD3_Q_GOAL, MOD3_GM_ALL, FM6S_SW + 5, 1},
    {MOD3_Q_GOAL, MOD3_GM(FX), FP_FB, 5},
    {MOD3_Q_GOAL | MOD3_RQ_NOTE, MOD3_GM_ALL, MOD_SCH_NONE, 0},
};
static void fm6_sch_def(uint8_t *b)
{
    memcpy(b, FM6_INIT, FM6_PACKED);
    memcpy(b + FM6_B_FN, FM6_FNDEF, FM6_NFN);
    b[FM6_B_ON] = FM6_ON_ALL;
}
static const char *const FM6S_STRIP[] = {"FM ", 0};   /* (the pages' short names: the title past these) */
static const uint8_t FM6S_SYSEX[] = {1, 0x43, 0, 0};   /* (Yamaha's: fm6_sysex.c) */
#define FM6S_CK(off, n, max) (uint8_t)(off), (uint8_t)((off) >> 8), (uint8_t)(n), 0, 0xFF, 0, 0, (uint8_t)(max)
static const uint8_t FM6S_CHECK[] = {          /* (as Melodee 1.x stored it: VMEM's bytes 7-bit; the functions; the switches) */
    FM6S_CK(0, FM6_PACKED, 127),
    FM6S_CK(FM6_B_FN + 0, 3, 12), FM6S_CK(FM6_B_FN + 3, 1, 1), FM6S_CK(FM6_B_FN + 4, 1, 127), FM6S_CK(FM6_B_FN + 5, 1, 1),
    FM6S_CK(FM6_B_FN + 6, 1, 99), FM6S_CK(FM6_B_FN + 7, 1, 7), FM6S_CK(FM6_B_FN + 8, 1, 99), FM6S_CK(FM6_B_FN + 9, 1, 7),
    FM6S_CK(FM6_B_FN + 10, 1, 99), FM6S_CK(FM6_B_FN + 11, 1, 7), FM6S_CK(FM6_B_FN + 12, 1, 99), FM6S_CK(FM6_B_FN + 13, 1, 7),
    FM6S_CK(FM6_B_FN + 14, 1, 1), FM6S_CK(FM6_B_FN + 15, 1, 2), FM6S_CK(FM6_B_ON, 1, FM6_ON_ALL),
};
#undef FM6S_CK
_Static_assert(FM6_NFN == 16u, "FM6S_CHECK: the functions' ranges are FM6_FNMAX's");
static const mod_sch_src FM6_SCHEMA = {
    .patch = FM6_BLOB, .def_make = fm6_sch_def, .store = MOD_SCH_STORE_VOICE, .utag = 'F', .mtag = 3, .flags = 0, .abbr = "FM6",
    .wdef = 36, .mmap = 442, .name = 118, .nlen = 10, .sound = FM6_PACKED,
    .param = FM6_SCH_PARAM, .nparam = sizeof FM6_SCH_PARAM / sizeof *FM6_SCH_PARAM,
    .page = FM6_SCH_PAGE, .npage = sizeof FM6_SCH_PAGE / sizeof *FM6_SCH_PAGE,
    .role = FM6_SCH_ROLE, .nrole = sizeof FM6_SCH_ROLE / sizeof *FM6_SCH_ROLE,
    .wid = FM6_SCH_WID, .nwid = sizeof FM6_SCH_WID,
    .rec = FM6S_REC, .nrec = sizeof FM6S_REC / sizeof *FM6S_REC,
    .strip = FM6S_STRIP, .design = &FM6S_DESIGN,
    .sysex = FM6S_SYSEX, .nsysex = 1, .check = FM6S_CHECK, .ncheck = sizeof FM6S_CHECK / MOD_SCH_CREC,
};
