/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Unsupported preset banks reset only at boot, through real NOR A/B saves. */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"

static void fixture(void)
{
    reset(); up_boot();
    memset(&boot_work, 0, sizeof boot_work);
    erases = boot_syncs = boot_trace_bad = 0;
}
static up_rec_t good_record(void)
{
    up_rec_t r = {0}; r.used = UP_USED; r.ver = UP_VER; r.engine = ENGI_SID; r.np = P_COUNT;   /* (a synth the bank names: its entry) */
    memcpy(r.name, "KEEP", 4);
    for (uint32_t i = 0; i < P_COUNT; i++) r.packed[i] = 64;
    return r;
}
static void mixed_bank(void)
{
    up_bank_t *b = up_cache_bank(1); memset(b, 0, sizeof *b);
    b->magic = UP_BANK_MAGIC; b->rsize = sizeof(up_rec_t); b->nslot = UP_PER_BANK;
    b->r[0] = good_record(); b->r[1].used = UP_USED; b->r[1].ver = 99;
    up_save_bank(1); up_cache_reset();
}
static int test_reset(void)
{
    int bad = 0; fixture();
    uint8_t old[80], got[80]; memset(old, 0x5A, sizeof old);
    st_save(OBJ_BANK0, old, sizeof old);           /* unrelated unreadable project stays protected */
    st_pair_save(OBJ_UPRESET0, old, sizeof old);    /* (a 1.x fixed pair: 2.0 never reads it, never erases it) */
    st_save(OBJ_NATIVEFM0, old, sizeof old);
    st_save(OBJ_CZBANK0, old, sizeof old);
    mixed_bank();
    uint32_t app_crc = st_crc32(nor, 0x93000u), before = erases;
    boot_trace = 1; up_boot();
#if UP_SLOTS > 64u
    uint32_t fm_slot = 72, cz_slot = 144;
#else
    uint32_t fm_slot = 0, cz_slot = 0;
#endif
    bad += check("boot resets unsupported general, FM6 and CZ banks", !up_old(0) && !up_used(0) && !native_old(ENGI_FM6, fm_slot) && !native_used(ENGI_FM6, fm_slot) && !native_old(ENGI_CZ, cz_slot) && !native_used(ENGI_CZ, cz_slot));
    up_rec_t good = good_record();
    bad += check("mixed readable bank keeps its good preset and clears only the bad entry", up_used(UP_PER_BANK) && !memcmp(up_rec(UP_PER_BANK), &good, sizeof good) && !up_old(UP_PER_BANK+1) && !up_used(UP_PER_BANK+1));
    bad += check("all reset writes are visible and application/project bytes survive", erases == before + 3 && !boot_work.state && !boot_trace_bad && st_crc32(nor, 0x93000u) == app_crc && st_load(OBJ_BANK0, got, sizeof got) == sizeof got && !memcmp(got, old, sizeof old));
    before = erases; uint32_t flushed = boot_syncs; up_boot();
    bad += check("committed default banks require no writes or progress next boot", erases == before && boot_syncs == flushed);
    return bad;
}
static int test_powercuts(void)
{
    int bad = 0; const int cuts[] = {0, 1, 7, 15};
    for (uint32_t i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
        fixture(); mixed_bank();
        static uint8_t original[4096], got[4096];
        int n = st_load(up_obj(1), original, sizeof original);
        boot_trace = 1; fail_after = cuts[i]; up_boot();
        bad += check("cut reset preserves old committed mixed bank and visible failure", boot_work.state == 2u && !boot_trace_bad && st_load(up_obj(1), got, sizeof got) == n && !memcmp(got, original, (uint32_t)n));
        fail_after = -1; memset(&boot_work, 0, sizeof boot_work); up_boot();
        up_rec_t good = good_record();
        bad += check("reboot retries reset and preserves the compatible preset", !boot_work.state && !up_old(UP_PER_BANK+1) && !up_used(UP_PER_BANK+1) && up_used(UP_PER_BANK) && !memcmp(up_rec(UP_PER_BANK), &good, sizeof good));
        uint32_t before = erases; up_boot();
        bad += check("successful reset retry is not repeated", erases == before);
    }
    return bad;
}
int main(void)
{
    int bad = test_reset() + test_powercuts();
    puts(bad ? "PRESET BOOT RESET TEST FAILED" : "preset boot reset test passed");
    return bad != 0;
}
