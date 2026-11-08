/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A native synth module's compile (firmware/src/module_native.h): the firmware's headers as firmware/src/melodee.c
 * sets them up, its data the import table's (MOD_NATIVE_SIDE). A module is a built-in engine's own source:
 *     #include "mod_native.h"
 *     #include "eng_x.c"
 *     static x_part_t *x_part(uint32_t part) { return mod_part_mem(part, sizeof(x_part_t)); }   (engines.c's)
 *     MOD_NATIVE_EXPORT(mod_native_x) = {MOD_NATIVE_HEAD, .engine = N, .name = "X", .npresets = .., .eng = &ENG_X};
 * tools/mod_build.py --native builds it with the image's flags (tools/build.py: beside the image, for its board);
 * host tests compile the same file natively (no section, the descriptor by its name). */
#pragma once
#define MOD_NATIVE_SIDE 1
#include "fm1_cc.h"                              /* BOARD_SMK37 */
#include "build_options.h"
#ifndef MELODEE_FLASH                            /* (melodee.c's defaults: KIT_REC's, the module never uses it) */
#define MELODEE_FLASH 1
#endif
#ifndef MELODEE_USB_AUDIO
#define MELODEE_USB_AUDIO 1
#endif
#include <stdint.h>
#include <stddef.h>
#ifdef FM1_IRQ_TARGET
void *memset(void *d, int c, size_t n);          /* (modules/sdk/mod_rt.c: the module carries its own) */
void *memcpy(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
#define __builtin_memset memset
#else
#include <string.h>
#define __attribute__(x)                         /* (as tests/hostsim.c builds the firmware: the same code) */
#endif
#include "melodee_tables.h"
#include "core.h"
#include "dsp.c"
#include "drum_map.c"
#include "drum_kit.h"
#include "cz_parts.h"
#include "module_native.h"
