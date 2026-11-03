/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The user's drum kits (kit_store.c), 83:
 *   [0] list
 *   [1, slot, length in 3 x 7 bits] begin (the transport stops; the record's sectors are taken and erased)
 *   [2, offset in 3 x 7 bits, packed bytes (at most 256)] write, in order
 *   [3, CRC-32 of every byte in 5 x 7 bits] commit: the record checked as the player reads it, then published
 *   [4, slot] delete
 * Reply: rc (0 ok, 1 bad request, 2 no flash, 3 flash error, 4 busy: stop, 5 storage full, 6 lost: another save
 * came first, begin again, 7 not a kit the player can play), then per slot: used, payload length (3 x 7 bits), its
 * name (DRK_NAMELEN bytes, zeros after). */
#define ED_KIT 83u
static st_stream_t ed_kit_w;
static uint8_t ed_kit_slot = 0xFFu;
static uint32_t ed_kit_len;
static uint32_t ed_kit_rc(int r) { return r == -5 ? 5u : r == -6 ? 6u : r == -2 ? 7u : r == -1 ? 1u : r ? 3u : 0u; }
static int ed_kit_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (cmd != ED_KIT) return 0;
    uint32_t rc = 1;
    if (!flash_ok) rc = 2;
    else if (n == 1u && a[0] == 0u) rc = 0;
    else if (n == 5u && a[0] == 1u && a[1] < ST_NKIT) {
        uint32_t len = a[2] | (uint32_t)a[3] << 7 | (uint32_t)a[4] << 14;
        ed_kit_slot = 0xFFu;
        if (ed_flash_stop()) rc = 4;
        else {
            int r = kit_put_begin(&ed_kit_w, a[1], len);
            rc = ed_kit_rc(r);
            if (!r) { ed_kit_slot = a[1]; ed_kit_len = len; }
        }
    } else if (n > 4u && a[0] == 2u && ed_kit_slot < ST_NKIT) {
        uint32_t off = a[1] | (uint32_t)a[2] << 7 | (uint32_t)a[3] << 14;
        uint32_t k = ed_unpack7(a + 4, n - 4u, ed_transfer_buf, 256u);
        if (k && off == ed_kit_w.pos && k <= ed_kit_len - off) {
            int r = kit_put_write(&ed_kit_w, ed_transfer_buf, k);
            rc = ed_kit_rc(r);
            if (r) ed_kit_slot = 0xFFu;
        }
    } else if (n == 6u && a[0] == 3u && ed_kit_slot < ST_NKIT) {
        uint32_t crc = 0;
        for (uint32_t i = 0; i < 5u; i++) crc |= (uint32_t)a[1u + i] << (7u * i);
        rc = ed_kit_w.pos != ed_kit_len || ~ed_kit_w.crc != crc ? 7u : ed_kit_rc(kit_put_commit(&ed_kit_w, ed_kit_slot));
        ed_kit_slot = 0xFFu;
    } else if (n == 2u && a[0] == 4u && a[1] < ST_NKIT) {
        ed_kit_slot = 0xFFu;
        rc = ed_flash_stop() ? 4u : kit_delete(a[1]) ? 3u : 0u;
    }
    ed_b(rc);
    for (uint32_t u = 0; u < ST_NKIT; u++) {
        char name[6];
        uint32_t len = kit_len[u];
        drk_user_name(u, name);
        ed_b(len != 0u);
        for (uint32_t i = 0; i < 3u; i++) ed_b((len >> (7u * i)) & 127u);
        for (uint32_t i = 0; i < DRK_NAMELEN; i++) ed_b(len && i < str_len(name) ? (uint8_t)name[i] & 127u : 0u);
    }
    return 1;
}
