/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A kit synth module's sources (808, 909, SID KIT, CR78, FM KIT, SAMPLER: firmware/modules/<name>/<name>.c): the kits'
 * engine (eng_drum.c) with DRUM_ONLY set to the module's one kit, so the other kits' code folds away. KIT SID plays the
 * SID's chip (../sid/eng_sid.c: the module's own copy). tools/mod_build.py */
#ifndef DRUM_ONLY
#error "a kit module names its kit: DRUM_ONLY"
#endif
#define D9_API static                    /* (x0x/drum909.c's API: this module's own; another kit's code drops it) */
#include "../sid/eng_sid.c"                             /* (KIT SID's chip: the module's own copy) */
#include "eng_drum.c"

/* the part's working memory for SID's chip (eng_drum.c has its kits') */
static sid_part_t *sid_part(uint32_t part) { return mod_part_mem(part, sizeof(sid_part_t)); }
