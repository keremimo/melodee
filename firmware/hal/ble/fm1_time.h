/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Jacek Mariański */
/* TIMER4, the 24 MHz free-running counter (hal/fm1_time.h sets it up; ble.o only reads it) */
#pragma once
#include <stdint.h>
#define FM1_T4_CNT (*(volatile uint32_t *)0x10804u)
#define FM1_TICKS_PER_US 24u
static inline __attribute__((always_inline)) uint32_t fm1_ticks(void) { return FM1_T4_CNT; }
static inline __attribute__((always_inline)) void fm1_delay_us(uint32_t us)   /* (inline: also inside .ram_text) */
{
    uint32_t t0 = FM1_T4_CNT, n = us * FM1_TICKS_PER_US;
    while (FM1_T4_CNT - t0 < n)
        ;
}
