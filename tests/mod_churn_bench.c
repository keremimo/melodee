/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Host-only rejected routing rebuild probes, retained from 6f92a22.
 * Baseline routing code originates in mod.c (292aa13/dc7c5ce); retained
 * Felucca portions remain credited there. No production probe option. */
#define MELODEE_MOD_CACHE 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
static uint64_t instructions(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 r;
    if(!proc_pid_rusage(getpid(),RUSAGE_INFO_V4,(rusage_info_t *)&r))return r.ri_instructions;
#endif
    return 0;
}
static __attribute__((noinline)) void prepare_baseline(mod_route_t *r,const track_t *t)
{
    const int16_t *p=t->p;
    memset(r,0,sizeof *r);
    r->engine=t->engine;r->valid=1;
    for(uint32_t k=0;k<NMSLOT;k++){
        uint32_t key=mod_route_key(t,k),d=(uint32_t)p[P_M1DST+3u*k];
        r->key[k]=key;
        if(!(key&MOD_ROUTE_ACTIVE))continue;
        if(d==MD_DEPTH){
            r->depth[r->nd++]=(uint8_t)k;
        }
        if(!d || d==MD_DEPTH || (d>=MD_RESERVED6 && d<=MD_RESERVED9) || d>=MD_N)continue;
        if(d<=MD_AMP){
            r->amp|=d==MD_AMP;
            r->voice[r->nv].dst=(uint8_t)d;r->voice[r->nv++].slot=(uint8_t)k;
            continue;
        }
        uint32_t j=0,id=mod_param(d);
        for(;j<r->nk && r->kid[j]!=id;j++);
        if(j==r->nk){
            const param_desc_t *pd=id>=P_E0?&ENGINES[t->engine]->edit[id-P_E0]:&TP[id];
            r->kid[j]=(uint8_t)id;r->lo[j]=pd->min;r->hi[j]=pd->max;r->nk++;
        }
        r->term[r->nt].index=(uint8_t)j;r->term[r->nt++].slot=(uint8_t)k;
    }
}

static __attribute__((noinline)) int begin_baseline(track_t *t)
{
    uint32_t first;
    for(first=0;first<NMSLOT;first++)
        if(t->p[P_M1SRC+3u*first] && (((0x1FFFFFu & ~0x3C1u)>>((uint32_t)t->p[P_M1DST+3u*first]&31u))&1u) && t->p[P_M1AMT+3u*first])break;
    if(first==NMSLOT){mod.on=0;return 0;}
    uintptr_t address=(uintptr_t)t,base=(uintptr_t)trk;
    if(address<base || address>=base+sizeof trk || (address-base)%sizeof(track_t))return mod_begin_reference(t);
    mod_route_t *r=&mod_routes[(address-base)/sizeof(track_t)];
    uint32_t changed=!r->valid || r->engine!=t->engine;
    for(uint32_t k=0;!changed && k<NMSLOT;k++)
        changed=r->key[k]!=mod_route_key(t,k);
    if(changed)prepare_baseline(r,t);
    mod.on=1;
    int32_t lfo=mulq15(t->lfo_val,t->lfo_fade),off[NMSLOT]={0};
    mod.amp=r->amp;mod.nv=0;mod.nk=r->nk;
    mod.pit=mod.cut=mod.shp=0;mod.gain=mod.ldep=32767;
    for(uint32_t k=0;k<r->nd;k++){
        uint32_t slot=r->depth[k],s=(uint32_t)t->p[P_M1SRC+3u*slot];
        mod.ldep=mulq15(mod.ldep,mod_gain(s==MS_LFO && t->p[P_LPOL]?MS_ENV:s,mod_tsrc(t,s,lfo),t->p[P_M1AMT+3u*slot]));
    }
    if(mod.ldep<32767)lfo=mulq15(lfo,mod.ldep);
    for(uint32_t k=0;k<r->nv;k++){
        uint32_t slot=r->voice[k].slot,s=(uint32_t)t->p[P_M1SRC+3u*slot],d=r->voice[k].dst;
        int32_t a=t->p[P_M1AMT+3u*slot];
        if(MS_VOICE(s)){
            mod.vsrc[mod.nv]=(uint8_t)s;mod.vdst[mod.nv]=(uint8_t)d;mod.vamt[mod.nv++]=(int8_t)a;
        }else mod_voice_add(s==MS_LFO && t->p[P_LPOL]?MS_ENV:s,d,mod_tsrc(t,s,lfo),a,&mod.pit,&mod.cut,&mod.shp,&mod.gain);
    }
    for(uint32_t j=0;j<mod.nk;j++){mod.kid[j]=r->kid[j];mod.keep[j]=t->p[r->kid[j]];}
    for(uint32_t k=0;k<r->nt;k++){
        uint32_t slot=r->term[k].slot,s=(uint32_t)t->p[P_M1SRC+3u*slot],j=r->term[k].index;
        int32_t a=t->p[P_M1AMT+3u*slot];
        off[j]+=(((mod_tsrc(t,s,lfo)*a)>>6)*(r->hi[j]-r->lo[j]))>>15;
    }
    for(uint32_t j=0;j<mod.nk;j++)t->p[r->kid[j]]=(int16_t)clamp(mod.keep[j]+off[j],r->lo[j],r->hi[j]);
    return 1;
}
static __attribute__((noinline)) void prepare_partial(mod_route_t *r,const track_t *t)
{
    const int16_t *p=t->p;
    r->amp=r->nv=r->nk=r->nt=r->nd=0;
    r->engine=t->engine;r->valid=1;
    for(uint32_t k=0;k<NMSLOT;k++){
        uint32_t key=mod_route_key(t,k),d=(uint32_t)p[P_M1DST+3u*k];
        r->key[k]=key;
        if(!(key&MOD_ROUTE_ACTIVE))continue;
        if(d==MD_DEPTH){
            r->depth[r->nd++]=(uint8_t)k;
        }
        if(!d || d==MD_DEPTH || (d>=MD_RESERVED6 && d<=MD_RESERVED9) || d>=MD_N)continue;
        if(d<=MD_AMP){
            r->amp|=d==MD_AMP;
            r->voice[r->nv].dst=(uint8_t)d;r->voice[r->nv++].slot=(uint8_t)k;
            continue;
        }
        uint32_t j=0,id=mod_param(d);
        for(;j<r->nk && r->kid[j]!=id;j++);
        if(j==r->nk){
            const param_desc_t *pd=id>=P_E0?&ENGINES[t->engine]->edit[id-P_E0]:&TP[id];
            r->kid[j]=(uint8_t)id;r->lo[j]=pd->min;r->hi[j]=pd->max;r->nk++;
        }
        r->term[r->nt].index=(uint8_t)j;r->term[r->nt++].slot=(uint8_t)k;
    }
}

