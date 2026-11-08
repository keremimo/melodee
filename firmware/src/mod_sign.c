/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * The Ed25519 verification below follows TweetNaCl (Daniel J. Bernstein, Bernard van Gastel, Wesley Janssen, Tanja
 * Lange, Peter Schwabe, Sjaak Smetsers: public domain), restated for streaming the message from the flash. */
/* A synth module's signature (mod_abi3.h MOD3_SIGN_TAG): Ed25519 (RFC 8032) over every byte from the file's head's end
 * to the SIGN chunk, by one of Melodee's keys (MOD_SIGN_KEYS). Checked once, when a module is installed
 * (module_store.c mod_put_commit): a module without a good signature installs all the same, with a one-line warning.
 * Main loop only; no division, 64-bit multiplies (pi32v2: int64 mul in hardware). Built for size (tools/size_fns.py). */
typedef int64_t sg_gf[16];
static const sg_gf SG_D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                            0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406},
                   SG_D = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
                           0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203},
                   SG_X = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
                           0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169},
                   SG_Y = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
                           0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666},
                   SG_I = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
                           0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};
static void sg_set(sg_gf r, const sg_gf a) { for (int i = 0; i < 16; i++) r[i] = a[i]; }
static void sg_one(sg_gf r) { for (int i = 0; i < 16; i++) r[i] = !i; }
static void sg_car(sg_gf o)
{
    for (int i = 0; i < 16; i++) {
        o[i] += (int64_t)1 << 16;
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}
static void sg_sel(sg_gf p, sg_gf q, int b)
{
    int64_t c = ~(int64_t)(b - 1);
    for (int i = 0; i < 16; i++) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}
static void sg_pack25519(uint8_t *o, const sg_gf n)
{
    sg_gf m, t;
    sg_set(t, n);
    sg_car(t); sg_car(t); sg_car(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sg_sel(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}
static int sg_vn(const uint8_t *x, const uint8_t *y, int n)   /* 0: equal */
{
    uint32_t d = 0;
    for (int i = 0; i < n; i++) d |= x[i] ^ y[i];
    return (1 & ((d - 1) >> 8)) - 1;
}
static int sg_neq(const sg_gf a, const sg_gf b)
{
    uint8_t c[32], d[32];
    sg_pack25519(c, a);
    sg_pack25519(d, b);
    return sg_vn(c, d, 32);
}
static uint8_t sg_par(const sg_gf a) { uint8_t d[32]; sg_pack25519(d, a); return d[0] & 1; }
static void sg_unpack25519(sg_gf o, const uint8_t *n)
{
    for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}
static void sg_A(sg_gf o, const sg_gf a, const sg_gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void sg_Z(sg_gf o, const sg_gf a, const sg_gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }
static void sg_M(sg_gf o, const sg_gf a, const sg_gf b)
{
    int64_t t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    sg_car(o);
    sg_car(o);
}
static void sg_S(sg_gf o, const sg_gf a) { sg_M(o, a, a); }
static void sg_inv(sg_gf o, const sg_gf i)
{
    sg_gf c;
    sg_set(c, i);
    for (int a = 253; a >= 0; a--) {
        sg_S(c, c);
        if (a != 2 && a != 4) sg_M(c, c, i);
    }
    sg_set(o, c);
}
static void sg_pow2523(sg_gf o, const sg_gf i)
{
    sg_gf c;
    sg_set(c, i);
    for (int a = 250; a >= 0; a--) {
        sg_S(c, c);
        if (a != 1) sg_M(c, c, i);
    }
    sg_set(o, c);
}
/* ---- SHA-512 (FIPS 180-4), streamed */
static const uint64_t SG_K[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull, 0x3956c25bf348b538ull,
    0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull, 0xd807aa98a3030242ull, 0x12835b0145706fbeull,
    0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull, 0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull,
    0xc19bf174cf692694ull, 0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull, 0x983e5152ee66dfabull,
    0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull, 0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
    0x06ca6351e003826full, 0x142929670a0e6e70ull, 0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull,
    0x53380d139d95b3dfull, 0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
    0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull, 0xd192e819d6ef5218ull,
    0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull, 0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull,
    0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull, 0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull,
    0x682e6ff3d6b2b8a3ull, 0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull, 0xca273eceea26619cull,
    0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull, 0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull,
    0x113f9804bef90daeull, 0x1b710b35131c471bull, 0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull,
    0x431d67c49c100d4cull, 0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull};
typedef struct { uint64_t h[8], n; uint8_t b[128]; uint32_t k; } sg_sha_t;
static uint64_t sg_ror(uint64_t x, int c) { return (x >> c) | (x << (64 - c)); }
static void sg_block(uint64_t *h, const uint8_t *m)
{
    uint64_t w[80], a[8];
    for (int i = 0; i < 16; i++) {
        uint64_t v = 0;
        for (int j = 0; j < 8; j++) v = v << 8 | m[8 * i + j];
        w[i] = v;
    }
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = sg_ror(w[i - 15], 1) ^ sg_ror(w[i - 15], 8) ^ (w[i - 15] >> 7),
                 s1 = sg_ror(w[i - 2], 19) ^ sg_ror(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    for (int i = 0; i < 8; i++) a[i] = h[i];
    for (int i = 0; i < 80; i++) {
        uint64_t t1 = a[7] + (sg_ror(a[4], 14) ^ sg_ror(a[4], 18) ^ sg_ror(a[4], 41)) + ((a[4] & a[5]) ^ (~a[4] & a[6])) +
                      SG_K[i] + w[i],
                 t2 = (sg_ror(a[0], 28) ^ sg_ror(a[0], 34) ^ sg_ror(a[0], 39)) + ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
        for (int j = 7; j > 0; j--) a[j] = a[j - 1];
        a[4] += t1;
        a[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) h[i] += a[i];
}
static void sg_sha_init(sg_sha_t *s)
{
    static const uint64_t IV[8] = {0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull,
                                   0xa54ff53a5f1d36f1ull, 0x510e527fade682d1ull, 0x9b05688c2b3e6c1full,
                                   0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};
    for (int i = 0; i < 8; i++) s->h[i] = IV[i];
    s->n = 0;
    s->k = 0;
}
static void sg_sha_add(sg_sha_t *s, const uint8_t *m, uint32_t n)
{
    s->n += n;
    while (n--) {
        s->b[s->k++] = *m++;
        if (s->k == 128u) {
            sg_block(s->h, s->b);
            s->k = 0;
        }
    }
}
static void sg_sha_end(sg_sha_t *s, uint8_t *out)
{
    uint64_t bits = s->n << 3;
    uint8_t pad = 0x80;
    sg_sha_add(s, &pad, 1);
    pad = 0;
    while (s->k != 112u) sg_sha_add(s, &pad, 1);
    for (int i = 0; i < 8; i++) s->b[s->k++] = 0;   /* (the length's high 64 bits) */
    for (int i = 7; i >= 0; i--) s->b[s->k++] = (uint8_t)(bits >> (8 * i));
    sg_block(s->h, s->b);
    for (int i = 0; i < 64; i++) out[i] = (uint8_t)(s->h[i / 8] >> (8 * (7 - i % 8)));
}
/* ---- the curve */
/* Ed25519's working values, off the main loop's stack and out of RAM of their own: in the main loop's staging
 * (wire_take: one install at a time) */
typedef struct {
    struct { sg_gf a, b, c, d, t, e, f, g, h; } ad;
    sg_sha_t h;
    sg_gf p[4], q[4];
    int64_t x[64];
    uint8_t b[128];
} sg_work_t;
static sg_work_t *sg_w;
static void sg_add(sg_gf p[4], sg_gf q[4])
{
#define a sg_w->ad.a
#define b sg_w->ad.b
#define c sg_w->ad.c
#define d sg_w->ad.d
#define t sg_w->ad.t
#define e sg_w->ad.e
#define f sg_w->ad.f
#define g sg_w->ad.g
#define h sg_w->ad.h
    sg_Z(a, p[1], p[0]); sg_Z(t, q[1], q[0]); sg_M(a, a, t);
    sg_A(b, p[0], p[1]); sg_A(t, q[0], q[1]); sg_M(b, b, t);
    sg_M(c, p[3], q[3]); sg_M(c, c, SG_D2);
    sg_M(d, p[2], q[2]); sg_A(d, d, d);
    sg_Z(e, b, a); sg_Z(f, d, c); sg_A(g, d, c); sg_A(h, b, a);
    sg_M(p[0], e, f); sg_M(p[1], h, g); sg_M(p[2], g, f); sg_M(p[3], e, h);
#undef a
#undef b
#undef c
#undef d
#undef t
#undef e
#undef f
#undef g
#undef h
}
static void sg_cswap(sg_gf p[4], sg_gf q[4], uint8_t b) { for (int i = 0; i < 4; i++) sg_sel(p[i], q[i], b); }
static void sg_pack(uint8_t *r, sg_gf p[4])
{
    sg_gf tx, ty, zi;
    sg_inv(zi, p[2]);
    sg_M(tx, p[0], zi);
    sg_M(ty, p[1], zi);
    sg_pack25519(r, ty);
    r[31] ^= (uint8_t)(sg_par(tx) << 7);
}
static void sg_scalarmult(sg_gf p[4], sg_gf q[4], const uint8_t *s)
{
    sg_gf z = {0};
    sg_set(p[0], z); sg_one(p[1]); sg_one(p[2]); sg_set(p[3], z);
    for (int i = 255; i >= 0; i--) {
        uint8_t b = (s[i / 8] >> (i & 7)) & 1;
        sg_cswap(p, q, b);
        sg_add(q, p);
        sg_add(p, p);
        sg_cswap(p, q, b);
    }
}
static void sg_scalarbase(sg_gf p[4], const uint8_t *s)
{
    sg_gf q[4];
    sg_set(q[0], SG_X); sg_set(q[1], SG_Y); sg_one(q[2]); sg_M(q[3], SG_X, SG_Y);
    sg_scalarmult(p, q, s);
}
static const int64_t SG_L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
static void sg_modL(uint8_t *r, int64_t x[64])
{
    int64_t carry;
    int i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * SG_L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; j++) {
        x[j] += carry - (x[31] >> 4) * SG_L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; j++) x[j] -= carry * SG_L[j];
    for (i = 0; i < 32; i++) {
        x[i + 1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}
static int sg_unpackneg(sg_gf r[4], const uint8_t p[32])
{
    sg_gf t, chk, num, den, den2, den4, den6;
    sg_one(r[2]);
    sg_unpack25519(r[1], p);
    sg_S(num, r[1]); sg_M(den, num, SG_D); sg_Z(num, num, r[2]); sg_A(den, r[2], den);
    sg_S(den2, den); sg_S(den4, den2); sg_M(den6, den4, den2); sg_M(t, den6, num); sg_M(t, t, den);
    sg_pow2523(t, t); sg_M(t, t, num); sg_M(t, t, den); sg_M(t, t, den); sg_M(r[0], t, den);
    sg_S(chk, r[0]); sg_M(chk, chk, den);
    if (sg_neq(chk, num)) sg_M(r[0], r[0], SG_I);
    sg_S(chk, r[0]); sg_M(chk, chk, den);
    if (sg_neq(chk, num)) return -1;
    if (sg_par(r[0]) == (p[31] >> 7)) sg_Z(r[0], (sg_gf){0}, r[0]);
    sg_M(r[3], r[0], r[1]);
    return 0;
}
/* Melodee's keys (Ed25519 public keys): the release key (tools/mod_build.py signs with its private half,
 * ~/.config/melodee/module-sign.key, kept off the repository); host builds also the tests' key */
static const uint8_t MOD_SIGN_KEYS[][32] = {
#include "mod_sign_keys.h"
};
/* the signature sig (64 bytes) of the n bytes read(off, dst, len) gives from 0, by key k: 0 good */
static int mod_sign_check(const uint8_t *sig, const uint8_t *pk, uint32_t n, int (*read)(uint32_t, void *, uint32_t))
{
#define h sg_w->h
#define p sg_w->p
#define q sg_w->q
#define x sg_w->x
#define b sg_w->b
    uint8_t d[64], t[32];
    if (sig[63] & 0xE0u || sg_unpackneg(q, pk))
        return 1;
    sg_sha_init(&h);
    sg_sha_add(&h, sig, 32);
    sg_sha_add(&h, pk, 32);
    for (uint32_t at = 0, m; at < n; at += m) {
        m = n - at < sizeof b ? n - at : (uint32_t)sizeof b;
        if (read(at, b, m))
            return 1;
        sg_sha_add(&h, b, m);
    }
    sg_sha_end(&h, d);
    for (int i = 0; i < 64; i++) x[i] = d[i];
    sg_modL(d, x);
    sg_scalarmult(p, q, d);
    sg_scalarbase(q, sig + 32);
    sg_add(p, q);
    sg_pack(t, p);
    return sg_vn(sig, t, 32) != 0;
#undef h
#undef p
#undef q
#undef x
#undef b
}
/* a file's signature (its SIGN chunk at at, the last; the bytes it covers: from the head's end to the chunk) by any of
 * Melodee's keys: 1 signed, 0 not */
static uint8_t *wire_take(void);                   /* (project.c: the main loop's staging) */
static int mod_signed(const uint8_t *sig, uint32_t n, int (*read)(uint32_t, void *, uint32_t))
{
    _Static_assert(sizeof(sg_work_t) + 8u <= WIRE_SCRATCH_BYTES, "Ed25519's working values fit the staging");
    sg_w = (sg_work_t *)(void *)(((uintptr_t)wire_take() + 7u) & ~(uintptr_t)7u);
    for (uint32_t k = 0; k < sizeof MOD_SIGN_KEYS / sizeof MOD_SIGN_KEYS[0]; k++)
        if (!mod_sign_check(sig, MOD_SIGN_KEYS[k], n, read))
            return 1;
    return 0;
}
