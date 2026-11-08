/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Who a test's module file is (mod_abi3.h IDNT version 2): its IDNT chunk made from a key and a slug (or unsigned
 * from its vendor), and its SIGN chunk, made here with Ed25519 signing (RFC 8032) on mod_sign.c's own arithmetic.
 * Include after module_store.c (mod_sign.c). The test key is the host builds' (mod_sign_keys.h: its seed SHA-256 of
 * "melodee module test key"); Melodee's synths name Melodee's release key and the test key signs them, as a tree
 * without the release key does (tools/mod_build.py). */
static const uint8_t TEST_SEED[32] = {0x02, 0x8b, 0x43, 0x16, 0x50, 0x52, 0x19, 0xda, 0x8b, 0x45, 0xb5, 0x87, 0x81,
                                      0xaf, 0x5e, 0x22, 0xcd, 0x8e, 0x97, 0x71, 0x37, 0x70, 0x68, 0xf9, 0xa1, 0x23,
                                      0x2d, 0x7e, 0x87, 0x2b, 0x9f, 0x52};
static void test_reduce(uint8_t *r)                 /* r (64 bytes) mod L into its first 32 */
{
    int64_t x[64];
    for (int i = 0; i < 64; i++) x[i] = r[i];
    sg_modL(r, x);
}
/* the public key of seed into pk, and (m: n bytes) its signature into sig */
static void test_sign(const uint8_t *seed, const uint8_t *m, uint32_t n, uint8_t *sig, uint8_t *pk)
{
    sg_sha_t s;
    sg_gf p[4];
    uint8_t d[64], r[64], h[64];
    int64_t x[64];
    mod_sign_begin();
    sg_sha_init(&s); sg_sha_add(&s, seed, 32); sg_sha_end(&s, d);
    d[0] &= 248; d[31] &= 127; d[31] |= 64;
    sg_scalarbase(p, d); sg_pack(pk, p);
    if (!m)
        return;
    sg_sha_init(&s); sg_sha_add(&s, d + 32, 32); sg_sha_add(&s, m, n); sg_sha_end(&s, r);
    test_reduce(r);
    sg_scalarbase(p, r); sg_pack(sig, p);
    sg_sha_init(&s); sg_sha_add(&s, sig, 32); sg_sha_add(&s, pk, 32); sg_sha_add(&s, m, n); sg_sha_end(&s, h);
    test_reduce(h);
    for (int i = 0; i < 64; i++) x[i] = i < 32 ? r[i] : 0;
    for (int i = 0; i < 32; i++)
        for (int j = 0; j < 32; j++) x[i + j] += (int64_t)h[i] * d[j];
    sg_modL(sig + 32, x);
}
static void test_chunk(uint8_t *b, uint32_t *at, uint32_t tag, const void *p, uint32_t n)
{
    memcpy(b + *at, &tag, 4);
    memcpy(b + *at + 4, &n, 4);
    memcpy(b + *at + 8, p, n);
    for (uint32_t i = n; i < ((n + 3u) & ~3u); i++) b[*at + 8u + i] = 0;
    *at += 8u + ((n + 3u) & ~3u);
}
/* an IDNT chunk at *at in b: vendor, slug, tag; key: the identity key (0: unsigned), into id too (0: not wanted).
 * Returns the ID's first 4 bytes (the head's uid) */
static uint32_t test_idnt(uint8_t *b, uint32_t *at, const char *vendor, const char *slug, const char *tag,
                          const uint8_t *key, mod_ident_v3 *out)
{
    mod_ident_v3 id;
    memset(&id, 0, sizeof id);
    id.version = MOD3_IDNT_VERSION;
    id.flags = key ? MOD3_IDNT_SIGNED : 0u;
    memcpy(id.tag, tag, strlen(tag) < 4u ? strlen(tag) : 4u);
    memcpy(id.slug, slug, strlen(slug) < 16u ? strlen(slug) : 16u);
    memcpy(id.vendor, vendor, strlen(vendor) < 16u ? strlen(vendor) : 16u);
    if (key)
        memcpy(id.key, key, 32);
    mod_sign_begin();
    mod_id_make(&id, id.id);
    test_chunk(b, at, MOD3_IDNT_TAG, &id, sizeof id);
    if (out)
        *out = id;
    return (uint32_t)id.id[0] | (uint32_t)id.id[1] << 8 | (uint32_t)id.id[2] << 16 | (uint32_t)id.id[3] << 24;
}
/* the SIGN chunk of seed over b's bytes from the head's end to *at, at *at (the file's last) */
static void test_seal(uint8_t *b, uint32_t *at, const uint8_t *seed)
{
    uint8_t sig[64], pk[32];
    test_sign(seed, b + MOD3_HEAD, *at - MOD3_HEAD, sig, pk);
    test_chunk(b, at, MOD3_SIGN_TAG, sig, sizeof sig);
}
/* Melodee's synth slug for a name: lowercase, anything but letters, digits and '-' a '-' ("CZ-1" cz-1, "MIDI?" midi-) */
static const char *test_slug(const char *name)
{
    static char s[MOD3_NAME_MAX + 1u];
    uint32_t i = 0;
    for (; name[i] && i < MOD3_NAME_MAX; i++) {
        char c = name[i];
        s[i] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ? c : '-';
    }
    s[i] = 0;
    return s;
}
