/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SMK-37 flash map (build with -DBOARD_SMK37=1):
 * every 4 KiB sector of the 1 MiB part through the app's allow-list (FL_RANGE_OK / FL_GUARD, fm1_flash.h),
 * the update loader's (loader.c) and ldr_core.c's record window: none of them may ever reach
 * the head 0x0-0x3FFF, BTIF / USRTRIM 0xC0000-0xC1FFF, USR 0xF4000-0xFDFFF or key_mac 0xFF000-;
 * Melodee's store (storage.c: the settings pair, the 60-sector pool) lies inside the allowed windows, never shares a
 * sector, and leaves the owner tag sector, the resource region (res.c) and the stock marker's sector 0xEA000 alone
 * (the marker only erased once: the release); the OTA staging (ota.c) too; ranges that wrap or straddle a boundary
 * are refused. The legacy A/B copies of every object but the settings are absent. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1_cc.h"
#if !BOARD_SMK37
#error "build with -DBOARD_SMK37=1"
#endif

#include "fm1_flash_map.h"

/* a simulated NOR: the store owner check (storage.c st_owner_check) runs on it; writes outside FL_STORE_OK count */
static uint8_t nor[0x100000];
static uint32_t bad_writes, erases, tag_erases, fail_erase;   /* fail_erase: that sector's erase fails (0: none) */
static int st_read(uint32_t off, void *dst, uint32_t n) { if (off + n > sizeof nor) return -1; memcpy(dst, nor + off, n); return 0; }
static void st_stock_release(void);
static int st_in_usrflash(uint32_t off);
static int st_erase(uint32_t off)
{
    if ((!FL_STORE_OK(off, 0x1000u) && !FL_STOCKTAG_OK(off, 0x1000u)) || (off & 0xFFFu)) { bad_writes++; return -8; }
    if (fail_erase && off == fail_erase)
        return -1;
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
    if (!FL_STORE_OK(off, n) || ((off & 0xFFu) + n) > 256u) { bad_writes++; return -8; }
    if (st_in_usrflash(off))
        st_stock_release();
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
static int in_res(uint32_t s) { return s >= FL_RES_LO && s < FL_RES_HI; }

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
static void res_fill(uint8_t v) { memset(nor + FL_RES_LO, v, FL_RES_HI - FL_RES_LO); }
static int res_is(uint8_t v)
{
    uint32_t i;
    for (i = FL_RES_LO; i < FL_RES_HI; i++) if (nor[i] != v) return 0;
    return 1;
}

static uint8_t payload[ST_PROJECT_MAX];

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
    check("loader: app area 0x4000-0x9CFFF, records and the resource region 0x9D000-0xBFFFF only",
          LDR_RANGE_OK(0x4000u, 0x1000u) && LDR_RANGE_OK(0x9C000u, 0x1000u) && LDR_RANGE_OK(0xBF000u, 0x1000u) &&
          !LDR_RANGE_OK(0xC0000u, 0x1000u) && FL_REC_HI == 0xC0000u &&
          LDR_RANGE_OK(FL_RES_LO, FL_RES_HI - FL_RES_LO) && FL_RES_LO == 0x9F000u && FL_RES_HI == 0xAF000u);
    check("the resource region: 16 sectors in store A, below the owner tag, the app may write it (res.c res_begin)",
          FL_RES_HI - FL_RES_LO == 16u * 0x1000u && FL_RES_HI == ST_SMK_TAG && FL_STORE_OK(FL_RES_LO, FL_RES_HI - FL_RES_LO));
    {   /* the pool: 60 sectors in the three windows, distinct, never the settings, the tag, the marker or the region */
        static uint8_t owner[256];
        uint32_t overlap = 0, outside = 0, n = 0, absent_ok = 1, shape_ok = 1;
        memset(owner, 0xFF, sizeof owner);
        for (c = 0; c < ST_U_N; c++) {
            uint32_t at = st_pool_at(c), sec = at >> 12;
            n++;
            outside += !FL_STORE_OK(at, 0x1000u) || (at & 0xFFFu) || at == ST_SMK_TAG || at == FL_STOCKTAG_LO || in_res(at) ||
                       at == st_sector(OBJ_SETTINGS, 0) || at == st_sector(OBJ_SETTINGS, 1);
            if (owner[sec] != 0xFF) overlap++;
            owner[sec] = (uint8_t)c;
        }
        shape_ok = st_pool_at(0) == 0xB0000u && st_pool_at(10) == 0xBA000u && st_pool_at(11) == 0xC2000u &&
                   st_pool_at(50) == 0xE9000u && st_pool_at(51) == 0xEB000u && st_pool_at(59) == 0xF3000u && !st_pool_at(ST_U_N);
        check("storage.c: 60 pool sectors in FL_DATA / FL_UFL / FL_GAP, never 0xEA000, the tag, the region or the settings",
              n == 60u && ST_U_N == 60u && outside == 0 && overlap == 0 && owner[ST_SMK_TAG >> 12] == 0xFF &&
              owner[FL_STOCKTAG_LO >> 12] == 0xFF);
        check("storage.c: pool indices 0..10 from 0xB0000, 11..50 from 0xC2000, 51..59 from 0xEB000", shape_ok);
        for (o = 0; o < OBJ_COUNT; o++) {
            st_hdr_t h;
            if (o == OBJ_SETTINGS) continue;
            absent_ok &= st_sector(o, 0) == 0 && st_sector(o, 1) == 0 && st_head(o, 0, &h) < 0 && st_head(o, 1, &h) < 0;
            if (st_small(o)) absent_ok &= st_sector(o, 2) == 0 && st_sector(o, 2u + 22u) == 0;
        }
        check("storage.c: the settings pair at 0x9D000 / 0x9E000; every other legacy copy absent (no former pools)",
              st_sector(OBJ_SETTINGS, 0) == 0x9D000u && st_sector(OBJ_SETTINGS, 1) == 0x9E000u && absent_ok &&
              st_sector(OBJ_BANK0, 2) == st_pool_at(0) && st_sector(OBJ_CZBANK0, ST_SMALL_U0 + 5u) == st_pool_at(5) &&
              ST_SP_N == 0u);
    }
    {   /* saves through the pool on a blank store */
        static uint8_t back[ST_PROJECT_MAX];
        uint32_t i;
        memset(nor, 0xFF, sizeof nor);
        bad_writes = 0;
        for (i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(i * 13u + 7u);
        check("storage.c: a project (8 sectors) saves into the pool and loads back",
              st_save(OBJ_BANK0, payload, sizeof payload) == 0 && st_load(OBJ_BANK0, back, sizeof back) == (int)sizeof payload &&
              !memcmp(back, payload, sizeof payload) && nor[0xB0000u] != 0xFFu && bad_writes == 0);
        check("storage.c: a preset bank saves at the top of the pool (store C, 0xF3000) and loads back",
              st_save(OBJ_CZBANK0, payload, 3716u) == 0 && st_load(OBJ_CZBANK0, back, sizeof back) == 3716 &&
              !memcmp(back, payload, 3716u) && nor[0xF3000u] != 0xFFu && bad_writes == 0);
        check("storage.c: the settings save into their pair",
              st_save(OBJ_SETTINGS, payload, 1000u) == 0 && st_load(OBJ_SETTINGS, back, sizeof back) == 1000 &&
              nor[0x9D000u] != 0xFFu && st_save(OBJ_SETTINGS, payload, 1001u) == 0 && nor[0x9E000u] != 0xFFu && bad_writes == 0);
        check("storage.c: a retired or legacy-only object refuses a save", st_save(OBJ_PROJECT0, "x", 1) == -1);
    }
    check("FL_PLAIN_LO = the VM start 0x9D000", FL_PLAIN_LO == 0x9D000u);
    {   /* the store owner tag at 0xAF000 */
        static const uint8_t SLOOP_TAG[16] = "SMK37DATASLOOP";
        uint32_t k, outside_ok = 1, store_clear = 1;
        memset(nor, 0x5A, sizeof nor);                      /* everything "written" */
        res_fill(0x33);                                     /* the resource region the installer wrote */
        memset(nor + ST_SMK_TAG, 0xFF, 0x1000u);
        memcpy(nor + ST_SMK_TAG, SLOOP_TAG, 16);            /* another port's store */
        erases = bad_writes = 0;
        k = (uint32_t)st_owner_check();
        for (s = FL_DATA_LO; s < FL_DATA_HI; s++)
            store_clear &= (s >= ST_SMK_TAG && s < ST_SMK_TAG + 0x1000u) || in_res(s) ? 1 : nor[s] == 0xFF;
        for (s = FL_UFL_LO; s < FL_UFL_HI; s++) store_clear &= nor[s] == 0xFF;
        for (s = FL_GAP_LO; s < FL_GAP_HI; s++) store_clear &= nor[s] == 0xFF;
        for (s = 0; s < 0x100000u; s++)
            if ((s < FL_DATA_LO || s >= FL_DATA_HI) && (s < FL_UFL_LO || s >= FL_UFL_HI) && (s < FL_GAP_LO || s >= FL_GAP_HI))
                outside_ok &= nor[s] == 0x5A;
        check("owner: another port's store (SLOOP) is cleared, A, B and C, and tagged MELODEE",
              k == 1u && store_clear && !memcmp(nor + ST_SMK_TAG, "SMK37DATAMELODEE", 16) && bad_writes == 0);
        check("owner: the resource region (the installer's) and nothing outside the store windows touched", outside_ok && res_is(0x33));
        nor[0x9D000u] = 0x12;                               /* our data */
        erases = 0;
        check("owner: our own tag: nothing erased", st_owner_check() == 0 && erases == 0 && nor[0x9D000u] == 0x12);
        memset(nor + ST_SMK_TAG, 0xFF, 0x1000u);            /* untagged (FELUCCA_DATA_UNTAGGED_OK=0): cleared */
        check("owner: an untagged store is cleared too (FELUCCA_DATA_UNTAGGED_OK=0)",
              st_owner_check() == 1 && nor[0x9D000u] == 0xFF && !memcmp(nor + ST_SMK_TAG, "SMK37DATAMELODEE", 16) && res_is(0x33));
        memset(nor + ST_SMK_TAG, 0xFF, 0x1000u);
        memcpy(nor + ST_SMK_TAG, SLOOP_TAG, 16);
        fail_erase = 0xB3000u;                              /* one sector of another port's store will not erase */
        check("owner: an erase fails: -1, the other port's tag stays (cleared again at the next boot)",
              st_owner_check() == -1 && !memcmp(nor + ST_SMK_TAG, SLOOP_TAG, 16));
        fail_erase = 0;
        check("... the next boot clears it and tags it", st_owner_check() == 1 && !memcmp(nor + ST_SMK_TAG, "SMK37DATAMELODEE", 16));
    }
    {   /* the stock mapping in USRFLASH: kept while intact, its marker released before Melodee's first write there
         * and at boot over anything else */
        static uint8_t keep[0x29000];
        static const uint32_t HIT[] = {0xC2000u, 0xC6000u, 0xD6000u, 0xDB000u, 0xE5000u, 0xEA000u};
        uint32_t h, ok = 1, i;
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
        for (i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(i * 3u + 1u);
        check("a preset bank's save (the top of the pool, store C) leaves the intact stock mapping and its marker alone",
              st_save(OBJ_CZBANK0 + 6u, payload, 3716u) == 0 && st_stock_intact() && tag_erases == 0);
        check("the first project saves fill store A: the stock mapping still kept",
              st_save(OBJ_BANK0, payload, sizeof payload) == 0 && st_stock_intact() && tag_erases == 0);
        nor[0xEB000u] = 0x77;                               /* (store C: must stay as it is) */
        check("Melodee's first save into USRFLASH: the marker sector erased first (once), the project saved",
              st_save(OBJ_BANK0 + 1u, payload, sizeof payload) == 0 && !st_stock_tagged() && nor[0xEAFF8u] == 0xFF &&
              tag_erases == 1 && nor[0xEB000u] == 0x77 && memcmp(keep, nor + 0xC2000u, 0x1000u) != 0);
        check("... a second save: no second release", st_save(OBJ_BANK0 + 1u, payload, sizeof payload) == 0 && tag_erases == 1);
        for (h = 0; h < sizeof HIT / sizeof HIT[0]; h++) {
            stock();
            nor[HIT[h]] = 0x46;                    /* (other data over the stock layout) */
            ok &= !st_stock_intact();
        }
        check("the stock layout checked: its settings' magic, each preset's erased tail", (int)ok);
        stock();
        memset(nor + 0xD6000u, 0x33, 0x2000u);      /* pool records saved by an older build */
        (void)st_owner_check();
        check("the marker over Melodee's data (an older build): released at boot, the data kept",
              !st_stock_tagged() && nor[0xD6000u] == 0x33 && bad_writes == 0);
    }
    check("FL_JEDEC_OK: the SMK's 0xEB6014 and the FM-1's parts (0x856014, 0xB36014) only",
          FL_JEDEC_OK(0xEB6014u) && FL_JEDEC_OK(0x856014u) && FL_JEDEC_OK(0xB36014u) && !FL_JEDEC_OK(0u) && !FL_JEDEC_OK(0xFFFFFFu));
    printf("%s\n", bad ? "SMK FLASH TEST FAILED" : "smk flash test passed");
    return bad != 0;
}
