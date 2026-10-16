/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* SMK-37 flash map (build with -DBOARD_SMK37=1):
 * every 4 KiB sector of the 1 MiB part through the app's allow-list (FL_RANGE_OK / FL_GUARD, fm1_flash.h),
 * the update loader's (loader.c) and ldr_core.c's record window: none of them may ever reach
 * the head 0x0-0x3FFF, BTIF / USRTRIM 0xC0000-0xC1FFF, USR 0xF4000-0xFDFFF or key_mac 0xFF000-;
 * Melodee's store objects (storage.c: A/B copies, the project banks' five-sector copies and their
 * extensions) lie inside the allowed windows, never share a sector and leave the owner tag sector free;
 * the stock marker's sector 0xEA000 is never data, only erased once (the release); the OTA staging (ota.c)
 * too; ranges that wrap or straddle a boundary are refused. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fm1_cc.h"
#if !BOARD_SMK37
#error "build with -DBOARD_SMK37=1"
#endif

#include "fm1_flash_map.h"

/* a simulated NOR: the store owner check (storage.c st_owner_check) runs on it; writes outside FL_STORE_OK count */
static uint8_t nor[0x100000];
static uint32_t bad_writes, erases, tag_erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { if (off + n > sizeof nor) return -1; memcpy(dst, nor + off, n); return 0; }
static void st_stock_release(void);
static int st_in_usrflash(uint32_t off);
static int st_erase(uint32_t off)
{
    if ((!FL_STORE_OK(off, 0x1000u) && !FL_STOCKTAG_OK(off, 0x1000u)) || (off & 0xFFFu)) { bad_writes++; return -8; }
    if (off == FL_STOCKTAG_LO)
        tag_erases++;
    if (st_in_usrflash(off))                    /* (as storage_hw.c st_erase: the stock marker first) */
        st_stock_release();
    memset(nor + off, 0xFF, 0x1000u);
    erases++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (!FL_STORE_OK(off, n)) { bad_writes++; return -8; }
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"
#define LDR_RANGE_OK(off, n) FL_LDR_RANGE_OK(off, n)       /* what loader.c uses on the SMK */

static int bad;
static void check(const char *what, int ok) { printf("smk flash: %-78s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static int protected_sector(uint32_t s)
{
    return s < 0x4000u || (s >= 0xC0000u && s < 0xC2000u) || (s >= 0xF4000u && s < 0xFE000u) || s >= 0xFF000u;
}

static void stock(void)                            /* the stock mapping as V016 writes it (01-hardware 2.3) */
{
    uint32_t n;
    memset(nor + 0xC2000, 0xFF, 0x29000);
    memset(nor + 0xC2000, 0x07, 0x86);
    nor[0xC2000] = 0xA3; nor[0xC2001] = 0x3A;
    for (n = 0; n < 8u; n++) {
        memset(nor + 0xC3000 + n * 0x5000, 0x01, 0x29E3);
        nor[0xC3000 + n * 0x5000 + 4] = 0x7F;
    }
    memcpy(nor + 0xEAFF8, "usrdata", 8);
}

int main(void)
{
    uint32_t s, o, c, leak_app = 0, leak_ldr = 0, leak_guard = 0, allowed = 0;
    for (s = 0; s < 0x100000u; s += 0x1000u) {
        if (!protected_sector(s))
            continue;
        leak_app += FL_RANGE_OK(s, 0x1000u) || FL_STORE_OK(s, 0x1000u) || FL_RANGE_OK(s, 256u) || FL_RANGE_OK(s + 0xF00u, 256u);
        leak_ldr += LDR_RANGE_OK(s, 0x1000u) || LDR_RANGE_OK(s + 0xF00u, 256u);
        leak_guard += !FL_PROTECTED(s, 0x1000u) || !FL_PROTECTED(s + 0xF00u, 256u);
    }
    for (s = 0; s < 0x100000u; s += 0x1000u)
        allowed += FL_GUARD(s, 0x1000u);
    check("no protected sector passes the app's allow-list (erase, page)", leak_app == 0);
    check("no protected sector passes the loader's allow-list", leak_ldr == 0);
    check("the RAM driver's own net (FL_PROTECTED) covers every protected sector", leak_guard == 0);
    check("straddling ranges refused: 0xBF000, 0xE9000, 0xEA000, 0xF3000 + 0x2000, 0x3000 + 0x2000",
          !FL_GUARD(0xBF000u, 0x2000u) && !FL_GUARD(0xE9000u, 0x2000u) && !FL_GUARD(0xEA000u, 0x2000u) &&
          !FL_GUARD(0xF3000u, 0x2000u) && !LDR_RANGE_OK(0xBF000u, 0x2000u) &&
          !LDR_RANGE_OK(0x3000u, 0x2000u) && FL_PROTECTED(0xBF000u, 0x2000u) && FL_PROTECTED(0x3F00u, 0x200u));
    check("wrapping ranges refused (off + n past 4 GiB)",
          !FL_GUARD(0xBB000u, 0xFFFFF000u) && !LDR_RANGE_OK(0x9D000u, 0xFFFFFF00u) && FL_PROTECTED(0xBB000u, 0xFFFFF000u));
    check("app windows: FL_DATA 0x9D000-0xBAFFF, FL_OTA 0xBB000-0xBFFFF, FL_UFL 0xC2000-0xE9FFF, FL_GAP 0xEB000-0xF3FFF",
          FL_GUARD(0x9D000u, 0x1000u) && FL_GUARD(0xBA000u, 0x1000u) && FL_GUARD(0xBF000u, 0x1000u) &&
          FL_GUARD(0xC2000u, 0x1000u) && FL_GUARD(0xE9000u, 0x1000u) && FL_GUARD(0xEB000u, 0x1000u) &&
          FL_GUARD(0xF3000u, 0x1000u) && !FL_GUARD(0xF4000u, 0x1000u) && !FL_GUARD(0xFE000u, 0x1000u) &&
          !FL_GUARD(0x9C000u, 0x1000u) &&
          allowed == (0xC0000u - 0x9D000u + 0xEA000u - 0xC2000u + 0x1000u + 0xF4000u - 0xEB000u) / 0x1000u);
    check("the stock marker's sector 0xEA000: its whole-sector erase only (the release), never a store window",
          FL_GUARD(0xEA000u, 0x1000u) && !FL_STORE_OK(0xEA000u, 0x1000u) && !FL_GUARD(0xEA000u, 256u) &&
          !FL_GUARD(0xEAF00u, 256u) && !FL_STORE_OK(0xEAFF8u, 8u) && FL_STOCKTAG_OFF == 0xEAFF8u);
    check("loader: app area 0x4000-0x9CFFF, records 0x9D000-0xBFFFF only",
          LDR_RANGE_OK(0x4000u, 0x1000u) && LDR_RANGE_OK(0x9C000u, 0x1000u) && LDR_RANGE_OK(0xBF000u, 0x1000u) &&
          !LDR_RANGE_OK(0xC0000u, 0x1000u) && FL_REC_HI == 0xC0000u);
    {   /* every sector an object can write: A/B copies, five per project bank copy, its two extension sectors */
        static uint8_t owner[256];
        uint32_t overlap = 0, outside = 0, absent_ok = 1, k, n_present = 0;
        memset(owner, 0xFF, sizeof owner);
        for (o = 0; o < OBJ_COUNT; o++) {
            uint32_t has = ST_SMK_HAS(o);
            int legacy = o >= OBJ_PROJECT0 && o < OBJ_PROJECT0 + 4u, cd = o >= OBJ_BANK0 + 2u && o < OBJ_BANK0 + 4u;
            absent_ok &= has == (uint32_t)!(legacy || cd);
            if (!has) {
                absent_ok &= st_sector(o, 0) == 0 && st_sector(o, 1) == 0;
                continue;
            }
            n_present++;
            for (c = 0; c < 2u; c++) {
                uint32_t secs[8], ns = 0;
                for (k = 0; k < (st_banked(o) ? 5u : 1u); k++) secs[ns++] = st_sector(o, c) + k * 0x1000u;
                if (st_banked(o)) { secs[ns++] = st_extension(o, c); secs[ns++] = st_extension(o, c) + 0x1000u; }
                for (k = 0; k < ns; k++) {
                    uint32_t sec = secs[k] >> 12;
                    outside += !FL_STORE_OK(secs[k], 0x1000u) || secs[k] == ST_SMK_TAG || secs[k] == FL_STOCKTAG_LO ||
                               (secs[k] & 0xFFFu);
                    if (owner[sec] != 0xFF) overlap++;
                    owner[sec] = (uint8_t)o;
                }
            }
        }
        check("storage.c: every object's sectors (A/B, bank copies, extensions) in FL_DATA / FL_UFL / FL_GAP, never 0xEA000",
              outside == 0 && owner[FL_STOCKTAG_LO >> 12] == 0xFF);
        check("storage.c: no two objects (or copies) share a sector; the owner tag sector 0xAF000 is free",
              overlap == 0 && owner[ST_SMK_TAG >> 12] == 0xFF);
        check("storage.c: without flash on the SMK: only the legacy projects and project slots C, D",
              absent_ok && n_present == OBJ_COUNT - 6u);
        check("storage.c: a missing object reads empty and refuses a save",
              st_save(OBJ_BANK0 + 2u, "x", 1) == -1 && st_load(OBJ_PROJECT0, owner, sizeof owner) == -1);
    }
    check("storage.c: settings 0x9D000, Prophet banks 0xB0000.., projects A 0xC2000, CZ 1-6 to 0xE9FFF, 7-8 0xEB000..",
          st_sector(OBJ_SETTINGS, 0) == 0x9D000u && st_sector(OBJ_SETTINGS, 1) == 0x9E000u &&
          st_sector(OBJ_P5BANK0, 0) == 0xB0000u && st_sector(OBJ_P5BANK0 + 4u, 1) == 0xB9000u &&
          st_sector(OBJ_BANK0, 0) == 0xC2000u && st_extension(OBJ_BANK0 + 1u, 1) == 0xDC000u &&
          st_sector(OBJ_CZBANK0, 0) == 0xDE000u && st_sector(OBJ_CZBANK0 + 5u, 1) == 0xE9000u &&
          st_sector(OBJ_CZBANK0 + 6u, 0) == 0xEB000u && st_sector(OBJ_CZBANK0 + 7u, 1) == 0xEE000u &&
          st_sector(OBJ_SHARED_EXT, 0) == 0xEF000u && st_sector(OBJ_SHARED_EXT, 1) == 0xF0000u);
    check("FL_PLAIN_LO = the VM start 0x9D000", FL_PLAIN_LO == 0x9D000u);
    {   /* the store owner tag at 0xAF000 */
        static const uint8_t SLOOP_TAG[16] = "SMK37DATASLOOP";
        uint32_t k, outside_ok = 1, store_clear = 1;
        memset(nor, 0x5A, sizeof nor);                      /* everything "written" */
        memset(nor + ST_SMK_TAG, 0xFF, 0x1000u);
        memcpy(nor + ST_SMK_TAG, SLOOP_TAG, 16);            /* another port's store */
        erases = bad_writes = 0;
        k = (uint32_t)st_owner_check();
        for (s = FL_DATA_LO; s < FL_DATA_HI; s++) store_clear &= s >= ST_SMK_TAG && s < ST_SMK_TAG + 0x1000u ? 1 : nor[s] == 0xFF;
        for (s = FL_UFL_LO; s < FL_UFL_HI; s++) store_clear &= nor[s] == 0xFF;
        for (s = FL_GAP_LO; s < FL_GAP_HI; s++) store_clear &= nor[s] == 0xFF;
        for (s = 0; s < 0x100000u; s++)
            if ((s < FL_DATA_LO || s >= FL_DATA_HI) && (s < FL_UFL_LO || s >= FL_UFL_HI) && (s < FL_GAP_LO || s >= FL_GAP_HI))
                outside_ok &= nor[s] == 0x5A;
        check("owner: another port's store (SLOOP) is cleared, A, B and C, and tagged MELODEE",
              k == 1u && store_clear && !memcmp(nor + ST_SMK_TAG, "SMK37DATAMELODEE", 16) && bad_writes == 0);
        check("owner: nothing outside the store windows touched (OTA staging, BTIF, USRTRIM, the marker's sector, USR, key_mac)",
              outside_ok);
        nor[0x9D000u] = 0x12;                               /* our data */
        erases = 0;
        check("owner: our own tag: nothing erased", st_owner_check() == 0 && erases == 0 && nor[0x9D000u] == 0x12);
        memset(nor + ST_SMK_TAG, 0xFF, 0x1000u);            /* untagged (FELUCCA_DATA_UNTAGGED_OK=0): cleared */
        check("owner: an untagged store is cleared too (FELUCCA_DATA_UNTAGGED_OK=0)",
              st_owner_check() == 1 && nor[0x9D000u] == 0xFF && !memcmp(nor + ST_SMK_TAG, "SMK37DATAMELODEE", 16));
    }
    {   /* the stock mapping in USRFLASH: kept while intact, its marker released before Melodee's first write there
         * and at boot over anything else */
        static uint8_t keep[0x29000];
        static const uint32_t HIT[] = {0xC2000u, 0xC6000u, 0xD6000u, 0xDB000u, 0xE5000u, 0xEA000u};
        uint32_t h, ok = 1;
        memset(nor, 0x5A, sizeof nor);
        stock();
        memcpy(keep, nor + 0xC2000u, sizeof keep);
        memset(nor + ST_SMK_TAG, 0xFF, 0x1000u);
        memcpy(nor + ST_SMK_TAG, "SMK37DATASLOOP", 15);
        bad_writes = 0;
        tag_erases = 0;
        check("owner change over the intact stock mapping: store A and C cleared, USRFLASH and its marker kept",
              st_owner_check() == 1 && nor[0x9D000u] == 0xFF && nor[0xEB000u] == 0xFF && nor[0xF3FFFu] == 0xFF &&
              nor[0xF4000u] == 0x5A && !memcmp(keep, nor + 0xC2000u, sizeof keep) && st_stock_intact() &&
              tag_erases == 0);
        check("... and at the next boot (ours now)", st_owner_check() == 0 && !memcmp(keep, nor + 0xC2000u, sizeof keep));
        nor[0xEB000u] = 0x77;                       /* (store C: must stay as it is) */
        check("Melodee's first save into USRFLASH: the marker sector erased first (once), the bank saved",
              st_save(OBJ_BANK0, "bank", 4) == 0 && !st_stock_tagged() && nor[0xEAFF8u] == 0xFF && tag_erases == 1 &&
              nor[0xEB000u] == 0x77);
        check("... a second save: no second release", st_save(OBJ_BANK0, "bank", 4) == 0 && tag_erases == 1);
        stock();
        memset(nor + 0xEB000u, 0x5A, 0x9000u);
        tag_erases = 0;
        check("a save into store C (CZ bank 7) leaves the intact stock mapping and its marker alone",
              st_save(OBJ_CZBANK0 + 6u, "cz", 2) == 0 && st_stock_intact() && tag_erases == 0);
        for (h = 0; h < sizeof HIT / sizeof HIT[0]; h++) {
            stock();
            nor[HIT[h]] = 0x46;                    /* (other data over the stock layout) */
            ok &= !st_stock_intact();
        }
        check("the stock layout checked: its settings' magic, each preset's erased tail", (int)ok);
        stock();
        memset(nor + 0xD6000u, 0x33, 0x2000u);      /* project extensions saved by an older build */
        (void)st_owner_check();
        check("the marker over Melodee's data (an older build): released at boot, the data kept",
              !st_stock_tagged() && nor[0xD6000u] == 0x33 && bad_writes == 0);
    }
    check("FL_JEDEC_OK: the SMK's 0xEB6014 and the FM-1's 0x856014 only",
          FL_JEDEC_OK(0xEB6014u) && FL_JEDEC_OK(0x856014u) && !FL_JEDEC_OK(0u) && !FL_JEDEC_OK(0xFFFFFFu));
    printf("%s\n", bad ? "SMK FLASH TEST FAILED" : "smk flash test passed");
    return bad != 0;
}
