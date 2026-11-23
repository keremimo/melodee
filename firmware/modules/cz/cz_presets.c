/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Casio's 64 CZ-1 tones as host builds make them (INIT TONE and the tones: the module's file carries them in its PRST
 * chunk, tools/mod_build.py writing them from these makers; the device's image makes none). Included by eng_cz.c */
#include "melodee_cz1.h"            /* Casio's 64 CZ-1 preset tones (tools/gen_cz1_factory.py) */
/* Casio's tone k (0..CZ_FACTORY_N-1) into raw (CZ_BYTES): unpacked against CZ_DICT (tools/gen_cz1_factory.py) */
static void cz_factory(uint32_t k, uint8_t *raw)
{
    uint8_t b[2u * CZ_BYTES];
    if (!RES_OK()) { cz_patch_init(raw); return; }  /* (res.c: Casio's tones not installed) */
    memcpy(b, CZ_DICT, CZ_BYTES);
    lz_unpack(b + CZ_BYTES, CZ_LZ + CZ_LZ_AT[k % CZ_FACTORY_N], CZ_BYTES);
    memcpy(raw, b + CZ_BYTES, CZ_BYTES);
}
/* INIT TONE, then Casio's tones (BANK A..D, PTCH 1..16), dry as the CZ-1 (no effects); named as their LCD shows
 * them, or shorter (CZ_SHORT) */
static void cz_preset_make(uint32_t k, preset_t *pr, char *name)
{
    uint8_t raw[CZ_BYTES];
    uint32_t i, a = 128u, z = CZ_BYTES;
    pr->e[7] = CZ_NATIVE;
    pr->env[1] = 70; pr->env[2] = 127; pr->env[3] = 60;
    if (!k) { str_cpy(name, "INIT TONE", 21); return; }
    k--;
    pr->e[0] = (int8_t)(k / 16u);
    pr->e[1] = (int8_t)(k % 16u + 1u);
    for (i = 0; i < NELEM(CZ_SHORT); i++)
        if (CZ_SHORT[i].k == k) { str_cpy(name, CZ_SHORT[i].name, 21); return; }
    cz_factory(k, raw);
    while (a < z && raw[a] == ' ') a++;
    while (z > a && raw[z - 1u] == ' ') z--;
    for (i = 0; a < z; ) name[i++] = (char)raw[a++];
    name[i] = 0;
}
/* preset k's patch (INIT TONE: the starting tone), its length */
static uint32_t cz_preset_patch(uint32_t k, uint8_t *p)
{
    if (k) cz_factory(k - 1u, p); else cz_patch_init(p);
    return CZ_BYTES;
}
