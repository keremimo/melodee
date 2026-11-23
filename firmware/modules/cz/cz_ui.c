/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* On-device editing of a native CZ-1 tone (its schema's pages: cz_schema.h). The part keeps Casio's 144 bytes; a page
 * shows them as panel values (cz_panel.h's flat layout) and an edit writes back only the bytes whose encoding changed,
 * so an imported tone keeps its exact rates and levels until that value is turned. Function settings (bend, wheel,
 * aftertouch, portamento, glide, chorus) are not part of a Casio tone and have no page here. The TOOLS page's line
 * copies are the module's (pput 200 / 201); NAME and COMP are the firmware's (MOD3_A_*). Included by eng_cz.c (the
 * module and host builds) */
#include "cz_panel.h"
#if !MOD_SIDE
#include "cz_schema.h"                            /* (the module's file carries it: its SCHM chunk) */
#endif
/* Casio bytes -> panel values (the inverse of lcz_sx_encode), clamped to the panel's ranges. SUS follows the first
 * marked point before END, as the renderer does; END's own level shows 0 (the CZ ignores it). */
static __attribute__((noinline)) void cz_ed_decode(uint8_t *p,const uint8_t *d)
{
    uint32_t line=d[0]&3u,oct=(d[0]>>2)&3u,fine=d[2]>>2,mb=(d[15]>>3)&7u;
    memset(p,0,LCZ_PACKED);
    p[LCZ_LINE]=(uint8_t)(line==2u?3u:line==3u?2u:line);p[LCZ_OCT]=(uint8_t)(oct==2u?0u:oct==1u?2u:1u);
    p[LCZ_SIGN]=d[1]&1u;p[LCZ_FINE]=(uint8_t)(fine-(fine>>4));p[LCZ_DOCT]=d[3]/12u;p[LCZ_NOTE]=d[3]%12u;
    p[LCZ_VWAVE]=(uint8_t)(d[4]==4u?1u:d[4]==32u?2u:d[4]==2u?3u:0u);
    p[LCZ_VDELAY]=d[5];p[LCZ_VRATE]=d[8];p[LCZ_VDEP]=d[11];p[LCZ_MOD]=(uint8_t)(mb==3u?2u:mb==4u?1u:0u);
    for(uint32_t l=0;l<2u;l++){
        uint32_t b=LCZ_LBASE(l),o=l?71u:14u;
        p[b+LCZ_W1]=d[o]>>5;p[b+LCZ_W2]=(uint8_t)((d[o]&2u)?1u+((d[o]>>2)&7u):0u);
        p[LCZ_WIN(l)]=(uint8_t)(((d[o]&1u)<<2)|(d[o+1u]>>6));
        p[b+LCZ_KA]=d[o+2u]&15u;p[b+LCZ_KW]=d[o+4u]&15u;p[b+LCZ_LEVEL]=(uint8_t)(15u-(d[o+2u]>>4));
        for(uint32_t e=0;e<3u;e++){
            uint8_t *ep=p+LCZ_EBASE(l,e);uint32_t off=o+(e==2u?6u:e==1u?23u:40u);
            ep[17]=d[off]&7u;ep[16]=8;
            p[b+(e==0u?LCZ_VP:e==1u?LCZ_VW:LCZ_VA)]=(uint8_t)(15u-(d[off]>>4));
            for(uint32_t j=0;j<8u;j++){
                ep[j]=(uint8_t)lcz_sx_unrate(e,d[off+1u+2u*j]);ep[8u+j]=(uint8_t)lcz_sx_unlevel(e,d[off+2u+2u*j]);
                if((d[off+2u+2u*j]&128u) && ep[16]==8u && j<ep[17])ep[16]=(uint8_t)j;
            }
            ep[8u+ep[17]]=0;
        }
    }
    for(uint32_t j=0;j<LCZ_NP;j++)if(p[j]>LCZ_MAX[j])p[j]=LCZ_MAX[j];
    for(uint32_t l=0;l<2u;l++)if(!p[LCZ_LBASE(l)+LCZ_LEVEL])p[LCZ_LBASE(l)+LCZ_LEVEL]=1;
    memcpy(p+LCZ_NP,d+128,LCZ_NAME);
}

/* 0: panel value id has no effect on the tone d as it is (cz_native_render reads it so): its card is drawn
 * dim. Line 2's own values sound in LINE2 and 1+2' (1+1' copies line 1), line 1's in all but
 * LINE2; DETUNE and MOD act on line 2; SIGN on a detune; vibrato WAVE RATE DELAY need a DEPTH. An envelope
 * runs its steps up to END: rates to END's (the release to 0), levels before END's, a SUS before END */
static int cz_ed_active(const uint8_t *d, uint32_t id)
{
    uint32_t ls = d[0] & 3u, line, k, e, end;
    if (id == LCZ_SIGN)
        return ls && ((d[2] >> 2) || d[3]);
    if ((id >= LCZ_DOCT && id <= LCZ_FINE) || id == LCZ_MOD)
        return ls != 0u;
    if (id == LCZ_VWAVE || id == LCZ_VRATE || id == LCZ_VDELAY)
        return d[11] != 0u;
    if (id < LCZ_GLOBALS || id >= LCZ_NP)
        return 1;
    line = id >= LCZ_WIN(0) ? id - LCZ_WIN(0) : id < LCZ_EBASE(0, 0) ? (id - LCZ_GLOBALS) / 8u : (id - LCZ_EBASE(0, 0)) / 54u;
    if (line ? ls != 1u && ls != 3u : ls == 1u)
        return 0;
    if (id < LCZ_EBASE(0, 0) || id >= LCZ_OLD_NP)
        return 1;
    k = (id - LCZ_EBASE(0, 0)) % 18u;
    e = (id - LCZ_EBASE(0, 0)) / 18u % 3u;
    end = d[(line ? 71u : 14u) + (e == 2u ? 6u : e == 1u ? 23u : 40u)] & 7u;
    return k < 8u ? k <= end : k < 16u ? k - 8u < end : k == 16u ? end != 0u : 1;
}

/* One panel value into the tone d, as raw: the new synthesis bytes (0 = no change). Untouched bytes stay verbatim; a
 * byte whose only change is its flag bit (a rate's direction, a level's SUS mark) keeps its value bits. SUS sounds only
 * before END: turning it up past END gives "-", down from "-" gives the point before END; a new END drops a SUS at or
 * after it. */
static int cz_ed_put(const uint8_t *d,uint32_t id,uint32_t v,uint8_t *raw)
{
    uint8_t p[LCZ_PACKED],base[CZ_BYTES],next[CZ_BYTES];
    if(id>=LCZ_NP)return 0;
    memcpy(raw,d,CZ_BYTES);
    cz_ed_decode(p,raw);lcz_sx_encode(p,base,1);
    if(id>=LCZ_EBASE(0,0) && id<LCZ_OLD_NP && (id-LCZ_EBASE(0,0))%18u==16u){
        uint32_t end=p[id+1u];
        if(v!=8u && v>=end)v=v>p[id] || !end ? 8u : end-1u;
    }
    p[id]=(uint8_t)v;
    for(uint32_t l=0;l<2u;l++)for(uint32_t e=0;e<3u;e++){
        uint8_t *ep=p+LCZ_EBASE(l,e);
        ep[8u+ep[17]]=0;if(ep[16]!=8u && ep[16]>=ep[17])ep[16]=8;
    }
    lcz_sx_encode(p,next,1);
    for(uint32_t i=0;i<128u;i++){
        uint32_t x=base[i]^next[i];
        if(x==128u)raw[i]=(uint8_t)((raw[i]&127u)|(next[i]&128u));
        else if(x)raw[i]=next[i];
    }
    return cz_patch_valid(raw) && memcmp(raw,d,128u);
}
static uint32_t cz_rel_id(const uint8_t *d, uint32_t line) { return LCZ_EBASE(line, 2) + (d[CZ_ENV_END[line][2]] & 7u); }
static int32_t cz_pget(const track_t *t, const uint8_t *d, uint32_t id)
{
    uint8_t p[LCZ_PACKED];
    (void)t;
    if (id == 202u || id == 203u)
        id = cz_rel_id(d, id - 202u);
    if (id >= LCZ_NP)
        return 0;
    cz_ed_decode(p, d);
    return p[id];
}
static int cz_pput(track_t *t, uint8_t *d, uint32_t id, int32_t v)
{
    uint8_t raw[CZ_BYTES];
    (void)t;
    if (id == 200u || id == 201u) {               /* 1 > 2 / 2 > 1: line src's wave, window, key follow, level,
                                                   * velocity and envelopes over the other line (Casio's 57-byte line
                                                   * block); MOD stays line 1's */
        uint32_t s = id == 201u ? 71u : 14u, o = id == 201u ? 14u : 71u, mod = d[o + 1u] & 0x38u;
        if (!v)
            return 0;
        memcpy(d + o, d + s, 57u);
        d[o + 1u] = (uint8_t)((d[o + 1u] & ~0x38u) | mod);
        return 1;
    }
    if (id == 202u || id == 203u)
        id = cz_rel_id(d, id - 202u);
    if (!cz_ed_put(d, id, (uint32_t)v, raw))
        return 0;
    memcpy(d, raw, 128u);
    return 1;
}
static int cz_pactive(const track_t *t, const uint8_t *d, uint32_t id) { (void)t; return cz_ed_active(d, id); }
