/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#pragma once
#include <stdint.h>
#include "../src/build_options.h"

/* Carried across soft resets (a crash, the watchdog, an update), lost at
 * power-off; nothing goes to flash on a unit that may be unstable. Once CPU1
 * was retired, or CPU0 crashed with CPU1 working (its data can go bad too,
 * where no check reaches), every boot until power-off renders on one core. */
#define FM1_CORE1_BAR_MAGIC 0x52414231u      /* "1BAR" */
typedef struct { uint32_t magic, barred; } fm1_core1_bar_t;
/* Not static, as fm1_crash: the compiler must not assume a zero start. */
fm1_core1_bar_t fm1_core1_bar __attribute__((section(".noinit")));
static inline int fm1_core1_barred(void)
{
    return fm1_core1_bar.magic == FM1_CORE1_BAR_MAGIC && fm1_core1_bar.barred == 1u;
}
static inline void fm1_core1_bar_set(void)
{
    fm1_core1_bar.magic = FM1_CORE1_BAR_MAGIC;
    fm1_core1_bar.barred = 1;
}

/* No-fallback diagnostics ignore saved retirement and early-boot failures,
 * but never start CPU1 after a failed supply setup. */
static inline int fm1_core1_boot_allowed(uint32_t power_ready, uint32_t boot_failed)
{
#if MELODEE_CORE1_FALLBACK
    return power_ready && !boot_failed && !fm1_core1_barred();
#else
    (void)boot_failed;
    return power_ready != 0;
#endif
}
