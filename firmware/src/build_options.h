/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Shared by the application and assembly startup. Explicit build overrides win. */
#ifndef MELODEE_BUILD_OPTIONS_H
#define MELODEE_BUILD_OPTIONS_H
#ifndef MELODEE_DUAL_CORE
#define MELODEE_DUAL_CORE 1     /* paired FM6/Prophet kernels; serial fallback if CPU1 is unavailable */
#endif
#ifndef MELODEE_CORE1_FALLBACK
#define MELODEE_CORE1_FALLBACK 1 /* 0: diagnose CPU1 failures by halting for the watchdog, never render alone */
#endif
#if MELODEE_CORE1_FALLBACK != 0 && MELODEE_CORE1_FALLBACK != 1
#error "MELODEE_CORE1_FALLBACK must be 0 or 1"
#endif
#endif
