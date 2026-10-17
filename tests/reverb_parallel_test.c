/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define MELODEE_AUDIO_PROFILE 1
#define MELODEE_AUDIO_BENCH 1
static int reverb_test_join(void);
#define AUDIO_JOIN_WRAPPER reverb_test_join
#define AUDIO_MATRIX_INCLUDE 1
#include "dual_core_test.c"
#undef audio_worker_join
static int drop_join;
static int reverb_test_join(void)
{
    int ok=audio_worker_join();
    if(drop_join){drop_join=0;audio_worker_online=0;return 0;}
    return ok;
}
#define BLOCKS 256u
static int32_t expected[BLOCKS][2u*CTL];
static uint64_t state_hash[BLOCKS];
static uint64_t hash_bytes(uint64_t h,const void *p,uint32_t n)
{const uint8_t *b=p;for(uint32_t i=0;i<n;i++){h^=b[i];h*=1099511628211ull;}return h;}
static uint64_t tanks(void)
{
    uint64_t h=hash_bytes(1469598103934665603ull,fx_state,sizeof fx_state);
    for(uint32_t p=0;p<NPART;p++)for(uint32_t k=0;k<FX_SLOTS;k++){
        uint32_t id=RES_FX0+p*FX_SLOTS+k;
        if(resource[id].size)h=hash_bytes(h,resource[id].ptr,resource[id].size);
    }
    return h;
}
static void prepare(uint32_t variant,int enabled,int serial)
{
    setup_case(8,0,0);memset(&song,0,sizeof song);song.ntrk=4;song.master_q12=2048;song.g[G_BPM]=120;
    memset(fx_state,0,sizeof fx_state);fx_epoch=17;fx_reverb_pending=0;
    fx_bus_mix=fx_bus_target=fx_bus_pending=fx_bus_result=0;
    serial_render=serial;audio_worker_online=1;fx_reverb_parallel_enabled=(uint8_t)enabled;
    const uint32_t engines[]={ENGI_PROPHET,ENGI_DRUM,ENGI_FM6,ENGI_CZ};
    for(uint32_t p=0;p<NPART;p++){
        host_preset(&trk[p],engines[(p+variant)%4u],0);
        trk[p].p[P_M1AMT]=trk[p].p[P_M2AMT]=trk[p].p[P_M3AMT]=trk[p].p[P_M4AMT]=0;
        trk[p].p[P_SPRD]=0;trk[p].p[P_PAN]=(int32_t)p*19-30;
        trk[p].p[P_FXTYPE]=FX_REVERB;trk[p].p[P_FXMIX]=43;trk[p].p[P_FXRATE]=60;trk[p].p[P_FXCHAR]=64;
        trk[p].p[P_FX2TYPE]=trk[p].p[P_FX3TYPE]=0;trk_note_on(&trk[p],60+p,100);
    }
}
static void render(uint32_t variant,int mode)
{
    prepare(variant,mode!=0,mode==2);
    for(uint32_t b=0;b<BLOCKS;b++){
        uint32_t n=(b%3u)?CTL:CTL/2u;
        if(b==24u)trk[1].p[P_FXMIX]=95;
        if(b==40u)trk[1].p[P_SPRD]=55;
        if(b==56u)trk[1].p[P_SPRD]=0;
        if(b==72u){trk[1].p[P_M1SRC]=MS_LFO;trk[1].p[P_M1DST]=MD_PAN;trk[1].p[P_M1AMT]=9;}
        if(b==88u)trk[1].p[P_M1AMT]=0;
        if(b==104u){trk[1].p[P_FX2TYPE]=FX_CHORUS;trk[1].p[P_FX2MIX]=50;}
        if(b==120u)trk[1].p[P_FX2TYPE]=0;
        if(b==136u)for(uint32_t p=0;p<NPART;p++)trk_note_off(&trk[p],60+p);
        if(b==152u)for(uint32_t p=0;p<NPART;p++)trk_note_on(&trk[p],72+p,90);
        if(b==168u)trk[1].p[P_FXTYPE]=FX_OFF;
        if(b==184u)trk[1].p[P_FXTYPE]=FX_REVERB;
        if(b==200u)for(uint32_t p=0;p<NPART;p++)trk[p].p[P_FXSEND]=55;
        int32_t out[2u*CTL];mix_block(out,n);
        assert(!fx_reverb_pending && !fx_bus_pending);
        assert(AW_LOAD(&audio_worker.request)==AW_LOAD(&audio_worker.complete));
        uint64_t h=tanks();
        if(!mode){memcpy(expected[b],out,2u*n*sizeof *out);state_hash[b]=h;}
        else {assert(!memcmp(expected[b],out,2u*n*sizeof *out));assert(state_hash[b]==h);}
    }
}
int main(void)
{
    pthread_t thread;assert(!pthread_create(&thread,0,worker_thread,0));
    while(!AW_LOAD(&audio_worker.ready))sched_yield();
    for(uint32_t v=0;v<4;v++)for(int mode=0;mode<3;mode++)render(v,mode);
    prepare(0,1,0);int32_t pcm[CTL]={0};fx_plan_t first;
    assert(fx_reverb_private_start(&trk[0],&trk[1],pcm,CTL,&first));
    assert(fx_reverb_pending);assert(audio_worker_join());
    uint64_t before=tanks();drop_join=1;
    assert(!fx_reverb_private_take(&trk[1],pcm,CTL));
    assert(!fx_reverb_pending && tanks()==before && !audio_worker_online);
    assert(!pthread_cancel(thread));assert(!pthread_join(thread,0));
    puts("Reverb pairs: engine orders, PCM/tanks, controls/tails/fallbacks and no replay after failed worker exact");
}
