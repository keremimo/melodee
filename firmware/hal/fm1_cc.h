/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM1_INLINE: register helpers that must inline into their caller, so the
 * SFR access sequence (and its timing, in ISRs and IRQ-off sections) is the
 * one written at the call site; -Os would otherwise out-line a helper with
 * several callers. RAM-resident code (.ram_text) may only use these, never
 * a called helper (build.py checks .ram_text for calls). */
#pragma once

#define FM1_INLINE static inline __attribute__((always_inline))

/* Target board: 0 = FM-1 (default), 1 = SMK-37 (same AC791N, SPL and update loader; different panel
 * wiring). build.py sets it from BOARD=smk37. */
#ifndef BOARD_SMK37
#define BOARD_SMK37 0
#endif
/* MELODEE_LEGACY_STORE: read what earlier Melodee builds left in the store (preset banks in their old fixed pairs,
 * pre-UPB2 bank records, old native FM / CZ / Prophet banks, settings without the owner tag) and migrate it. The
 * SMK-37's store was never written by one of those builds: its build leaves these paths out (~2 KB of flash) */
#ifndef MELODEE_LEGACY_STORE
#if BOARD_SMK37
#define MELODEE_LEGACY_STORE 0
#else
#define MELODEE_LEGACY_STORE 1
#endif
#endif
/* SMK_MODEL: the model name the SMK build reports (its identity, its update loader's, the boot stub's record): the
 * SMK-37 Pro, SMK-37 Elite and MKE-P37 are the same keyboard under three names, and the stock updater only takes a
 * unit's own factory package. build.py sets it from MODEL=elite / mke. */
#if BOARD_SMK37 && !defined(SMK_MODEL)
#define SMK_MODEL "SMK-37 Pro"
#endif