static __attribute__((noinline)) int begin_partial(track_t *t)
{
    uint32_t first;
    for(first=0;first<NMSLOT;first++)
        if(t->p[P_M1SRC+3u*first] && (((0x1FFFFFu & ~0x3C1u)>>((uint32_t)t->p[P_M1DST+3u*first]&31u))&1u) && t->p[P_M1AMT+3u*first])break;
    if(first==NMSLOT){mod.on=0;return 0;}
    uintptr_t address=(uintptr_t)t,base=(uintptr_t)trk;
    if(address<base || address>=base+sizeof trk || (address-base)%sizeof(track_t))return mod_begin_reference(t);
    mod_route_t *r=&mod_routes[(address-base)/sizeof(track_t)];
    uint32_t changed=!r->valid || r->engine!=t->engine;
    for(uint32_t k=0;!changed && k<NMSLOT;k++)
        changed=r->key[k]!=mod_route_key(t,k);
    if(changed)prepare_partial(r,t);
    mod.on=1;
    int32_t lfo=mulq15(t->lfo_val,t->lfo_fade),off[NMSLOT]={0};
    mod.amp=r->amp;mod.nv=0;mod.nk=r->nk;
    mod.pit=mod.cut=mod.shp=0;mod.gain=mod.ldep=32767;
    for(uint32_t k=0;k<r->nd;k++){
        uint32_t slot=r->depth[k],s=(uint32_t)t->p[P_M1SRC+3u*slot];
        mod.ldep=mulq15(mod.ldep,mod_gain(s==MS_LFO && t->p[P_LPOL]?MS_ENV:s,mod_tsrc(t,s,lfo),t->p[P_M1AMT+3u*slot]));
    }
    if(mod.ldep<32767)lfo=mulq15(lfo,mod.ldep);
    for(uint32_t k=0;k<r->nv;k++){
        uint32_t slot=r->voice[k].slot,s=(uint32_t)t->p[P_M1SRC+3u*slot],d=r->voice[k].dst;
        int32_t a=t->p[P_M1AMT+3u*slot];
        if(MS_VOICE(s)){
            mod.vsrc[mod.nv]=(uint8_t)s;mod.vdst[mod.nv]=(uint8_t)d;mod.vamt[mod.nv++]=(int8_t)a;
        }else mod_voice_add(s==MS_LFO && t->p[P_LPOL]?MS_ENV:s,d,mod_tsrc(t,s,lfo),a,&mod.pit,&mod.cut,&mod.shp,&mod.gain);
    }
    for(uint32_t j=0;j<mod.nk;j++){mod.kid[j]=r->kid[j];mod.keep[j]=t->p[r->kid[j]];}
    for(uint32_t k=0;k<r->nt;k++){
        uint32_t slot=r->term[k].slot,s=(uint32_t)t->p[P_M1SRC+3u*slot],j=r->term[k].index;
        int32_t a=t->p[P_M1AMT+3u*slot];
        off[j]+=(((mod_tsrc(t,s,lfo)*a)>>6)*(r->hi[j]-r->lo[j]))>>15;
    }
    for(uint32_t j=0;j<mod.nk;j++)t->p[r->kid[j]]=(int16_t)clamp(mod.keep[j]+off[j],r->lo[j],r->hi[j]);
    return 1;
}
static __attribute__((noinline)) void prepare_deferred(mod_route_t *r,const track_t *t)
{
    const int16_t *p=t->p;
    memset(r,0,sizeof *r);
    r->engine=t->engine;r->valid=1;
    for(uint32_t k=0;k<NMSLOT;k++){
        uint32_t key=mod_route_key(t,k),d=(uint32_t)p[P_M1DST+3u*k];
        r->key[k]=key;
        if(!(key&MOD_ROUTE_ACTIVE))continue;
        if(d==MD_DEPTH){
            r->depth[r->nd++]=(uint8_t)k;
        }
        if(!d || d==MD_DEPTH || (d>=MD_RESERVED6 && d<=MD_RESERVED9) || d>=MD_N)continue;
        if(d<=MD_AMP){
            r->amp|=d==MD_AMP;
            r->voice[r->nv].dst=(uint8_t)d;r->voice[r->nv++].slot=(uint8_t)k;
            continue;
        }
        uint32_t j=0,id=mod_param(d);
        for(;j<r->nk && r->kid[j]!=id;j++);
        if(j==r->nk){
            const param_desc_t *pd=id>=P_E0?&ENGINES[t->engine]->edit[id-P_E0]:&TP[id];
            r->kid[j]=(uint8_t)id;r->lo[j]=pd->min;r->hi[j]=pd->max;r->nk++;
        }
        r->term[r->nt].index=(uint8_t)j;r->term[r->nt++].slot=(uint8_t)k;
    }
}

