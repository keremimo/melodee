/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The user drum kits' records (KIT 1..8, values 9..16): their layout, the codec's tables, the slots' sectors. The
 * firmware writes and checks them (kit_store.c, kit_take.c, drum_enc.c) and names them (drk_user_name); DRUM plays
 * them (drum_real.c, in the DRUM module on the device), reading drk_user, the firmware's. */
#pragma once
typedef struct {
    uint16_t at, n;                              /* its first slice (8 bytes each), samples */
    uint16_t loop_at, loop_slice;                /* the sample its tail loops back to (0: one shot), that sample's slice */
    uint16_t inc;                                /* samples per output sample, Q15 */
    uint8_t dark, st;                            /* a looped tail's darkening per block (Q12); its predictor (loops[]) */
} drk_sample_t;
typedef struct {
    uint8_t sample, alt;                         /* the take (0xFF: an empty pad), the other (a closed hat's alternate, */
    int8_t st, tone;                             /* the swapped kick; 0xFF none); semitones; TONE's */
    uint16_t out, ms, pivot;                     /* offset, as a knob's; level (Q12); -40 dB (ms, DECY 64); TONE's Hz */
} drk_map_t;

/* A user kit's record (kit_store.c writes it, any host checks it with drk_kit_check before it plays): this header at
 * the start of the first sector's payload, then the slices, two little-endian 32-bit words each, the high one first.
 * Slice k of the payload is at payload byte 8 k: a sector's 4,056 payload bytes hold 507 slices, so none straddles;
 * in flash they sit in two runs, 476 from byte 32, then the 8-byte guard at 0xF00 (storage.c: erased), then 31. */
#define DRK_KIT_MAGIC 0x3154494Bu               /* KIT1 */
#define DRK_NDRUM 11u                            /* the 909's eleven drums (D9_*) */
#define DRK_NAMELEN 5u                              /* the card's value: a name never cut short */
#define DRK_KSEC 7u                              /* a kit's sectors at most: 28,392 bytes */
#define DRK_SECBYTES 4056u                       /* a list record's payload per sector (storage.c ST_LIN_PAYLOAD_MAX) */
#define DRK_SECOFF 32u                           /* where it starts (ST_POOL_PAYLOAD_OFF) */
#define DRK_SECSLICES (DRK_SECBYTES / 8u)        /* 507 */
#define DRK_RUN0 476u                            /* the slices before the guard (storage.c ST_RUN0 / 8) */
#define DRK_SLOTS (DRK_SECSLICES + 1u)           /* 8-byte places from byte 32 to the sector's end, the guard's one too */
/* slice j of a sector: its words from the sector's payload start (the guard's place skipped) */
#define DRK_SLOT(j) (2u * ((j) + ((j) >= DRK_RUN0)))
typedef struct {
    uint32_t magic;
    uint16_t bytes;                              /* this header's bytes: the slices begin there (a multiple of 8) */
    uint8_t nsmp, ndrum;                         /* takes, drums (DRK_NDRUM) */
    char name[12];                               /* DRK_NAMELEN characters at most, zeros after */
    drk_map_t map[DRK_NDRUM];
    drk_sample_t smp[DRK_NDRUM];                 /* .at counts slices from the payload's start */
    int32_t loops[DRK_NDRUM][8];                 /* a looped take's predictor at its loop (h, w): .st indexes it */
} drk_kit_t;
_Static_assert(sizeof(drk_map_t) == 10u && sizeof(drk_sample_t) == 12u && sizeof(drk_kit_t) == 616u &&
               __builtin_offsetof(drk_kit_t, map) == 20u && __builtin_offsetof(drk_kit_t, smp) == 130u &&
               __builtin_offsetof(drk_kit_t, loops) == 264u, "the user kit header: the record's own layout");
#define DRK_KIT_BYTES ((uint32_t)(sizeof(drk_kit_t) + 7u) & ~7u)
#define DRK_KIT_MAX (DRK_KSEC * DRK_SECBYTES)

/* Each user slot's sectors (4 KB numbers; n 0: empty), set by kit_store.c in the main loop. drk_user_gen is odd
 * while it changes them: the audio side then plays nothing, and a part that took a slot under an older number
 * stops reading its sectors before a later save can erase them (kit_store.c waits two blocks). */
typedef struct { uint8_t n, sec[DRK_KSEC]; } drk_user_t;
#if MOD_NATIVE_SIDE                              /* (DRUM as a module: the firmware's, which kit_store.c keeps) */
static drk_user_t *mod_imp_drk_user;
static volatile uint32_t *mod_imp_drk_user_gen;
#define drk_user mod_imp_drk_user
#define drk_user_gen (*mod_imp_drk_user_gen)
#else
static drk_user_t drk_user[DK_NUSER];
static volatile uint32_t drk_user_gen;
#endif
#ifndef DRK_XIP
#ifdef FM1_IRQ_TARGET
#include "fm1_xip.h"
#define DRK_XIP(off) fm1_xip_ptr(off)            /* the data flash reads plain (fl_plain_window_init) */
#else
#define DRK_XIP(off) ((const uint8_t *)0)        /* (host builds without a flash image: no user kits) */
#endif
#endif


/* the codec (drum_enc.c encodes, drum_real.c decodes): samples per slice of each depth, the 16 scales, the levels */
static const uint8_t DRK_NS[4] = {58, 29, 19, 14};
static const uint16_t DRK_SF[16] = {1, 2, 3, 6, 11, 20, 36, 66, 120, 218, 398, 724, 1319, 2400, 4370, 7960};
static const int8_t DRK_LV[4][16] = {
    {5, -5},
    {3, -3, 10, -10},
    {3, -3, 10, -10, 18, -18, 28, -28},
    {2, -2, 6, -6, 10, -10, 14, -14, 18, -18, 22, -22, 26, -26, 30, -30},
};


#if !MOD_NATIVE_SIDE
/* a user slot's name for the KIT card (b: 6 bytes): EMPTY when there is none */
static void drk_user_name(uint32_t u, char *b)
{
    uint32_t g = drk_user_gen;
    str_cpy(b, "EMPTY", 6);
    if (u >= DK_NUSER || (g & 1u) || !drk_user[u].n)
        return;
    const drk_kit_t *H = (const drk_kit_t *)DRK_XIP(drk_user[u].sec[0] * 4096u + DRK_SECOFF);
    char t[DRK_NAMELEN + 1u];
    for (uint32_t i = 0; i < DRK_NAMELEN; i++)
        t[i] = H->name[i];
    t[DRK_NAMELEN] = 0;
    if (drk_user_gen == g)
        str_cpy(b, t, 6);
}
#endif
