/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The resource region (res.c), USB only. 82: [0] status; [1] begin (old saves moved out, the region erased);
 * [2, offset in 3 x 7 bits, packed bytes (at most 256)] write; [3] commit (checked, then the header).
 * Reply: rc (0 ok, 1 bad request, 2 no flash, 3 flash error, 4 busy: stop, radio off, 7 check failed),
 * installed, then this build's payload length and CRC-32 (5 x 7 bits each). */
#define ED_RES 82u
static int ed_res_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (cmd != ED_RES) return 0;
    uint32_t rc = 1;
    if (n == 1u && a[0] == 0u) rc = 0;
    else if (n == 1u && a[0] == 1u) rc = (uint32_t)res_begin();
    else if (n > 4u && a[0] == 2u) {
        uint32_t off = a[1] | (uint32_t)a[2] << 7 | (uint32_t)a[3] << 14;
        uint32_t k = ed_unpack7(a + 4, n - 4u, ed_transfer_buf, sizeof ed_transfer_buf);
        rc = k ? (uint32_t)res_write(off, ed_transfer_buf, k) : 1u;
    } else if (n == 1u && a[0] == 3u) rc = (uint32_t)res_commit();
    ed_b(rc); ed_b(res_ok);
    for (uint32_t i = 0; i < 5u; i++) ed_b(res_want[1] >> 7u * i);
    for (uint32_t i = 0; i < 5u; i++) ed_b(res_want[2] >> 7u * i);
    return 1;
}
