/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6's factory voices as host builds make them (F1..F24: the module's file carries them in its PRST chunk,
 * tools/mod_build.py writing them from these makers; the device's image makes none) and the init voice (its schema's
 * starting patch). Included by eng_fm6.c before the engine */
#include "x0x/fastmath.h"
#include "melodee_fm6.h"         /* tools/gen_fm6_patches.py: FM6_INIT, FM6_FACTORY[FM6_NFACTORY] (F1..F8: host builds) */

/* a Melodee factory voice: OP1..OP6 in patch order within an operator (DET 0..14, 7 = none), ALG 0..31,
 * TRNSP 24 = no transposition */
typedef struct {
    uint8_t op[6][FP_OP];
    uint8_t pr[4], pl[4], alg, fb, oks, lfs, lfd, lpmd, lamd, lfks, lfw, lpms, trnsp;
    char name[11];
} fm6_rom_t;
#include "eng_fm6_rom.h"         /* FM6_ROM[FM6_NROM]: F9.. */
_Static_assert(FM6_NROM == 16u && FM6_NFACTORY + FM6_NROM == FM6_NFAC, "FM6: F1..F8, F9..F24");

static void fm6_rom_patch(const fm6_rom_t *r, uint8_t *v)   /* a Melodee factory voice -> 155-byte voice */
{
    uint32_t n, i;
    for (n = 1; n <= 6u; n++)
        for (i = 0; i < FP_OP; i++)
            v[(6u - n) * FP_OP + i] = r->op[n - 1u][i];
    for (i = 0; i < 4u; i++) {
        v[FP_PR1 + i] = r->pr[i];
        v[FP_PL1 + i] = r->pl[i];
    }
    v[FP_ALG] = r->alg;
    v[FP_FB] = r->fb;
    v[FP_OKS] = r->oks;
    v[FP_LFS] = r->lfs;
    v[FP_LFD] = r->lfd;
    v[FP_LPMD] = r->lpmd;
    v[FP_LAMD] = r->lamd;
    v[FP_LKS] = r->lfks;
    v[FP_LFW] = r->lfw;
    v[FP_LPMS] = r->lpms;
    v[FP_TRNSP] = r->trnsp;
    for (i = 0; i < 10u; i++)
        v[FP_NAME + i] = (uint8_t)(r->name[i] ? r->name[i] : ' ');
    fm6_sanitize(v);
}
/* factory patch k (F1..F24) as a packed record */
static void fm6_factory(uint32_t k, uint8_t *pk)
{
    uint8_t v[FP_SIZE + 1u];
    if (!RES_OK()) { memcpy(pk, FM6_INIT, FM6_PACKED); return; }   /* (res.c: the factory voices not installed) */
    if (k < FM6_NFACTORY) {
        memcpy(pk, FM6_FACTORY[k], FM6_PACKED);
        return;
    }
    fm6_rom_patch(&FM6_ROM[(k - FM6_NFACTORY) % FM6_NROM], v);
    memset(pk, 0, FM6_PACKED);
    fm6_pack(v, pk);
}
/* preset k's patch (after F24: INIT VOICE), its length */
static uint32_t fm6_preset_patch(uint32_t k, uint8_t *p)
{
    if (k < FM6_NFAC) fm6_factory(k, p); else memcpy(p, FM6_INIT, FM6_PACKED);
    return FM6_PACKED;
}

