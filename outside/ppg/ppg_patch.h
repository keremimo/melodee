/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* PPG: a part's sound, a PPG Wave 2.2 program (Saadet's ppg_patch.h byte for byte: 115 bytes). One byte a control, in
 * the 2.2's own numbers: its knobs 0..63 (the engine steps them as the 2.2 quantises them), its DIGITAL codes (UW, SW,
 * KW, KF, KL, MW/MF/ML, BD, TW/TF/TL/TM, VF/VL) and its TUNING codes (DETU, MO/MS, EO/ES, BI). The order is the pages',
 * not the 2.2's memory: a V8 OS bank is read into it (ppg.c ppg_sysex), none is written. Bytes stored offset: SEMI and SEMI2..8 (24 = none) and
 * ENV 3's AMOUNT (32 = none, as the 2.2's ENV 3 ATT knob). SEMI is the 2.2's SEMIT of voice 1, SEMI2..8 of voices 2..8:
 * each of its eight voices transposes by its own (a voice playing this group; KEYB 2 and 3 stack them into chords). */
#pragma once
#include <stdint.h>
enum {
    PG_TABLE, PG_OSC, PG_SUB, PG_SW,           /* WAVES · TABLE: 0..29 and 30 UPPER; WAVES-OSC, WAVES-SUB; SW 0..3 */
    PG_E1W, PG_KW, PG_UW,                      /* WAVES · SWEEP: ENVELOPE 1 - WAVES; KW 0..7; UW */
    PG_SEMI, PG_DETU, PG_BI, PG_BD,            /* TUNE */
    PG_CUT, PG_EMPH, PG_E1F, PG_KF,            /* FILTER: VCF-CUTOFF, VCF-EMPHASIS, ENVELOPE 1 - VCF, KF 0..7 */
    PG_A1, PG_D1, PG_S1, PG_R1,                /* ENV 1 */
    PG_A2, PG_D2, PG_S2, PG_R2,                /* ENV 2 */
    PG_LOUD, PG_KL,                            /* LOUD · LEVEL: ENVELOPE 2 - LOUDN.; KL 0..7 (4 flat) */
    PG_A3, PG_D3, PG_E3AMT, PG_E3TO,           /* ENV 3: ATTACK 3, DECAY 3, ENV 3 ATT (32 = none); EO/ES as 0..3 */
    PG_RATE, PG_SHAPE, PG_DELAY, PG_WHEEL,     /* LFO: RATE, WAVESHAPE 0..3, DELAY; the stored mod wheel */
    PG_MP, PG_MW, PG_MF, PG_ML,                /* LFO · TO: MO/MS as 0..3, MW, MF, ML */
    PG_TW, PG_TF, PG_TL, PG_TM,                /* PLAY · TOUCH */
    PG_VF, PG_VL,                              /* PLAY · VELOCITY */
    PG_SEMI2, PG_SEMI3, PG_SEMI4, PG_SEMI5, PG_SEMI6, PG_SEMI7, PG_SEMI8,   /* SEMIT of voices 2..8 (PG_SEMI: voice 1's) */
    PG_RAW
};
/* the panel's fields past a group's bytes (Saadet's ppg_edit.h ids, the schema's): GROUP, KEYB and SPLIT, the program's;
 * V1, voice 1's SEMIT alone (SEMI on TUNE moves all eight voices' together, a chord kept) */
enum { PGX_SEL = PG_RAW, PGX_KEYB, PGX_SPLIT, PGX_V1, PGX_N };
#define PG_UPPER 30u                           /* TABLE 30: the upper table alone (the 2.2's WAVETABLE 30 with UW 1) */
#define PG_SAMPLE 31u                          /* TABLE 31: the 2.2's natural sounds, piano and saxophone, in its short-loop
                                                * transient mode: OSC the start, ENV 1 WAVES the end it loops (ppg.c) */
#define PG_SEMI0 24
#define PG_E3AMT0 32
#define PG_SPLIT0 36                           /* SPLIT shows as a key: the 2.2's lowest C is MIDI 36 */
#define PG_NAME_LEN 12u
/* A program, as the 2.2's: two groups (A, B), each a whole sound, the keyboard mode that plays them and the split point
 * in keys from the lowest C. Measured on the V3 OS: its voices pair up, even ones playing the A slot, odd ones the B
 * slot; KEYB 0 all eight A; 1 A+B a key (4 keys); 2 two A+B a key (2 keys); 3 four A+B on one key; 4..8 split, keys up
 * to SPLIT (the OS's key numbers, lowest C 1) B, above A. A recalled program has A in the A slot. SEL is the group
 * KEYB 0 plays and the pages edit (the GROUP button: the 2.2's load of B into the A slot) */
