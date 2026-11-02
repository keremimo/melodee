/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Jacek Mariański */
/* What the radio's headers (fm1_ble.h, fm1_ble_rf.h) and Optimist's BLE stack take from a firmware's fm1_cc.h */
#pragma once
#define FM1_INLINE static inline __attribute__((always_inline))
#define FM1_CSYNC() __asm__ volatile("csync" ::: "memory")
#define FM1_SSYNC() __asm__ volatile("ssync" ::: "memory")
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* (as core.h: the slot before the index) */
