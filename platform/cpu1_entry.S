/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
    .section .text.core1,"ax",@progbits
    .globl fm1_core1_start
fm1_core1_start:
    cli
    icfg = 0
    usp = _cpu1_ustack_top
    sp = _cpu1_sstack_top
    ssp = _cpu1_sstack_top
    reti = fm1_core1_main
    rti
