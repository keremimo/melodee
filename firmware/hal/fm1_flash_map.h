/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The flash map: which ranges the RAM flash driver (fm1_flash.h) may erase / program, per board
 * (BOARD_SMK37, fm1_cc.h). Macros and compile-time checks only, so host tests can include it. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

/* [off, off + n) inside [lo, hi), without wrapping: off + n can overflow, and
 * the 1 MiB part ignores the high address bits, so a wrapped range lands low. */
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
/* The flash chip Melodee's storage accepts (JEDEC id from 0x9F, project.c); extend it here for another part. */
#if BOARD_SMK37
/* SMK-37: 0xEB6014, 1 MiB. The driver uses only generic SPI NOR commands (9F, 05/35, 06, 20, 02, 0B). */
#define FL_JEDEC_OK(id) ((id) == 0xEB6014u || (id) == 0x856014u)
#else
#define FL_JEDEC_OK(id) ((id) == 0x856014u)
#endif

#if BOARD_SMK37
/* ---- SMK-37 flash map (the stock v016 region table) ----
 *   0x00000-0x03FFF  head (SPL, isd_config)         NEVER
 *   0x04000-0x9CFFF  app area (stock PRCT end)      update loader only (FL_APP)
 *   0x9D000-0xBAFFF  VM: Melodee store A (settings, user presets, FM6 banks, the store owner tag, Prophet banks)
 *                                                                                  FL_DATA
 *   0xBB000-0xBFFFF  VM: M-UPGRADE loader staging + update record (ota.c)          FL_OTA
 *   0xC0000-0xC0FFF  BTIF (BT pairing)              NEVER
 *   0xC1000-0xC1FFF  USRTRIM (key/pad/wheel calibration, "trimdat")   NEVER
 *   0xC2000-0xE9FFF  USRFLASH: Melodee store B (project banks A, B and their extensions, CZ banks 1-6)  FL_UFL
 *                    (the stock mapping until Melodee first writes there)
 *   0xEA000-0xEAFFF  USRFLASH tail: the stock end marker "usrdata" at 0xEAFF8; never data, only erased once
 *                    before the first write into FL_UFL (FL_STOCKTAG)
 *   0xEB000-0xF3FFF  free (unlisted): Melodee store C (CZ banks 7, 8, the shared preset pair)    FL_GAP
 *   0xF4000-0xFDFFF  USR (stock resources; kept for a rollback)   NEVER
 *   0xFE000-0xFEFFF  free, unused
 *   0xFF000-0xFFFFF  key_mac                        NEVER
 * Everything below is an explicit allow-list; the asserts keep each allowed window out of the
 * protected ranges at compile time. */
#define FL_HEAD_HI      0x00004000u                /* protected: [0, FL_HEAD_HI) */
#define FL_CAL_LO       0x000C0000u                /* protected: BTIF + USRTRIM */
#define FL_CAL_HI       0x000C2000u
#define FL_USRRES_LO    0x000F4000u                /* protected: stock USR resources */
#define FL_USRRES_HI    0x000FE000u
#define FL_KEYMAC_LO    0x000FF000u                /* protected: key_mac and above */
#define FL_APP_LO       0x00004000u                /* the update loader: app area */
#define FL_APP_HI       0x0009D000u
#define FL_REC_LO       0x0009D000u                /* the update loader: where it may drop records (VM only) */
#define FL_REC_HI       0x000C0000u
#define FL_PLAIN_LO     0x0009D000u                /* plain XIP window from the VM start */
#define FL_DATA_LO      0x0009D000u                /* Melodee store A */
#define FL_DATA_HI      0x000BB000u
#define FL_OTA_LO       0x000BB000u                /* M-UPGRADE loader staging, ota.c */
#define FL_OTA_HI       0x000C0000u
#define FL_UFL_LO       0x000C2000u                /* Melodee store B (storage.c), USRFLASH up to the marker */
#define FL_UFL_HI       0x000EA000u
#define FL_GAP_LO       0x000EB000u                /* Melodee store C (storage.c), the unlisted sectors below USR */
#define FL_GAP_HI       0x000F4000u
/* USRFLASH is the stock firmware's mapping (global settings 0xC2000, 8 presets from 0xC3000) with its end marker
 * "usrdata\0" at 0xEAFF8: while the marker is there the stock firmware trusts the area and never rebuilds it.
 * Before the first write into USRFLASH the marker sector is erased (storage.c st_stock_release), so a return to
 * stock rebuilds the factory mapping. Exactly that one sector, and never as data. */
