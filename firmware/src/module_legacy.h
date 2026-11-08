/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* TEMPORARY. The per-synth data the firmware still keeps for the five synths it shipped with (the DRUM kits' patches,
 * hits and recorded kits, the CZ-1's tones, FM6's voices and function settings, the Prophet's programs), handed to
 * their modules through the host table (mod_abi3.h mod_host_v3.legacy) until the patch blob makes it the modules' own.
 * This block is laid out as this firmware has it: a module that reads it names MOD3_LEGACY_KEY in its descriptor and
 * file head, and a firmware with another key leaves it unplayed (module_store.c: MS_STALE, as a native ABI 2 module
 * was). Nothing else of ABI 3 depends on it: delete it in one move.
 * Both sides: after core.h, drum_map.c, drum_kit.h, cz_parts.h, fm6_parts.h and p5_parts.h. */
#pragma once
typedef struct mod_legacy_v3 {
    drum_patch_t *lg_drum_patch, *lg_drum_patch_none;  /* drum_patch[NPART] (core.h) */
    int32_t *lg_drum_event_pitch;                   /* drum_map.c: the hit being started */
    uint32_t *lg_drum_event_length;
    volatile uint8_t *lg_drum_flash;                /* drum_flash[NTRK]: the pads' lights */
    drk_user_t *lg_drk_user;                        /* drk_user[DK_NUSER] (kit_store.c keeps it) */
    volatile uint32_t *lg_drk_user_gen;
    cz_patch_t *lg_cz_patch, *lg_cz_patch_none;        /* cz_parts.h */
    uint8_t (*lg_fm6_patch)[FP_SIZE + 1u];          /* fm6_parts.h: the voices, their write counts, the operator */
    uint8_t *lg_fm6_patch_none;                     /* switches, the function settings, the CPU lab's pair count */
    volatile uint8_t *lg_fm6_pgen, *lg_fm6_lgen;
    uint8_t *lg_fm6_on;
    uint8_t (*lg_fm6_fn)[FM6_NFN];
    uint32_t *lg_fm6_pairs;
    p5_patch_t *lg_p5_patch;                        /* p5_parts.h */
    uint8_t *lg_p5_ready;
} mod_legacy_v3;
#define MLK(i, v) ((uint32_t)(v) * (0x9E3779B1u * (uint32_t)(i) | 1u) + (uint32_t)(i) * 0x85EBCA77u)
#define MOD3_LEGACY_KEY (MLK(1, sizeof(mod_legacy_v3)) + MLK(2, sizeof(drum_patch_t)) + MLK(3, sizeof(drk_user_t)) +      \
    MLK(4, DK_NUSER) + MLK(5, DRK_KSEC) + MLK(6, sizeof(cz_patch_t)) + MLK(7, FP_SIZE) + MLK(8, FM6_NFN) +                 \
    MLK(9, sizeof(p5_patch_t)) + MLK(10, NPART) + MLK(11, NTRK) + MLK(12, BOARD_SMK37) + MLK(13, MELODEE_USER_KITS) +   \
    MLK(14, sizeof(void *)) + 1u)                /* (+ 1: never 0, a module that reads no legacy data names 0) */
