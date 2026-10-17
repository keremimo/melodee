/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Exact reference and cached routing execute in one process. The benchmark
 * excludes synthesis and reports host instructions, not device CPU savings. */
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
    assert(mod_begin(t)==active);
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
    puts("routing,changing,reference_instructions_per_block,cached_instructions_per_block");
    for(uint32_t active=0;active<3;active++)for(uint32_t changing=0;changing<(active?NELEM(changes):2u);changing++){
        double costs[2];
        for(uint32_t cached=0;cached<2;cached++){
            host_preset(t,19,0);t->lfo_val=12131;t->lfo_fade=32767;
            memset(mod_routes,0,sizeof mod_routes);
            for(uint32_t k=0;k<NMSLOT;k++){
                t->p[P_M1SRC+3u*k]=active?MS_LFO:0;
                t->p[P_M1DST+3u*k]=active==1?MD_E1+k:MD_CUT;
                t->p[P_M1AMT+3u*k]=32;
            }
            uint64_t start=instructions();
            for(uint32_t k=0;k<200000;k++){
                if(changing==1)t->p[P_M1AMT]=(int16_t)(32+(k&1));
                if(changing==2)t->p[P_M1SRC]=(int16_t)(k&1?MS_LFO:MS_MODW);
                if(changing==3)for(uint32_t slot=0;slot<NMSLOT;slot++)
                    t->p[P_M1SRC+3u*slot]=(int16_t)(1+(k+slot)%(MS_N-1));
                if(changing==4)t->p[P_M1DST]=(int16_t)(active==1?MD_E1+(k&1):MD_PITCH+(k&1));
                if(changing==5)t->p[P_M1AMT]=(int16_t)(k&1?0:32);
                if(changing==6)t->engine=(uint8_t)(k&1?19:12);
                int on=cached?mod_begin(t):mod_begin_reference(t);
                checksum+=mod.cut;if(on)mod_end(t);
            }
            costs[cached]=(double)(instructions()-start)/200000;
        }
        printf("%u,%s,%.1f,%.1f\n",active,changes[changing],costs[0],costs[1]);
        /* Routing-only host instruction gates. Rebuild rows remain reported
         * separately; they are not proof of device timing or acceptance. */
        if(costs[0]>0 && changing<4){
            assert(costs[1]<costs[0]*(active==1?.85:1.04));
        }
    }
}
int main(int argc,char **argv)
{
    assert(argc==1 || (argc==2 && !strcmp(argv[1],"--parity-only")));
    parity();
    if(argc==1)benchmark();
    return 0;
}
