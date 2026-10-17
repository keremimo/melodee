/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Independent Chorus kernel retained from db1efe6, Melodee-authored fx.c.
 * Compare exact histories and rings, then compare full routing traces in two
 * executables compiled with the candidate disabled/enabled. */
#ifndef MELODEE_FX_DORMANT
#define MELODEE_FX_DORMANT 1
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main
#undef __attribute__
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
/* Restore the firmware's separate fallback contract after hostsim's attributes. */
static void fx_chorus(fx_state_t *,int16_t *,uint32_t,const int32_t *,int32_t) __attribute__((noinline));
static __attribute__((noinline)) void reference(fx_state_t *restrict f,int16_t *restrict mem,uint32_t n,const int32_t *step,int32_t target)
{
    int32_t l=f->rev_l,r=f->rev_r,mix=f->mix;uint32_t pos=f->pos;
    for(uint32_t i=0;i<n;i++){
        mem[pos*2u]=(int16_t)clamp(fx_left[i]>>2,-32767,32767);
        mem[pos*2u+1u]=(int16_t)clamp(fx_right[i]>>2,-32767,32767);
        l+=step[0];r+=step[1];
        int32_t wl=fx_chorus_read(mem,pos,0,l),wr=fx_chorus_read(mem,pos,1,r);
        fx_return(target,0,0,i,wl,wr,&mix);pos=(pos+1u)&511u;
    }
    f->pos=pos;f->rev_l=l;f->rev_r=r;f->mix=mix;
}
static __attribute__((noinline)) void candidate(fx_state_t *f,int16_t *mem,uint32_t n,const int32_t *step,int32_t target)
{fx_chorus_run(f,mem,n,step,target);}
static int32_t test_rng=523;
static uint32_t random_u(void){return noise32(&test_rng);}
static void parity(void)
{
    fx_state_t a={.type=FX_CHORUS,.pos=501,.rev_l=256*65536,.rev_r=256*65536},b=a;
    int16_t am[1024],bm[1024];int32_t input[2][CTL],out[2][CTL];
    for(uint32_t i=0;i<1024;i++)am[i]=bm[i]=(int16_t)random_u();
    uint32_t dormant=0,wakes=0;
    for(uint32_t block=0;block<40000;block++){
        uint32_t n=block%37u==0?0u:1u+random_u()%CTL;
        uint32_t position=block%300u,signal=position<20u;
        /* Includes late-only samples, tiny positive/negative inputs, hot
         * clipping, unequal stereo, ring wraps, and consecutive long silence. */
        for(uint32_t i=0;i<n;i++)for(uint32_t ch=0;ch<2;ch++){
            int32_t x=signal?(int32_t)(random_u()%2000001u)-1000000:0;
            if(signal && position<8u)x=i==n-1u?(int32_t)position-4:0;
            input[ch][i]=x;
        }
        int32_t delay=128*65536+(int32_t)(random_u()%(256u*65536u));
        int32_t step[2]={n?(delay-a.rev_l)/(int32_t)n:0,n?(512*65536-delay-a.rev_r)/(int32_t)n:0};
        int32_t target=(int32_t)(random_u()%32769u);
        for(uint32_t fast=0;fast<2;fast++){
            memcpy(fx_left,input[0],n*4u);memcpy(fx_right,input[1],n*4u);
            memcpy(fx_dry_l,input[0],n*4u);memcpy(fx_dry_r,input[1],n*4u);
            if(!fast){reference(&a,am,n,step,target);memcpy(out[0],fx_left,n*4u);memcpy(out[1],fx_right,n*4u);}
            else {candidate(&b,bm,n,step,target);assert(!memcmp(out[0],fx_left,n*4u) && !memcmp(out[1],fx_right,n*4u));}
        }
        if(b.echo==512)dormant++;else if(signal && position==0)wakes++;
        assert(!memcmp(am,bm,sizeof am));
        fx_state_t normalized=b;normalized.echo=a.echo;
        assert(!memcmp(&a,&normalized,sizeof a));
        if(b.echo==512)for(uint32_t i=0;i<1024;i++)assert(!bm[i]);
    }
#if MELODEE_FX_DORMANT
    assert(dormant>20000 && wakes>100);
#endif
    puts("Chorus dormancy: 40,000 variable/zero-size blocks, exact PCM/ring/state, long tails, controls and wakeups");
}
static uint64_t hash=14695981039346656037ull;
static void digest(const void *data,size_t n)
{const uint8_t *p=data;while(n--){hash^=*p++;hash*=1099511628211ull;}}
static void trace(void)
{
    host_tracks_init();
    int32_t input[CTL],out[CTL*2];
    /* Insert, shared CHAIN and SPLIT. Other slots keep time/filter histories
     * audible long after zero input; Chorus must wait for its own exact proof. */
    for(uint32_t mode=0;mode<3;mode++)for(uint32_t effect=FX_PHASER;effect<FX_COUNT;effect++){
        for(uint32_t id=0;id<RES_COUNT;id++)resource_release(id);
        memset(fx_state,0,sizeof fx_state);memset(trk,0,sizeof trk);memset(&song,0,sizeof song);host_tracks_init();
        fx_bus_mix=0;song.g[G_FXMODE]=mode==2;fx_bus_select();fx_clear_all();
        for(uint32_t slot=0;slot<FX_SLOTS;slot++){
            uint32_t p=P_FXTYPE+slot*FX_PARAMS,g=G_MFXTYPE+slot*FX_PARAMS;
            trk[0].p[p]=song.g[g]=slot==1?effect:FX_CHORUS;
            trk[0].p[p+1]=song.g[g+1]=127;
        }
        song.g[G_FXRETURN]=127;
        for(uint32_t block=0;block<2400;block++){
            uint32_t cycle=block%600u;
            for(uint32_t slot=0;slot<FX_SLOTS;slot++){
                uint32_t p=P_FXTYPE+slot*FX_PARAMS,g=G_MFXTYPE+slot*FX_PARAMS;
                trk[0].p[p+2]=song.g[g+2]=(int16_t)((block*17u+slot*51u)%128u);
                trk[0].p[p+3]=song.g[g+3]=(int16_t)((block*7u+slot*43u)%128u);
                trk[0].p[p+1]=song.g[g+1]=cycle>550u?0:127;
                if(cycle==580u)trk[0].p[p]=song.g[g]=FX_OFF;
                if(cycle==0u)trk[0].p[p]=song.g[g]=slot==1?effect:FX_CHORUS;
            }
            spread_on=mode==0 && block%800u<400u;
            trk[0].p[P_SPRD]=79;trk[0].p[P_PAN]=(int16_t)((block/19u)%128u)-64;
            for(uint32_t i=0;i<CTL;i++){
                int32_t x=cycle<24u?(int32_t)(random_u()%100001u)-50000:0;
                input[i]=x;spread_buf[i]=x/3;fx_send_l[i]=x;fx_send_r[i]=-x/2;
                for(uint32_t k=0;k<FX_SLOTS-1;k++){fx_send_extra_l[k][i]=x/(int32_t)(k+2u);fx_send_extra_r[k][i]=-x;}
            }
            if(mode==0){int active=fx_process(&trk[0],input,CTL);digest(&active,sizeof active);}
            else {fx_bus_start(CTL);fx_bus_join();digest(&fx_bus_result,sizeof fx_bus_result);}
            digest(fx_left,sizeof fx_left);digest(fx_right,sizeof fx_right);
            for(uint32_t si=0;si<FX_STATE_COUNT;si++){
                fx_state_t f=fx_state[si];if(f.type==FX_CHORUS)f.echo=0;digest(&f,sizeof f);
                digest(&resource[RES_FX0+si].size,sizeof resource[RES_FX0+si].size);
                if(resource[RES_FX0+si].size)digest(resource[RES_FX0+si].ptr,resource[RES_FX0+si].size);
            }
            if(mode){memset(mix_l,0,sizeof mix_l);memset(mix_r,0,sizeof mix_r);master_samples(out,CTL);digest(out,sizeof out);}
            if(block==1100u)fx_clear_all();
        }
    }
    printf("%016llx\n",(unsigned long long)hash);
}
static volatile uint32_t checksum;
static void benchmark(void)
{
    puts("input,reference_instructions_per_sample,candidate_instructions_per_sample");
    for(uint32_t kind=0;kind<4;kind++){
        double cost[2];
        for(uint32_t fast=0;fast<2;fast++){
            fx_state_t f={.type=FX_CHORUS,.echo=512,.rev_l=256*65536,.rev_r=256*65536,.mix=32768};
            int16_t mem[1024]={0};int32_t step[2]={0};
            void (*volatile run)(fx_state_t *,int16_t *,uint32_t,const int32_t *,int32_t)=fast?candidate:reference;
            uint64_t start=instructions();
            for(uint32_t block=0;block<50000;block++){
                for(uint32_t i=0;i<CTL;i++){
                    int32_t x=kind==1?8000:kind==2?(i==CTL-1u?1:0):kind==3?(block%64u==0u?8000:0):0;
                    fx_left[i]=fx_dry_l[i]=x;fx_right[i]=fx_dry_r[i]=-x;
                }
                run(&f,mem,CTL,step,32768);checksum+=(uint32_t)fx_left[0];
            }
            cost[fast]=(double)(instructions()-start)/(50000u*CTL);
        }
        printf("%u,%.2f,%.2f\n",kind,cost[0],cost[1]);
        if(cost[0])assert(cost[1]<cost[0]*(kind==0?.70:1.04));
    }
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--trace")){trace();return 0;}
    parity();if(argc==1)benchmark();return 0;
}
