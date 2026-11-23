/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A packed module image (mod_abi3.h MOD3_FILE_PACKED; tools: mod_build.py pack_image): one byte lc + 9 lp + 45 pb, raw
 * LZMA1 with its end marker, zero bytes to a whole word. The boot decoder's range coder (boot_lzma.h) with the file's
 * props at run time, its tables and its input buffer in MOD_LZ_WORK bytes after the image in the module's own block
 * (module_store.c mod_load: the block is that much longer until it is unpacked), the input read through rd a buffer at
 * a time, every byte of it into the CRC. Strict as boot_lzma: the output exactly its size, then the end marker, every
 * input byte used but the zero bytes to the word, the range coder finished at zero. Included by module_store.c (after
 * mod_read_t), before main.c includes boot_lzma.h again (the guard: one copy) */
#include "boot_lzma.h"

#define MOD_LZ_P(lclp, pb) (951u + 56u * (1u << (pb)) + (0x300u << (lclp)))   /* the tables' probabilities */
#define MOD_LZ_IN 512u                      /* the input buffer: filled again when less than MOD_LZ_LOW is left (a symbol
                                             * reads 48 bytes at most: one a bit) */
#define MOD_LZ_LOW 64u
#define MOD_LZ_WORK (2u * MOD_LZ_P(MOD3_PACK_LCLP, MOD3_PACK_PB) + MOD_LZ_IN)   /* (the buffer's bytes after the tables) */

typedef struct { uint32_t off, left, crc; uint8_t *buf; } mod_lz_in_t;
/* the buffer again: its unread rest first, then as much more of the image as fits (rd a parameter all the way from
 * mod_load's caller: the stack audit sees the one reader, tools/stack_budget.py) */
static void mod_lz_fill(boot_rc_t *r, mod_lz_in_t *in, mod_read_t rd)
{
    uint32_t keep = (uint32_t)(r->end - r->at), m = MOD_LZ_IN - keep;
    m = m < in->left ? m : in->left;
    for (uint32_t i = 0; i < keep; i++)
        in->buf[i] = r->at[i];
    if (m && rd(in->off, in->buf + keep, m)) {
        r->bad = 1;
        m = 0;
    }
    in->crc = st_crc32_update(in->crc, in->buf + keep, m);
    in->off += m;
    in->left -= m;
    r->at = in->buf;
    r->end = in->buf + keep + m;
}
static uint32_t mod_lz_len(boot_rc_t *r, volatile uint16_t *l, uint32_t ps, uint32_t np)
{
    if (!boot_rc_bit(r, &l[0]))
        return boot_rc_tree(r, l + 2u + ps * 8u, 3);
    if (!boot_rc_bit(r, &l[1]))
        return 8u + boot_rc_tree(r, l + 2u + np * 8u + ps * 8u, 3);
    return 16u + boot_rc_tree(r, l + 2u + 16u * np, 8);
}
/* out: size bytes from the n bytes at off (rd); work: MOD_LZ_WORK bytes, word aligned; *crc carried over the n bytes.
 * 1: unpacked, 0: malformed or unreadable (out partly written) */
