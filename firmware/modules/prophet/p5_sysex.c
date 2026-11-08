/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The Prophet's SysEx (mod_engine_v3.sysex / sysex_dump, host 3): Sequential's program dumps, F0 01 31 (Rev 3) or 32
 * (Rev 4), the firmware routes here (its schema's MOD3_R_SYSEX). Program bytes travel packed: a mask byte with the top
 * bits of the next seven.
 *   F0 01 3m 02 gg pp <program> F7     a stored program, F0 01 3m 03 <program> F7 the edit buffer -> offered for a
 *                                      user slot (133 or 128 bytes)
 *   F0 01 3m 06 F7                     the edit buffer requested: the selected part's program
 *   F0 01 3m 05 gg pp F7               program gg * 40 + pp requested: that user slot's
 * Included by eng_prophet_test.c (the module's file; host builds: built in) */
static uint32_t p5_sysex_dump(const uint8_t *patch, uint32_t what, uint32_t ch, uint8_t *out, uint32_t max)
{
    p5_patch_t p;
    (void)ch;
    if ((what & 255u) == MOD3_DUMP_BANK)         /* (no bank dump: its programs go one by one) */
        return 0;
    memcpy(&p, patch, sizeof p);
    if (what == MOD3_DUMP_PART)
        p.command = 3;                           /* (a part's sound: the edit buffer) */
    return p5_patch_encode(&p, out, max);
}
static uint32_t p5_sysex(const uint8_t *f, uint32_t n, uint32_t ev)
{
    uint8_t frame[P5_FRAME_MAX];
    p5_patch_t patch;
    const uint8_t *body = f + 1;                 /* (between F0 and F7) */
    if (ev != MOD3_SX_END)
        return ev == MOD3_SX_ABORT ? MOD3_SXR_DONE : MOD3_SXR_MORE;
    if (n < 5u || body[0] != 1u || (body[1] != 0x31u && body[1] != 0x32u))
        return MOD3_SXR_DONE;
    n -= 2u;
    if (body[2] == 2u || body[2] == 3u) {
        if (n + 2u <= sizeof frame && p5_patch_decode(&patch, f, n + 2u))
            (void)MOD_HOST3_CALL(sx_stage)(ENGI_PROPHET, (const uint8_t *)&patch, 1, sizeof patch);
        else if (n + 2u <= sizeof frame)
            MOD_HOST3_CALL(message)("INVALID PROPHET");
    } else if (body[2] == 6u && n == 3u) {       /* the edit buffer: the selected part's program */
        int tr = MOD_HOST3_CALL(sx_part)(ENGI_PROPHET, MOD3_SX_SEL);
        const uint8_t *p = MOD_HOST3_CALL(part_patch)((uint32_t)tr, ENGI_PROPHET);
        uint32_t m = p ? p5_sysex_dump(p, MOD3_DUMP_PART, 0, frame, sizeof frame) : 0;
        if (m)
            (void)MOD_HOST3_CALL(sx_send)(frame, m);
    } else if (body[2] == 5u && n == 5u) {       /* a stored program: its user slot's */
        uint32_t slot = body[3] * 40u + body[4];
        uint8_t raw[MOD3_PATCH_MAX];
        if (body[3] <= 9u && body[4] <= 39u && MOD_HOST3_CALL(slot_get)(ENGI_PROPHET, slot, raw) >= sizeof patch) {
            memcpy(&patch, raw, sizeof patch);
            patch.model = body[1];
            patch.command = 2;
            patch.group = body[3];
            patch.program = body[4];
            uint32_t m = p5_patch_encode(&patch, frame, sizeof frame);
            if (m)
                (void)MOD_HOST3_CALL(sx_send)(frame, m);
        }
    }
    return MOD3_SXR_DONE;
}
