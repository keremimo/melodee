/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FM6's DX7 SysEx (mod_engine_v3.sysex / sysex_dump, host 3): the frames the firmware routes here (its schema's
 * MOD3_R_SYSEX: F0 43) and the voice it sends; host 4's bank dump (MOD3_DUMP_BANK: user slots as a VMEM bank, the
 * editor's bank export). Accepted on any channel n:
 *   F0 43 0n 00 01 1B <155 bytes> <checksum> F7    a voice (VCED) -> offered for a user slot
 *   F0 43 0n 09 20 00 <4096 bytes> <checksum> F7   32 voices (VMEM) -> offered for 32 user slots
 *   F0 43 0n 7D 04 08 <155 bytes> <69 bytes> F7   a voice from the 231-byte wrapper a MainStage file has (its opaque
 *                                                  extra settings ignored)
 *   F0 43 1n gg pp dd F7                           a voice parameter pp + 128 gg (155: the six operator switches, OP1 =
 *                                                  bit 5) -> the part's patch
 *   F0 43 1n 08 pp dd F7                           a function parameter (64 mono, 65 bend range, 66 step, 68 glissando,
 *                                                  69 portamento time, 70..77 wheel / foot / breath / aftertouch range
 *                                                  and target) -> the part's function settings
 *   F0 43 2n 00 F7 / F0 43 2n 09 F7                dump requests: the part's voice / the 32 user slots of the STORE
 *                                                  page's bank
 * The part: the selected one when it plays FM6, else part n + 1, else the first FM6 part (host sx_part). Dexed or any
 * DX7 librarian edits a part live. Included by eng_fm6.c (the module's file; host builds: built in) */
#include "melodee_fm6.h"         /* FM6_INIT: an empty slot's voice in a bank sent */
static uint8_t fm6_sx_chk(const uint8_t *p, uint32_t n)  /* DX7 checksum: data + it = 0 mod 128 */
{
    uint32_t s = 0;
    while (n--)
        s += *p++;
    return (uint8_t)(-s & 127u);
}
/* the packed patch pk as a VCED frame on channel ch (163 bytes) */
static uint32_t fm6_sx_voice(const uint8_t *pk, uint32_t ch, uint8_t *out)
{
    static const uint8_t H[6] = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
    uint8_t v[FP_SIZE + 1u];
    memcpy(out, H, 6);
    out[2] = (uint8_t)(ch & 15u);
    fm6_unpack(pk, v);
    memcpy(out + 6, v, 155);
    out[161] = fm6_sx_chk(out + 6, 155);
    out[162] = 0xF7;
    return 163;
}
/* 32 voices as a VMEM bank frame on channel ch into f (MOD3_SX_MAX bytes): voice k from user slot slots[k] (u16, low
 * byte first; n of them) or, without slots, first + k; the init voice where a slot holds no FM6 voice and past the n.
 * repack: each voice unpacked and packed again, as the old web editor's FM6.bankSysex wrote its banks (the editor's
 * bank export, 88f4eb02); else as stored (the dump request's) */
static uint32_t fm6_sx_bank(uint8_t *f, uint32_t ch, const uint8_t *slots, uint32_t n, uint32_t first, int repack)
{
    static const uint8_t H[6] = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    memcpy(f, H, 6);
    f[2] = (uint8_t)(ch & 15u);
    for (uint32_t k = 0; k < 32u; k++) {
        uint8_t *pk = f + 6u + k * FM6_PACKED;
        uint32_t s = slots ? (k < n ? slots[2u * k] | (uint32_t)slots[2u * k + 1u] << 8 : 0xFFFFu) : first + k;
        if (s == 0xFFFFu || MOD_HOST3_CALL(slot_get)(ENGI_FM6, s, pk) != FM6_PACKED)
            memcpy(pk, FM6_INIT, FM6_PACKED);
        else if (repack) {
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(pk, v);
            fm6_pack(v, pk);
        }
    }
    f[4102] = fm6_sx_chk(f + 6, 4096);
    f[4103] = 0xF7;
    return MOD3_SX_MAX;
}
static uint32_t fm6_sysex_dump(const uint8_t *patch, uint32_t what, uint32_t ch, uint8_t *out, uint32_t max)
{
    if ((what & 255u) == MOD3_DUMP_BANK)         /* (the editor's bank export: the first 32 slots given) */
        return max < MOD3_SX_MAX || !(what >> 8) ? 0u : fm6_sx_bank(out, ch, patch, what >> 8, 0, 1);
    return max < 163u ? 0u : fm6_sx_voice(patch, ch, out);
}
/* part tr's whole patch into b (FM6_BLOB bytes): 1, or 0 (not FM6's) */
static int fm6_sx_patch(uint32_t tr, uint8_t *b)
{
    const uint8_t *p = MOD_HOST3_CALL(part_patch)(tr, ENGI_FM6);
    if (!p)
        return 0;
    memcpy(b, p, FM6_BLOB);
    return 1;
}
static void fm6_sx_fn(uint32_t tr, uint32_t k, int32_t v)   /* a function setting of part tr */
{
    uint8_t b[FM6_BLOB];
    if (tr < NTRK && k < FM6_NFN && fm6_sx_patch(tr, b)) {
        b[FM6_B_FN + k] = (uint8_t)clamp(v, 0, FM6_FNMAX[k]);
        (void)MOD_HOST3_CALL(patch_put)(tr, ENGI_FM6, b, FM6_BLOB, MOD3_PATCH_EDIT);
    }
}
static uint32_t fm6_sysex(const uint8_t *b, uint32_t n, uint32_t ev)
{
    uint32_t st, ch, j;
    int tr;
    if (ev != MOD3_SX_END)
        return ev == MOD3_SX_ABORT ? MOD3_SXR_DONE : MOD3_SXR_MORE;   /* (a frame is read whole) */
    if (n < 5u || b[n - 1u] != 0xF7u)
        return MOD3_SXR_DONE;
    for (j = 2; j + 1u < n; j++)
        if (b[j] > 127u)
            return MOD3_SXR_DONE;
    st = b[2] & 0xF0u;
    ch = b[2] & 15u;
    tr = MOD_HOST3_CALL(sx_part)(ENGI_FM6, ch);
    if (n == 163u && st == 0x00u && b[3] == 0x00 && b[4] == 0x01 && b[5] == 0x1B && fm6_sx_chk(b + 6, 155) == b[161]) {
        uint8_t pk[FM6_PACKED];                  /* one voice */
        fm6_pack(b + 6, pk);
        (void)MOD_HOST3_CALL(sx_stage)(ENGI_FM6, pk, 1, FM6_PACKED);
    } else if (n == 231u && st == 0x00u && b[3] == 0x7D && b[4] == 4 && b[5] == 8) {
        uint8_t pk[FM6_PACKED];                  /* (the wrapper has no checksum: an intact, in-range VCED; its opaque
                                                  * tail is not taken for Yamaha's function parameters) */
        for (uint32_t k = 0; k < FP_SIZE; k++)
            if (b[6 + k] > fm6_max(k) || (k >= FP_NAME && b[6 + k] < 32u))
                return MOD3_SXR_DONE;
        fm6_pack(b + 6, pk);
        (void)MOD_HOST3_CALL(sx_stage)(ENGI_FM6, pk, 1, FM6_PACKED);
    } else if (n == MOD3_SX_MAX && st == 0x00u && b[3] == 0x09 && b[4] == 0x20 && b[5] == 0x00 &&
               fm6_sx_chk(b + 6, 4096) == b[4102]) {
        (void)MOD_HOST3_CALL(sx_stage)(ENGI_FM6, b + 6, 32, FM6_PACKED);   /* (32 voices: chosen from the frame) */
    } else if (n == 7u && st == 0x10u && !(b[3] >> 2) && tr >= 0) {   /* a voice parameter */
        uint32_t k = (uint32_t)(b[3] & 3u) << 7 | b[4];
        uint8_t pb[FM6_BLOB];
        if (k < 155u) {
            uint8_t v[FP_SIZE + 1u];
            if (fm6_sx_patch((uint32_t)tr, pb)) {
                fm6_unpack(pb, v);
                v[k] = b[5];
                fm6_sanitize(v);
                fm6_pack(v, pb);
                (void)MOD_HOST3_CALL(patch_put)((uint32_t)tr, ENGI_FM6, pb, FM6_PACKED, MOD3_PATCH_EDIT);
            }
        } else if (k == 155u && fm6_sx_patch((uint32_t)tr, pb)) {
            pb[FM6_B_ON] = (uint8_t)(b[5] & FM6_ON_ALL);   /* bit 0 OP6 .. bit 5 OP1, as the switches */
            (void)MOD_HOST3_CALL(patch_put)((uint32_t)tr, ENGI_FM6, pb, FM6_BLOB, MOD3_PATCH_EDIT);
        }
        MOD_HOST3_CALL(message)(0);
    } else if (n == 7u && st == 0x10u && b[3] == 0x08u && tr >= 0) {   /* a function parameter: the part's */
        uint32_t v = b[5], f = (uint32_t)tr;
        mod_part_v3 *t = MOD_HOST3_CALL(part)(f);
        switch (b[4]) {
        case 64:                                 /* MONO: Dexed's (legato, the highest key) */
            t->p[P_VOICE] = (int16_t)(v ? V_LEGATO : V_POLY);
            if (v)
                t->p[P_PRIO] = 2;
            break;
        case 65:
            fm6_sx_fn(f, FN_PBUP, (int32_t)v);
            fm6_sx_fn(f, FN_PBDN, (int32_t)v);
            break;
        case 66: fm6_sx_fn(f, FN_PBSTEP, (int32_t)v); break;
        case 68: fm6_sx_fn(f, FN_GLISS, (int32_t)v); break;
        case 69: fm6_sx_fn(f, FN_PTIME, (int32_t)(v > 99u ? 99u : v) * 127 / 99); break;   /* 0..99 -> CC 5 */
        default:
            if (b[4] >= 70u && b[4] <= 77u)      /* range, target: wheel, foot, breath, aftertouch */
                fm6_sx_fn(f, FN_MWR + (uint32_t)(b[4] - 70u), (int32_t)v);
            break;
        }
        MOD_HOST3_CALL(message)(0);
    } else if (n == 5u && st == 0x20u) {         /* dump requests */
        int rc = 0;
        if (b[3] == 0x00 && tr >= 0) {
            uint8_t f[163], pb[FM6_BLOB];
            const uint8_t *p = MOD_HOST3_CALL(part_patch)((uint32_t)tr, ENGI_FM6);
            if (p) { memcpy(pb, p, FM6_BLOB); rc = MOD_HOST3_CALL(sx_send)(f, fm6_sx_voice(pb, ch, f)); }
        } else if (b[3] == 0x09) {               /* the STORE page's bank: its 32 slots, the init voice where empty */
            if (MOD_HOST3_CALL(sx_send)(0, 0))
                rc = -1;
            else {                               /* (a slot's stored voice: its packed 128 bytes) */
                uint8_t *f = MOD_HOST3_CALL(sx_scratch)();
                rc = MOD_HOST3_CALL(sx_send)(f, fm6_sx_bank(f, ch, 0, 0, MOD_HOST3_CALL(store_slot)(ENGI_FM6) / 32u * 32u, 0));
            }
        }
        if (rc)
            MOD_HOST3_CALL(message)("SYSEX SEND ERROR");
    }
    return MOD3_SXR_DONE;
}
