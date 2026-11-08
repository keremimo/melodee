/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The parts' FM6 voices: the firmware's data (the DX7 patches, their operator switches, the function settings),
 * written by the stores, the editors, the pages, Motion and DX7 SysEx (fm6_voices.c, fm6_store.c), read by the FM6
 * engine (eng_fm6.c: a synth module on the device, firmware/modules/fm6). With them the patch's layout and the 32
 * algorithms, which the engine plays and the pages draw */
#pragma once

/* the 155-byte patch: 6 x 21 operator bytes (the sixth operator first), then the voice */
enum {
    FP_R1 = 0, FP_L1 = 4, FP_BP = 8, FP_LD, FP_RD, FP_LC, FP_RC, FP_RS, FP_AMS, FP_KVS, FP_OL, FP_MODE, FP_FC, FP_FF,
    FP_DET, FP_OP = 21,
    FP_PR1 = 126, FP_PL1 = 130, FP_ALG = 134, FP_FB, FP_OKS, FP_LFS, FP_LFD, FP_LPMD, FP_LAMD, FP_LKS, FP_LFW,
    FP_LPMS, FP_TRNSP, FP_NAME, FP_SIZE = 155
};

/* the FM6 function settings (a DX7's function mode; each track its own, as each Dexed instance has, saved with the
 * project and the template; project.c):
 * pitch bend range up / down and step (0 = smooth), portamento (PEDAL: while CC 65 is down, ON) and its time
 * (CC 5 sets it) and glissando, then wheel, foot, breath and aftertouch: range and target (bit 0 pitch, 1
 * amplitude, 2 EG bias), Dexed's velocity scaling to the DX7's range, and ENGINE */
enum { FN_PBUP, FN_PBDN, FN_PBSTEP, FN_PMODE, FN_PTIME, FN_GLISS, FN_MWR, FN_MWA, FN_FCR, FN_FCA, FN_BCR, FN_BCA,
       FN_ATR, FN_ATA, FN_VNORM, FN_ENGINE, FM6_NFN };
enum { FM6_MODERN, FM6_MARK1, FM6_OPL };

#define FM6_PACKED 128u                          /* a packed record (VMEM, the 32-voice bank's) */
#define FM6_ON_ALL 0x3Fu
#define FM6_NFAC 24u                             /* F1..F24: the factory patches (fm6_voices.c: melodee_fm6.h's F1..F8,
                                                  * eng_fm6_rom.h's F9..F24), then INIT VOICE */

/* ------------------------------------------------------- the algorithms --- */
/* The 32 algorithms as Dexed (MSFA fm_core.cc) has them, operators in the order they run (the sixth first):
 * bits 0-1 where the output goes (0 = the voice, 1 / 2 = bus A / B), bit 2 adds to it, bits 4-5 the bus that
 * modulates the operator, bits 6 + 7 its own feedback (bit 7 alone: the end of a feedback loop through OP6,
 * algorithms 4 and 6). tests/fm6_test.c checks the modulation graph of each against the DX7 diagrams */
enum { FM6_OB1 = 1, FM6_OB2 = 2, FM6_OADD = 4, FM6_IB1 = 16, FM6_IB2 = 32, FM6_FBIN = 64, FM6_FBOUT = 128 };
static const uint8_t FM6_ALG[32][6] = {
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14},
    {0xc1, 0x11, 0x94, 0x01, 0x11, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0xc1, 0x94, 0x01, 0x14, 0x01, 0x14},
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14},
    {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}, {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14},
    {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}, {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}, {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14},
    {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04},
    {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04}};
static inline __attribute__((always_inline)) int fm6_carrier(uint32_t alg, uint32_t n) { return !(FM6_ALG[alg & 31u][6u - n] & 3u); }   /* OP n: to the voice */
/* bit k: operator k (the sixth first) is a carrier of algorithm a: it writes the voice */
static uint32_t fm6_carriers(uint32_t a)
{
    uint32_t k, c = 0;
    for (k = 0; k < 6u; k++)
        c |= ((FM6_ALG[a & 31u][k] & 3u) ? 0u : 1u) << k;
    return c;
}

/* ----------------------------------------------------- the parts' voices --- */
#if MOD_NATIVE_SIDE                              /* (a native synth module: the firmware's, module_native.h) */
static uint8_t (*mod_imp_fm6_patch)[FP_SIZE + 1u], *mod_imp_fm6_patch_none;
static volatile uint8_t *mod_imp_fm6_pgen, *mod_imp_fm6_lgen;
static uint8_t *mod_imp_fm6_on;
static uint8_t (*mod_imp_fm6_fn)[FM6_NFN];
static uint32_t *mod_imp_fm6_pairs;
#define fm6_patch mod_imp_fm6_patch
#define fm6_patch_none mod_imp_fm6_patch_none
#define fm6_pgen mod_imp_fm6_pgen
#define fm6_lgen mod_imp_fm6_lgen
#define fm6_on mod_imp_fm6_on
#define fm6_fn mod_imp_fm6_fn
#define fm6_pairs (*mod_imp_fm6_pairs)
#else
static uint8_t fm6_patch[NPART][FP_SIZE + 1u];  /* the parts' patches (main loop writes, then fm6_pgen) */
static uint8_t fm6_patch_none[FP_SIZE + 1u];     /* a MIDI-only track's: read, written, never heard */
static volatile uint8_t fm6_pgen[NTRK];          /* +1 after each write of fm6_patch[t] */
static volatile uint8_t fm6_lgen[NTRK];          /* +1 when the write was a new voice (a load): the notes stop */
static uint8_t fm6_on[NTRK];                     /* the operator switches (bit k: the sixth first), all on at a load */
static uint8_t fm6_fn[NTRK][FM6_NFN];            /* per track (fm6_fn_reset at power-on, a new project) */
#if MELODEE_DUAL_CORE
static uint32_t fm6_pairs;                       /* FM6's voices paired on CPU1 outside the queue (the CPU lab's count) */
#endif
#endif
static uint8_t *fm6_patch_of(uint32_t k) { return k < NPART ? fm6_patch[k] : fm6_patch_none; }
static inline __attribute__((always_inline)) int fm6_op_on(const track_t *t, uint32_t k) { return (fm6_on[(uint32_t)(t - trk) % NTRK] >> k) & 1u; }
