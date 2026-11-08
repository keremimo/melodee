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
    reset();
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
