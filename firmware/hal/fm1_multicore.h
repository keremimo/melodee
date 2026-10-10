/* SPDX-License-Identifier: GPL-3.0-only */
/* Bare-metal AC791N/WL82 CPU1 launch, following AC79 SDK system.a/port.c.o
 * EnableOtherCpu and cpu.a/startup.S.o (SDK 1.2.1). No OS library required.
 * Internal SRAM is shared; only SDRAM uses the SDK's uncached alias. csync
 * plus compiler barriers publish SRAM state between the two cores. */
#pragma once
#include <stdint.h>
typedef volatile uint32_t fm1_aw_word;
#define AW_WORD fm1_aw_word
static inline __attribute__((always_inline)) uint32_t fm1_aw_load(volatile uint32_t *p)
{
    uint32_t value = *p;
    __asm__ volatile("csync" ::: "memory");
    return value;
}
static inline __attribute__((always_inline)) void fm1_aw_store(volatile uint32_t *p, uint32_t value)
{
    __asm__ volatile("csync" ::: "memory");
    *p = value;
    __asm__ volatile("csync" ::: "memory");
}
#define AW_LOAD(p) fm1_aw_load(p)
#define AW_STORE(p,v) fm1_aw_store(p,v)
static inline __attribute__((always_inline)) uint32_t fm1_aw_ticks(void) { return FM1_T4_CNT; }
#define AW_TICKS() fm1_aw_ticks()
#define AW_TIMEOUT_TICKS (10000u * FM1_TICKS_PER_US)
#define AW_HOLD() fm1_core1_stop()
#define AW_FAULTS() fm1_core1_fault.count
/* A misread answered: the core supply goes up to FM1_SYSVDD_BOOST
 * (fm1_power.h). The audio ISR asks; the main loop's fm1_core1_supply_poll
 * owns P33. Misreads seen until then are forgiven, any later one retires the
 * worker. Started on the boost already (fm1_carry), the first one retires it. */
static volatile uint8_t fm1_core1_boost;     /* 0, 1 asked (audio ISR), 2 applied (main loop) */
static uint32_t fm1_core1_forgiven;          /* misreads up to the boost */
#if MELODEE_CORE1_TEST
static int core1_test_count_only;
#endif
static int fm1_core1_misread(uint32_t misreads)
{
#if MELODEE_CORE1_TEST
    if (core1_test_count_only)
        return 1;
#endif
    if (fm1_core1_boost == 0u && fm1_power_get_cached(FM1_RAIL_SYSVDD) < FM1_SYSVDD_BOOST)
        fm1_core1_boost = 1;
    return fm1_core1_boost == 1u || misreads <= fm1_core1_forgiven;
}
#define AW_MISREAD(n) fm1_core1_misread(n)
#define AW_RAM_LOOP __attribute__((section(".dsp_text"), noinline, noreturn, used))
#include "audio_worker.h"
#undef AW_RAM_LOOP
#undef AW_MISREAD
#undef AW_FAULTS
#undef AW_HOLD
#undef AW_TIMEOUT_TICKS
#undef AW_TICKS
#undef AW_STORE
#undef AW_LOAD
#undef AW_WORD

static uint32_t cpu1_stacks[2048] __attribute__((section(".cpu1_stacks"), aligned(32), used));
extern void fm1_core1_start(void);
extern uint8_t _cpu1_stacks_lo[], _cpu1_stacks_hi[];
/* Assembly names this symbol; the full polling body must be in RAM. */
void __attribute__((section(".dsp_text"), noreturn, used)) fm1_core1_main(void)
{
    /* Same combined-stack limit used on CPU0, at CPU1's EMU bank. Reserve
     * the bottom 256 bytes so a descending stack traps before shared buffers. */
    uint32_t lo = (uint32_t)(uintptr_t)_cpu1_stacks_lo + 256u;
    uint32_t hi = (uint32_t)(uintptr_t)_cpu1_stacks_hi - 1u;
    *(volatile uint32_t *)0x1EEF2D8u = hi;
    *(volatile uint32_t *)0x1EEF2DCu = lo;
    *(volatile uint32_t *)0x1EEF2E0u = hi;
    *(volatile uint32_t *)0x1EEF2E4u = lo;
    *(volatile uint32_t *)0x1EEF2D0u = (*(volatile uint32_t *)0x1EEF2D0u & ~((1u << 2) | (0x1Fu << 16))) | (1u << 3);
    audio_worker_loop();
}

static int fm1_multicore_start(void)
{
    uint32_t start, div;
    fm1_core1_stop();
    audio_worker_online = 0;
    fm1_aw_store(&audio_worker.ready, 0);
    fm1_aw_store(&audio_worker.request, 0);
    fm1_aw_store(&audio_worker.complete, 0);
    fm1_aw_store(&audio_worker.rejected, 0);
    /* CPU1's IRQ bank (CPU0 is at 0x1EEF100); worker stays IRQ-disabled. */
    for (uint32_t i = 0; i < 32u; i++)
        *(volatile uint32_t *)(0x1EEF300u + 4u * i) = 0;
    *(volatile uint32_t *)0x01C7FFF8u = (uint32_t)(uintptr_t)fm1_core1_start;
    div = *(volatile uint32_t *)0x10008u;
    *(volatile uint32_t *)0x10008u = div | 8u;
    __asm__ volatile("csync" ::: "memory");
    *(volatile uint32_t *)0x1EEE004u |= 8u;
    *(volatile uint32_t *)0x1EEE004u &= ~2u;
    start = fm1_ticks();
    while (!fm1_aw_load(&audio_worker.ready) && (uint32_t)(fm1_ticks() - start) < 10000u * FM1_TICKS_PER_US)
        ;
    audio_worker_online = fm1_aw_load(&audio_worker.ready) != 0;
    if (!audio_worker_online) fm1_core1_stop();
    *(volatile uint32_t *)0x10008u = div;
    return audio_worker_online;
}

/* main loop: apply a boost the audio ISR asked for (fm1_core1_misread); a
 * CPU1 fault or stall, which retired the worker at once, asks for it too.
 * Both carry to the next boot (fm1_carry_failed): a retirement on the highest
 * level keeps CPU1 out until power-off, any other starts it on the boost. */
static int8_t fm1_core1_was_online = -1;
static void fm1_core1_supply_poll(void)
{
    if (fm1_core1_was_online < 0)
        fm1_core1_was_online = (int8_t)audio_worker_online;
    if (fm1_core1_was_online && !audio_worker_online) {
        fm1_core1_was_online = 0;
        fm1_carry_failed(1);                  /* (before this boost: the level it retired on) */
    }
    if (!fm1_core1_boost && (fm1_core1_fault.count || audio_worker.timeouts) &&
        fm1_power_get_cached(FM1_RAIL_SYSVDD) < FM1_SYSVDD_BOOST)
        fm1_core1_boost = 1;
    if (fm1_core1_boost != 1u)
        return;
    fm1_irq_off();
    fm1_power_core(FM1_SYSVDD_BOOST);
    fm1_carry.sysvdd = FM1_SYSVDD_BOOST;
    fm1_core1_forgiven = fm1_aw_load(&audio_worker.rejected);
    fm1_core1_boost = 2;
    fm1_irq_on();
}
