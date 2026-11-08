/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A virtual FM-1's backup protocol, in process (EDITOR_PROTOCOL.md: 65 LIST, 66 GET, 67 PUT): included after a
 * melodee_core.c (1.x's in legacy.c, 2.0's in check.c), whose editor (ed_handle, ed_out) it calls as a page would */
static const uint8_t *lg_reply;                         /* the last reply's payload (after its command byte) */
static uint32_t lg_reply_n;
static uint32_t lg_call(uint32_t cmd, const uint8_t *a, uint32_t n)   /* the reply's first payload byte, 0x100: none */
{
    static uint8_t f[600];
    if (n + 4u > sizeof f) return 0x100u;
    f[0] = ED_HDR0; f[1] = ED_HDR1; f[2] = ED_HDR2; f[3] = (uint8_t)cmd;
    memcpy(f + 4, a, n);
    ed_n = 0;
    in_loop = 1;
    ed_handle(f, n + 4u);
    in_loop = 0;
    core_drain();
    core_sysex_out(scratch, sizeof scratch);           /* (the page's copy: not read here) */
    if (ed_n < 6u || ed_out[4] != cmd) return 0x100u;
    lg_reply = ed_out + 5; lg_reply_n = ed_n - 6u;    /* (without the F7) */
    return lg_reply[0];
}
static void lg_put35(uint8_t *b, uint32_t v) { for (uint32_t i = 0; i < 5u; i++) b[i] = (uint8_t)(v >> (7u * i) & 127u); }
static uint32_t lg_get35(const uint8_t *b) { uint32_t v = 0; for (uint32_t i = 0; i < 5u; i++) v |= (uint32_t)b[i] << (7u * i); return v; }
static uint32_t lg_pack7(uint8_t *o, const uint8_t *p, uint32_t n)
{
    uint32_t at = 0;
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = 0;
        for (uint32_t i = 0; i < k; i++) m |= (uint32_t)(p[i] >> 7) << i;
        o[at++] = (uint8_t)m;
        for (uint32_t i = 0; i < k; i++) o[at++] = p[i] & 127u;
        p += k; n -= k;
    }
    return at;
}
/* an object into the device (PUT begin, data, commit): its rc (0 ok) */
static uint32_t lg_bk_put(uint32_t id, const uint8_t *p, uint32_t n)
{
    uint8_t a[2u + 5u + 300u];
    uint32_t rc;
    a[0] = 0; a[1] = (uint8_t)id; lg_put35(a + 2, n); lg_put35(a + 7, st_crc32(p, n));
    if (lg_call(ED_BACKUP_PUT, a, 12u) > 0xFFu || (rc = lg_reply[2]) != 0) return rc | 0x10u;   /* (a reply: a[0], a[1], rc) */
    for (uint32_t at = 0; at < n; at += 256u) {
        uint32_t k = n - at > 256u ? 256u : n - at;
        a[0] = 1; a[1] = (uint8_t)id; lg_put35(a + 2, at);
        if (lg_call(ED_BACKUP_PUT, a, 7u + lg_pack7(a + 7, p + at, k)) > 0xFFu || (rc = lg_reply[2]) != 0) return rc | 0x20u;
    }
    a[0] = 2; a[1] = (uint8_t)id;
    if (lg_call(ED_BACKUP_PUT, a, 2u) > 0xFFu) return 0x3Fu;
    return lg_reply[2] ? lg_reply[2] | 0x30u : 0u;
}
/* LIST: the device's objects captured (the runtime project and the settings as its writers write them now) */
static uint32_t lg_list[256], lg_list_crc[256];        /* (by id: its length + 1, 0 not listed) */
static uint32_t lg_bk_list(void)
{
    uint32_t rc = lg_call(ED_BACKUP_LIST, 0, 0);
    if (rc != 1u || lg_reply_n < 3u || lg_reply[1]) return lg_reply_n >= 2u ? lg_reply[1] | 0x40u : 0x4Fu;
    memset(lg_list, 0, sizeof lg_list);
    for (uint32_t i = 0, at = 3; i < lg_reply[2] && at + 11u <= lg_reply_n; i++, at += 11u) {
        lg_list[lg_reply[at]] = lg_get35(lg_reply + at + 1) + 1u;
        lg_list_crc[lg_reply[at]] = lg_get35(lg_reply + at + 6);
    }
    return 0;
}
/* GET: object id (listed) into o (room max): its length, or 0 with *rc set */
static uint32_t lg_bk_get(uint32_t id, uint8_t *o, uint32_t max, uint32_t *rc)
{
    uint32_t n = lg_list[id] ? lg_list[id] - 1u : 0u;
    *rc = 0;
    if (n > max) { *rc = 0x50u; return 0; }
    for (uint32_t at = 0; at < n; at += 256u) {
        uint8_t a[8];
        uint32_t k = n - at > 256u ? 256u : n - at;
        a[0] = (uint8_t)id; lg_put35(a + 1, at); a[6] = (uint8_t)(k & 127u); a[7] = (uint8_t)(k >> 7);
        lg_call(ED_BACKUP_GET, a, 8u);
        if (lg_reply_n < 9u || lg_reply[0] != id || lg_reply[1]) { *rc = lg_reply_n >= 2u ? lg_reply[1] | 0x60u : 0x6Fu; return 0; }
        if (ed_unpack7(lg_reply + 9, lg_reply_n - 9u, o + at, k) != k) { *rc = 0x70u; return 0; }
    }
    if (st_crc32(o, n) != lg_list_crc[id]) { *rc = 0x71u; return 0; }
    return n;
}