typedef struct {
    uint8_t raw[PG_RAW];                       /* group A */
    uint8_t rawb[PG_RAW];                      /* group B */
    uint8_t keyb, split, sel;
    char name[PG_NAME_LEN];                    /* ASCII, space-padded */
} ppg_patch_t;
#define PPG_KEYB (2u * PG_RAW)                 /* the program's bytes' offsets */
#define PPG_SPLIT (PPG_KEYB + 1u)
#define PPG_SEL (PPG_KEYB + 2u)
#define PPG_NAME (PPG_KEYB + 3u)
#define PPG_BLOB (PPG_NAME + PG_NAME_LEN)      /* the patch's bytes: 115 */
_Static_assert(sizeof(ppg_patch_t) == PPG_BLOB, "PPG program layout");
static inline uint8_t *ppg_group(ppg_patch_t *p, uint32_t g) { return g ? p->rawb : p->raw; }
static inline const uint8_t *ppg_group_c(const ppg_patch_t *p, uint32_t g) { return g ? p->rawb : p->raw; }

/* each byte's largest value: the pages' ranges, and what a load accepts */
static const uint8_t PG_MAX[PG_RAW] = {
    [PG_TABLE] = 31, [PG_OSC] = 63, [PG_SUB] = 63, [PG_SW] = 3, [PG_E1W] = 63, [PG_KW] = 7, [PG_UW] = 1,
    [PG_SEMI] = 48, [PG_DETU] = 7, [PG_BI] = 3, [PG_BD] = 7, [PG_CUT] = 63, [PG_EMPH] = 63, [PG_E1F] = 63, [PG_KF] = 7,
    [PG_A1] = 63, [PG_D1] = 63, [PG_S1] = 63, [PG_R1] = 63, [PG_A2] = 63, [PG_D2] = 63, [PG_S2] = 63, [PG_R2] = 63,
    [PG_LOUD] = 63, [PG_KL] = 7, [PG_A3] = 63, [PG_D3] = 63, [PG_E3AMT] = 63, [PG_E3TO] = 3,
    [PG_RATE] = 63, [PG_SHAPE] = 3, [PG_DELAY] = 63, [PG_WHEEL] = 63, [PG_MP] = 3, [PG_MW] = 1, [PG_MF] = 1, [PG_ML] = 1,
    [PG_TW] = 1, [PG_TF] = 1, [PG_TL] = 1, [PG_TM] = 1, [PG_VF] = 1, [PG_VL] = 1,
    [PG_SEMI2 ... PG_SEMI8] = 48,
};
/* voice h's (0..7) SEMIT byte */
static inline uint32_t pg_semi_id(uint32_t h) { return h ? PG_SEMI2 + h - 1u : PG_SEMI; }
static inline int pg_is_semi(uint32_t id) { return id == PG_SEMI || (id >= PG_SEMI2 && id <= PG_SEMI8); }

/* INIT: wave 63 of table 0 (the saw every table ends with), no SUB, the filter open, the loudness held while the key is */
static const uint8_t PPG_INIT[][2] = {
    {PG_OSC, 63}, {PG_SW, 3}, {PG_SEMI, PG_SEMI0}, {PG_BD, 1}, {PG_CUT, 63}, {PG_KF, 3},
    {PG_D1, 30}, {PG_S1, 63}, {PG_R1, 20}, {PG_D2, 30}, {PG_S2, 63}, {PG_R2, 16}, {PG_LOUD, 63}, {PG_KL, 4},
    {PG_D3, 20}, {PG_E3AMT, PG_E3AMT0}, {PG_RATE, 32},
    {PG_SEMI2, PG_SEMI0}, {PG_SEMI3, PG_SEMI0}, {PG_SEMI4, PG_SEMI0}, {PG_SEMI5, PG_SEMI0}, {PG_SEMI6, PG_SEMI0},
    {PG_SEMI7, PG_SEMI0}, {PG_SEMI8, PG_SEMI0},
};
static void ppg_patch_init(ppg_patch_t *p)
{
    uint8_t *b = (uint8_t *)p;
    for (uint32_t i = 0; i < PPG_BLOB; i++) b[i] = 0;
    for (uint32_t i = 0; i < sizeof PPG_INIT / sizeof *PPG_INIT; i++) p->raw[PPG_INIT[i][0]] = PPG_INIT[i][1];
    for (uint32_t i = 0; i < PG_RAW; i++) p->rawb[i] = p->raw[i];
    p->split = 24;
    const char *n = "INIT WAVE   ";
    for (uint32_t i = 0; i < PG_NAME_LEN; i++) p->name[i] = n[i];
}
static int ppg_patch_valid(const ppg_patch_t *p)
{
    for (uint32_t i = 0; i < PG_RAW; i++) if (p->raw[i] > PG_MAX[i] || p->rawb[i] > PG_MAX[i]) return 0;
    return p->keyb <= 8u && p->split <= 60u && p->sel <= 1u;
}
