/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Comparator retained from mix.c at f109b21 (Melodee-authored mono mixer).
 * Compare all outputs, sends, peaks and stems, including noncenter fallback. */
#define MELODEE_MIX_FAST 1
#define MELODEE_USB_AUDIO 1
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
static __attribute__((noinline)) void reference(track_t *t,int32_t *b,uint32_t n,const mix_cache_t *mc)
{
    uint32_t send=fx_send_on?fx_send_gain(t):0u;
    int32_t lvl=mc->lvl,gl=mc->gl,gr=mc->gr,pk=t->peak;
    for(uint32_t i=0;i<n;i++){
        int32_t x=clamp(((clamp(b[i],-884000,884000)>>2)*lvl)>>10,-524287,524287),a=x<0?-x:x;
#if MELODEE_USB_AUDIO
        if(track_capture_on)track_capture[i*NTRK+(uint32_t)(t-trk)]=x;
#endif
        if(a>pk)pk=a;
        mix_add((x*gl)>>12,(x*gr)>>12,i,send);
    }
    t->peak=pk;
}
/* hostsim drops target section attributes; restore only this function's
 * noinline contract so the fallback benchmark matches the firmware split. */
static void mix_mono_general(track_t *,int32_t *,uint32_t,const mix_cache_t *) __attribute__((noinline));
static __attribute__((noinline)) void candidate(track_t *t,int32_t *b,uint32_t n,const mix_cache_t *mc)
{ mix_mono_samples(t,b,n,mc); }
static int32_t source[CTL];
static struct { int32_t left[CTL],right[CTL],send_l[CTL],send_r[CTL],capture[CTL*NTRK],peak; } expected;
static void reset(void)
{
    for(uint32_t i=0;i<CTL;i++){
        mix_l[i]=(int32_t)i*139;mix_r[i]=-(int32_t)i*173;
        fx_send_l[i]=319;fx_send_r[i]=-497;
    }
    for(uint32_t i=0;i<CTL*NTRK;i++)track_capture[i]=123456;
}
static void parity(void)
{
    host_tracks_init();
    static const int32_t edge[]={INT32_MIN,-884001,-884000,-524288,-7,-4,-1,0,1,3,4,7,524287,884000,884001,INT32_MAX};
    for(uint32_t i=0;i<CTL;i++)source[i]=edge[i%NELEM(edge)];
    for(uint32_t ti=0;ti<NTRK;ti++)for(int32_t level=0;level<128;level++)for(int32_t pan=-64;pan<64;pan++)
        for(uint32_t send=0;send<3;send++)for(uint32_t capture=0;capture<2;capture++){
            track_t *t=&trk[ti];t->p[P_LEVEL]=(int16_t)level;t->p[P_PAN]=(int16_t)pan;
            t->p[P_FXSEND]=(int16_t)(send==2?127:send);fx_send_on=send!=0;track_capture_on=(uint8_t)capture;
            mix_cache_t mc;mix_cache_update(t,&mc);
            reset();t->peak=123;reference(t,source,CTL,&mc);
            memcpy(expected.left,mix_l,sizeof mix_l);memcpy(expected.right,mix_r,sizeof mix_r);
            memcpy(expected.send_l,fx_send_l,sizeof fx_send_l);memcpy(expected.send_r,fx_send_r,sizeof fx_send_r);
            memcpy(expected.capture,track_capture,sizeof track_capture);expected.peak=t->peak;
            reset();t->peak=123;candidate(t,source,CTL,&mc);
            assert(!memcmp(expected.left,mix_l,sizeof mix_l) && !memcmp(expected.right,mix_r,sizeof mix_r));
            assert(!memcmp(expected.send_l,fx_send_l,sizeof fx_send_l) && !memcmp(expected.send_r,fx_send_r,sizeof fx_send_r));
            assert(!memcmp(expected.capture,track_capture,sizeof track_capture) && expected.peak==t->peak);
        }
    puts("mono mixer: every level/pan, send extremes, capture on/off, all tracks and input clipping exact");
}
static volatile uint32_t checksum;
static void benchmark(void)
{
    puts("pan,send,reference_instructions_per_sample,fast_instructions_per_sample");
    track_t *t=&trk[0];track_capture_on=0;t->p[P_LEVEL]=100;
    for(uint32_t pan=0;pan<2;pan++)for(uint32_t send=0;send<2;send++){
        double cost[2];t->p[P_PAN]=(int16_t)(pan?17:0);t->p[P_FXSEND]=127;fx_send_on=(uint8_t)send;
        mix_cache_t mc;mix_cache_update(t,&mc);
        for(uint32_t fast=0;fast<2;fast++){
            void (*volatile run)(track_t *,int32_t *,uint32_t,const mix_cache_t *)=fast?candidate:reference;
            uint64_t start=instructions();
            for(uint32_t k=0;k<100000;k++){
                reset();run(t,source,CTL,&mc);
                checksum+=(uint32_t)mix_l[0];
            }
            cost[fast]=(double)(instructions()-start)/(100000u*CTL);
        }
        printf("%u,%u,%.1f,%.1f\n",pan,send,cost[0],cost[1]);
        if(cost[0]>0)assert(cost[1]<cost[0]*(!pan && send?.85:1.04));
    }
}
int main(int argc,char **argv)
{
    assert(argc==1 || (argc==2 && !strcmp(argv[1],"--parity-only")));
    parity();if(argc==1)benchmark();return 0;
}
