/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The CZ-1's patch: the part's bytes are a native Casio tone (cz_patch.h: 128 synthesis bytes, the 16-byte LCD name;
 * what Melodee 1.x stored), read by the engine where the firmware keeps it (mod_patch) */
#pragma once
#include "cz_patch.h"
#define CZ_FACTORY_N 64u                         /* Casio's tones: the CZ-1's presets after INIT TONE (its module's
                                                  * file; melodee_cz1.h on host builds) */
static cz_patch_t *cz_tone(const track_t *t) { return (cz_patch_t *)(void *)mod_patch(t); }