static int mod_unpack(uint8_t *out, uint32_t size, mod_read_t rd, uint32_t off, uint32_t n, uint32_t *crc, uint8_t *work)
{
    mod_lz_in_t in = {off, n, *crc, work + 2u * MOD_LZ_P(MOD3_PACK_LCLP, MOD3_PACK_PB)};
    boot_rc_t r = {in.buf, in.buf, 0xFFFFFFFFu, 0, 0};
    mod_lz_fill(&r, &in, rd);
    if (r.end - r.at < 6 || r.bad)
        return 0;
    uint32_t pr = r.at[0], lc = pr % 9u, lp = pr / 9u % 5u, pb = pr / 45u;
    if (pr >= 225u || lc + lp > MOD3_PACK_LCLP || pb > MOD3_PACK_PB || r.at[1])
        return 0;
    r.code = (uint32_t)r.at[2] << 24 | (uint32_t)r.at[3] << 16 | (uint32_t)r.at[4] << 8 | r.at[5];
    r.at += 6;
    if (r.code == r.range)
        return 0;
    /* the tables (boot_lzma.h's order, np position states): is-match, is-rep, G0, G1, G2, rep0-long, slots, special
     * distances, align, the two length coders, the literals */
    uint32_t np = 1u << pb, t_rep = 12u * np, t_g0 = t_rep + 12u, t_g1 = t_g0 + 12u, t_g2 = t_g1 + 12u,
             t_long = t_g2 + 12u, t_slot = t_long + 12u * np, t_spec = t_slot + 256u, t_align = t_spec + 115u,
             t_mlen = t_align + 16u, t_rlen = t_mlen + 258u + 16u * np, t_lit = t_rlen + 258u + 16u * np;
    uint16_t *tables = (uint16_t *)(void *)work;
    volatile uint16_t *p = tables;
    volatile uint8_t *o = out;
    for (uint32_t i = 0, e = MOD_LZ_P(lc + lp, pb); i < e; i++)
        p[i] = 1024u;
    uint32_t pos = 0, state = 0, rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0, len;
    for (;;) {
        if (r.end - r.at < (int)MOD_LZ_LOW && in.left)
            mod_lz_fill(&r, &in, rd);
        if (r.bad)
            return 0;
        uint32_t ps = pos & (np - 1u);
        if (!boot_rc_bit(&r, &p[state * np + ps])) {                     /* a literal */
            if (pos >= size)
                return 0;
            uint32_t prev = pos ? o[pos - 1u] : 0u, sym = 1;
            volatile uint16_t *lit = p + t_lit + 0x300u * (((pos & ((1u << lp) - 1u)) << lc) + (prev >> (8u - lc)));
            if (state >= 7u) {
                if (rep0 >= pos)
                    return 0;
                uint32_t mb = o[pos - rep0 - 1u];
                do {
                    uint32_t m = (mb >> 7) & 1u, b;
                    mb <<= 1;
                    b = boot_rc_bit(&r, &lit[((1u + m) << 8) + sym]);
                    sym = (sym << 1) | b;
                    if (m != b)
                        break;
                } while (sym < 0x100u);
            }
            while (sym < 0x100u)
                sym = (sym << 1) | boot_rc_bit(&r, &lit[sym]);
            o[pos++] = (uint8_t)sym;
            state = state < 4u ? 0u : state < 10u ? state - 3u : state - 6u;
            continue;
        }
        if (boot_rc_bit(&r, &p[t_rep + state])) {                        /* a repeated distance */
            if (!pos)
                return 0;
            if (!boot_rc_bit(&r, &p[t_g0 + state])) {
                if (!boot_rc_bit(&r, &p[t_long + state * np + ps])) {    /* one byte at rep0 */
                    if (pos >= size || rep0 >= pos)
                        return 0;
                    state = state < 7u ? 9u : 11u;
                    o[pos] = o[pos - rep0 - 1u];
                    pos++;
                    continue;
                }
            } else {
                uint32_t d;
                if (!boot_rc_bit(&r, &p[t_g1 + state]))
                    d = rep1;
                else {
                    if (!boot_rc_bit(&r, &p[t_g2 + state]))
                        d = rep2;
                    else {
                        d = rep3;
                        rep3 = rep2;
                    }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = d;
            }
            len = mod_lz_len(&r, p + t_rlen, ps, np);
            state = state < 7u ? 8u : 11u;
        } else {                                                         /* a match, or the end marker */
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            len = mod_lz_len(&r, p + t_mlen, ps, np);
            state = state < 7u ? 7u : 10u;
            uint32_t slot = boot_rc_tree(&r, p + t_slot + (len < 3u ? len : 3u) * 64u, 6);
            if (slot < 4u)
                rep0 = slot;
            else {
                uint32_t nd = (slot >> 1) - 1u;
                rep0 = (2u | (slot & 1u)) << nd;
                if (slot < 14u)
                    rep0 += boot_rc_rtree(&r, p + t_spec + rep0 - slot, nd);
                else {
                    rep0 += boot_rc_direct(&r, nd - 4u) << 4;
                    rep0 += boot_rc_rtree(&r, p + t_align, 4);
                }
            }
            if (rep0 == 0xFFFFFFFFu) {                                   /* the end: the rest only the zeros to a word */
                if (r.bad || pos != size || r.code || (uint32_t)(r.end - r.at) + in.left > 3u)
                    return 0;
                mod_lz_fill(&r, &in, rd);
                for (; r.at < r.end; r.at++)
                    if (*r.at)
                        return 0;
                *crc = in.crc;
                return !r.bad;
            }
        }
        len += 2u;
        if (r.bad || rep0 >= pos || len > size - pos)
            return 0;
        while (len--) {
            o[pos] = o[pos - rep0 - 1u];
            pos++;
        }
    }
}
