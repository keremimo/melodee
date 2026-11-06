/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Retained Melodee worker fault recovery, with the fm1-base platform facade.
 * The legacy CPU0 boot, exception reporter and update code are not included. */
#include "../../firmware/hal/fm1_core1_stop.h"
#define FM1_T4_CNT (*(volatile uint32_t *)0x10804u)
#define FM1_DBG_WR_EN (*(volatile uint32_t *)0x1EEE240u)
#define FM1_DBG_MSG (*(volatile uint32_t *)0x1EEE244u)
#define FM1_DBG_MSG_CLR (*(volatile uint32_t *)0x1EEE248u)
#define FM1_DBG_C0 ((1u<<7)|(1u<<9)|(1u<<12)|(1u<<13)|(7u<<19))
#define FM1_DBG_C1 ((1u<<6)|(1u<<8)|(1u<<10)|(1u<<11)|(7u<<16))
static uint32_t fm1_cnum(void){uint32_t v;__asm__ volatile("%0 = cnum":"=r"(v));return v;}
static __attribute__((noreturn)) void fm1_core1_failure(void){fm1_core1_stop();for(;;);}
static struct { volatile uint32_t count; uint32_t echoes, cpu, dbg, emu, pc, rets; } fm1_core1_fault;

int ml_fault_frame(const uint32_t *f)   /* 1: handled, CPU0 resumes */
{
#if MELODEE_DUAL_CORE
    uint32_t cpu = fm1_cnum(), dbg = FM1_DBG_MSG, was;
    if (!cpu && (dbg & FM1_DBG_C0))
        return 0;                                   /* CPU0's own */
    /* CPU1 took it too and cleared DBG_MSG first. Once per CPU1 fault, so a
     * genuine CPU0 exception with DBG_MSG clear that lands in that window is
     * resumed once; it faults again and is reported then. */
    if (!cpu && !(dbg & FM1_DBG_C1)) {
        if (fm1_core1_fault.echoes >= fm1_core1_fault.count)
            return 0;                               /* else CPU0's own */
        fm1_core1_fault.echoes++;
        return 1;
    }
    if (!cpu && fm1_core1_fault.count >= 16u)
        return 0;                                   /* a hold that did not take */
    if (!cpu)
        fm1_core1_stop();
    fm1_core1_fault.cpu = cpu;
    fm1_core1_fault.dbg = dbg;
    fm1_core1_fault.emu = *(volatile uint32_t *)(cpu ? 0x1EEF2D4u : 0x1EEF0D4u);   /* the faulting core's EMU_MSG */
    fm1_core1_fault.pc = f[16];
    fm1_core1_fault.rets = f[25];
    fm1_core1_fault.count++;                        /* (before DBG_MSG clears: see the echo above) */
    was = FM1_DBG_WR_EN & 1u;                       /* (writing 0xE7 toggles the unlock) */
    if (!was)
        FM1_DBG_WR_EN = 0xE7u;
    FM1_DBG_MSG_CLR = dbg;
    if (!was)
        FM1_DBG_WR_EN = 0xE7u;
    if (cpu) {
        __asm__ volatile("csync" ::: "memory");     /* publish the fault; CPU0 joins and holds us */
        for (;;)
            ;
    }
#if !MELODEE_CORE1_FALLBACK
    fm1_core1_failure();                     /* CPU0 must not resume into serial rendering */
#endif
    return 1;
#else
    (void)f;
    return 0;
#endif
}

#include "../../firmware/hal/fm1_multicore.h"
void ml_worker_stop(void){fm1_core1_stop();}
void ml_worker_crash(void){fm1_core1_bar_set();fm1_core1_stop();}

#define FM1_CLOCK_HOST_TEST 1
#define FCLK_READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#include "../../firmware/hal/fm1_clock.h"
