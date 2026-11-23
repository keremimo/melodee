/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Charles Vestal */
/* boot_lzma.h against what Python made (tests/boot_audio_test.py):
 *   boot_lzma_test PACKED RAW SIZE    a stream
 *   boot_lzma_test -c STORED RAW      boot_uncall
 *   boot_lzma_test -d STORED RAW      boot_undelta and boot_take */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "../firmware/src/boot_lzma.h"
static uint8_t *rd(const char *path,long *n)
{
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));*n=ftell(f);rewind(f);
    uint8_t *b=malloc((size_t)*n+64u);assert(b);assert(fread(b,1,(size_t)*n,f)==(size_t)*n);fclose(f);return b;
}
static int filters(char kind,const char *stored,const char *raw)
{
    long n,m;uint8_t *s=rd(stored,&n),*r=rd(raw,&m);assert(n==m);
    uint8_t *g=malloc((size_t)n+32u);assert(g);
    memset(g,0xa7,(size_t)n+32u);memcpy(g+16,s,(size_t)n);
    if(kind=='c')boot_uncall(g+16,(uint32_t)n);
    else{
        assert(!(n&3));uint32_t *w=malloc((size_t)n+4u),*t=malloc((size_t)n+4u);assert(w&&t);
        memcpy(w,g+16,(size_t)n);
        boot_undelta(w,w+n/4);
        assert(boot_take(t,t+n/4,w)==w+n/4);
        memcpy(g+16,t,(size_t)n);free(w);free(t);
    }
    assert(!memcmp(g+16,r,(size_t)n));
    for(int i=0;i<16;i++)assert(g[i]==0xa7&&g[n+16+i]==0xa7);
    free(s);free(r);free(g);return 0;
}
int main(int argc,char **argv)
{
    assert(argc==4);
    if(argv[1][0]=='-')return filters(argv[1][1],argv[2],argv[3]);
    uint32_t size=(uint32_t)strtoul(argv[3],0,10);
    uint16_t *probs=malloc(BOOT_LZMA_PROBS*sizeof *probs);assert(probs);
    long n;uint8_t *in=rd(argv[1],&n),*out=malloc(size+16u);assert(out);
    memset(out,0x5a,size+16u);assert(boot_lzma(out,size,in,(uint32_t)n,probs));
    FILE *f=fopen(argv[2],"rb");assert(f);uint8_t *expected=malloc(size?size:1u);assert(expected);assert(fread(expected,1,size,f)==size);fclose(f);assert(!memcmp(out,expected,size));
    for(uint32_t i=size;i<size+16u;i++)assert(out[i]==0x5a);
    if(n)assert(!boot_lzma(out,size,in,(uint32_t)n-1u,probs));            /* cut short */
    if(size)assert(!boot_lzma(out,size-1u,in,(uint32_t)n,probs));         /* more than the room */
    assert(!boot_lzma(out,size+1u,in,(uint32_t)n,probs));                 /* less than asked */
    in[n]=0;assert(!boot_lzma(out,size,in,(uint32_t)n+1u,probs));         /* a byte after the end */
    /* Deterministic malformed streams and mutated valid streams, bounded output.
     * ASan/UBSan checks both ends; explicit guards check the destination bound. */
    uint32_t seed=90125u;uint8_t fuzz[512],bounded[4096+32];
    for(uint32_t trial=0;trial<10000u;trial++){
        seed=seed*1664525u+1013904223u;uint32_t len=seed%sizeof fuzz;
        seed=seed*1664525u+1013904223u;uint32_t cap=seed%4097u;
        for(uint32_t i=0;i<len;i++){
            seed=seed*1664525u+1013904223u;
            fuzz[i]=(trial&1u) && i<(uint32_t)n?in[i]:(uint8_t)(seed>>24);
        }
        if(len){seed=seed*1664525u+1013904223u;fuzz[seed%len]^=(uint8_t)(seed>>16);}
        memset(bounded,0xa7,sizeof bounded);
        (void)boot_lzma(bounded+16u,cap,fuzz,len,probs);
        for(uint32_t i=0;i<16u;i++)assert(bounded[i]==0xa7);
        for(uint32_t i=16u+cap;i<sizeof bounded;i++)assert(bounded[i]==0xa7);
        /* the call filter stays inside any length, whatever it finds */
        memset(bounded,0xa7,sizeof bounded);memcpy(bounded+16u,fuzz,len);
        for(uint32_t i=0;i+1u<len;i+=2u)if(fuzz[i]&1u){bounded[16u+i]=(uint8_t)(0x80u|(fuzz[i]&0x3Fu));bounded[17u+i]=0xEAu;}
        boot_uncall(bounded+16u,len);
        for(uint32_t i=0;i<16u;i++)assert(bounded[i]==0xa7);
        for(uint32_t i=16u+len;i<sizeof bounded;i++)assert(bounded[i]==0xa7);
    }
    free(in);free(out);free(expected);free(probs);return 0;
}
