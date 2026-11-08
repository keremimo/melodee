/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#pragma once
#define A4_MIN 400
#define A4_MAX 480
#define A4_DEFAULT 440
/* Device preference, independent of a project's cents offset (G_TUNE). */
#if defined(MOD_NATIVE_SIDE) && MOD_NATIVE_SIDE   /* (a native synth module: the firmware's, core.h) */
static int16_t *mod_imp_tuning_a4;
#define tuning_a4 (*mod_imp_tuning_a4)
#else
static int16_t tuning_a4 = A4_DEFAULT;
#endif
