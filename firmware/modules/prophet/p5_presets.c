/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Sequential's 200 v1.03 programs and the presets made from them as host builds make them (the module's file carries
 * them in its PRST chunk, tools/mod_build.py writing them from these makers; the device's image makes none). Included by
 * eng_prophet_test.c */
#include "melodee_prophet_factory.h"   /* Sequential's 200 v1.03 programs (tools/gen_prophet_factory.py) */

/* Sequential's factory program k (0..P5_FACTORY_N-1): unpacked (tools/gen_prophet_factory.py pack) on host builds,
 * the Prophet module's preset k + 1 on the device; INIT without either */
static void p5_factory(uint32_t k,p5_patch_t *p)
{
    if(!RES_OK()){p5_patch_init(p);return;}         /* (res.c: the factory programs not installed) */
    const uint8_t *b=P5_FPACK+k*P5_FACTORY_BYTES;
    uint32_t pos=0;
    for(uint32_t i=0;i<P5_RAW_MAX;i++){
        uint32_t v=0;
        for(uint32_t j=0;j<P5_FWIDTH[i];j++,pos++)v|=(uint32_t)(b[pos>>3]>>(pos&7u)&1u)<<j;
        p->raw[i]=(uint8_t)(P5_FBASE[i]+v);
    }
    p->size=P5_RAW_MAX;p->model=0x32;p->command=2;p->group=(uint8_t)(k/40u);p->program=(uint8_t)(k%40u);
}
/* INIT, then Sequential's factory programs in their order (111..588 on the
 * Prophet: preset k is program k-1 of the v1.03 bank), named as the programs are. Dry, as the Prophet. */
static void p5_preset_make(uint32_t k,preset_t *pr,char *name)
{
    p5_patch_t p;
    pr->env[2]=127;
    if(!k){str_cpy(name,"INIT PROPHET",21);return;}
    p5_factory(k-1u,&p);p5_patch_name(name,&p);
}
/* preset k's patch (INIT: the starting program), its length */
static uint32_t p5_preset_patch(uint32_t k, uint8_t *p)
{
    p5_patch_t q;
    if (k && k <= P5_FACTORY_N) p5_factory(k - 1u, &q); else p5_patch_init(&q);
    memcpy(p, &q, sizeof q);
    return sizeof q;
}
