/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6's pages as its schema (fm6_schema.h) shows them: an operator's frequency in its CRS and FINE columns (pformat:
 * a ratio, CRS its step 0.5, 1, 2 .., FINE the whole ratio; or FIXED, Hz: CRS 1, 10, 100, 1000). Included by
 * eng_fm6.c (the module and host builds) */
#if !MOD_SIDE
#include "fm6_schema.h"                           /* (the module's file carries it: its SCHM chunk) */
#endif
#if MOD_SIDE                                     /* (the firmware's libc.c fmt_fix: the same digits) */
static void fmt_fix(char *b, int32_t v, int decimals)
{
    char t[12];
    int n = 0, neg = v < 0, i;
    uint32_t u = (uint32_t)(neg ? -v : v);
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
        if (n == decimals)
            t[n++] = '.';
    } while (u || n <= decimals);
    if (neg)
        t[n++] = '-';
    for (i = 0; i < n; i++)
        b[i] = t[n - 1 - i];
    b[n] = 0;
}
static void fmt_int(char *b, int32_t v) { fmt_fix(b, v, 0); }
#endif
/* 2^(x / 65536) in Q16 */
static inline __attribute__((always_inline)) uint32_t fm6_pow2(int32_t x)
{
    return (uint32_t)fm_minf(fm_exp2f((float)x * (1.0f / 65536.0f)) * 65536.0f, 4294967040.0f);
}
static void fm6_pformat(const track_t *t, const uint8_t *b, uint32_t id, int32_t v, char *val, const char **unit)
{
    uint32_t k = id / FP_OP, coarse = id % FP_OP == FP_FC;
    const uint8_t *o = b + (k % 6u) * 17u;
    int32_t c = (o[15] >> 1) & 31, f = coarse ? 0 : o[16] & 0x7F;
    (void)t;
    (void)v;
    if (!(o[15] & 1u)) {
        if (coarse && c)
            fmt_int(val, c);
        else
            fmt_fix(val, (c ? c * 100 : 50) * (100 + f) / 100, 2);
        *unit = "";
    } else {                                          /* Hz x 100 = 2^(log2(10) (COARSE % 4 + FINE / 100) + log2(100)) */
        uint32_t h = fm6_pow2(217706 * ((c & 3) * 100 + f) / 100 + 435412 - (16 << 16));
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else if (h < 100000u)
            fmt_fix(val, (int32_t)(h / 10u), 1);
        else
            fmt_int(val, (int32_t)(h / 100u));
        *unit = "Hz";
    }
}
/* a voice value by its id (its place in the 155-byte voice: Motion's, mod_schema.h MBYTES off) in the packed voice:
 * an operator's field (the sixth operator first, 21 ids / 17 bytes apart) or the voice's: byte, shift, width */
static const uint8_t FM6_OPF[21][3] = {{0, 0, 7}, {1, 0, 7}, {2, 0, 7}, {3, 0, 7}, {4, 0, 7}, {5, 0, 7}, {6, 0, 7},
    {7, 0, 7}, {8, 0, 7}, {9, 0, 7}, {10, 0, 7}, {11, 0, 2}, {11, 2, 2}, {12, 0, 3}, {13, 0, 2}, {13, 2, 3}, {14, 0, 7},
    {15, 0, 1}, {15, 1, 5}, {16, 0, 7}, {12, 3, 4}};
static const uint8_t FM6_VF[19][3] = {{102, 0, 7}, {103, 0, 7}, {104, 0, 7}, {105, 0, 7}, {106, 0, 7}, {107, 0, 7},
    {108, 0, 7}, {109, 0, 7}, {110, 0, 5}, {111, 0, 3}, {111, 3, 1}, {112, 0, 7}, {113, 0, 7}, {114, 0, 7}, {115, 0, 7},
    {116, 0, 1}, {116, 1, 3}, {116, 4, 3}, {117, 0, 7}};
static const uint8_t *fm6_field(uint32_t id, uint32_t *at)
{
    if (id < FP_PR1) {
        *at = id / FP_OP * 17u + FM6_OPF[id % FP_OP][0];
        return FM6_OPF[id % FP_OP];
    }
    if (id <= FP_TRNSP) {
        *at = FM6_VF[id - FP_PR1][0];
        return FM6_VF[id - FP_PR1];
    }
    return 0;
}
static int32_t fm6_pget(const track_t *t, const uint8_t *b, uint32_t id)
{
    uint32_t at;
    const uint8_t *f = fm6_field(id, &at);
    (void)t;
    return f ? (b[at] >> f[1]) & ((1u << f[2]) - 1u) : 0;
}
static int fm6_pput(track_t *t, uint8_t *b, uint32_t id, int32_t v)
{
    uint32_t at, m;
    const uint8_t *f = fm6_field(id, &at);
    uint8_t old;
    (void)t;
    if (!f)
        return 0;
    m = ((1u << f[2]) - 1u) << f[1];
    old = b[at];
    b[at] = (uint8_t)((b[at] & ~m) | (((uint32_t)clamp(v, 0, (int32_t)fm6_max(id)) << f[1]) & m));
    return b[at] != old;
}
