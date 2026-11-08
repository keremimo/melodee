/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Sequential's 200 v1.03 programs (melodee_prophet_factory.h, in the resource region), the presets made from them, a
 * program accepted on a part: the firmware's, with or without the Prophet engine (a synth module on the device:
 * firmware/modules/prophet makes its presets here, p5_preset_make imported) */
#include "melodee_prophet_factory.h"   /* Sequential's 200 v1.03 programs (tools/gen_prophet_factory.py) */
/* Native performance controls become the track's allocator settings at load
 * or native edit. Afterwards Melodee's VOICE/PRIO/DETUNE pages can override. */
static void p5_track_accept(track_t *t)
{
    const uint8_t *p=p5_patch_of(t)->raw;
    t->p[P_VOICE]=p[P5_UNISON]?V_UNISON:V_POLY;
    t->p[P_PRIO]=p[P5_RETRIGGER]<2u?1:0;
    t->p[P_DETUNE]=clamp(p[P5_UNISON_DETUNE],0,7)*127/7;
    t->p[P_ALLOC]=1; /* repeated keys reuse a voice, as the original does */
}
/* Sequential's factory program k (0..P5_FACTORY_N-1), unpacked (tools/gen_prophet_factory.py pack) */
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
/* preset k's name into nm (cap bytes): as eng_preset names it, the engine's module loaded or not (the browser, the
 * preset card) */
static void p5_preset_name(uint32_t k,char *nm,uint32_t cap)
{
    preset_t pr;char name[21];
    memset(&pr,0,sizeof pr);name[0]=0;
    p5_preset_make(k,&pr,name);str_cpy(nm,name,cap);
}
/* A factory preset's native program; preset 0 is INIT. */
static void p5_preset_loaded(track_t *t,uint32_t pi)
{
    p5_patch_t *p=p5_patch_of(t);
    if(pi && pi<=P5_FACTORY_N)p5_factory(pi-1u,p);else p5_patch_init(p);
    p5_track_accept(t);
}
