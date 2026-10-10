/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 supply rails (P33 analog controls, as the AC79 SDK's p33.h), for the
 * dual-core build: the FM-1 runs the AC79 at 360 MHz on the boot loader's
 * 1.26 V core rail (BUILDING.md, "Units whose CPU1 misreads SRAM").
 *
 *   fm1_power_init()        boot, IRQs off, before CPU1 starts: records the
 *                           levels the boot loader left, then raises the core
 *                           rails to FM1_SYSVDD_LEVEL, or to where the last run
 *                           ended (fm1_carry)
 *   fm1_power_core(s)       SYSVDD to s and VDC14 with it, never lower (IRQs off)
 *   fm1_power_set(rail, l)  one rail to level l, a step at a time (IRQs off)
 *   fm1_power_get(rail)     P33 read; fm1_power_get_cached: no P33 (any context)
 *   fm1_carry_failed(dual)  a crash or a retired CPU1 worker: the next boot
 *                           starts on the raised supply, or on one core
 *
 * Levels (SDK p33.h): SYSVDD, the core and SRAM rail (isd_config "DVDD"),
 * 0.93 V + 30 mV a step: 9 1.20 V, 11 1.26 V, 14 1.35 V, 15 1.38 V.
 * VDC14 ("DCDC14") 1.25 V + 50 mV a step: 3 1.40 V, 4 1.45 V, 5 1.50 V. VDDIO
 * 2.8 V + 100 mV a step: 4 3.2 V.
 */
#pragma once
#include <stdint.h>
#include "fm1_sys.h"
#include "fm1_time.h"

enum { FM1_RAIL_SYSVDD, FM1_RAIL_VDC14, FM1_RAIL_VDDIO, FM1_RAILS };
static const uint8_t fm1_rail_reg[FM1_RAILS] = { 0x09, 0x06, 0x05 };   /* P3_ANA_CON9, 6, 5 */
static const uint8_t fm1_rail_max[FM1_RAILS] = { 15, 7, 7 };            /* field masks [3:0], [2:0], [2:0] */

#ifndef FM1_SYSVDD_LEVEL
#define FM1_SYSVDD_LEVEL 14u          /* 1.35 V, JieLi's profile above 320 MHz */
#endif
#ifndef FM1_SYSVDD_BOOST
#define FM1_SYSVDD_BOOST 15u          /* 1.38 V, the highest: after CPU1 trouble or a crash */
#endif
#define FM1_VDC14_FOR(s) ((s) - 10u)  /* 14: 4 1.45 V, 15: 5 1.50 V, 100 mV and more over SYSVDD */

static struct { uint8_t boot[FM1_RAILS], now[FM1_RAILS]; } fm1_power;

/* Carried across soft resets (a crash, the watchdog, an update), lost at
 * power-off; nothing is written to flash on a unit that may be unstable. */
#define FM1_CARRY_MAGIC 0x31525750u   /* "PWR1" */
typedef struct { uint32_t magic; uint8_t sysvdd, single, pad[2]; } fm1_carry_t;
/* Not static, as fm1_crash: the compiler must not assume a zero start. */
fm1_carry_t fm1_carry __attribute__((section(".noinit")));

static uint32_t fm1_power_get(uint32_t rail)
{
    return fm1_p33_read(fm1_rail_reg[rail]) & fm1_rail_max[rail];
}
static inline uint32_t fm1_power_get_cached(uint32_t rail) { return fm1_power.now[rail]; }

/* Steps of one level, 100 us apart: the regulators slew without a current
 * surge. Interrupts off: P33 access is not reentrant. */
static void fm1_power_set(uint32_t rail, uint32_t level)
{
    uint32_t now = fm1_power_get(rail), reg = fm1_rail_reg[rail], mask = fm1_rail_max[rail];
    if (level > mask)
        level = mask;
    while (now != level) {
        now += now < level ? 1u : (uint32_t)-1;
        fm1_p33_write(reg, (uint8_t)((fm1_p33_read(reg) & ~mask) | now));
        fm1_delay_us(100);
    }
    fm1_power.now[rail] = (uint8_t)now;
}

/* VDC14 first going up, so SYSVDD never runs close to it */
static void fm1_power_core(uint32_t sysvdd)
{
    if (fm1_power.now[FM1_RAIL_VDC14] < FM1_VDC14_FOR(sysvdd))
        fm1_power_set(FM1_RAIL_VDC14, FM1_VDC14_FOR(sysvdd));
    if (fm1_power.now[FM1_RAIL_SYSVDD] < sysvdd)
        fm1_power_set(FM1_RAIL_SYSVDD, sysvdd);
}

static void fm1_power_init(void)
{
    uint32_t i;
    if (fm1_carry.magic != FM1_CARRY_MAGIC || fm1_carry.sysvdd > FM1_SYSVDD_BOOST || fm1_carry.single > 1u) {
        fm1_carry.magic = FM1_CARRY_MAGIC;
        fm1_carry.sysvdd = FM1_SYSVDD_LEVEL;
        fm1_carry.single = 0;
    }
    for (i = 0; i < FM1_RAILS; i++)
        fm1_power.boot[i] = fm1_power.now[i] = (uint8_t)fm1_power_get(i);
    fm1_power_core(fm1_carry.sysvdd > FM1_SYSVDD_LEVEL ? fm1_carry.sysvdd : FM1_SYSVDD_LEVEL);
    fm1_delay_us(1000);
}

/* A crash (dual: CPU1 was working), or the worker retired (dual: 1). At the
 * highest level already, CPU1 sits out every boot until power-off; below it,
 * the next boot starts there. */
static void fm1_carry_failed(int dual)
{
    if (dual && fm1_power.now[FM1_RAIL_SYSVDD] >= FM1_SYSVDD_BOOST)
        fm1_carry.single = 1;
    if (fm1_carry.sysvdd < FM1_SYSVDD_BOOST)
        fm1_carry.sysvdd = FM1_SYSVDD_BOOST;
}

#if MELODEE_CORE1_TEST
/* Test builds: the clock tree as the boot loader set it (SDK WL82.h: JL_CLOCK
 * SYS_DIV, CLK_CON0..3) and a measured CPU clock: 64 dependent adds a pass
 * from a warm instruction cache, against TIMER4. */
static void fm1_clock_regs(uint32_t *v)   /* 5 words */
{
    uint32_t i;
    for (i = 0; i < 5u; i++)
        v[i] = *(volatile uint32_t *)(0x10008u + 4u * i);
}

static __attribute__((noinline)) uint32_t fm1_cpu_adds(uint32_t passes)
{
    uint32_t x = 0;
    while (passes--)
        __asm__ volatile(".rept 64\n\t%0 = %0 + 1\n\t.endr" : "+r"(x));
    return x;
}

static uint32_t fm1_cpu_ticks(void)          /* IRQs off: TIMER4 ticks for 4000 x 64 adds */
{
    uint32_t t;
    fm1_cpu_adds(4);
    t = fm1_ticks();
    fm1_cpu_adds(4000);
    return fm1_ticks() - t;
}
#endif
