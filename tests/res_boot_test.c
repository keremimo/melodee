/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The resource region at boot (res.c res_boot): once st_small_migrate has moved every preset bank into the pool,
 * the old copies still in the region go, so the update loader (ldr_core.c ldr_res: it leaves a region holding
 * storage records alone) writes it at the next install. Through real NOR saves. */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"

#define FL_DATA_HI 0x000E0000u                      /* (fm1_flash.h) */
static uint8_t res_ok;
const uint8_t _mres_start[1], _mres_end[1];         /* (res_want unpatched: never installed here) */
#include "../firmware/src/res.c"

static int records_in_region(void)                  /* sectors that start with a valid storage commit record */
{
    int n = 0;
    for (uint32_t s = 0; s < RES_SECTORS; s++) {
        st_hdr_t h;
        memcpy(&h, nor + RES_BASE + s * ST_SECTOR, sizeof h);
        n += h.magic == ST_MAGIC && h.hcrc == st_crc32(&h, sizeof h - 4u);
    }
    return n;
}

int main(void)
{
    int bad = 0;
    uint8_t up[80], fm[80], got[80], junk[4096];
    reset();
    memset(up, 0x5A, sizeof up); memset(fm, 0x3C, sizeof fm); memset(junk, 0xA5, sizeof junk);
    st_pair_save(OBJ_UPRESET0, up, sizeof up);       /* an older layout's banks, in their fixed pairs here */
    st_pair_save(OBJ_NATIVEFM0, fm, sizeof fm);
    memcpy(nor + RES_BASE, junk, sizeof junk);        /* foreign data without a record (the official's VM) */
    bad += check("fixture: two preset banks in the region", records_in_region() == 2);
    res_boot(0);
    bad += check("banks not moved yet: nothing erased, not installed", records_in_region() == 2 && !res_ok);
    erases = 0;
    int moved = st_small_migrate(wire_take(), sizeof proj_wire_u);
    res_boot(moved == 0);
    bad += check("banks moved: their old copies leave the region", !moved && !records_in_region() && !res_ok);
    bad += check(".. foreign data left for the loader", !memcmp(nor + RES_BASE, junk, sizeof junk));
    bad += check(".. both banks read back from the pool", st_load(OBJ_UPRESET0, got, sizeof got) == sizeof up &&
                 !memcmp(got, up, sizeof up) && st_load(OBJ_NATIVEFM0, got, sizeof got) == sizeof fm &&
                 !memcmp(got, fm, sizeof fm));
    uint32_t before = erases;
    res_boot(1);
    bad += check("the next boot erases nothing more", erases == before);
    printf("resource boot test %s\n", bad ? "FAILED" : "passed");
    return !!bad;
}
