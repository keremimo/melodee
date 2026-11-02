/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Jacek Mariański */
/* The interrupt calls the radio's headers (fm1_ble.h) and Optimist's driver make, for ble.o (src/ble_unit.c): the
 * same registers as hal/fm1_irq.h, without the objects that file defines for melodee.o (fm1_crash, the fault
 * vectors), which a second unit must not define again. */
#pragma once
#include <stdint.h>
#define FM1_VEC ((volatile uint32_t *)0x01C7FE00u)
#define FM1_ICFG(n) (*(volatile uint32_t *)(0x1EEF100u + 4u * ((n) >> 3)))
static inline void fm1_irq_attach(uint32_t n, void (*h)(void), uint32_t prio)   /* IRQs off */
{
    uint32_t sh = (n & 7u) * 4u;
    FM1_VEC[n] = (uint32_t)(uintptr_t)h;
    FM1_ICFG(n) = (FM1_ICFG(n) & ~(0xFu << sh)) | ((((prio & 7u) << 1) | 1u) << sh);
}
static inline uint32_t fm1_icfg(void) { uint32_t v; __asm__ volatile("%0 = icfg" : "=r"(v)); return v; }
static inline void fm1_irq_off(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void fm1_irq_on(void) { __asm__ volatile("csync\n\tsti" ::: "memory"); }
