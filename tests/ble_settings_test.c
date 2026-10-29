/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The compact settings body leaves old PER5/template offsets unchanged while
 * fitting the BLE trailer. Exercise the actual A/B commit and every byte cut. */
#define main storage_existing_main
#include "storage_test.c"
#undef main
#include <assert.h>

int main(void)
{
    static uint8_t old[3748], next[3960], got[4064], snapshot[8192];
    memset(nor, 0xFF, sizeof nor);
    memset(old, 0x31, sizeof old); memset(next, 0x72, sizeof next);
    assert(!st_save(OBJ_SETTINGS, old, sizeof old));
    assert(st_load(OBJ_SETTINGS, got, sizeof got) == sizeof old && !memcmp(old, got, sizeof old));
    memcpy(snapshot, nor + 0xFC000, sizeof snapshot);
    for (int cut=0; cut < (int)(sizeof next + sizeof(st_hdr_t)); cut++) {
        memcpy(nor + 0xFC000, snapshot, sizeof snapshot);
        fail_bytes = cut; int rc = st_save(OBJ_SETTINGS, next, sizeof next); fail_bytes = -1;
        assert(rc);
        assert(st_load(OBJ_SETTINGS, got, sizeof got) == sizeof old && !memcmp(old, got, sizeof old));
    }
    memcpy(nor + 0xFC000, snapshot, sizeof snapshot);
    assert(!st_save(OBJ_SETTINGS, next, sizeof next));
    assert(st_load(OBJ_SETTINGS, got, sizeof got) == sizeof next && !memcmp(next, got, sizeof next));
    st_hdr_t h; int copy = st_current(OBJ_SETTINGS, &h);
    assert(copy >= 0 && h.rsv[0] == ST_SETTINGS_TAG && st_payload_off_header(OBJ_SETTINGS, &h) == 32);
    nor[st_sector(OBJ_SETTINGS, (uint32_t)copy) + 32u + sizeof old + 10] ^= 1;
    assert(st_load(OBJ_SETTINGS, got, sizeof got) == sizeof old && !memcmp(old, got, sizeof old));
    puts("BLE settings: legacy reads, 3992 byte cuts, page bounds, CRC fallback and A/B save passed");
    return 0;
}
