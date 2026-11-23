/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The CZ-1's SysEx (mod_engine_v3.sysex / sysex_dump, host 3): Casio's tone dumps, F0 44 00 00 7n (the firmware routes
 * them here: its schema's MOD3_R_SYSEX). A tone is 144 bytes (the CZ-1's: 128 of sound, its 16-character name) or 128
 * (the CZ-101 / 1000's: no name, other key-follow codes), sent low nibble first.
 *   F0 44 00 00 7n 30 <tone> F7               a tone sent straight -> offered for a user slot
 *   F0 44 00 00 7n 20|21 60 .. <tone> F7      a tone sent to it with Casio's handshake: it answers the header
 *                                              (F0 44 00 00 7n 30), the tone comes, it answers F7
 *   F0 44 00 00 7n 10|11 60 F7                its part's tone requested: sent with the header
 *   F0 44 00 00 7n 10|11 60 7n 31 .. F7       .. with the handshake: the header answered, then on 7n 31 the tone
 * A handshake waits 3 s at most. The part: the selected one when it plays the CZ-1, else part n + 1, else the first.
 * Included by eng_cz.c (the module's file; host builds: built in) */
#define CZ_SX_MAX 296u                           /* the longest frame: a 0x21 tone with its header */
static uint8_t cz_tx[295], cz_handshake, cz_req_seen, cz_go_seen;
static uint32_t cz_wait_at;
static void cz_sx_header(uint32_t ch)
{
    cz_tx[0] = 0xf0; cz_tx[1] = 0x44; cz_tx[2] = cz_tx[3] = 0; cz_tx[4] = (uint8_t)ch; cz_tx[5] = 0x30;
}
/* tone raw (144 bytes) as a frame on channel byte ch into cz_tx: native the CZ-1's 144 bytes (else the 128 of the
 * CZ-101: its key-follow codes) */
static uint32_t cz_sx_frame(const uint8_t *raw, uint32_t ch, int native)
{
    uint8_t data[144];
    uint32_t n = native ? 144u : 128u;
    memcpy(data, raw, CZ_BYTES);
    if (!native) {
        static const uint8_t kw[] = {0, 25, 51, 78, 106, 134, 163, 193, 223, 255};
        for (uint32_t l = 0; l < 2; l++) {
            uint32_t o = l * 57u;
            data[16 + o] &= 15;
            data[19 + o] = kw[data[18 + o]];
            for (uint32_t j = 20; j <= 54; j += 17)
                data[j + o] &= 15;
        }
    }
    cz_sx_header(ch);
    for (uint32_t j = 0; j < n; j++) {
        cz_tx[6 + 2 * j] = data[j] & 15;
        cz_tx[7 + 2 * j] = data[j] >> 4;
    }
    cz_tx[6 + 2 * n] = 0xf7;
    return 2 * n + 7u;
}
static void cz_sx_send(int tr, uint32_t ch, int native, int continuation)   /* part tr's tone (continuation: no header) */
{
    const uint8_t *raw = tr >= 0 ? MOD_HOST3_CALL(part_patch)((uint32_t)tr, MOD_HANDLE(ENGI_CZ)) : 0;
    uint8_t zero[CZ_BYTES];
    uint32_t n;
    if (!raw) {
        memset(zero, 0, sizeof zero);
        raw = zero;
    }
    n = cz_sx_frame(raw, ch, native);
    (void)MOD_HOST3_CALL(sx_send)(cz_tx + (continuation ? 6 : 0), n - (continuation ? 6u : 0u));
}
static uint32_t cz_sysex_dump(const uint8_t *patch, uint32_t what, uint32_t ch, uint8_t *out, uint32_t max)
{
    uint32_t n;
    if ((what & 255u) == MOD3_DUMP_BANK)         /* (no bank dump: its tones go one by one) */
        return 0;
    n = cz_sx_frame(patch, what == MOD3_DUMP_STORED ? 0x70u : 0x70u | (ch & 15u), 1);
    if (n > max)
        return 0;
    memcpy(out, cz_tx, n);
    return n;
}
/* a tone received (n nibbles: 256 the CZ-101's, 288 the CZ-1's) offered for a user slot: 1, or 0 not a tone */
static int cz_sx_import(const uint8_t *b, uint32_t n)
{
    uint8_t data[CZ_BYTES];
    if (n != 256 && n != 288)
        return 0;
    for (uint32_t j = 0; j < n; j++)
        if (b[j] > 15)
            return 0;
    if (n == 256)
        memset(data + 128, ' ', 16);
    for (uint32_t j = 0; j < n / 2; j++)
        data[j] = (uint8_t)(b[j * 2] | b[j * 2 + 1] << 4);
    if (n == 256) {
        static const uint8_t kw[] = {0, 31, 44, 57, 70, 83, 96, 111, 146, 255};
        for (uint32_t l = 0; l < 2; l++) {
            uint32_t o = l * 57u;
            if ((data[18 + o] & 15) > 9)
                return 0;
            data[16 + o] &= 15;
            data[19 + o] = kw[data[18 + o] & 15];
            for (uint32_t j = 20; j <= 54; j += 17)
                data[j + o] |= 240;
        }
    }
    if (!cz_patch_valid(data))
        return 0;
    return MOD_HOST3_CALL(sx_stage)(MOD_HANDLE(ENGI_CZ), data, 1, CZ_BYTES);
}
static uint32_t cz_sx_done(void) { cz_req_seen = cz_go_seen = 0; return MOD3_SXR_DONE; }
static uint32_t cz_sysex(const uint8_t *f, uint32_t n, uint32_t ev)
{
    uint32_t cmd, ch;
    int tr;
    if (ev == MOD3_SX_ABORT || n > CZ_SX_MAX ||
        (cz_handshake && (MOD_HOST3_MS() - cz_wait_at > 3000u || MOD_HOST3_CALL(sx_send)(0, 0)))) {
        if (cz_handshake && !MOD_HOST3_CALL(sx_send)(0, 0)) {
            cz_tx[0] = 0xf7;
            (void)MOD_HOST3_CALL(sx_send)(cz_tx, 1);
        }
        cz_handshake = 0;
        return cz_sx_done();
    }
    if (n < 7u)
        return ev == MOD3_SX_END ? cz_sx_done() : MOD3_SXR_MORE;
    cmd = f[5];
    ch = f[4];
    tr = MOD_HOST3_CALL(sx_part)(MOD_HANDLE(ENGI_CZ), ch & 15u);
    if (f[2] || f[3] || (ch & 0xf0) != 0x70)
        return cz_sx_done();
    if (tr < 0 && cmd != 0x20 && cmd != 0x21 && cmd != 0x30)
        return cz_sx_done();
    if (ev == MOD3_SX_END) {
        if (cmd == 0x20 || cmd == 0x21) {
            int ok = ((cmd == 0x20 && n == 264) || (cmd == 0x21 && n == 296)) && cz_sx_import(f + 7, n - 8);
            if (ok || cz_handshake) {
                if (cz_handshake) { cz_tx[0] = 0xf7; (void)MOD_HOST3_CALL(sx_send)(cz_tx, 1); }
                else { cz_sx_header(ch); cz_tx[6] = 0xf7; (void)MOD_HOST3_CALL(sx_send)(cz_tx, 7); }
            }
            if (!ok)
                MOD_HOST3_CALL(message)("CZ INVALID TONE");
        } else if (cmd == 0x30 && (n == 263 || n == 295)) {
            (void)cz_sx_import(f + 6, n - 7);
        } else if ((cmd == 0x10 || cmd == 0x11) && n == 8 && f[6] == 0x60) {
            cz_sx_send(tr, ch, cmd == 0x11, 0);
        }
        cz_handshake = 0;
        return cz_sx_done();
    }
    if (ev == MOD3_SX_MORE && !cz_go_seen && n >= 9u && (cmd == 0x10 || cmd == 0x11) && f[7] == f[4] && f[8] == 0x31) {
        cz_go_seen = cz_req_seen = 1;            /* 7n 31: the tone, after the header answered */
        cz_sx_send(tr, ch, cmd == 0x11, 1);
        return cz_handshake ? MOD3_SXR_IDLE : MOD3_SXR_MORE;
    }
    if (ev == MOD3_SX_MORE && !cz_req_seen) {   /* the header: answered for a handshake */
        cz_req_seen = 1;
        if ((cmd == 0x10 || cmd == 0x11 || cmd == 0x20 || cmd == 0x21) && f[6] == 0x60) {
            cz_sx_header(ch);
            (void)MOD_HOST3_CALL(sx_send)(cz_tx, 6);
            cz_handshake = 1;
            cz_wait_at = MOD_HOST3_MS();
        }
    }
    return cz_handshake ? MOD3_SXR_IDLE : MOD3_SXR_MORE;
}
