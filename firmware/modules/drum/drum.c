/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* DRUM (engine 10) as a native synth module: firmware/src/eng_drum.c as it was built in, with the 808 (drum_808.c),
 * the 909 (x0x/), KIT SID (drum_sid.c on its own copy of eng_sid.c's chip), CR78 and FM (drum_lanes.c) and the user
 * kits (drum_real.c, reading the firmware's drk_user). tools/mod_build.py --native */
#include "mod_native.h"
#ifndef FM1_IRQ_TARGET
/* (x0x/drum909.h's functions are external: on the host the module's own under names of their own, so a test links it
 * beside a firmware that has DRUM built in; the device's build keeps them, tools/size_fns.py knows them by name) */
#define drum909_init mod_drum909_init
#define drum909_trigger mod_drum909_trigger
#define drum909_render mod_drum909_render
#define drum909_render_st mod_drum909_render_st
#define drum909_pan_settle mod_drum909_pan_settle
#define drum909_nparams mod_drum909_nparams
#define drum909_param mod_drum909_param
#define drum909_set mod_drum909_set
#define drum909_get mod_drum909_get
#define drum909_active mod_drum909_active
#endif
#include "eng_sid.c"                             /* (KIT SID's chip: the module's own copy) */
#include "eng_drum.c"

/* the part's working memory as engines.c hands it to the built-in engine */
static sid_part_t *sid_part(uint32_t part) { return mod_part_mem(part, sizeof(sid_part_t)); }
static drum909_part_t *drum909_part(uint32_t part) { return mod_part_mem(part, sizeof(drum909_part_t)); }
static drum_lane_t *drum_kit_part(uint32_t part) { return mod_part_mem(part, sizeof(drum_lane_t) * DV_NLANE); }
static dsk_part_t *dsk_part(uint32_t part) { return mod_part_mem(part, sizeof(dsk_part_t)); }
static dlk_part_t *dlk_part(uint32_t part) { return mod_part_mem(part, sizeof(dlk_part_t)); }
#if MELODEE_USER_KITS
static drk_part_t *drk_part(uint32_t part) { return mod_part_mem(part, sizeof(drk_part_t)); }
#endif

MOD_NATIVE_EXPORT(mod_native_drum) = {
    MOD_NATIVE_HEAD, .engine = ENGI_DRUM, .name = "DRUM", .npresets = DRUM_NPRESETS, .eng = &ENG_DRUM,
};
