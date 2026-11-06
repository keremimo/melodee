/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The resource region at boot (res.c res_boot): once st_convert has moved every preset bank into the pool, the old
 * copies still in the region go, so the update loader (ldr_core.c ldr_res: it leaves a region holding storage
 * records alone) writes it at the next install; and the installed header's window (res_window), which keeps the
 * pool out of the region's sectors below it. Through real NOR saves. */
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
    st_old_scan();
    int left = st_convert(wire_take(), 0);
    int moved = st_held(RES_BASE, RES_BASE + RES_RESERVE * ST_SECTOR);
    res_boot(moved == 0);
    bad += check("banks moved: their old copies leave the region", !left && !moved && !records_in_region() && !res_ok);
    bad += check(".. foreign data left for the loader", !memcmp(nor + RES_BASE, junk, sizeof junk));
    bad += check(".. both banks read back from the pool", st_load(OBJ_UPRESET0, got, sizeof got) == sizeof up &&
                 !memcmp(got, up, sizeof up) && st_load(OBJ_NATIVEFM0, got, sizeof got) == sizeof fm &&
                 !memcmp(got, fm, sizeof fm));
    uint32_t before = erases;
    res_boot(1);
    bad += check("the next boot erases nothing more", erases == before);
    {   /* the installed header's window */
        res_hdr_t h;
        int ok = 1;
        memset(nor + RES_BASE, 0xFF, ST_SECTOR);
        ok &= res_window() == RES_RESERVE && st_res_end == RES_BASE + RES_RESERVE * ST_SECTOR;   /* none: the floor */
        uint32_t want[4] = {0u, RES_RESERVE, 3u, RES_SECTORS - 1u}, got_n[4] = {RES_SECTORS, RES_RESERVE, RES_RESERVE, RES_SECTORS - 1u};
        for (uint32_t k = 0; k < 4u; k++) {
            memset(&h, 0, sizeof h);
            h.magic = RES_MAGIC; h.len = 100u; h.rsv[0] = want[k]; h.hcrc = st_crc32(&h, sizeof h - 4u);
            memcpy(nor + RES_BASE, &h, sizeof h);
            ok &= res_window() == got_n[k] && st_res_end == RES_BASE + got_n[k] * ST_SECTOR;
        }
        nor[RES_BASE + 5u] ^= 1u;                                    /* a torn header: the floor again */
        ok &= res_window() == RES_RESERVE;
        bad += check("res_window: 0 = the whole region, none or torn = the floor, never below it", ok);
    }
    printf("resource boot test %s\n", bad ? "FAILED" : "passed");
    return !!bad;
}
