/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* (From felucca-bsp chip/fm1_clock.h, unchanged but for this line: Melodee includes it in a Bluetooth build only,
 * hal/fm1_clock.h stays read-only otherwise.)
 * FM-1 system clock: as M-VAVE's bootloader leaves it, whichever bootloader started us.
 *
 * Read off FM-1s with fm1-x0x's debug build (tools/fm1_debug.py, 2026-10-23): under M-VAVE's bootloader the CPU
 * runs at 360 MHz (a one-instruction loop 51.43 per us): PLL1 = 24 MHz / 12 * 270 = 540 MHz (0x119A8 0x0940022B,
 * 0x119AC 0x0750310C), the system clock 2/3 of it (0x10014 [7:0] = 6), HSB /2 = 180, LSB /3 = 60 (0x10008
 * 0x00010200), 0x1000C 0x1C1. Under m-bootloader 1.2 the same FM-1 ran at 320 MHz (45.71 per us): 0x10014 = 2,
 * PLL1 off (0x119A8 = 0x119AC = 0); everything else the same, the flash's divider (0x40204 = 1) too. Every firmware
 * was 11 % slower, and the peripherals' clock (the LCD's SPI, the UARTs' dividers, set for 60 MHz) 53 MHz.
 *
 * So at the start: if the clock is not the stock one, PLL1 is started with M-VAVE's values and the system clock
 * moved onto it, the steps in m-bootloader's own clock setup (github.com/jvitkauskas/m-bootloader, MIT,
 * src/platform.c hal_clock_a): the system clock off the PLL mux (0x1000C bit 8: the crystal) while PLL1 starts,
 * the source picked, back onto the mux. From RAM with interrupts off (the flash's clock moves under it); the waits
 * on TIMER4 (the crystal, fm1_time.h), inline. The flash's divider stays as the bootloader set it: the same as
 * under M-VAVE's. Returns 0 nothing to do, 1 set (and reads back stock), -1 set but did not read back stock. */
#pragma once
#include <stdint.h>
#include "fm1_flash.h"                                  /* RAMFN, RAMINL, FL_FAR, REG32 */

#define CLK_HSB_CON   REG32(0x10008u)                   /* [17:16] HSB divider - 1, [10:8] LSB divider - 1 */
#define CLK_MUX_CON   REG32(0x1000Cu)                   /* bit 8: the system clock from the PLL mux */
#define CLK_SYS_CON   REG32(0x10014u)                   /* [7:0] the system clock's source: 6 = PLL1 * 2/3 */
#define CLK_PLL1_CON0 REG32(0x119A8u)                   /* [1:0] enable, run; [6:2] reference divider - 2 */
#define CLK_PLL1_CON1 REG32(0x119ACu)                   /* [11:0] multiplier - 2 */
#define CLK_T4_CNT    REG32(0x10804u)                   /* TIMER4, 24 MHz from the crystal (fm1_time.h) */
#define CLK_PLL0_CON0 REG32(0x119A0u)                   /* the PLL m-bootloader starts before PLL1 (0x45400200, */
#define CLK_PLL0_CON1 REG32(0x119A4u)                   /* 0x3F503026; running, 0x45400203, under M-VAVE's too) */
#define CLK_STOCK_SYS   6u
#define CLK_STOCK_PLL0  0x0940022Bu
#define CLK_STOCK_PLL1  0x0750310Cu
#define CLK_STOCK_HSB   0x00010200u

/* the clock's registers as they are (a report): the system clock's source, the mux, PLL1, the other PLL */
static inline void fm1_clock_read(uint32_t r[6])
{
    r[0] = CLK_SYS_CON & 0xFFu;
    r[1] = CLK_MUX_CON;
    r[2] = CLK_PLL1_CON0;
    r[3] = CLK_PLL1_CON1;
    r[4] = CLK_PLL0_CON0;
    r[5] = CLK_PLL0_CON1;
}

static inline int fm1_clock_is_stock(void)
{
    return (CLK_SYS_CON & 0xFFu) == CLK_STOCK_SYS && CLK_PLL1_CON0 == CLK_STOCK_PLL0 &&
           (CLK_PLL1_CON1 & 0x07FFFFFFu) == CLK_STOCK_PLL1 && (CLK_MUX_CON & (1u << 8));
}

RAMINL void fm1_clock_wait_us(uint32_t us)              /* (inline: RAM code calls nothing) */
{
    uint32_t t0 = CLK_T4_CNT;
    while ((uint32_t)(CLK_T4_CNT - t0) < us * 24u)
        ;
}

static RAMFN void fm1_clock_set_stock_ram(void)
{
    CLK_MUX_CON &= ~(1u << 8);                          /* the system clock on the crystal meanwhile */
    __asm__ volatile("csync" ::: "memory");
    fm1_clock_wait_us(20);
    CLK_PLL1_CON0 = CLK_STOCK_PLL0 & ~3u;               /* PLL1: 24 / 12 * 270 = 540 MHz, stopped */
    CLK_PLL1_CON1 = CLK_STOCK_PLL1;
    CLK_PLL1_CON0 |= 1u;                                /* on */
    fm1_clock_wait_us(100);
    CLK_PLL1_CON0 |= 2u;                                /* running */
    fm1_clock_wait_us(900);                                 /* (its lock, as m-bootloader waits) */
    CLK_SYS_CON = (CLK_SYS_CON & ~0xFFu) | CLK_STOCK_SYS;   /* 2/3 of it: 360 MHz */
    CLK_HSB_CON = (CLK_HSB_CON & ~0x30700u) | CLK_STOCK_HSB;  /* HSB /2, LSB /3 (as they were) */
    CLK_MUX_CON |= (1u << 6) | (1u << 7);
    CLK_MUX_CON |= (1u << 8);                           /* back onto the mux */
    __asm__ volatile("csync" ::: "memory");
    fm1_clock_wait_us(20);
}

/* (interrupts off around it: the caller's, as fm1_main's start has them) */
static inline int fm1_clock_stock(void)
{
    if (fm1_clock_is_stock())
        return 0;
    FL_FAR(fm1_clock_set_stock_ram)();
    return fm1_clock_is_stock() ? 1 : -1;
}