#define FL_STOCKTAG_LO  0x000EA000u
#define FL_STOCKTAG_HI  0x000EB000u
#define FL_STOCKTAG_OFF 0x000EAFF8u
#define FL_STOCKTAG_OK(off, n) ((uint32_t)(off) == FL_STOCKTAG_LO && (uint32_t)(n) == FL_STOCKTAG_HI - FL_STOCKTAG_LO)
/* a window [lo, hi) is allowed: sector aligned, non-empty, and clear of every protected range */
#define FL_WIN_SAFE(lo, hi) ((lo) % 0x1000u == 0u && (hi) % 0x1000u == 0u && (lo) < (hi) && \
                             (lo) >= FL_HEAD_HI && (hi) <= FL_KEYMAC_LO && \
                             ((hi) <= FL_CAL_LO || (lo) >= FL_CAL_HI) && \
                             ((hi) <= FL_USRRES_LO || (lo) >= FL_USRRES_HI))
_Static_assert(FL_WIN_SAFE(FL_DATA_LO, FL_DATA_HI), "SMK: FL_DATA overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_OTA_LO, FL_OTA_HI), "SMK: FL_OTA overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_UFL_LO, FL_UFL_HI), "SMK: FL_UFL overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_GAP_LO, FL_GAP_HI), "SMK: FL_GAP overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_STOCKTAG_LO, FL_STOCKTAG_HI) && FL_STOCKTAG_LO == FL_UFL_HI &&
               FL_STOCKTAG_HI == FL_GAP_LO && FL_STOCKTAG_OFF + 8u == FL_STOCKTAG_HI,
               "SMK: the stock marker sector lies between FL_UFL and FL_GAP, in neither");
_Static_assert(FL_UFL_LO == FL_CAL_HI && FL_GAP_HI == FL_USRRES_LO, "SMK: FL_UFL / FL_GAP bounds");
_Static_assert(FL_WIN_SAFE(FL_APP_LO, FL_APP_HI), "SMK: FL_APP overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_REC_LO, FL_REC_HI), "SMK: FL_REC overlaps a protected range");
_Static_assert(FL_REC_HI == FL_CAL_LO && FL_OTA_HI <= FL_REC_HI && FL_OTA_LO >= FL_REC_LO,
               "SMK: the update record (OTA area) must lie in the loader's record window, below BTIF");
_Static_assert(FL_DATA_HI <= FL_OTA_LO && FL_APP_HI <= FL_DATA_LO, "SMK: windows overlap");
/* the update loader (loader.c): the app area, and records only in the VM below BTIF */
#define FL_LDR_RANGE_OK(off, n) (FL_IN(off, n, FL_APP_LO, FL_APP_HI) || FL_IN(off, n, FL_REC_LO, FL_REC_HI))
/* Melodee's own store: FL_DATA, FL_UFL and FL_GAP (never the marker sector) */
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_UFL_LO, FL_UFL_HI) || \
                             FL_IN(off, n, FL_GAP_LO, FL_GAP_HI))
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || FL_STOCKTAG_OK(off, n))
#endif
/* second, independent net in the RAM driver: [off, off + n) touches a protected range (also when a
 * caller supplies its own FL_RANGE_OK, as the update loader does) */
#define FL_HITS(off, n, lo, hi) ((uint32_t)(off) < (hi) && \
                                 ((uint32_t)(off) >= (lo) || (uint32_t)(n) > (lo) - (uint32_t)(off)))
#define FL_PROTECTED(off, n) (FL_HITS(off, n, 0u, FL_HEAD_HI) || FL_HITS(off, n, FL_CAL_LO, FL_CAL_HI) || \
                              FL_HITS(off, n, FL_USRRES_LO, FL_USRRES_HI) || (uint32_t)(off) >= FL_KEYMAC_LO || \
                              (uint32_t)(n) > FL_KEYMAC_LO - (uint32_t)(off))
#define FL_GUARD(off, n) (FL_RANGE_OK(off, n) && !FL_PROTECTED(off, n))
#else /* FM-1: Melodee's map, unchanged */
#define FL_PLAIN_LO     0x00093000u                /* plain XIP window: the app area ends there; the Prophet banks begin */
#define FL_DATA_LO      0x00097000u                /* Melodee main store */
#define FL_DATA_HI      0x000E0000u
#define FL_GLOB_LO      0x000FC000u                /* Melodee superblock / globals */
#define FL_GLOB_HI      0x000FF000u
#define FL_BANK_EXT_LO  0x000E5000u                /* expanded presets and project extension copies; after OTA staging */
#define FL_BANK_EXT_HI  0x000FB000u
#define FL_OTA_LO       0x000E0000u                /* M-UPGRADE loader staging, ota.c */
#define FL_OTA_HI       0x000E5000u
/* Melodee's own store (projects, user samples; settings) */
#define FL_STORE_OK(off, n) (FL_IN(off, n, 0x93000u, 0x97000u) || FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI) || FL_IN(off, n, FL_BANK_EXT_LO, FL_BANK_EXT_HI))
/* Where the RAM driver may erase / program. The app build allows only its own
 * data regions; the update loader (firmware/loader) defines its own window. */
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#endif
#define FL_GUARD(off, n) FL_RANGE_OK(off, n)
#endif /* BOARD_SMK37 */
