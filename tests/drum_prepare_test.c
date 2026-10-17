/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Original Melodee metal preparation/render kernels retained from 6516ee6. */
#define MELODEE_DRUM_FAST 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#undef __attribute__
#include <assert.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
static uint64_t instructions(void) {
#ifdef __APPLE__
 struct rusage_info_v4 r;
 if(!proc_pid_rusage(getpid(),RUSAGE_INFO_V4,(rusage_info_t *)&r))return r.ri_instructions;
#endif
 return 0;
}
static __attribute__((noinline)) void tune_reference(const dr8_t *d,dr_scratch_t *scratch)
{
    uint32_t ratio=(uint32_t)pow2_q16(d->pitch);
    for(uint32_t k=0;k<6;k++)scratch->inc[k]=d->pitch?(uint32_t)(((uint64_t)DR_OSC[k]*ratio)>>16):DR_OSC[k];
}
static __attribute__((noinline)) void tune_probe(const dr8_t *d,dr_scratch_t *scratch)
{dr_metal_tune(d,scratch);}
static __attribute__((noinline)) void metal_reference(float pitch,int kind,uint32_t inc[6]) {
 static const float hz[6]={205.3f,304.4f,369.6f,522.7f,540.0f,800.0f};
 for(int k=0;k<6;k++)inc[k]=d9_inc(hz[k]*pitch*(kind==3?1.48f:1.0f));
}
static __attribute__((noinline)) void metal_probe(float pitch,int kind,uint32_t inc[6]) {
 static const uint32_t base[2][6]={
 {(uint32_t)(205.3f * D9_PHASE_PER_HZ),(uint32_t)(304.4f * D9_PHASE_PER_HZ),(uint32_t)(369.6f * D9_PHASE_PER_HZ),(uint32_t)(522.7f * D9_PHASE_PER_HZ),(uint32_t)(540.0f * D9_PHASE_PER_HZ),(uint32_t)(800.0f * D9_PHASE_PER_HZ)},
 {(uint32_t)(205.3f * 1.48f * D9_PHASE_PER_HZ),(uint32_t)(304.4f * 1.48f * D9_PHASE_PER_HZ),(uint32_t)(369.6f * 1.48f * D9_PHASE_PER_HZ),(uint32_t)(522.7f * 1.48f * D9_PHASE_PER_HZ),(uint32_t)(540.0f * 1.48f * D9_PHASE_PER_HZ),(uint32_t)(800.0f * 1.48f * D9_PHASE_PER_HZ)}};
 if(pitch==1.0f){for(int k=0;k<6;k++)inc[k]=base[kind==3][k];return;}
 static const float hz[6]={205.3f,304.4f,369.6f,522.7f,540.0f,800.0f};
 for(int k=0;k<6;k++)inc[k]=d9_inc(hz[k]*pitch*(kind==3?1.48f:1.0f));
}
static void metal_render_reference(d9_smp_t *s, const d9_bus_t *bus, int n)
{
    static const float hz[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
    int m = n < s->mute ? n : s->mute;
    uint32_t inc[6];
    for (int k = 0; k < 6; k++) inc[k] = d9_inc(hz[k] * s->pitch * (s->kind == 3 ? 1.48f : 1.0f));
    s->mute -= m;
    d9_env_anchor(&s->out);
    for (int i = 0; i < m; i++) {
        float metal = 0.0f;
        for (int k = 0; k < 6; k++) {
            s->ph[k] += inc[k];
            metal += s->ph[k] & 0x80000000u ? 0.16f : -0.16f;
        }
        float nz = d9_noise(&s->noise);
        float exc = s->kind == 2 ? metal * 0.6f + nz * 0.4f : metal;
        float y = d9_biquad_tick(&s->hp, d9_biquad_tick(&s->bp, exc));
        y = d9_shape(&s->shape, y * 2.0f, s->crush_st) * d9_env_tick(&s->out);
        d9_emit(bus, i, y);
    }
    if (!s->mute) s->playing = 0;
}

static __attribute__((noinline)) void run8_reference(dr8_t *d, const int16_t *p, int32_t *y, uint32_t n,dr_scratch_t *scratch)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        y[i] = 0;
    if (!d->on)
        return;
    if ((DR_NOISY >> d->ins) & 1u)
        for (i = 0; i < n; i++)
            scratch->nz[i] = (int32_t)(noise32(&d->nst) >> 17) - 16384;
    if ((DR_METAL >> d->ins) & 1u) {
        tune_reference(d,scratch);
        void (*volatile metal)(dr8_t *, uint32_t,dr_scratch_t *) = dr_metal;
        metal(d,n,scratch);
    }
    /* Both signatures cross the XIP/RAM distance through a register call. */
#define DR8_KNOBS(fn) do { void (*volatile run)(dr8_t *,const int16_t *,int32_t *,uint32_t,dr_scratch_t *) = fn; run(d,p,y,n,scratch); } while (0)
#define DR8_HIT(fn) do { void (*volatile run)(dr8_t *,int32_t *,uint32_t,dr_scratch_t *) = fn; run(d,y,n,scratch); } while (0)
    switch (d->ins) {
    case DR_BD: DR8_KNOBS(dr_bd); break;
    case DR_SD: DR8_KNOBS(dr_sd); break;
    case DR_RS: DR8_HIT(dr_rs); break;
    case DR_CL: DR8_HIT(dr_cl); break;
    case DR_CP: DR8_HIT(dr_cp); break;
    case DR_MA: DR8_HIT(dr_ma); break;
    case DR_CB: DR8_HIT(dr_cb); break;
    case DR_CY: DR8_KNOBS(dr_cy); break;
    case DR_OH: case DR_CH: DR8_KNOBS(dr_hat); break;
    default: DR8_KNOBS(dr_tom); break;
    }
#undef DR8_KNOBS
#undef DR8_HIT
    for (i = 0; i < n; i++)
        y[i] = (y[i] * DR8_OUT) >> 12;
    if (d->t > DR_LIFE)
        dr_off(d);
}
static void parity(void) {
 dr8_t d={0};dr_scratch_t a={0},b={0};uint32_t ia[6],ib[6];
 for(int32_t pitch=-3072;pitch<3072;pitch++) {d.pitch=pitch;tune_reference(&d,&a);tune_probe(&d,&b);assert(!memcmp(&a,&b,sizeof a));}
 for(int kind=0;kind<4;kind++)for(uint32_t q=0;q<=65536;q++){
   float pitch=(float)q/16384.0f;metal_reference(pitch,kind,ia);metal_probe(pitch,kind,ib);assert(!memcmp(ia,ib,sizeof ia));
 }
 puts("Drum preparation probe: 6144 808 pitch steps and 262148 909 kind/pitch values exact");
}
static volatile uint32_t checksum;
static void benchmark(void) {
 puts("engine,pattern,reference_instructions_per_prepare,probe_instructions_per_prepare");
 for(uint32_t engine=0;engine<2;engine++)for(uint32_t mode=0;mode<4;mode++) {
   double cost[2];
   for(uint32_t fast=0;fast<2;fast++) {
    dr8_t d={0};dr_scratch_t scratch={0};uint32_t inc[6];
    void (*volatile run8)(const dr8_t *,dr_scratch_t *)=fast?tune_probe:tune_reference;
    void (*volatile run9)(float,int,uint32_t *)=fast?metal_probe:metal_reference;
    uint64_t start=instructions();
    for(uint32_t i=0;i<500000;i++) {
     if(engine==0){d.pitch=mode==0?0:mode==1?192:mode==2?(int32_t)(i%1537)-768:i&1?0:192;run8(&d,&scratch);checksum+=scratch.inc[0];}
     else{float pitch=mode==0?1.0f:mode==1?1.125f:mode==2?(float)(i%16385+8192)/16384.0f:i&1?1.0f:1.125f;run9(pitch,(int)(i%4),inc);checksum+=inc[0];}
    }
    cost[fast]=(double)(instructions()-start)/500000;
   }
   printf("%u,%u,%.2f,%.2f\n",engine,mode,cost[0],cost[1]);
 }
}
static void render_parity(void) {
 static drum909_t kit;drum909_init(&kit);d9_smp_t state=kit.smp[0];
 float a[CTL],b[CTL],ar[CTL],br[CTL];uint32_t cases=0;
 for(uint32_t i=0;i<20000;i++) {
   state.kind=(int32_t)(i%4);state.pitch=i%5==0?1.0f:(float)(i%4097)/1024.0f;
   if(i%17==0){d9_env_set(&state.out,1.0f);d9_env_exp(&state.out,.00001f,250.0f);state.mute=200+(int32_t)(i%31);state.playing=1;}
   if(i%23==0)d9_smp_rate(&state);
   if(i%29==0){state.dist_type=(int32_t)(i%7);state.drive=(float)(i%33)/16;d9_shape_prep(&state.shape,state.drive,state.dist_type);}
   uint32_t n=i%33;
   for(uint32_t k=0;k<CTL;k++)a[k]=b[k]=ar[k]=br[k]=(float)((int32_t)k-16)*.01f;
   d9_smp_t want=state;
   d9_bus_t ba={a,i%2?ar:0,.707f,.707f,.001f,-.001f},bb=ba;bb.dry=b;bb.dry_r=i%2?br:0;
   metal_render_reference(&want,&ba,(int)n);d9_render_smp(&state,&bb,(int)n);
   assert(!memcmp(&want,&state,sizeof state));assert(!memcmp(a,b,sizeof a));assert(!memcmp(ar,br,sizeof ar));cases++;
 }
 dr8_t d={0};dr_scratch_t scratch={0};int16_t p[P_COUNT]={0};int32_t x[CTL],z[CTL];
 p[P_E1]=p[P_E2]=p[P_E3]=p[P_E4]=64;
 for(uint32_t i=0;i<20000;i++) {
   d.pitch=i%5==0?0:(int32_t)(i%385)-192;
   if(i%41==0)dr8_hit(&d,i/41%DR_N,90,p);
   uint32_t n=i%33;dr8_t want=d;dr_scratch_t want_scratch=scratch;
   for(uint32_t k=0;k<CTL;k++)x[k]=z[k]=(int32_t)k-16;
   run8_reference(&want,p,x,n,&want_scratch);dr8_run_into(&d,p,z,n,&scratch);
   assert(!memcmp(&want,&d,sizeof d));assert(!memcmp(&want_scratch,&scratch,sizeof scratch));assert(!memcmp(x,z,sizeof x));cases++;
 }
 printf("Drum render: %u variable 0..32-sample blocks, pitches/retriggers, 808 circuits/scratch and 909 stereo/pan/distortion/state exact\n",cases);
}
static void renderer_benchmark(void) {
 puts("909_kind,pitch_pattern,reference_instructions_per_sample,candidate_instructions_per_sample");
 static drum909_t kit;drum909_init(&kit);
 for(uint32_t kind=0;kind<4;kind++)for(uint32_t mode=0;mode<3;mode++) {
  double cost[2];
  for(uint32_t fast=0;fast<2;fast++) {
    d9_smp_t state=kit.smp[kind];float out[CTL];d9_bus_t bus={out,0,1,1,0,0};
    void (*volatile run)(d9_smp_t *,const d9_bus_t *,int)=fast?d9_render_smp:metal_render_reference;
    uint64_t start=instructions();
    for(uint32_t i=0;i<10000;i++) {
      memset(out,0,sizeof out);state.pitch=mode==0?1:mode==1?1.125f:(float)(i%16385+8192)/16384.0f;
      state.mute=128;run(&state,&bus,CTL);checksum+=(uint32_t)(out[i%CTL]+10.0f);
    }
    cost[fast]=(double)(instructions()-start)/(10000*CTL);
  }
  printf("%u,%u,%.2f,%.2f\n",kind,mode,cost[0],cost[1]);
  if(cost[0])assert(cost[1]<cost[0]*1.04);
 }
}

int main(int argc,char **argv){parity();render_parity();if(argc==1){benchmark();renderer_benchmark();}return 0;}
