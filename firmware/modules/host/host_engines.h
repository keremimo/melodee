/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Host builds' reference engines: the synths Melodee ships as modules (../shipped.h), compiled into a host test's
 * firmware as built-in engines, so every host test plays them and the module tests hold the modules to them (the
 * device's image has none: eng_module.c plays them from their files). core.h includes this (MELODEE_HOST_ENGINES,
 * every build that is not the device's), engines.c includes host_engines.c. A host test leaves one out with
 * -DMELODEE_<X>_BUILTIN=0 (tests/run_tests.sh module_tests: the device's configuration). */
#pragma once
#include "../shipped.h"
#ifndef MELODEE_SID_BUILTIN
#define MELODEE_SID_BUILTIN 1
#endif
#ifndef MELODEE_DRUM_BUILTIN
#define MELODEE_DRUM_BUILTIN MELODEE_SID_BUILTIN
#endif
#ifndef MELODEE_CZ_BUILTIN
#define MELODEE_CZ_BUILTIN MELODEE_SID_BUILTIN
#endif
#ifndef MELODEE_FM6_BUILTIN
#define MELODEE_FM6_BUILTIN MELODEE_SID_BUILTIN
#endif
#ifndef MELODEE_P5_BUILTIN
#define MELODEE_P5_BUILTIN MELODEE_SID_BUILTIN
#endif
#define MELODEE_PROPHET 1
/* the engines one can pick among them (core.h NENG_SHOWN): the built-in ones and, in reference builds, the retired
 * PHASE VOICE TRIO WHEEL PHYS NOISE */
#define HOST_NENG (MELODEE_P5_BUILTIN + MELODEE_FM6_BUILTIN + MELODEE_CZ_BUILTIN + MELODEE_SID_BUILTIN + \
                   MELODEE_DRUM_BUILTIN + 6u * MELODEE_LEGACY_EXTRAS)
/* schema.c writes the built-in engines' SCHM chunks from their descriptions */
#define HOST_SCHEMAS (MELODEE_P5_BUILTIN || MELODEE_CZ_BUILTIN || MELODEE_FM6_BUILTIN || MELODEE_DRUM_BUILTIN || \
                      MELODEE_SID_BUILTIN || MELODEE_LEGACY_EXTRAS)
/* presets.c: the factory presets' patches from the built-in engines' makers */
#define HOST_MAKERS (MELODEE_P5_BUILTIN || MELODEE_CZ_BUILTIN || MELODEE_FM6_BUILTIN)
/* engines.c eng_state_size: the retired engines' part memory (reference builds: WHEEL's drawbars, PHASE's CZ core) */
#if MELODEE_LEGACY_EXTRAS
#define HOST_STATE_SIZES case ENGI_WHEEL: return sizeof(drw_part_t); \
                         case 2: return sizeof(cz_part_t);
#else
#define HOST_STATE_SIZES
#endif
/* engine e built in (eng_module_main.c mod_shipped_ok: a built-in synth needs no module) */
#define HOST_BUILTIN(e) (((e) == ENGI_SID && MELODEE_SID_BUILTIN) || ((e) == ENGI_DRUM && MELODEE_DRUM_BUILTIN) || \
                         ((e) == ENGI_CZ && MELODEE_CZ_BUILTIN) || ((e) == ENGI_FM6 && MELODEE_FM6_BUILTIN) || \
                         ((e) == ENGI_PROPHET && MELODEE_P5_BUILTIN))