static __attribute__((noinline)) int begin_deferred(track_t *t)
{
    uint32_t first;
    for(first=0;first<NMSLOT;first++)
        if(t->p[P_M1SRC+3u*first] && (((0x1FFFFFu & ~0x3C1u)>>((uint32_t)t->p[P_M1DST+3u*first]&31u))&1u) && t->p[P_M1AMT+3u*first])break;
    if(first==NMSLOT){mod.on=0;return 0;}
    uintptr_t address=(uintptr_t)t,base=(uintptr_t)trk;
    if(address<base || address>=base+sizeof trk || (address-base)%sizeof(track_t))return mod_begin_reference(t);
    mod_route_t *r=&mod_routes[(address-base)/sizeof(track_t)];
    uint32_t changed=!r->valid || r->engine!=t->engine;
    for(uint32_t k=0;!changed && k<NMSLOT;k++)
        changed=r->key[k]!=mod_route_key(t,k);
    if(changed){
        r->engine=t->engine;r->valid=2;
        for(uint32_t k=0;k<NMSLOT;k++)r->key[k]=mod_route_key(t,k);
        return mod_begin_reference(t);
    }
    if(r->valid==2)prepare_deferred(r,t);
    mod.on=1;
    int32_t lfo=mulq15(t->lfo_val,t->lfo_fade),off[NMSLOT]={0};
    mod.amp=r->amp;mod.nv=0;mod.nk=r->nk;
    mod.pit=mod.cut=mod.shp=0;mod.gain=mod.ldep=32767;
    for(uint32_t k=0;k<r->nd;k++){
        uint32_t slot=r->depth[k],s=(uint32_t)t->p[P_M1SRC+3u*slot];
        mod.ldep=mulq15(mod.ldep,mod_gain(s==MS_LFO && t->p[P_LPOL]?MS_ENV:s,mod_tsrc(t,s,lfo),t->p[P_M1AMT+3u*slot]));
    }
    if(mod.ldep<32767)lfo=mulq15(lfo,mod.ldep);
    for(uint32_t k=0;k<r->nv;k++){
        uint32_t slot=r->voice[k].slot,s=(uint32_t)t->p[P_M1SRC+3u*slot],d=r->voice[k].dst;
        int32_t a=t->p[P_M1AMT+3u*slot];
        if(MS_VOICE(s)){
            mod.vsrc[mod.nv]=(uint8_t)s;mod.vdst[mod.nv]=(uint8_t)d;mod.vamt[mod.nv++]=(int8_t)a;
        }else mod_voice_add(s==MS_LFO && t->p[P_LPOL]?MS_ENV:s,d,mod_tsrc(t,s,lfo),a,&mod.pit,&mod.cut,&mod.shp,&mod.gain);
    }
    for(uint32_t j=0;j<mod.nk;j++){mod.kid[j]=r->kid[j];mod.keep[j]=t->p[r->kid[j]];}
    for(uint32_t k=0;k<r->nt;k++){
        uint32_t slot=r->term[k].slot,s=(uint32_t)t->p[P_M1SRC+3u*slot],j=r->term[k].index;
        int32_t a=t->p[P_M1AMT+3u*slot];
        off[j]+=(((mod_tsrc(t,s,lfo)*a)>>6)*(r->hi[j]-r->lo[j]))>>15;
    }
    for(uint32_t j=0;j<mod.nk;j++)t->p[r->kid[j]]=(int16_t)clamp(mod.keep[j]+off[j],r->lo[j],r->hi[j]);
    return 1;
}
static int (*begin_probe)(track_t *);
static uint32_t random_state=0x8f329ac1;
static uint32_t random_next(void)
{
    random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;
    return random_state;
}
static void compare(track_t *t)
{
    int16_t original[P_COUNT],expected[P_COUNT];
    memcpy(original,t->p,sizeof original);
    memset(&mod,0x5a,sizeof mod);
    int active=mod_begin_reference(t);
    __typeof__(mod) expected_mod=mod;
    memcpy(expected,t->p,sizeof expected);
    if(active)mod_end(t);
    assert(!memcmp(original,t->p,sizeof original));
    memset(&mod,0x5a,sizeof mod);
    assert(begin_probe(t)==active);
    assert(!memcmp(expected,t->p,sizeof expected));
    assert(!memcmp(&mod,&expected_mod,sizeof mod));
    if(active)mod_end(t);
    assert(!memcmp(original,t->p,sizeof original));
}
static void parity(void)
{
    static const uint32_t engines[]={19,12,15,3,10};
    host_tracks_init();
    for(uint32_t k=0;k<50000;k++){
        track_t *t=&trk[k%NPART];
        if(k<NPART || k%127u==0)host_preset(t,engines[random_next()%NELEM(engines)],0);
        if(k%3u==0)for(uint32_t slot=0;slot<NMSLOT;slot++){
            t->p[P_M1SRC+3u*slot]=(int16_t)(random_next()%(MS_N+3u));
            t->p[P_M1DST+3u*slot]=(int16_t)(random_next()%(MD_N+34u));
            t->p[P_M1AMT+3u*slot]=(int16_t)((int32_t)(random_next()%128u)-64);
        }
        t->p[P_LPOL]=random_next()&1;
        t->lfo_val=(int32_t)(random_next()%65535u)-32767;t->lfo_fade=random_next()%32768u;
        t->m_env=random_next()%32768u;t->m_vel=random_next()%128u;t->m_key=random_next()%128u;
        t->m_rnd=(int16_t)random_next();t->mw=random_next()%128u;t->at=random_next()%128u;t->ex_off=random_next()%128u;
        t->lfo_rnd=random_next();t->lfo_prev=(int32_t)(random_next()%65536u)-32768;t->lfo_ph=random_next();
        compare(t);compare(t); /* unchanged key, moving values and engine changes */
    }
    /* Every legal source/destination, duplicated destinations, DEPTH order,
     * changes back to a previous configuration and changing parameter bases. */
    track_t *t=&trk[0];
    for(uint32_t e=0;e<NELEM(engines);e++){
        host_preset(t,engines[e],0);
        for(uint32_t s=0;s<MS_N;s++)for(uint32_t d=0;d<MD_N;d++)for(uint32_t k=0;k<4;k++){
            for(uint32_t slot=0;slot<NMSLOT;slot++){
                t->p[P_M1SRC+3u*slot]=(int16_t)s;t->p[P_M1DST+3u*slot]=(int16_t)d;
                t->p[P_M1AMT+3u*slot]=(int16_t)(slot&1?-64:63);
            }
            t->p[P_PAN]=(int16_t)(k*31-46);compare(t);
            t->p[P_M1AMT]=(int16_t)(k==0?0:k==1?-63:k==2?1:63);compare(t);
            t->p[P_M2AMT]=0;compare(t);t->p[P_M2AMT]=-64;compare(t);
        }
    }
    /* Change every valid source without touching the destination layout.
     * Include transitions between per-voice and per-track dependencies, then
     * invalid signed values, inactive slots, collisions and mixed DEPTH. */
    static const int16_t invalid[]={-32768,-1,MS_N,32767};
    for(uint32_t d=MD_PITCH;d<MD_N;d++){
        host_preset(t,19,0);
        for(uint32_t slot=0;slot<NMSLOT;slot++){
            t->p[P_M1SRC+3u*slot]=MS_LFO;
            t->p[P_M1DST+3u*slot]=(int16_t)(slot==3?MD_DEPTH:d);
            t->p[P_M1AMT+3u*slot]=(int16_t)(slot&1?-64:63);
        }
        compare(t);
        for(uint32_t source=1;source<MS_N;source++)for(uint32_t slot=0;slot<NMSLOT;slot++){
            uint32_t key=mod_route_key(t,slot);
            t->p[P_M1SRC+3u*slot]=(int16_t)source;
            assert(mod_route_key(t,slot)==key);
            compare(t);
            for(uint32_t bad=0;bad<NELEM(invalid);bad++){
                t->p[P_M1SRC+3u*slot]=invalid[bad];compare(t);
            }
            t->p[P_M1SRC+3u*slot]=0;compare(t);
            t->p[P_M1SRC+3u*slot]=(int16_t)source;compare(t);
        }
    }
    /* Non-track callers retain the reference path without indexing the cache. */
    track_t detached=*t;compare(&detached);
    printf("mod routes: exact full prepared state/parameter parity; cache %zu B\n",sizeof mod_routes);
}
static volatile uint32_t checksum;
static void benchmark(void)
{
    track_t *t=&trk[0];host_preset(t,19,0);t->lfo_val=12131;t->lfo_fade=32767;
    static const char *changes[]={"static","amount","source","all_sources","destination","zero_crossing","engine"};
    puts("routing,changing,reference_instructions_per_block,retained_cache,partial_reset_probe,deferred_probe");
    for(uint32_t active=0;active<3;active++)for(uint32_t changing=0;changing<(active?NELEM(changes):2u);changing++){
        double costs[4];
        for(uint32_t cached=0;cached<4;cached++){
            host_preset(t,19,0);t->lfo_val=12131;t->lfo_fade=32767;
            memset(mod_routes,0,sizeof mod_routes);
            for(uint32_t k=0;k<NMSLOT;k++){
                t->p[P_M1SRC+3u*k]=active?MS_LFO:0;
                t->p[P_M1DST+3u*k]=active==1?MD_E1+k:MD_CUT;
                t->p[P_M1AMT+3u*k]=32;
            }
            int (*volatile run)(track_t *)=cached==0?mod_begin_reference:cached==1?begin_baseline:cached==2?begin_partial:begin_deferred;
            uint64_t start=instructions();
            for(uint32_t k=0;k<200000;k++){
                if(changing==1)t->p[P_M1AMT]=(int16_t)(32+(k&1));
                if(changing==2)t->p[P_M1SRC]=(int16_t)(k&1?MS_LFO:MS_MODW);
                if(changing==3)for(uint32_t slot=0;slot<NMSLOT;slot++)
                    t->p[P_M1SRC+3u*slot]=(int16_t)(1+(k+slot)%(MS_N-1));
                if(changing==4)t->p[P_M1DST]=(int16_t)(active==1?MD_E1+(k&1):MD_PITCH+(k&1));
                if(changing==5)t->p[P_M1AMT]=(int16_t)(k&1?0:32);
                if(changing==6)t->engine=(uint8_t)(k&1?19:12);
                int on=run(t);
                checksum+=mod.cut;if(on)mod_end(t);
            }
            costs[cached]=(double)(instructions()-start)/200000;
        }
        printf("%u,%s,%.1f,%.1f,%.1f,%.1f\n",active,changes[changing],costs[0],costs[1],costs[2],costs[3]);
        /* These probes are rejected, not improvement gates. Retain their
         * steady/changing costs without promoting either to firmware. */
    }
}

int main(int argc,char **argv) {
 int (*const probes[])(track_t *)={begin_baseline,begin_partial,begin_deferred};
 for(uint32_t i=0;i<NELEM(probes);i++){begin_probe=probes[i];memset(mod_routes,0xa5,sizeof mod_routes);parity();}
 if(argc==1)benchmark();
 return 0;
}
