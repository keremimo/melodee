/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Copyright (C) 2026 Charles Vestal */
/* Bounded raw-LZMA1 decoder for the boot load image (tools/boot_audio.py: lc 3, lp 1, pb 1, with the end marker),
 * after Igor Pavlov's LZMA specification (LzmaSpec.cpp, public domain), and the packager's two filters undone. Boot
 * only: no heap, globals, libc, or calls into the SRAM being initialized; the caller lends the probability tables
 * (BOOT_LZMA_PROBS uint16_t). Strict: the output exactly size bytes, then the end marker, every input byte used and
 * the range coder finished at zero. */
#ifndef MELODEE_BOOT_LZMA_H
#define MELODEE_BOOT_LZMA_H
#include <stdint.h>
#define BOOT_LZMA_MAGIC 0x32414D4Cu  /* LMA2 */
#define BOOT_LZMA_LC 3u
#define BOOT_LZMA_LP 1u
#define BOOT_LZMA_PB 1u
#define BL_STATES 12u
#define BL_POS (1u << BOOT_LZMA_PB)
/* the probability tables, in one array (offsets below); the length coders 2 + 2 x 8 x BL_POS + 256 each */
#define BL_LEN (2u + 16u * BL_POS + 256u)
#define BL_IS_MATCH 0u
#define BL_IS_REP (BL_IS_MATCH + BL_STATES * BL_POS)
#define BL_G0 (BL_IS_REP + BL_STATES)
#define BL_G1 (BL_G0 + BL_STATES)
#define BL_G2 (BL_G1 + BL_STATES)
#define BL_REP0_LONG (BL_G2 + BL_STATES)
#define BL_SLOT (BL_REP0_LONG + BL_STATES * BL_POS)
#define BL_SPEC (BL_SLOT + 4u * 64u)
#define BL_ALIGN (BL_SPEC + 1u + 128u - 14u)
#define BL_MATCH_LEN (BL_ALIGN + 16u)
#define BL_REP_LEN (BL_MATCH_LEN + BL_LEN)
#define BL_LIT (BL_REP_LEN + BL_LEN)
#define BOOT_LZMA_PROBS (BL_LIT + (0x300u << (BOOT_LZMA_LC + BOOT_LZMA_LP)))
typedef struct {const uint8_t *at,*end;uint32_t range,code;int bad;} boot_rc_t;
static inline void boot_rc_norm(boot_rc_t *r)
{
    if(r->range<(1u<<24)){
        if(r->at==r->end){r->bad=1;return;}
        r->range<<=8;r->code=(r->code<<8)|*r->at++;
    }
}
/* (one copy: inlined into every caller it costs about 1 KB, for about 40% fewer instructions per bit) */
static __attribute__((noinline)) uint32_t boot_rc_bit(boot_rc_t *r,volatile uint16_t *p)
{
    uint32_t v=*p,bound=(r->range>>11)*v,b;
    if(r->code<bound){r->range=bound;*p=(uint16_t)(v+((2048u-v)>>5));b=0;}
    else{r->range-=bound;r->code-=bound;*p=(uint16_t)(v-(v>>5));b=1;}
    boot_rc_norm(r);return b;
}
static uint32_t boot_rc_tree(boot_rc_t *r,volatile uint16_t *p,uint32_t bits)
{
    uint32_t m=1;
    for(uint32_t i=0;i<bits;i++)m=(m<<1)+boot_rc_bit(r,&p[m]);
    return m-(1u<<bits);
}
static uint32_t boot_rc_rtree(boot_rc_t *r,volatile uint16_t *p,uint32_t bits)
{
    uint32_t m=1,s=0;
    for(uint32_t i=0;i<bits;i++){uint32_t b=boot_rc_bit(r,&p[m]);m=(m<<1)+b;s|=b<<i;}
    return s;
}
static uint32_t boot_rc_direct(boot_rc_t *r,uint32_t bits)
{
    uint32_t s=0;
    while(bits--){
        r->range>>=1;r->code-=r->range;uint32_t t=0u-(r->code>>31);r->code+=r->range&t;
        if(r->code==r->range)r->bad=1;
        boot_rc_norm(r);s=(s<<1)+(t+1u);
    }
    return s;
}
static uint32_t boot_rc_len(boot_rc_t *r,volatile uint16_t *l,uint32_t ps)
{
    if(!boot_rc_bit(r,&l[0]))return boot_rc_tree(r,l+2u+ps*8u,3);
    if(!boot_rc_bit(r,&l[1]))return 8u+boot_rc_tree(r,l+2u+BL_POS*8u+ps*8u,3);
    return 16u+boot_rc_tree(r,l+2u+16u*BL_POS,8);
}
/* out: size bytes from the n at in; p: BOOT_LZMA_PROBS of scratch. 1: unpacked, 0: malformed (out partly written) */
static __attribute__((noinline)) int boot_lzma(uint8_t *out,uint32_t size,const uint8_t *in,uint32_t n,uint16_t *probs)
{
    /* Volatile: the table fill and the copies cannot become calls into uninitialized RAM (memset, memcpy). */
    volatile uint16_t *p=probs;volatile uint8_t *o=out;
    for(uint32_t i=0;i<BOOT_LZMA_PROBS;i++)p[i]=1024u;
    if(n<5u||in[0])return 0;
    boot_rc_t r={in+5,in+n,0xFFFFFFFFu,(uint32_t)in[1]<<24|(uint32_t)in[2]<<16|(uint32_t)in[3]<<8|in[4],0};
    uint32_t pos=0,state=0,rep0=0,rep1=0,rep2=0,rep3=0;
    if(r.code==r.range)return 0;
    for(;;){
        if(r.bad)return 0;
        uint32_t ps=pos&(BL_POS-1u),len;
        if(!boot_rc_bit(&r,&p[BL_IS_MATCH+state*BL_POS+ps])){         /* a literal */
            if(pos>=size)return 0;
            uint32_t prev=pos?o[pos-1u]:0u,sym=1;
            volatile uint16_t *lp=p+BL_LIT+0x300u*(((pos&((1u<<BOOT_LZMA_LP)-1u))<<BOOT_LZMA_LC)+(prev>>(8u-BOOT_LZMA_LC)));
            if(state>=7u){
                if(rep0>=pos)return 0;
                uint32_t mb=o[pos-rep0-1u];
                do{
                    uint32_t m=(mb>>7)&1u,b;mb<<=1;
                    b=boot_rc_bit(&r,&lp[((1u+m)<<8)+sym]);sym=(sym<<1)|b;
                    if(m!=b)break;
                }while(sym<0x100u);
            }
            while(sym<0x100u)sym=(sym<<1)|boot_rc_bit(&r,&lp[sym]);
            o[pos++]=(uint8_t)sym;
            state=state<4u?0u:state<10u?state-3u:state-6u;
            continue;
        }
        if(boot_rc_bit(&r,&p[BL_IS_REP+state])){                       /* a repeated distance */
            if(!pos)return 0;
            if(!boot_rc_bit(&r,&p[BL_G0+state])){
                if(!boot_rc_bit(&r,&p[BL_REP0_LONG+state*BL_POS+ps])){  /* one byte at rep0 */
                    if(pos>=size||rep0>=pos)return 0;
                    state=state<7u?9u:11u;o[pos]=o[pos-rep0-1u];pos++;continue;
                }
            }else{
                uint32_t d;
                if(!boot_rc_bit(&r,&p[BL_G1+state]))d=rep1;
                else{if(!boot_rc_bit(&r,&p[BL_G2+state]))d=rep2;else{d=rep3;rep3=rep2;}rep2=rep1;}
                rep1=rep0;rep0=d;
            }
            len=boot_rc_len(&r,p+BL_REP_LEN,ps);state=state<7u?8u:11u;
        }else{                                                         /* a match, or the end marker */
            rep3=rep2;rep2=rep1;rep1=rep0;
            len=boot_rc_len(&r,p+BL_MATCH_LEN,ps);state=state<7u?7u:10u;
            uint32_t slot=boot_rc_tree(&r,p+BL_SLOT+(len<3u?len:3u)*64u,6);
            if(slot<4u)rep0=slot;
            else{
                uint32_t nd=(slot>>1)-1u;rep0=(2u|(slot&1u))<<nd;
                if(slot<14u)rep0+=boot_rc_rtree(&r,p+BL_SPEC+rep0-slot,nd);
                else{rep0+=boot_rc_direct(&r,nd-4u)<<4;rep0+=boot_rc_rtree(&r,p+BL_ALIGN,4);}
            }
            if(rep0==0xFFFFFFFFu)                                      /* the end: all of it, all the input, clean */
                return !r.bad&&pos==size&&r.at==r.end&&r.code==0;
        }
        len+=2u;
        if(r.bad||rep0>=pos||len>size-pos)return 0;
        while(len--){o[pos]=o[pos-rep0-1u];pos++;}
    }
}
/* words from .. to start .. end; the next source. Volatile, as in boot_lzma: never a call to memcpy */
static __attribute__((noinline)) const uint32_t *boot_take(uint32_t *start,uint32_t *end,const uint32_t *from)
{
    volatile uint32_t *d=start,*e=end;const volatile uint32_t *s=from;
    while(d<e)*d++=*s++;
    return from+(end-start);
}
/* the tables, stored as second differences of their words (smooth curves: float tanh and 2^x): summed twice */
static __attribute__((noinline)) void boot_undelta(uint32_t *start,uint32_t *end)
{
    volatile uint32_t *d=start,*e=end;uint32_t a=0,b=0;
    while(d<e){a+=*d;b+=a;*d++=b;}
}
/* the code, its call targets stored absolute (a call is [80..BF] EA LL HH: 22 bits of halfwords after that first
 * byte's two top bits, low bytes last): calls to one function then look alike wherever they are. The packager scans
 * the same way (a hit skips its four bytes), so any bytes that look like a call come back too */
static __attribute__((noinline)) void boot_uncall(uint8_t *code,uint32_t n)
{
    volatile uint8_t *p=code;
    for(uint32_t i=0;n>=4u&&i<=n-4u;){
        uint32_t b=p[i];
        if(p[i+1u]==0xEAu&&b>=0x80u&&b<=0xBFu){
            uint32_t d=((b&0x3Fu)<<16|(uint32_t)p[i+3u]<<8|p[i+2u])-(i>>1);
            p[i]=(uint8_t)(0x80u|((d>>16)&0x3Fu));p[i+2u]=(uint8_t)d;p[i+3u]=(uint8_t)(d>>8);i+=4u;
        }else i+=2u;
    }
}
#endif
