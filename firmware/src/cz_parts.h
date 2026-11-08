/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The parts' CZ-1 tones (native Casio tones, cz_patch.h): the firmware's data, written by the stores, the editors and
 * the panel, read by the CZ-1 engine (eng_cz.c: a synth module on the device, firmware/modules/cz) */
#pragma once
#include "cz_patch.h"
#if MOD_NATIVE_SIDE                              /* (a native synth module: the firmware's, module_native.h) */
static cz_patch_t *mod_imp_cz_patch, *mod_imp_cz_patch_none;
#define cz_patch mod_imp_cz_patch
#define cz_patch_none (*mod_imp_cz_patch_none)
#else
static cz_patch_t cz_patch[NPART] __attribute__((section(".pool"))), cz_patch_none;
#endif
/* track k's CZ-1 tone; a MIDI-only track's is none: read, written, never heard */
static cz_patch_t *cz_patch_of(uint32_t k) { return k < NPART ? &cz_patch[k] : &cz_patch_none; }
#if !MOD_NATIVE_SIDE
static void cz_init(void) { for (uint32_t k = 0; k <= NPART; k++) cz_patch_init(cz_patch_of(k)->raw); }
#endif
