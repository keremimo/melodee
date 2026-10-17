/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Host-only sample-job ranking. Do not use these values as target scheduler
 * weights: CPU0 preemption, XIP, floating-point and coordination differ. */
#include <stdint.h>
static uint64_t job_instructions(void);
#define MELODEE_AUDIO_PROFILE 1
#define AUDIO_PROFILE_TICKS() ((uint32_t)job_instructions())
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
#include <assert.h>
static uint64_t job_instructions(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 r;
    if(!proc_pid_rusage(getpid(),RUSAGE_INFO_V4,(rusage_info_t *)&r))return r.ri_instructions;
#endif
    return 0;
}
static int32_t out[CTL];
static void measure(uint32_t engine,uint32_t variant,uint32_t feature,uint32_t note)
{
    memset(trk,0,sizeof trk);eng_state_reset();host_tracks_init();
    track_t *t=&trk[0];host_preset(t,engine,0);t->p[P_VOICE]=V_MONO;t->p[P_ALLOC]=1;
    t->p[P_LD_PIT]=t->p[P_ED_PIT]=0;t->p[P_SUS]=127;
    uint32_t id;
    if(engine==ENGI_PROPHET){
        id=AP_PROPHET;
        p5_patch_of(t)->raw[P5_FILTER_REV]=(uint8_t)variant;
        p5_patch_of(t)->raw[P5_POLY_ENV]=(uint8_t)(feature?100:0);
        p5_patch_of(t)->raw[P5_POLY_B]=(uint8_t)(feature?100:0);
        p5_patch_of(t)->raw[P5_POLY_FREQ]=(uint8_t)feature;
        p5_patch_of(t)->raw[P5_POLY_FILTER]=(uint8_t)feature;
    }else if(engine==ENGI_FM6){
        id=AP_FM6;uint8_t p[FP_SIZE+1];fm6_unpack(FM6_INIT,p);p[FP_ALG]=(uint8_t)variant;
        p[FP_FB]=7;
        for(uint32_t k=0;k<6;k++)p[k*FP_OP+FP_OL]=(uint8_t)(feature?90:k?0:90);
        fm6_put_patch(0,p,1);fm6_fn[0][FN_ENGINE]=0;
    }else if(engine==ENGI_CZ){
        id=AP_CZ;cz_patch[0].raw[0]=(uint8_t)variant;
    }else if(engine==3){id=AP_SID;t->p[P_E0]=(int16_t)variant;t->p[P_E2]=(int16_t)(feature?6:1);}
    else {t->p[P_E0]=(int16_t)(variant?DK_909:DK_808);id=variant?AP_909:AP_808;}
    trk_note_on(t,note,110);audio_batch_active=1;
    uint64_t totals[2]={0},maximum[2]={0};uint32_t calls[2]={0};
    for(uint32_t block=0;block<256;block++){
        if(block && !(block%32u)){trk_note_off(t,note);trk_note_on(t,note,110);}
        memset(audio_profile,0,sizeof audio_profile);
        track_render(t,out,CTL);assert(audio_batch_count<=1u);audio_batch_flush();
        audio_profile_span_t *s=&audio_profile[id];
        uint32_t phase=block%32u<4u?0u:1u;
        totals[phase]+=s->ticks;calls[phase]+=s->calls;
        if(s->max_ticks>maximum[phase])maximum[phase]=s->max_ticks;
    }
    audio_batch_active=0;
    for(uint32_t phase=0;phase<2;phase++)printf("%u,%u,%u,%u,%s,%u,%.1f,%llu\n",engine,variant,feature,note,
        phase?"body":"attack",calls[phase],calls[phase]?(double)totals[phase]/calls[phase]:0,
        (unsigned long long)maximum[phase]);
}
int main(void)
{
    uint32_t overhead=UINT32_MAX;
    for(uint32_t k=0;k<100;k++){uint32_t start=AUDIO_PROFILE_TICKS(),cost=AUDIO_PROFILE_TICKS()-start;if(cost<overhead)overhead=cost;}
    fprintf(stderr,"host measurement minimum empty span: %u instructions; included, not subtracted\n",overhead);
    puts("engine,variant,feature,note,phase,jobs,mean_host_instructions,max_host_instructions");
    for(uint32_t v=0;v<2;v++)for(uint32_t f=0;f<2;f++)measure(ENGI_PROPHET,v,f,60);
    for(uint32_t v=0;v<32;v++)for(uint32_t f=0;f<2;f++)measure(ENGI_FM6,v,f,60);
    for(uint32_t v=0;v<4;v++)measure(ENGI_CZ,v,0,60);
    for(uint32_t v=0;v<2;v++)for(uint32_t f=0;f<2;f++)measure(3,v,f,60);
    for(uint32_t v=0;v<2;v++)for(uint32_t n=0;n<(v?D9_NUM:16u);n++){
        trk[0].p[P_E0]=(int16_t)(v?DK_909:DK_808);
        measure(10,v,0,drum_sound_note(&trk[0],n));
    }
    return 0;
}
