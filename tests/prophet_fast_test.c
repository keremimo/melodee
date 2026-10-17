/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define MELODEE_PROPHET_FAST 1
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
static void p5_mod_samples(p5_part_t *,const uint8_t *,uint32_t,float,float) __attribute__((noinline));
/* Original Melodee-authored shared wheel kernel, retained from 2a6f18a. */
static __attribute__((noinline)) void reference(p5_part_t *s,const uint8_t *p,uint32_t inc,float amount,float mix)
{
    uint32_t phase=s->lfo;if(!s->mod_noise)s->mod_noise=0x517953;
    for(uint32_t i=0;i<CTL*P5_OVERSAMPLE;i++){
        float noise=(float)(int32_t)(noise32(&s->mod_noise)>>8)*(1.0f/16777216.0f)-0.5f;
        float w=(p5_lfo_wave(phase,p)*(1.0f-mix)+noise*mix)*amount;
        s->wheel[i]=w;
        s->wheel_pitch[i]=p5_pitch_ratio(w*(470.0f/1200.0f));
        phase+=inc;
    }
    s->lfo=phase;s->lfo_value=(int32_t)(p5_lfo_wave(phase,p)*32768.0f);
}
static __attribute__((noinline)) void candidate(p5_part_t *s,const uint8_t *p,uint32_t inc,float amount,float mix)
{p5_mod_samples_run(s,p,inc,amount,mix);}
static uint32_t random_state=0x6714358u;
static uint32_t next_u(void){random_state=random_state*1664525u+1013904223u;return random_state;}
static void parity(void)
{
    p5_part_t a={0},b={0};uint8_t p[56]={0};uint32_t cases=0;
    for(uint32_t wave=0;wave<8;wave++)for(uint32_t rate=0;rate<128;rate++)for(uint32_t mix=0;mix<128;mix++)for(uint32_t level=0;level<3;level++){
        p[P5_LFO_SAW]=(uint8_t)(wave&1);p[P5_LFO_TRI]=(uint8_t)(wave&2);p[P5_LFO_PULSE]=(uint8_t)(wave&4);
        a.lfo=b.lfo=next_u();a.mod_noise=b.mod_noise=(cases%7u==0u?0:(int32_t)next_u());
        float amount=level?P5_AMOUNT[level==1?1:127]:cases&1u?-0.0f:0.0f;
        uint32_t inc=P5_LFO_INC[rate]/P5_OVERSAMPLE;
        reference(&a,p,inc,amount,(float)mix*(1.0f/127.0f));candidate(&b,p,inc,amount,(float)mix*(1.0f/127.0f));
        assert(!memcmp(&a,&b,sizeof a));cases++;
    }
    /* Boundary phases and exact zero crossings with dry waveform mixtures. */
    static const uint32_t phases[]={0,1,0x3fffffffu,0x40000000u,0x7fffffffu,0x80000000u,0xbfffffffu,0xc0000000u,UINT32_MAX};
    for(uint32_t k=0;k<NELEM(phases);k++){
        a.lfo=b.lfo=phases[k];reference(&a,p,0,1.0f,0.0f);candidate(&b,p,0,1.0f,0.0f);assert(!memcmp(&a,&b,sizeof a));
    }
    printf("Prophet wheel: %u waveform/rate/mix/amount cases, signed zeros, phase boundaries and full state exact (OS %u)\n",cases,P5_OVERSAMPLE);
}
static volatile uint32_t checksum;
static void benchmark(void)
{
    puts("amount,reference_instructions_per_sample,candidate_instructions_per_sample");
    uint8_t p[56]={0};p[P5_LFO_TRI]=1;
    for(uint32_t level=0;level<4;level++){
        double cost[2];
        for(uint32_t fast=0;fast<2;fast++){
            p5_part_t state={0};
            void (*volatile run)(p5_part_t *,const uint8_t *,uint32_t,float,float)=fast?candidate:reference;
            uint64_t start=instructions();
            for(uint32_t block=0;block<100000;block++){
                float amount=level==0?0.0f:level==1?P5_AMOUNT[127]:level==2?P5_AMOUNT[block%128u]:block&1u?0.0f:P5_AMOUNT[127];
                run(&state,p,P5_LFO_INC[73]/P5_OVERSAMPLE,amount,0.5f);
                checksum+=(uint32_t)state.mod_noise;
            }
            cost[fast]=(double)(instructions()-start)/(100000u*CTL*P5_OVERSAMPLE);
        }
        printf("%u,%.2f,%.2f\n",level,cost[0],cost[1]);
        if(cost[0])assert(cost[1]<cost[0]*(level?1.04:.85));
    }
}
int main(int argc,char **argv)
{parity();if(argc==1)benchmark();return 0;}
