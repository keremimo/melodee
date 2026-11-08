/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The CZ-1's schema (firmware/modules/sdk/mod_schema.h): its patch (cz_tone.h: Casio's 144 bytes), every panel value
 * (cz_panel.h's flat ids, LCZ_*) through the module (cz_ui.c pget / pput: Casio's bytes <-> panel values, an edit
 * writing back only the bytes whose encoding changed), its pages, roles, Motion's bytes (the tone's 1..127, as Melodee
 * 1.x recorded them; END's bit 3 stays 0). The module file carries it as its SCHM chunk (tools/mod_build.py); host
 * builds compile it in (cz_ui.c) */
static const char *const CZS_LINES[] = {"LINE1", "LINE2", "1+2", "1+1"};
static const char *const CZS_MODES[] = {"OFF", "RING", "NOISE"};
static const char *const CZS_VWAVES[] = {"TRI", "SAW UP", "SAW DN", "SQR"};
static const char *const CZS_STAGES[] = {"1", "2", "3", "4", "5", "6", "7", "8", "-"};
static const char *const CZS_CARRIERS[] = {"SAW", "SQUARE", "PULSE", "NULL", "DBL SINE", "SAW-PULS", "MULTISIN", "PULSE2"};
static const char *const CZS_CARRIERS2[] = {"OFF", "SAW", "SQUARE", "PULSE", "NULL", "DBL SINE", "SAW-PULS", "MULTISIN", "PULSE2"};
static const char *const CZS_WINDOWS[] = {"OFF", "SAW DN", "TRIANGLE", "TRAPEZ", "HALF SAW", "2 SAW UP", "2 SAW 6", "2 SAW 7"};
static const mod_sch_param_src CZ_SCH_PARAM[] = {
    {0, MOD3_SK_CALL, MOD3_F_ENUM, 0, 3, 2, 0, 0, 0, MOD3_SF_DIM, "LINE", 0, CZS_LINES, 0, 0},
    {1, MOD3_SK_CALL, MOD3_F_ENUM, 0, 2, 0, 0, 0, 0, MOD3_SF_DIM, "MOD", 0, CZS_MODES, 0, 0},
    {2, MOD3_SK_CALL, MOD3_F_INT, 0, 2, 1, 0, 0, 0, MOD3_SF_DIM, "OCT", 0, 0, 0, 0},
    {3, MOD3_SK_CALL, MOD3_F_ONOFF, 0, 1, 0, 0, 0, 0, MOD3_SF_DIM, "SIGN", 0, 0, 0, 0},
    {4, MOD3_SK_CALL, MOD3_F_INT, 0, 3, 0, 0, 0, 0, MOD3_SF_DIM, "OCT", 0, 0, 0, 0},
    {5, MOD3_SK_CALL, MOD3_F_INT, 0, 11, 0, 0, 0, 0, MOD3_SF_DIM, "NOTE", 0, 0, 0, 0},
    {6, MOD3_SK_CALL, MOD3_F_INT, 0, 60, 0, 0, 0, 0, MOD3_SF_DIM, "FINE", 0, 0, 0, 0},
    {7, MOD3_SK_CALL, MOD3_F_ENUM, 0, 3, 0, 0, 0, 0, MOD3_SF_DIM, "WAVE", 0, CZS_VWAVES, 0, 0},
    {8, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 50, 0, 0, 0, MOD3_SF_DIM, "RATE", 0, 0, 0, 0},
    {9, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "DEPTH", 0, 0, 0, 0},
    {10, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "DELAY", 0, 0, 0, 0},
    {23, MOD3_SK_CALL, MOD3_F_ENUM, 0, 7, 0, 0, 0, 0, MOD3_SF_DIM, "WAVE", 0, CZS_CARRIERS, 0, 0},
    {24, MOD3_SK_CALL, MOD3_F_ENUM, 0, 8, 0, 0, 0, 0, MOD3_SF_DIM, "WAVE2", 0, CZS_CARRIERS2, 0, 0},
    {25, MOD3_SK_CALL, MOD3_F_INT, 0, 9, 0, 0, 0, 0, MOD3_SF_DIM, "W.KEY", 0, 0, 0, 0},
    {26, MOD3_SK_CALL, MOD3_F_INT, 0, 9, 0, 0, 0, 0, MOD3_SF_DIM, "A.KEY", 0, 0, 0, 0},
    {27, MOD3_SK_CALL, MOD3_F_INT, 1, 15, 15, 0, 0, 0, MOD3_SF_DIM, "LEVEL", 0, 0, 0, 0},
    {28, MOD3_SK_CALL, MOD3_F_INT, 0, 15, 0, 0, 0, 0, MOD3_SF_DIM, "V.PIT", 0, 0, 0, 0},
    {29, MOD3_SK_CALL, MOD3_F_INT, 0, 15, 0, 0, 0, 0, MOD3_SF_DIM, "V.WAV", 0, 0, 0, 0},
    {30, MOD3_SK_CALL, MOD3_F_INT, 0, 15, 0, 0, 0, 0, MOD3_SF_DIM, "V.AMP", 0, 0, 0, 0},
    {31, MOD3_SK_CALL, MOD3_F_ENUM, 0, 7, 0, 0, 0, 0, MOD3_SF_DIM, "WAVE", 0, CZS_CARRIERS, 0, 0},
    {32, MOD3_SK_CALL, MOD3_F_ENUM, 0, 8, 0, 0, 0, 0, MOD3_SF_DIM, "WAVE2", 0, CZS_CARRIERS2, 0, 0},
    {33, MOD3_SK_CALL, MOD3_F_INT, 0, 9, 0, 0, 0, 0, MOD3_SF_DIM, "W.KEY", 0, 0, 0, 0},
    {34, MOD3_SK_CALL, MOD3_F_INT, 0, 9, 0, 0, 0, 0, MOD3_SF_DIM, "A.KEY", 0, 0, 0, 0},
    {35, MOD3_SK_CALL, MOD3_F_INT, 1, 15, 15, 0, 0, 0, MOD3_SF_DIM, "LEVEL", 0, 0, 0, 0},
    {36, MOD3_SK_CALL, MOD3_F_INT, 0, 15, 0, 0, 0, 0, MOD3_SF_DIM, "V.PIT", 0, 0, 0, 0},
    {37, MOD3_SK_CALL, MOD3_F_INT, 0, 15, 0, 0, 0, 0, MOD3_SF_DIM, "V.WAV", 0, 0, 0, 0},
    {38, MOD3_SK_CALL, MOD3_F_INT, 0, 15, 0, 0, 0, 0, MOD3_SF_DIM, "V.AMP", 0, 0, 0, 0},
    {39, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R1", 0, 0, 0, 0},
    {40, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R2", 0, 0, 0, 0},
    {41, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R3", 0, 0, 0, 0},
    {42, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R4", 0, 0, 0, 0},
    {43, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R5", 0, 0, 0, 0},
    {44, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R6", 0, 0, 0, 0},
    {45, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R7", 0, 0, 0, 0},
    {46, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R8", 0, 0, 0, 0},
    {47, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L1", 0, 0, 0, 0},
    {48, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L2", 0, 0, 0, 0},
    {49, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L3", 0, 0, 0, 0},
    {50, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L4", 0, 0, 0, 0},
    {51, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L5", 0, 0, 0, 0},
    {52, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L6", 0, 0, 0, 0},
    {53, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L7", 0, 0, 0, 0},
    {54, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L8", 0, 0, 0, 0},
    {55, MOD3_SK_CALL, MOD3_F_ENUM, 0, 8, 1, 0, 0, 0, MOD3_SF_DIM, "SUS", 0, CZS_STAGES, 0, 0},
    {56, MOD3_SK_CALL, MOD3_F_ENUM, 0, 7, 2, 0, 0, 0, MOD3_SF_DIM, "END", 0, CZS_STAGES, 0, 0},
    {57, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R1", 0, 0, 0, 0},
    {58, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R2", 0, 0, 0, 0},
    {59, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R3", 0, 0, 0, 0},
    {60, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R4", 0, 0, 0, 0},
    {61, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R5", 0, 0, 0, 0},
    {62, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R6", 0, 0, 0, 0},
    {63, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R7", 0, 0, 0, 0},
    {64, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R8", 0, 0, 0, 0},
    {65, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L1", 0, 0, 0, 0},
    {66, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L2", 0, 0, 0, 0},
    {67, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L3", 0, 0, 0, 0},
    {68, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L4", 0, 0, 0, 0},
    {69, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L5", 0, 0, 0, 0},
    {70, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L6", 0, 0, 0, 0},
    {71, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L7", 0, 0, 0, 0},
    {72, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L8", 0, 0, 0, 0},
    {73, MOD3_SK_CALL, MOD3_F_ENUM, 0, 8, 1, 0, 0, 0, MOD3_SF_DIM, "SUS", 0, CZS_STAGES, 0, 0},
    {74, MOD3_SK_CALL, MOD3_F_ENUM, 0, 7, 2, 0, 0, 0, MOD3_SF_DIM, "END", 0, CZS_STAGES, 0, 0},
    {75, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R1", 0, 0, 0, 0},
    {76, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R2", 0, 0, 0, 0},
    {77, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R3", 0, 0, 0, 0},
    {78, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R4", 0, 0, 0, 0},
    {79, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R5", 0, 0, 0, 0},
    {80, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R6", 0, 0, 0, 0},
    {81, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R7", 0, 0, 0, 0},
    {82, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R8", 0, 0, 0, 0},
    {83, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L1", 0, 0, 0, 0},
    {84, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L2", 0, 0, 0, 0},
    {85, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L3", 0, 0, 0, 0},
    {86, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L4", 0, 0, 0, 0},
    {87, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L5", 0, 0, 0, 0},
    {88, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L6", 0, 0, 0, 0},
    {89, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L7", 0, 0, 0, 0},
    {90, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L8", 0, 0, 0, 0},
    {91, MOD3_SK_CALL, MOD3_F_ENUM, 0, 8, 1, 0, 0, 0, MOD3_SF_DIM, "SUS", 0, CZS_STAGES, 0, 0},
    {92, MOD3_SK_CALL, MOD3_F_ENUM, 0, 7, 2, 0, 0, 0, MOD3_SF_DIM, "END", 0, CZS_STAGES, 0, 0},
    {93, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R1", 0, 0, 0, 0},
    {94, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R2", 0, 0, 0, 0},
    {95, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R3", 0, 0, 0, 0},
    {96, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R4", 0, 0, 0, 0},
    {97, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R5", 0, 0, 0, 0},
    {98, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R6", 0, 0, 0, 0},
    {99, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R7", 0, 0, 0, 0},
    {100, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R8", 0, 0, 0, 0},
    {101, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L1", 0, 0, 0, 0},
    {102, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L2", 0, 0, 0, 0},
    {103, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L3", 0, 0, 0, 0},
    {104, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L4", 0, 0, 0, 0},
    {105, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L5", 0, 0, 0, 0},
    {106, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L6", 0, 0, 0, 0},
    {107, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L7", 0, 0, 0, 0},
    {108, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L8", 0, 0, 0, 0},
    {109, MOD3_SK_CALL, MOD3_F_ENUM, 0, 8, 1, 0, 0, 0, MOD3_SF_DIM, "SUS", 0, CZS_STAGES, 0, 0},
    {110, MOD3_SK_CALL, MOD3_F_ENUM, 0, 7, 2, 0, 0, 0, MOD3_SF_DIM, "END", 0, CZS_STAGES, 0, 0},
    {111, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R1", 0, 0, 0, 0},
    {112, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R2", 0, 0, 0, 0},
    {113, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R3", 0, 0, 0, 0},
    {114, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R4", 0, 0, 0, 0},
    {115, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R5", 0, 0, 0, 0},
    {116, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R6", 0, 0, 0, 0},
    {117, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R7", 0, 0, 0, 0},
    {118, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R8", 0, 0, 0, 0},
    {119, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L1", 0, 0, 0, 0},
    {120, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L2", 0, 0, 0, 0},
    {121, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L3", 0, 0, 0, 0},
    {122, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L4", 0, 0, 0, 0},
    {123, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L5", 0, 0, 0, 0},
    {124, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L6", 0, 0, 0, 0},
    {125, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L7", 0, 0, 0, 0},
    {126, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L8", 0, 0, 0, 0},
    {127, MOD3_SK_CALL, MOD3_F_ENUM, 0, 8, 1, 0, 0, 0, MOD3_SF_DIM, "SUS", 0, CZS_STAGES, 0, 0},
    {128, MOD3_SK_CALL, MOD3_F_ENUM, 0, 7, 2, 0, 0, 0, MOD3_SF_DIM, "END", 0, CZS_STAGES, 0, 0},
    {129, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R1", 0, 0, 0, 0},
    {130, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R2", 0, 0, 0, 0},
    {131, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R3", 0, 0, 0, 0},
    {132, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R4", 0, 0, 0, 0},
    {133, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R5", 0, 0, 0, 0},
    {134, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R6", 0, 0, 0, 0},
    {135, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R7", 0, 0, 0, 0},
    {136, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_DIM, "R8", 0, 0, 0, 0},
    {137, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L1", 0, 0, 0, 0},
    {138, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L2", 0, 0, 0, 0},
    {139, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L3", 0, 0, 0, 0},
    {140, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L4", 0, 0, 0, 0},
    {141, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L5", 0, 0, 0, 0},
    {142, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L6", 0, 0, 0, 0},
    {143, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L7", 0, 0, 0, 0},
    {144, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 0, 0, 0, 0, MOD3_SF_DIM, "L8", 0, 0, 0, 0},
    {145, MOD3_SK_CALL, MOD3_F_ENUM, 0, 8, 1, 0, 0, 0, MOD3_SF_DIM, "SUS", 0, CZS_STAGES, 0, 0},
    {146, MOD3_SK_CALL, MOD3_F_ENUM, 0, 7, 2, 0, 0, 0, MOD3_SF_DIM, "END", 0, CZS_STAGES, 0, 0},
    {147, MOD3_SK_CALL, MOD3_F_ENUM, 0, 7, 0, 0, 0, 0, MOD3_SF_DIM, "LINE1", 0, CZS_WINDOWS, 0, 0},
    {148, MOD3_SK_CALL, MOD3_F_ENUM, 0, 7, 0, 0, 0, 0, MOD3_SF_DIM, "LINE2", 0, CZS_WINDOWS, 0, 0},
    {200, MOD3_SK_CALL, MOD3_F_INT, 0, 0, 0, 0, 0, 0, 0, "1 > 2", "LINE 1 > 2", 0, 0, 0},   /* (TOOLS: copy a line over the other) */
    {201, MOD3_SK_CALL, MOD3_F_INT, 0, 0, 0, 0, 0, 0, 0, "2 > 1", "LINE 2 > 1", 0, 0, 0},
    {202, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_HIDDEN, "REL", 0, 0, 0, 0},   /* (a line's amp release rate: END's, CC 72) */
    {203, MOD3_SK_CALL, MOD3_F_INT, 0, 99, 70, 0, 0, 0, MOD3_SF_HIDDEN, "REL", 0, 0, 0, 0},
    {MOD3_A_NAME, MOD3_SK_CALL, MOD3_F_INT, 0, 0, 0, 0, 0, 0, 0, "NAME", 0, 0, 0, 0},
    {MOD3_A_COMPARE, MOD3_SK_CALL, MOD3_F_INT, 0, 0, 0, 0, 0, 0, 0, "COMP", 0, 0, 0, 0},
};
#define CZS_PG(t, a, b, c, d, sec, fl) {t, MOD3_W_NONE, fl, {a, b, c, d}, 0, {0}, 0, 0, sec}
static const mod_sch_page_src CZ_SCH_PAGE[] = {
    {"CZ TOOLS", MOD3_W_TOOLS, MOD3_PG_LAST, {MOD3_A_NAME, 200, 201, MOD3_A_COMPARE}, 0, {0}, 0, 0, "Tools"},
    CZS_PG("CZ LINE", 0, 1, 2, MOD_SCH_NONE, "Line", 0),
    CZS_PG("CZ DETUNE", 3, 4, 5, 6, "Line", 0),
    CZS_PG("CZ VIBRATO", 7, 8, 9, 10, "Line", MOD3_PG_LFO),
    CZS_PG("CZ WINDOW", 147, 148, MOD_SCH_NONE, MOD_SCH_NONE, "Line", 0),
    CZS_PG("CZ1 WAVE", 23, 24, 25, 26, "Line 1", 0),
    CZS_PG("CZ1 TOUCH", 27, 28, 29, 30, "Line 1", 0),
    CZS_PG("C1 PIT R1-4", 39, 40, 41, 42, "Pitch 1", MOD3_PG_ENV),
    CZS_PG("C1 PIT R5-8", 43, 44, 45, 46, "Pitch 1", 0),
    CZS_PG("C1 PIT L1-4", 47, 48, 49, 50, "Pitch 1", 0),
    CZS_PG("C1 PIT L5-8", 51, 52, 53, 54, "Pitch 1", 0),
    CZS_PG("C1 PIT POINT", 55, 56, MOD_SCH_NONE, MOD_SCH_NONE, "Pitch 1", 0),
    CZS_PG("C1 WAV R1-4", 57, 58, 59, 60, "Wave 1", MOD3_PG_ENV),
    CZS_PG("C1 WAV R5-8", 61, 62, 63, 64, "Wave 1", 0),
    CZS_PG("C1 WAV L1-4", 65, 66, 67, 68, "Wave 1", 0),
    CZS_PG("C1 WAV L5-8", 69, 70, 71, 72, "Wave 1", 0),
    CZS_PG("C1 WAV POINT", 73, 74, MOD_SCH_NONE, MOD_SCH_NONE, "Wave 1", 0),
    CZS_PG("C1 AMP R1-4", 75, 76, 77, 78, "Amp 1", MOD3_PG_ENV),
    CZS_PG("C1 AMP R5-8", 79, 80, 81, 82, "Amp 1", 0),
    CZS_PG("C1 AMP L1-4", 83, 84, 85, 86, "Amp 1", 0),
    CZS_PG("C1 AMP L5-8", 87, 88, 89, 90, "Amp 1", 0),
    CZS_PG("C1 AMP POINT", 91, 92, MOD_SCH_NONE, MOD_SCH_NONE, "Amp 1", 0),
    CZS_PG("CZ2 WAVE", 31, 32, 33, 34, "Line 2", 0),
    CZS_PG("CZ2 TOUCH", 35, 36, 37, 38, "Line 2", 0),
    CZS_PG("C2 PIT R1-4", 93, 94, 95, 96, "Pitch 2", MOD3_PG_ENV),
    CZS_PG("C2 PIT R5-8", 97, 98, 99, 100, "Pitch 2", 0),
    CZS_PG("C2 PIT L1-4", 101, 102, 103, 104, "Pitch 2", 0),
    CZS_PG("C2 PIT L5-8", 105, 106, 107, 108, "Pitch 2", 0),
    CZS_PG("C2 PIT POINT", 109, 110, MOD_SCH_NONE, MOD_SCH_NONE, "Pitch 2", 0),
    CZS_PG("C2 WAV R1-4", 111, 112, 113, 114, "Wave 2", MOD3_PG_ENV),
    CZS_PG("C2 WAV R5-8", 115, 116, 117, 118, "Wave 2", 0),
    CZS_PG("C2 WAV L1-4", 119, 120, 121, 122, "Wave 2", 0),
    CZS_PG("C2 WAV L5-8", 123, 124, 125, 126, "Wave 2", 0),
    CZS_PG("C2 WAV POINT", 127, 128, MOD_SCH_NONE, MOD_SCH_NONE, "Wave 2", 0),
    CZS_PG("C2 AMP R1-4", 129, 130, 131, 132, "Amp 2", MOD3_PG_ENV),
    CZS_PG("C2 AMP R5-8", 133, 134, 135, 136, "Amp 2", 0),
    CZS_PG("C2 AMP L1-4", 137, 138, 139, 140, "Amp 2", 0),
    CZS_PG("C2 AMP L5-8", 141, 142, 143, 144, "Amp 2", 0),
    CZS_PG("C2 AMP POINT", 145, 146, MOD_SCH_NONE, MOD_SCH_NONE, "Amp 2", 0),
};
#undef CZS_PG
static const mod_sch_role_src CZ_SCH_ROLE[] = {
    {MOD3_R_HOME1, 0, 23, "WAVE"}, {MOD3_R_HOME2, 0, 65, "DCW"}, {MOD3_R_HOME3, 0, 6, "DETUNE"}, {MOD3_R_HOME4, 0, 9, "VIBRATO"},
    {MOD3_R_CC_ATK, MOD3_RF_INVERT, 75, 0}, {MOD3_R_CC_ATK, MOD3_RF_INVERT, 129, 0},
    {MOD3_R_CC_DEC, MOD3_RF_INVERT, 76, 0}, {MOD3_R_CC_DEC, MOD3_RF_INVERT, 130, 0},
    {MOD3_R_CC_REL, MOD3_RF_INVERT, 202, 0}, {MOD3_R_CC_REL, MOD3_RF_INVERT, 203, 0},
    {MOD3_R_CC_BRIGHT, MOD3_RF_TRACK, P_ED_FLT, 0},
    {MOD3_R_BANK_FILTER, 0, 5, 0}, {MOD3_R_BANK_ENV, 0, 17, 0}, {MOD3_R_BANK_LFO, 0, 3, 0},
};
/* Motion's bytes (MOD_SCH_F_MBYTES): the tone's synthesis bytes 1..127 (not the line select, not the name), at most their
 * highest values; the bits that stay 0 (a detune's low bits, END's flag); then its lanes' names per byte (Melodee 1.x's
 * SEQ > MOTION: "Detune", "DCW 1 env") */
static const uint8_t CZ_SCH_WID[] = {
    0, 1, 255, 47, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 233, 255, 9, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 233, 255, 9, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* Motion's lane names (MOD_SCH_F_MLANES): their offset (the writer's), count, each byte's */
    0, 0, 14, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13,
};
static void cz_sch_def(uint8_t *b) { cz_patch_init(b); }
static const char *const CZS_MLANE[] = {"Detune", "Vibrato", "Wave 1", "DCA key 1", "DCW key 1", "DCA 1 env", "DCW 1 env", "Pitch 1 env", "Wave 2", "DCA key 2", "DCW key 2", "DCA 2 env", "DCW 2 env", "Pitch 2 env"};
static const char *const CZS_STRIP[] = {"CZ1 ", "CZ2 ", "CZ ", "C1 PIT ", "C1 WAV ", "C1 AMP ", "C2 PIT ", "C2 WAV ", "C2 AMP ", 0};   /* (the pages' short names: the title past these) */
/* The sound designer (mod_schema.h): FEATURES (the line mode, ring, noise, line 2's detune, vibrato), what a dim
 * control needs (the line or path it belongs to), the pages' stages (a line's pages while its line plays); the goals'
 * recipes (each line's amp envelope, its end point first) */
static const char *const CZS_DLINES[] = {"Line 1", "Line 2", "1+2'", "1+1'"};
static const uint8_t CZS_D_LINES[] = {D_VAL(LCZ_LINE), MOD3_D_END};
static const uint8_t CZS_D_LINES_SET[] = {D_SETS(LCZ_LINE), MOD3_D_END};
#define CZS_LINE2 D_IF(1), D_NZ(LCZ_LINE), MOD3_D_NOT, MOD3_D_END, D_SET(LCZ_LINE, 2)   /* (line 2 needs a second line: 1+2') */
static const uint8_t CZS_D_RING[] = {D_NZ(LCZ_LINE), D_VAL(LCZ_MOD), D_CONST(1), MOD3_D_EQ, MOD3_D_AND, MOD3_D_END};
static const uint8_t CZS_D_RING_ON[] = {CZS_LINE2, D_SET(LCZ_MOD, 1), MOD3_D_END};
static const uint8_t CZS_D_RING_OFF[] = {D_IF(1), D_VAL(LCZ_MOD), D_CONST(1), MOD3_D_EQ, MOD3_D_END, D_SET(LCZ_MOD, 0), MOD3_D_END};
static const uint8_t CZS_D_NOISE[] = {D_NZ(LCZ_LINE), D_VAL(LCZ_MOD), D_CONST(2), MOD3_D_EQ, MOD3_D_AND, MOD3_D_END};
static const uint8_t CZS_D_NOISE_ON[] = {CZS_LINE2, D_SET(LCZ_MOD, 2), MOD3_D_END};
static const uint8_t CZS_D_NOISE_OFF[] = {D_IF(1), D_VAL(LCZ_MOD), D_CONST(2), MOD3_D_EQ, MOD3_D_END, D_SET(LCZ_MOD, 0), MOD3_D_END};
#define CZS_DETUNED D_NZ(LCZ_FINE), D_NZ(LCZ_NOTE), MOD3_D_OR, D_NZ(LCZ_DOCT), MOD3_D_OR
static const uint8_t CZS_D_DETUNE[] = {D_NZ(LCZ_LINE), CZS_DETUNED, MOD3_D_AND, MOD3_D_END};
static const uint8_t CZS_D_DETUNE_ON[] = {CZS_LINE2, D_IF(1), CZS_DETUNED, MOD3_D_NOT, MOD3_D_END, D_SET(LCZ_FINE, 8), MOD3_D_END};
static const uint8_t CZS_D_DETUNE_OFF[] = {D_SET(LCZ_FINE, 0), D_SET(LCZ_NOTE, 0), D_SET(LCZ_DOCT, 0), MOD3_D_END};
static const uint8_t CZS_D_VIB[] = {D_NZ(LCZ_VDEP), MOD3_D_END};
static const uint8_t CZS_D_VIB_ON[] = {D_AMT(LCZ_VDEP, 15), MOD3_D_END};
static const uint8_t CZS_D_VIB_OFF[] = {D_SET(LCZ_VDEP, 0), MOD3_D_END};
enum { CZS_F_LINES, CZS_F_RING, CZS_F_NOISE, CZS_F_DETUNE, CZS_F_VIB };
static const mod_sch_feat_src CZS_DFEAT[] = {
    {"Lines", CZS_DLINES, "Line 2", 4, 0, 0, 2, CZS_D_LINES, CZS_D_LINES_SET, CZS_D_LINES_SET},
    {"Ring", 0, 0, 2, 0, 0, 0, CZS_D_RING, CZS_D_RING_ON, CZS_D_RING_OFF},
    {"Noise", 0, 0, 2, 0, 0, 0, CZS_D_NOISE, CZS_D_NOISE_ON, CZS_D_NOISE_OFF},
    {"Detune", 0, 0, 2, 0, 0, 0, CZS_D_DETUNE, CZS_D_DETUNE_ON, CZS_D_DETUNE_OFF},
    {"Vibrato", 0, 0, 2, 0, 0, 0, CZS_D_VIB, CZS_D_VIB_ON, CZS_D_VIB_OFF},
    {"Voice", 0, 0, 4, MOD3_FF_HOST, MOD3_HF_VOICE, 0, 0, 0, 0},
};
static const uint8_t CZS_D_LIVE1[] = {MOD3_D_ACT, MOD3_D_ID(LCZ_LBASE(0) + LCZ_W1), MOD3_D_END};
static const uint8_t CZS_D_LIVE2[] = {MOD3_D_ACT, MOD3_D_ID(LCZ_LBASE(1) + LCZ_W1), MOD3_D_END};
/* a dim control: the vibrato's, the detune's sign, the globals (DETUNE, MOD: line 2's), an envelope step after its
 * END while its line plays (the POINT page's to change), else the line mode (line 2's values; line 1's in LINE2) */
static const mod_sch_need_src CZS_DNEED[] = {
    {LCZ_VWAVE, LCZ_VRATE, 0, MOD3_DN_DIM, CZS_F_VIB, 0, 0}, {LCZ_VDELAY, LCZ_VDELAY, 0, MOD3_DN_DIM, CZS_F_VIB, 0, 0},
    {LCZ_SIGN, LCZ_SIGN, 0, MOD3_DN_DIM, CZS_F_DETUNE, 0, 0}, {0, LCZ_GLOBALS - 1u, 0, MOD3_DN_DIM, CZS_F_LINES, 0, 0},
    {LCZ_EBASE(0, 0), LCZ_EBASE(1, 0) - 1u, 0, MOD3_DN_DIM, MOD3_DN_END, CZS_F_LINES, CZS_D_LIVE1},
    {LCZ_EBASE(1, 0), LCZ_OLD_NP - 1u, 0, MOD3_DN_DIM, MOD3_DN_END, CZS_F_LINES, CZS_D_LIVE2},
    {0, 0xFFFFu, 0, MOD3_DN_DIM, CZS_F_LINES, 0, 0},
};
#define CZS_L1 MOD3_D_ID(LCZ_LBASE(0) + LCZ_W1)
#define CZS_L2 MOD3_D_ID(LCZ_LBASE(1) + LCZ_W1)
/* the pages' stages (CZ_SCH_PAGE's order): a line's pages shaped while its line plays (its first wave acts) */
static const uint8_t CZS_DPAGE[] = {
    MOD3_DS_SOURCE, 0, 0, MOD3_DS_SOURCE, 0, 0, MOD3_DS_PLAY, 0, 0, MOD3_DS_MOVE, 0, 0,       /* TOOLS, LINE, DETUNE, VIBRATO */
    MOD3_DS_TONE | MOD3_DS_SKIP, 0, 0,                                                        /* WINDOW */
#define CZS_LINE_PAGES(L) \
    MOD3_DS_SOURCE | MOD3_DS_LIVE, L, MOD3_DS_SOURCE | MOD3_DS_LIVE, L,                       /* WAVE, TOUCH */ \
    MOD3_DS_ENV | MOD3_DS_SKIP | MOD3_DS_LIVE, L, MOD3_DS_ENV | MOD3_DS_SKIP | MOD3_DS_LIVE, L, /* PIT R1-4, R5-8 */ \
    MOD3_DS_ENV | MOD3_DS_SKIP | MOD3_DS_LIVE, L, MOD3_DS_ENV | MOD3_DS_SKIP | MOD3_DS_LIVE, L, /* PIT L1-4, L5-8 */ \
    MOD3_DS_ENV | MOD3_DS_SKIP | MOD3_DS_LIVE, L,                                             /* PIT POINT */ \
    MOD3_DS_ENV | MOD3_DS_LIVE, L, MOD3_DS_ENV | MOD3_DS_SKIP | MOD3_DS_LIVE, L,               /* WAV R1-4, R5-8 */ \
    MOD3_DS_ENV | MOD3_DS_LIVE, L, MOD3_DS_ENV | MOD3_DS_SKIP | MOD3_DS_LIVE, L,               /* WAV L1-4, L5-8 */ \
    MOD3_DS_ENV | MOD3_DS_LIVE, L,                                                            /* WAV POINT */ \
    MOD3_DS_ENV | MOD3_DS_LIVE, L, MOD3_DS_ENV | MOD3_DS_SKIP | MOD3_DS_LIVE, L,               /* AMP R1-4, R5-8 */ \
    MOD3_DS_ENV | MOD3_DS_LIVE, L, MOD3_DS_ENV | MOD3_DS_SKIP | MOD3_DS_LIVE, L,               /* AMP L1-4, L5-8 */ \
    MOD3_DS_ENV | MOD3_DS_LIVE, L                                                             /* AMP POINT */
    CZS_LINE_PAGES(CZS_L1), CZS_LINE_PAGES(CZS_L2),
#undef CZS_LINE_PAGES
};
_Static_assert(sizeof CZS_DPAGE == 3u * sizeof CZ_SCH_PAGE / sizeof *CZ_SCH_PAGE, "a stage a page");
static const mod_sch_design_src CZS_DESIGN = {
    MOD3_DSF_OWN_LFO | MOD3_DSF_TITLE_CHIP, CZS_DFEAT, sizeof CZS_DFEAT / sizeof *CZS_DFEAT,
    CZS_DNEED, sizeof CZS_DNEED / sizeof *CZS_DNEED, 0, 0, CZS_DPAGE,
};
/* the goals' recipes, in this order (an end point moved: sustain off while it moves); FX: 1+2', ring, detuned */
static const mod_sch_rec_src CZS_REC[] = {
    {MOD3_Q_GOAL, MOD3_GM_ALL, LCZ_EBASE(0, 2) + 16, 8},
    {MOD3_Q_GOAL, MOD3_GM_ALL, LCZ_EBASE(0, 2) + 17, 3},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), LCZ_EBASE(0, 2), 99},
    {MOD3_Q_GOAL, MOD3_GM(PAD), LCZ_EBASE(0, 2), 45},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), LCZ_EBASE(0, 2) + 1, 80},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), LCZ_EBASE(0, 2) + 1, 65},
    {MOD3_Q_GOAL, MOD3_GM_ALL, LCZ_EBASE(0, 2) + 2, 70},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), LCZ_EBASE(0, 2) + 3, 75},
    {MOD3_Q_GOAL, MOD3_GM(PAD), LCZ_EBASE(0, 2) + 3, 40},
    {MOD3_Q_GOAL, MOD3_GM_ALL, LCZ_EBASE(0, 2) + 8, 99},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), LCZ_EBASE(0, 2) + 9, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), LCZ_EBASE(0, 2) + 9, 70},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), LCZ_EBASE(0, 2) + 10, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), LCZ_EBASE(0, 2) + 10, 0},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), LCZ_EBASE(0, 2) + 16, 2},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), LCZ_EBASE(0, 2) + 16, 8},
    {MOD3_Q_GOAL, MOD3_GM_ALL, LCZ_EBASE(1, 2) + 16, 8},
    {MOD3_Q_GOAL, MOD3_GM_ALL, LCZ_EBASE(1, 2) + 17, 3},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), LCZ_EBASE(1, 2), 99},
    {MOD3_Q_GOAL, MOD3_GM(PAD), LCZ_EBASE(1, 2), 45},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), LCZ_EBASE(1, 2) + 1, 80},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), LCZ_EBASE(1, 2) + 1, 65},
    {MOD3_Q_GOAL, MOD3_GM_ALL, LCZ_EBASE(1, 2) + 2, 70},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~MOD3_GM(PAD), LCZ_EBASE(1, 2) + 3, 75},
    {MOD3_Q_GOAL, MOD3_GM(PAD), LCZ_EBASE(1, 2) + 3, 40},
    {MOD3_Q_GOAL, MOD3_GM_ALL, LCZ_EBASE(1, 2) + 8, 99},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), LCZ_EBASE(1, 2) + 9, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), LCZ_EBASE(1, 2) + 9, 70},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), LCZ_EBASE(1, 2) + 10, 99},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), LCZ_EBASE(1, 2) + 10, 0},
    {MOD3_Q_GOAL, MOD3_GM_ALL & ~(MOD3_GM(KEYS) | MOD3_GM(PLUCK)), LCZ_EBASE(1, 2) + 16, 2},
    {MOD3_Q_GOAL, MOD3_GM(KEYS) | MOD3_GM(PLUCK), LCZ_EBASE(1, 2) + 16, 8},
    {MOD3_Q_GOAL, MOD3_GM(FX), LCZ_LINE, 2},
    {MOD3_Q_GOAL, MOD3_GM(FX), LCZ_MOD, 1},
    {MOD3_Q_GOAL, MOD3_GM(FX), LCZ_FINE, 30},
};
static const mod_sch_src CZ_SCHEMA = {
    .patch = CZ_BYTES, .def_make = cz_sch_def, .store = MOD_SCH_STORE_TONE, .utag = 'Z', .mtag = 2,
    .flags = MOD_SCH_F_MBYTES | MOD_SCH_F_NOEDIT | MOD_SCH_F_MLANES, .abbr = "CZ-1", .wdef = MOD_SCH_NONE, .mmap = 0,
    .name = 128, .nlen = 16, .pname = "CZ TONE",
    .param = CZ_SCH_PARAM, .nparam = sizeof CZ_SCH_PARAM / sizeof *CZ_SCH_PARAM,
    .page = CZ_SCH_PAGE, .npage = sizeof CZ_SCH_PAGE / sizeof *CZ_SCH_PAGE,
    .role = CZ_SCH_ROLE, .nrole = sizeof CZ_SCH_ROLE / sizeof *CZ_SCH_ROLE,
    .wid = CZ_SCH_WID, .nwid = sizeof CZ_SCH_WID,
    .mlanes = CZS_MLANE, .nmlanes = sizeof CZS_MLANE / sizeof *CZS_MLANE,
    .rec = CZS_REC, .nrec = sizeof CZS_REC / sizeof *CZS_REC,
    .strip = CZS_STRIP, .design = &CZS_DESIGN,
};
