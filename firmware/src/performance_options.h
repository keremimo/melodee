/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Production performance defaults. The build tool reads these same values for
 * compiler flags, resident-code placement and linking. Explicit 0/1 overrides
 * retain the reference paths for diagnostics and parity checks. */
#ifndef MELODEE_PERFORMANCE_OPTIONS_H
#define MELODEE_PERFORMANCE_OPTIONS_H
#ifndef MELODEE_RAM_AUDIO
#define MELODEE_RAM_AUDIO 1
#endif
#ifndef MELODEE_CACHE_RAM
#define MELODEE_CACHE_RAM 1
#endif
#ifndef MELODEE_MOD_CACHE
#define MELODEE_MOD_CACHE 1
#endif
#ifndef MELODEE_CONTROL_FAST
#define MELODEE_CONTROL_FAST 1
#endif
#ifndef MELODEE_MIX_FAST
#define MELODEE_MIX_FAST 1
#endif
#ifndef MELODEE_FX_DORMANT
#define MELODEE_FX_DORMANT 1
#endif
#ifndef MELODEE_RECORD_INDEX
#define MELODEE_RECORD_INDEX 1
#endif
#ifndef MELODEE_PROPHET_FAST
#define MELODEE_PROPHET_FAST 1
#endif
#ifndef MELODEE_PANEL_FAST
#define MELODEE_PANEL_FAST 1
#endif
#ifndef MELODEE_FM6_FAST
#define MELODEE_FM6_FAST 1
#endif
#ifndef MELODEE_CZ_FAST
#define MELODEE_CZ_FAST 1
#endif
#ifndef MELODEE_DRUM_FAST
#define MELODEE_DRUM_FAST 1
#endif
#ifndef MELODEE_SID_FAST
#define MELODEE_SID_FAST 1
#endif
#ifndef MELODEE_CAPTURE_FAST
#define MELODEE_CAPTURE_FAST 1
#endif
/* audio.c: each half is rendered one half ahead of the DMA, so a render has two halves to finish in
 * (one half more latency, 2.9 ms) */
#ifndef MELODEE_RENDER_AHEAD
#define MELODEE_RENDER_AHEAD 1
#endif
#endif
