/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Which flash Melodee's store may touch on fm1-base: firmware/hal/fm1_flash_map.h's FL_STORE_OK, the windows the
 * store always had (storage.c st_sector holds the objects' addresses), so a unit's projects, banks and settings stay
 * where they are across the move onto the base. platform/app.c's ml_read / ml_erase / ml_write refuse the rest.
 * FM-1 (the base's plat_data windows are these: XDATA0 0x93000, the data area 0x97000..0xDFFFF, XDATA1 0xE5000..0xFBFFF,
 * XDATA2 0xFC000..0xFEFFF; 0xE0000..0xE4FFF is the update's staging, never the store's). */
#pragma once
#define ML_RANGE_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && (uint32_t)(n) <= (hi) - (uint32_t)(off))
#ifdef MELODEE_SMK37
/* the SMK-37: Melodee's store A (settings pair, resource region, owner tag, first pool sectors), B (USRFLASH up to the
 * stock marker) and C (the unlisted sectors below USR); the marker sector is the stock release's, not the store's */
#define ML_DATA_OK(off, n) (ML_RANGE_IN(off, n, 0x9D000u, 0xBB000u) || ML_RANGE_IN(off, n, 0xC2000u, 0xEA000u) || \
                            ML_RANGE_IN(off, n, 0xEB000u, 0xF4000u))
#else
#define ML_DATA_OK(off, n) (ML_RANGE_IN(off, n, 0x93000u, 0xE0000u) || ML_RANGE_IN(off, n, 0xE5000u, 0xFB000u) || \
                            ML_RANGE_IN(off, n, 0xFC000u, 0xFF000u))
#endif
