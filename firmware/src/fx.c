/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#include "performance_options.h"
/* Three stereo inserts per track, four sections per side per filter slot.
 * The EQ accelerator is time-shared, not claimed as 32 independent lanes.
 * OFF / MIX 0 leaves the existing dry mixer bit-identical and allocates nothing.
 * Only the audio owner mutates DSP state or the optional short-delay memory. */
#include "../hal/fm1_eq.h"
#define FX_RATE (FS/4u)
typedef struct { float b0,b1,b2,a1,a2; } fx_coeff_t;
typedef struct { float z1,z2; } fx_history_t;
typedef struct { float b0,b1,b2,a1,a2,z1,z2; } fx_bq_t; /* wet-input tone */
typedef struct {
    /* ROOM's four comb cursors/spans reuse idle filter histories after its
     * two wet-input tone sections. No reverb tank state remains. */
    union {
        /* Coefficients are identical on both sides; only histories differ. */
        struct { fx_coeff_t q[4]; fx_history_t history[2][4]; };
        struct { fx_bq_t time_tone[2]; uint16_t comb[4], rev_span[4]; };
    };
#if MELODEE_HW_EQ
    float hw[2][4][8];
#endif
    uint32_t phase, age, epoch, pos;
    int32_t rng, random, mix;
    int16_t rate, character;
    uint8_t type, mono, decim, failed;
    int32_t echo, damp[4];
    int32_t rev_damp[8], rev_l, rev_r, rev_prev_l, rev_prev_r;
    int32_t rev_bass[2];
    int16_t rev_rate, rev_char;
} fx_state_t;
static fx_state_t fx_state[FX_STATE_COUNT];
static volatile uint32_t fx_epoch;
static int32_t fx_left[CTL],fx_right[CTL],fx_dry_l[CTL],fx_dry_r[CTL];
static int32_t fx_low_l[CTL/4u+1u],fx_low_r[CTL/4u+1u];
#if MELODEE_HW_EQ
static float fx_in[CTL],fx_out[CTL];
#endif
#if defined(MELODEE_FX_PROFILE) && MELODEE_FX_PROFILE && defined(FM1_IRQ_TARGET)
/* Opt-in laboratory override: audio owns histories and allocations; no track
 * parameters, notes or persistent objects are edited by the performance run. */
static volatile uint32_t fx_profile_request;
static uint32_t fx_profile_control,fx_profile_start,fx_profile_ticks[4],fx_profile_max[4],fx_profile_blocks[4],fx_profile_bus_setup;
static void fx_profile_poll(void)
{
    uint32_t request=fx_profile_request;
    if(request!=fx_profile_control){
        fx_profile_control=request;fx_profile_start=fm1_ticks();fx_epoch++;
        memset(fx_profile_ticks,0,sizeof fx_profile_ticks);memset(fx_profile_max,0,sizeof fx_profile_max);memset(fx_profile_blocks,0,sizeof fx_profile_blocks);
    }
    if(request && (uint32_t)(fm1_ticks()-fx_profile_start)>10u*24000000u)fx_profile_request=0;
}
static int fx_profile_track(uint32_t ti){return (fx_profile_control>>(26u+ti))&1u;}
#endif
static void fx_clear_all(void) { fx_epoch++; } /* consumed at the next audio block */
static inline __attribute__((always_inline)) float fx_bound(float x,float lim) { return x < -lim ? -lim : x > lim ? lim : x; }
static inline __attribute__((always_inline)) int32_t fx_scale(int32_t a,uint32_t k)
{return (a>>16)*(int32_t)k+(int32_t)(((uint32_t)(a&65535)*k)>>16);}
/* Preserve C's truncation towards zero with shifts rather than the target's
 * expensive integer divider. Callers' bounded DSP values leave sign headroom. */
static inline __attribute__((always_inline)) int32_t fx_div_pow2(int32_t x,uint32_t shift)
{return (x+(int32_t)(((uint32_t)x>>31)*((1u<<shift)-1u)))>>shift;}
static inline __attribute__((always_inline)) int32_t fx_sine(uint32_t phase)
{
    uint32_t i=phase>>22;int32_t a=SINE[i],b=SINE[(i+1u)&1023u];
    return a+(((b-a)*(int32_t)((phase>>7)&32767u))>>15);
}
static void fx_coeff(fx_coeff_t *q,uint32_t type,int32_t hz,float resonance,float gain)
{
    uint32_t ph=(uint32_t)clamp(hz,60,(int32_t)FX_RATE*45/100)*(uint32_t)(0x100000000ull/FX_RATE);
    float s=(float)sine_i(ph)*(1.0f/32768.0f),c=(float)sine_i(ph+0x40000000u)*(1.0f/32768.0f);
    float alpha=s/(2.0f*resonance),inv=1.0f/(1.0f+alpha);
    q->a1=-2.0f*c*inv;q->a2=(1.0f-alpha)*inv;
    if(type==0u){q->b0=q->a2;q->b1=q->a1;q->b2=1.0f;} /* all pass */
    else if(type==1u){q->b0=(1.0f-c)*0.5f*inv;q->b1=2.0f*q->b0;q->b2=q->b0;}
    else if(type==2u){q->b0=alpha*inv;q->b1=0;q->b2=-q->b0;} /* band pass */
    else { /* peaking EQ: bounded pole radius, gain is amplitude's square root */
        inv=1.0f/(1.0f+alpha/gain);
        q->a1=-2.0f*c*inv;q->a2=(1.0f-alpha/gain)*inv;
        q->b0=(1.0f+alpha*gain)*inv;q->b1=q->a1;q->b2=(1.0f-alpha*gain)*inv;
    }
}
static inline __attribute__((always_inline)) float fx_tick(const fx_coeff_t *q,fx_history_t *h,float x)
{
    float y=q->b0*x+h->z1;
    h->z1=fx_bound(q->b1*x-q->a1*y+h->z2,2000000.0f);
    h->z2=fx_bound(q->b2*x-q->a2*y,2000000.0f);
    return fx_bound(y,1000000.0f);
}
static void fx_identity(fx_coeff_t *q) { q->b0=1;q->b1=q->b2=q->a1=q->a2=0; }
static void fx_design(fx_state_t *f,uint32_t n)
{
    static const uint16_t vowel[5][3]={{800,1150,2900},{400,1700,2600},{300,2200,3000},{500,800,2800},{350,650,2700}};
    uint32_t first=!f->age,old=f->phase;int32_t previous=f->random;
    int32_t sweep=(sine_i(f->phase)+32768)>>9,cha=f->character;
    f->phase+=LFO_INC[f->rate]*n/CTL;
    if(f->phase<old || first)f->random=(int32_t)(noise32(&f->rng)&127u);
    f->age=1;
    /* Reverb's coefficient cache is idle in the filter modes. Histories keep
     * running, but coefficients change only when their actual inputs change. */
    int moving=f->type==FX_PHASER || f->type==FX_WIDTH;
    if(!first && f->rev_char==cha && (moving?f->rev_rate==sweep:previous==f->random))return;
    f->rev_rate=(int16_t)sweep;f->rev_char=(int16_t)cha;
    for(uint32_t k=0;k<4u;k++){
        fx_coeff_t *q=&f->q[k];
        if(moving)fx_coeff(q,0,180+(int32_t)k*310+sweep*(3+cha/8),0.65f,1);
        else if(f->type==FX_FORMANT && k<3u)fx_coeff(q,3,vowel[(uint32_t)f->random%5u][k]*(80+cha)/144,4.0f,1.8f);
        else if(f->type==FX_SPIT && k==0u)fx_coeff(q,2,180+f->random*45,1.0f+cha*0.15f,1);
        else fx_identity(q);
#if MELODEE_HW_EQ
        for(uint32_t ch=0;ch<2u;ch++){
            float *h=f->hw[ch][k];h[0]=q->b0;h[1]=-q->a2;h[2]=q->b2/q->b0;h[3]=-q->a1;h[4]=q->b1/q->b0;
        }
#endif
    }
}
/* Try the accelerator once with known nontrivial poles, then trust only a
 * finite, bounded output. Both channels own their hardware history. A failed
 * probe permanently selects the CPU path for this session. */
static uint8_t fx_hw_status; /* 0 untried, 1 verified, 2 CPU */
static void fx_probe(void)
{
#if MELODEE_HW_EQ && defined(FM1_IRQ_TARGET)
    fx_coeff_t q[4]={{0}};fx_history_t history[4]={{0}};float row[4][8]={{0}},want[CTL];
    for(uint32_t k=0;k<4u;k++){
        fx_coeff(&q[k],0,300+(int32_t)k*400,0.7f,1);
        row[k][0]=q[k].b0;row[k][1]=-q[k].a2;row[k][2]=q[k].b2/q[k].b0;row[k][3]=-q[k].a1;row[k][4]=q[k].b1/q[k].b0;
    }
    for(uint32_t i=0;i<CTL;i++){
        float x=i?0:8192;fx_in[i]=x;
        for(uint32_t k=0;k<4u;k++)x=fx_tick(&q[k],&history[k],x);want[i]=x;
    }
    fx_hw_status=fm1_eq_run(row,fx_in,fx_out,CTL)?1:2;
    if(fx_hw_status==1)for(uint32_t i=0;i<CTL;i++)
        if(!(fx_out[i]>=want[i]-2 && fx_out[i]<=want[i]+2))fx_hw_status=2;
#else
    fx_hw_status=2;
#endif
}
/* Tape uses three pairs of stable one-poles instead of staging six biquad
 * passes. The fourth unused section stores shared coefficient/polarity
 * controls; the separate histories retain each channel's filter state. */
static void fx_tape_design(fx_state_t *f,uint32_t n)
{
    uint32_t first=!f->age,old_phase=f->phase;int32_t old_random=f->random;
    fx_coeff_t *control=&f->q[3];
    f->phase+=LFO_INC[f->rate]*n/CTL;
    if(f->phase<old_phase || first)f->random=(int32_t)(noise32(&f->rng)&127u);
    f->age=1; /* initialization marker; it must not wrap during a long session */
    if(!first && old_random==f->random && control->b2==(float)f->character)return;
    int32_t cha=f->character,low=200+(f->random-64)*cha/64;
    int32_t hz[3]={low,clamp(1400+(f->random-64)*cha/4,low+150,8000),6500-cha*38};
    for(uint32_t k=0;k<3u;k++){
        float w=(float)clamp(hz[k],60,(int32_t)FX_RATE*45/100)*(6.28318530718f/FX_RATE);
        f->q[k].b1=w/(1.0f+w);
        if(first)f->q[k].b0=f->q[k].b1;
    }
    control->b1=cha>32 && (f->random&64)?-1.0f:1.0f;control->b2=(float)cha;
    if(first)control->b0=1.0f;
}
static inline __attribute__((always_inline)) float fx_tape_lp(fx_history_t *q,float x,float g)
{
    q->z1+=(x-q->z1)*g;q->z2+=(q->z1-q->z2)*g;return q->z2;
}
static __attribute__((noinline,section(".dsp_text"))) void fx_tape(fx_state_t *restrict f,int32_t *restrict left,int32_t *restrict right,uint32_t n)
{
    /* About 15 ms of smoothing, even at the fastest random updates. Smooth
     * sign through zero; an instantaneous mid-band inversion makes a click. */
    const float slew=1.0f/(0.015f*FX_RATE);
    for(uint32_t i=0;i<n;i++){
        for(uint32_t k=0;k<4u;k++)f->q[k].b0+=(f->q[k].b1-f->q[k].b0)*slew;
        float blend=f->q[3].b0-1.0f;
        for(uint32_t ch=0;ch<(f->mono?1u:2u);ch++){
            int32_t *b=ch?right:left;float x=(float)b[i];
            float low=fx_tape_lp(&f->history[ch][0],x,f->q[0].b0);
            float upper=fx_tape_lp(&f->history[ch][1],x,f->q[1].b0);
            x+=blend*(upper-low);
            b[i]=(int32_t)fx_bound(fx_tape_lp(&f->history[ch][2],x,f->q[2].b0),1000000.0f);
        }
    }
    if(f->mono)for(uint32_t k=0;k<3u;k++){f->history[1][k]=f->history[0][k];}
    /* Clear inaudible tails before they can become costly denormals. */
    for(uint32_t ch=0;ch<2u;ch++)for(uint32_t k=0;k<3u;k++){
        fx_history_t *q=&f->history[ch][k];
        if(q->z1>-1e-9f && q->z1<1e-9f)q->z1=0;
        if(q->z2>-1e-9f && q->z2<1e-9f)q->z2=0;
    }
}
#if MELODEE_HW_EQ
#define FX_FILTER_SECTION ".audio_text"
#else
#define FX_FILTER_SECTION ".dsp_text"
#endif
static __attribute__((noinline,section(FX_FILTER_SECTION))) void fx_filter(fx_state_t *restrict f,uint32_t ch,int32_t *restrict b,uint32_t n)
{
#if MELODEE_HW_EQ
    if(fx_hw_status==1){
        for(uint32_t i=0;i<n;i++)fx_in[i]=(float)b[i];
        if(fm1_eq_run(f->hw[ch],fx_in,fx_out,n)){
            int ok=1;for(uint32_t i=0;i<n;i++)if(!(fx_out[i]>=-1000000 && fx_out[i]<=1000000))ok=0;
            if(ok){for(uint32_t i=0;i<n;i++)b[i]=(int32_t)fx_out[i];return;}
        }
        fx_hw_status=2;
    }
#endif
    uint32_t sections=f->type==FX_SPIT?1u:f->type==FX_FORMANT?3u:4u;
    const fx_coeff_t *end=f->q+sections;
    fx_history_t *history=f->history[ch];
    for(uint32_t i=0;i<n;i++){
        const fx_coeff_t *q=f->q;fx_history_t *h=history;
        float x=(float)b[i];
        do{x=fx_tick(q++,h++,x);}while(q!=end);
        b[i]=(int32_t)x;
    }
    /* Denormal tails must not turn a silent insert into a costly one. */
    for(uint32_t k=0;k<sections;k++){
        fx_history_t *q=&f->history[ch][k];if(q->z1>-1e-9f && q->z1<1e-9f)q->z1=0;if(q->z2>-1e-9f && q->z2<1e-9f)q->z2=0;
    }
}
/* Fuse interpolation/hold and the wet/dry return into one full-rate pass.
 * Equal mono histories need one channel of math; Width's right stays dry. */
static inline __attribute__((always_inline)) void fx_return(int32_t target,uint32_t type,uint32_t mono,uint32_t i,int32_t wl,int32_t wr,int32_t *mix)
{
    int32_t l=fx_dry_l[i],r=fx_dry_r[i];
    if(type==FX_PHASER){wl=fx_div_pow2(wl+l,1);wr=fx_div_pow2(wr+r,1);}
    if(type==FX_FORMANT){wl=fx_div_pow2(wl,1);wr=fx_div_pow2(wr,1);}
    if(*mix!=target)*mix+=clamp(target-*mix,-128,128);
    fx_left[i]=l+fx_scale(wl-l,(uint32_t)*mix*2u);
    fx_right[i]=mono?fx_left[i]:type==FX_WIDTH?r:r+fx_scale(wr-r,(uint32_t)*mix*2u);
}
/* Time effects reuse two idle biquads for first-order wet input tone cuts.
 * Coefficients and bypass fades slew over 10 ms. Each section's b0/b1 hold
 * the coefficient/fade, a1/a2 the target/cutoff cache, and z1 the integrator. */
static float fx_cut_coefficient(int32_t hz,uint32_t div)
{
    uint32_t ph=(uint32_t)((uint64_t)hz*div*0x80000000ull/FS);
    float s=(float)sine_i(ph),c=(float)sine_i(ph+0x40000000u);
    return s/(s+c); /* prewarped bilinear integrator */
}
static inline __attribute__((always_inline)) float fx_tone_section(fx_bq_t *q,float x,float slew,uint32_t highpass)
{
    float enabled=q->a2>0?1.0f:0.0f;
    if(!enabled && q->b1<0.000001f){q->b0=q->b1=q->z1=0;return x;}
    q->b0+=(q->a1-q->b0)*slew;q->b1+=(enabled-q->b1)*slew;
    float v=(x-q->z1)*q->b0,low=v+q->z1;q->z1=low+v;
    if(q->z1>-1e-9f && q->z1<1e-9f)q->z1=0;
    return highpass?x-low*q->b1:x+(low-x)*q->b1;
}
static inline __attribute__((always_inline)) int32_t fx_tone_input(fx_state_t *f,int32_t input,float slew)
{
    float x=fx_tone_section(&f->time_tone[0],(float)input,slew,1);
    return (int32_t)fx_tone_section(&f->time_tone[1],x,slew,0);
}
#include "fx_sloop_reverb.h"
#include "fx_sloop_insert.h"
/* ROOM: four damped combs at 13..26 ms, quarter-rate, 2 KiB. */
static __attribute__((noinline,section(".dsp_text"))) void fx_time(fx_state_t *restrict f,int16_t *restrict mem,uint32_t n,int32_t target)
{
    static const uint16_t len[4]={149,211,263,293};
    int32_t mix=f->mix;
    int32_t feedback=12000+f->character*120;
    if(f->type==FX_ROOM && (!f->rev_span[0] || f->rev_rate!=f->rate)){
        f->rev_rate=f->rate;for(uint32_t k=0;k<4u;k++)f->rev_span[k]=(uint16_t)((uint32_t)len[k]*(64u+(uint32_t)f->rate)/191u);
    }
    for(uint32_t i=0;i<n;i++){
        int32_t mono=fx_div_pow2(fx_left[i]+fx_right[i],1);
        if(!f->decim){
            mono=fx_tone_input(f,mono,1.0f/(0.010f*FX_RATE));
            {
                int32_t sum=0;uint32_t off=0;
                for(uint32_t k=0;k<4u;k++){
                    uint32_t span=f->rev_span[k];
                    if(f->comb[k]>=span)f->comb[k]=0;
                    int32_t x=mem[off+f->comb[k]];
                    f->damp[k]+=fx_div_pow2(x-f->damp[k],2);
                    mem[off+f->comb[k]]=(int16_t)clamp(fx_div_pow2(mono,2)+fx_div_pow2(f->damp[k]*feedback,15),-32767,32767);
                    if(++f->comb[k]>=span)f->comb[k]=0;off+=len[k];sum+=x;
                }
                f->echo=fx_div_pow2(sum,1);
            }
        }
        f->decim=(f->decim+1u)&3u;
        fx_return(target,0,f->mono,i,f->echo,f->damp[1]+f->damp[3],&mix);
    }
    f->mix=mix;
}
/* Full-band stereo chorus: two fractional taps, no feedback or filters.
 * 512 stereo int16 frames cost 2 KiB. The 5.8 ms centre and +/-2.9 ms
 * modulation fit comfortably in the ring. Sines/divides run once per block;
 * the SRAM sample loop only ramps, interpolates and mixes with integers.
 * Reuse the idle reverb output fields for Q16 delay positions. */
#define FX_CHORUS_BYTES 2048u
static inline __attribute__((always_inline)) int32_t fx_chorus_read(const int16_t *mem,uint32_t pos,uint32_t ch,int32_t delay)
{
    uint32_t whole=(uint32_t)delay>>16,frac=((uint32_t)delay>>8)&255u;
    uint32_t at=(pos-whole)&511u;
    int32_t a=mem[at*2u+ch],b=mem[((at-1u)&511u)*2u+ch];
    return (a*256+(b-a)*(int32_t)frac)>>6;
}
/* Use the existing POOL SRAM code section; low DSP SRAM is already full. */
static __attribute__((noinline,section(".audio_text"))) void fx_chorus(fx_state_t *restrict f,int16_t *restrict mem,uint32_t n,const int32_t *step,int32_t target)
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
/* Chorus has no feedback. A whole ring of zero input proves that every tap
 * is zero, independently of RATE/DEPTH and ring position. Reuse its otherwise
 * idle echo field as a conservative zero-write count; no persistent/RAM fields.
 * Keep the ordinary kernel separate so active input exits the scan early. */
#if MELODEE_FX_DORMANT
static __attribute__((noinline,section(".audio_text"))) void fx_chorus_run(fx_state_t *restrict f,int16_t *restrict mem,uint32_t n,const int32_t *step,int32_t target)
{
    uint32_t signal=n?(uint32_t)fx_left[0]|(uint32_t)fx_right[0]:0u;
    if(!signal)for(uint32_t i=0;i<n;i++)signal|=(uint32_t)fx_left[i]|(uint32_t)fx_right[i];
    if(!signal){
        if(f->echo==512){
            /* The dry copy is also zero. Repeated integer increments and the
             * monotone mix slew have exactly these closed forms. Phase/design
             * still run at their original block rate in fx_slot_run. */
            f->pos=(f->pos+n)&511u;
            f->rev_l+=step[0]*(int32_t)n;f->rev_r+=step[1]*(int32_t)n;
            f->mix+=clamp(target-f->mix,-(int32_t)n*128,(int32_t)n*128);
            return;
        }
        f->echo=clamp(f->echo+(int32_t)n,0,512);
    }else f->echo=0;
    void (*volatile run)(fx_state_t *,int16_t *,uint32_t,const int32_t *,int32_t)=fx_chorus;
    run(f,mem,n,step,target);
}
#else
#define fx_chorus_run fx_chorus
#endif
/* The filter inserts use a quarter-rate triangular decimator and pole,
 * independently per stereo side.
 * At most ceil(CTL/4) wet samples are staged; the dry audio stays at FS. */
static inline __attribute__((always_inline)) int32_t fx_filter_input(fx_state_t *f,uint32_t ch,int32_t x,uint32_t phase)
{
    int32_t *h=f->rev_damp+ch*4u;
    h[0]=(int32_t)((uint32_t)h[0]+(uint32_t)x);h[1]=(int32_t)((uint32_t)h[1]+(uint32_t)h[0]);
    if(!phase){
        uint32_t comb=(uint32_t)h[1]-(uint32_t)h[2];h[2]=h[1];
        int32_t input=fx_div_pow2((int32_t)(comb-(uint32_t)h[3]),4);h[3]=(int32_t)comb;
        f->rev_bass[ch]=fx_div_pow2(f->rev_bass[ch]+3*input,2);
    }
    return f->rev_bass[ch];
}
static __attribute__((noinline,section(".dsp_text"))) uint32_t fx_down(fx_state_t *restrict f,uint32_t n)
{
    uint32_t count=0,channels=f->mono || f->type==FX_WIDTH?1u:2u;
    for(uint32_t ch=0;ch<channels;ch++){
        int32_t *input=ch?fx_right:fx_left,*output=ch?fx_low_r:fx_low_l,*h=f->rev_damp+ch*4u;
        int32_t a=h[0],b=h[1],previous=h[2],difference=h[3],low=f->rev_bass[ch];
        uint32_t phase=f->decim;count=0;
        for(uint32_t i=0;i<n;i++){
            a=(int32_t)((uint32_t)a+(uint32_t)input[i]);b=(int32_t)((uint32_t)b+(uint32_t)a);
            if(!phase){
                uint32_t comb=(uint32_t)b-(uint32_t)previous;previous=b;
                int32_t x=fx_div_pow2((int32_t)(comb-(uint32_t)difference),4);difference=(int32_t)comb;
                low=fx_div_pow2(low+3*x,2);output[count++]=low;
            }
            phase=(phase+1u)&3u;
        }
        h[0]=a;h[1]=b;h[2]=previous;h[3]=difference;f->rev_bass[ch]=low;
    }
    if(channels==1u){for(uint32_t k=0;k<4u;k++)f->rev_damp[4+k]=f->rev_damp[k];f->rev_bass[1]=f->rev_bass[0];}
    return count;
}
static __attribute__((noinline,section(".dsp_text"))) void fx_up(fx_state_t *f,uint32_t n,int32_t target)
{
    uint32_t phase=f->decim,count=0;int32_t mix=f->mix,drive=1+f->character/8;
    for(uint32_t i=0;i<n;i++){
        if(!phase){
            f->rev_prev_l=f->rev_l;f->rev_prev_r=f->rev_r;
            int32_t l=fx_low_l[count],r=f->mono?l:fx_low_r[count];count++;
            if(f->type==FX_SPIT){l=softclip(clamp(l,-100000,100000)*drive);r=softclip(clamp(r,-100000,100000)*drive);}
            f->rev_l=l;f->rev_r=r;
        }
        int32_t l=f->rev_prev_l+fx_div_pow2((f->rev_l-f->rev_prev_l)*(phase+1u),2);
        int32_t r=f->mono?l:f->rev_prev_r+fx_div_pow2((f->rev_r-f->rev_prev_r)*(phase+1u),2);
        fx_return(target,f->type,f->mono,i,l,r,&mix);phase=(phase+1u)&3u;
    }
    f->decim=(uint8_t)phase;f->mix=mix;
}
static __attribute__((noinline,section(".dsp_text"))) void fx_copy(fx_state_t *f,uint32_t n)
{
    if(fx_sloop_type(f->type)){
        uint32_t mono=1,signal=0;
        for(uint32_t i=0;i<n;i++){
            int32_t l=fx_left[i],r=fx_right[i];fx_dry_l[i]=l;fx_dry_r[i]=r;
            signal|=(uint32_t)l|(uint32_t)r;if(l!=r)mono=0;
        }
        f->mono=(uint8_t)mono;f->echo=(int32_t)signal;return;
    }
    uint32_t mono=f->type<=FX_ECHO && f->type!=FX_WIDTH;
    for(uint32_t i=0;i<n;i++){
        fx_dry_l[i]=fx_left[i];fx_dry_r[i]=fx_right[i];if(fx_left[i]!=fx_right[i])mono=0;
    }
    /* Fold only equal signals AND equal histories. Panning/spread transitions
     * keep their stereo tails; returning to mono never resets a filter. */
    for(uint32_t k=0;k<4u && mono;k++)if((f->type<FX_ECHO && (f->history[0][k].z1!=f->history[1][k].z1 || f->history[0][k].z2!=f->history[1][k].z2)) || f->rev_damp[k]!=f->rev_damp[4+k])mono=0;
    if(f->rev_bass[0]!=f->rev_bass[1] || f->rev_l!=f->rev_r || f->rev_prev_l!=f->rev_prev_r)mono=0;
#if MELODEE_HW_EQ
    if(fx_hw_status==1)mono=0; /* hardware histories are held in its rows */
#endif
    f->mono=(uint8_t)mono;
}
static __attribute__((noinline,section(".audio_text"))) void fx_zero(fx_state_t *f)
{
    typedef uint32_t word_t __attribute__((may_alias));word_t *words=(word_t *)f;for(uint32_t k=0;k<sizeof *f/4u;k++)words[k]=0;
}
static __attribute__((noinline)) void *fx_memory(uint32_t id,uint32_t bytes){return resource_get(id,bytes);}
typedef struct {fx_state_t *f;int16_t *mem;int32_t target;int16_t third;uint8_t active,shared;} fx_plan_t;
/* CPU0 owns allocation, releases and control snapshots. A worker receives
 * only fixed handles, preventing a retired worker from interrupting the arena. */
static void fx_slot_prepare(fx_plan_t *plan,uint32_t si,const int16_t *p)
{
    plan->active=0;
    uint32_t type=(uint32_t)clamp(p[0],0,FX_COUNT-1);
    if(fx_is_midi((int32_t)type) || fx_retired((int32_t)type))type=FX_OFF; /* retired effects and note inserts never enter audio DSP */
    fx_state_t *f=&fx_state[si];
    if(fx_retired(f->type) || fx_retired(p[0])) { resource_release(RES_FX0+si); fx_zero(f); }
    int32_t amount=clamp(p[1],0,127);
    int32_t target=type?(amount+(amount==127))*256:0;
#if defined(MELODEE_FX_PROFILE) && MELODEE_FX_PROFILE && defined(FM1_IRQ_TARGET)
    uint32_t slot=si%FX_SLOTS;
    uint32_t bus=fx_profile_control>>30u,profiling=bus!=0u;
    if(profiling){type=(fx_profile_control>>(slot*4u))&15u;target=type?(bus?32768:100*256):0;}
#endif
    uint32_t changed=f->type!=type;
    if(f->type && type && changed && f->mix)target=0;
    if(!target && !f->mix){
        if(f->type || resource[RES_FX0+si].size){resource[RES_FX0+si].size=0;fx_zero(f);}
        return;
    }
    if(f->epoch!=fx_epoch || (type && changed && !f->mix)){
        resource[RES_FX0+si].size=0;fx_zero(f);
        f->epoch=fx_epoch;f->type=(uint8_t)type;f->rng=(int32_t)(0x73a9825du+(NPART*FX_SLOTS+si)*997u);
    }
    f->rate=(int16_t)clamp(p[2],0,127);f->character=(int16_t)clamp(p[3],0,127);
    if(fx_has_tone(f->type)){   /* the low cut (section 0: the floor, TONE above 0 higher), TONE below 0 the high cut (1) */
        uint32_t slot=si%FX_SLOTS,div=f->type==FX_REVERB?1u:4u;
        int32_t tone=clamp(song.g[G_MFXTONE+slot],-FX_TONE_MAX,FX_TONE_MAX);
        for(uint32_t k=0;k<2u;k++){   /* (a2: the cutoff now, Hz; 0 off) */
            int32_t hz=k?(tone<0?fx_tone_hz(tone):0):fx_low_cut_hz(f->type,tone);
            fx_bq_t *q=&f->time_tone[k];
            if(q->a2!=(float)hz){
                if(hz)q->a1=fx_cut_coefficient(hz,div);
                q->a2=(float)hz;
            }
        }
    }
#if defined(MELODEE_FX_PROFILE) && MELODEE_FX_PROFILE && defined(FM1_IRQ_TARGET)
    if(profiling){f->rate=(fx_profile_control>>12u)&127u;f->character=(fx_profile_control>>19u)&127u;}
#endif
    int16_t *mem=0;
    if(f->type==FX_ROOM || f->type==FX_CHORUS || f->type==FX_REVERB || fx_sloop_type(f->type)){
        if(f->type==FX_REVERB)for(uint32_t k=0;k<FX_STATE_COUNT;k++)
            if(k!=si && fx_state[k].type==FX_REVERB && resource[RES_FX0+k].size)return;
        uint32_t bytes=f->type==FX_REVERB?sizeof(fx_sloop_reverb_t):fx_sloop_type(f->type)?fx_sloop_insert_bytes(f->type):2048u;
        if(!f->failed && !resource[RES_FX0+si].size){
            void *(*volatile allocate)(uint32_t,uint32_t)=fx_memory;mem=allocate(RES_FX0+si,bytes);if(!mem)f->failed=1;
        }else if(resource[RES_FX0+si].size)mem=resource[RES_FX0+si].ptr;
        if(!mem){f->mix=0;return;}
    }
    plan->shared=1u;
    plan->third=song.g[G_MFXTONE+si%FX_SLOTS];
    plan->f=f;plan->mem=mem;plan->target=target;plan->active=1;
}
static __attribute__((noinline,section(".audio_text"))) int fx_slot_run(const fx_plan_t *plan,uint32_t n)
{
    if(!plan->active)return 0;
    fx_state_t *f=plan->f;int16_t *mem=plan->mem;int32_t target=plan->target;
    void (*volatile copy)(fx_state_t *,uint32_t)=fx_copy;copy(f,n);
    if(fx_sloop_type(f->type)){
        fx_sloop_insert_run(f,(fx_sloop_insert_t *)mem,n,target,fx_left,fx_right,plan->third,plan->shared);
    }
    else if(f->type==FX_REVERB){fx_sloop_reverb_run(f,(fx_sloop_reverb_t *)mem,n,target,fx_left,fx_right);}
    else if(f->type==FX_CHORUS){
        if(!n)return 1;
        if(!f->age){f->rev_l=f->rev_r=256*65536;f->age=1;}
        f->phase+=LFO_INC[f->rate]*n/CTL;
        int32_t depth=f->character*2,delay=256*65536+fx_sine(f->phase)*depth;
        int32_t step[2]={(delay-f->rev_l)/(int32_t)n,(512*65536-delay-f->rev_r)/(int32_t)n};
        void (*volatile run)(fx_state_t *,int16_t *,uint32_t,const int32_t *,int32_t)=fx_chorus_run;run(f,mem,n,step,target);
    }else if(f->type==FX_ROOM){void (*volatile run)(fx_state_t *,int16_t *,uint32_t,int32_t)=fx_time;run(f,mem,n,target);}
    else {
        if(f->type==FX_TAPE){void (*volatile design)(fx_state_t *,uint32_t)=fx_tape_design;design(f,n);}
        else {if(!fx_hw_status){void (*volatile probe)(void)=fx_probe;probe();}void (*volatile design)(fx_state_t *,uint32_t)=fx_design;design(f,n);}
        uint32_t (*volatile down)(fx_state_t *,uint32_t)=fx_down;uint32_t count=down(f,n);
        if(f->type==FX_TAPE){void (*volatile run)(fx_state_t *,int32_t *,int32_t *,uint32_t)=fx_tape;run(f,fx_low_l,fx_low_r,count);}
        else {
            void (*volatile run)(fx_state_t *,uint32_t,int32_t *,uint32_t)=fx_filter;
            run(f,0,fx_low_l,count);
            if(f->mono){for(uint32_t k=0;k<4u;k++){f->history[1][k]=f->history[0][k];}}
            else if(f->type!=FX_WIDTH)run(f,1,fx_low_r,count);
        }
        void (*volatile up)(fx_state_t *,uint32_t,int32_t)=fx_up;up(f,n,target);
    }
    return 1;
}
/* Shared send/return: one chain or three independent post-fader buses. CPU0 and CPU1 never
 * access this workspace concurrently; join before events or flash IO. */
static int32_t fx_send_l[CTL],fx_send_r[CTL];
static int32_t fx_send_extra_l[FX_SLOTS-1u][CTL],fx_send_extra_r[FX_SLOTS-1u][CTL];
static int32_t fx_bus_sum_l[CTL],fx_bus_sum_r[CTL];
static uint8_t fx_bus_separate;
static int32_t fx_bus_slot_mix[FX_SLOTS];
static uint32_t fx_bus_n;
/* a track's send for fx_scale on the bus (Q15 << MIX_FRAC, mix.c): its Q16 amount / MIX_ONE, so the sends are Q15 */
static inline __attribute__((always_inline)) uint32_t fx_send_gain(const track_t *t)
{
#if defined(MELODEE_FX_PROFILE) && MELODEE_FX_PROFILE && defined(FM1_IRQ_TARGET)
    if(fx_profile_control>>30u && fx_profile_track((uint32_t)(t-trk)))return 65536u>>MIX_FRAC;
#endif
    return (uint32_t)(t->p[P_FXSEND]+(t->p[P_FXSEND]==127))*(512u>>MIX_FRAC);
}
static int fx_bus_result,fx_bus_pending;
#if MELODEE_AUDIO_PROFILE
static uint32_t fx_audio_ticks,fx_audio_ready;
#endif
static int32_t fx_bus_mix,fx_bus_target;
static fx_plan_t fx_bus_plan[FX_SLOTS];
/* Fade the return before changing topology, then reset only the global histories.
 * Inserts keep their state; separate send levels survive a round trip to CHAIN. */
static void fx_bus_select(void)
{
    uint8_t separate=song.g[G_FXMODE]!=0;
    if(separate==fx_bus_separate || fx_bus_mix)return;
    fx_bus_separate=separate;
    memset(fx_bus_slot_mix,0,sizeof fx_bus_slot_mix);
    for(uint32_t slot=0;slot<FX_SLOTS;slot++)fx_state[slot].epoch=fx_epoch-1u;
}
static void fx_bus_kernel(void *unused)
{
    AP_START(audio_fx_start);
    (void)unused;fx_bus_result=0;
#if defined(MELODEE_FX_PROFILE) && MELODEE_FX_PROFILE && defined(FM1_IRQ_TARGET)
    uint32_t on=fx_profile_control>>30u,start=on?fm1_ticks():0;
#endif
    if(fx_bus_separate){
        memset(fx_bus_sum_l,0,fx_bus_n*sizeof *fx_bus_sum_l);memset(fx_bus_sum_r,0,fx_bus_n*sizeof *fx_bus_sum_r);
        for(uint32_t slot=0;slot<FX_SLOTS;slot++){
            if(!fx_bus_plan[slot].active){fx_bus_slot_mix[slot]=0;continue;}
            const int32_t *l=slot?fx_send_extra_l[slot-1u]:fx_send_l,*r=slot?fx_send_extra_r[slot-1u]:fx_send_r;
            memcpy(fx_left,l,fx_bus_n*sizeof *l);memcpy(fx_right,r,fx_bus_n*sizeof *r);
            fx_bus_result|=fx_slot_run(&fx_bus_plan[slot],fx_bus_n);
            int32_t gain=fx_bus_slot_mix[slot],target=fx_bus_plan[slot].target?32768:0;
            for(uint32_t i=0;i<fx_bus_n;i++){
                if(gain!=target)gain+=clamp(target-gain,-128,128);
                fx_bus_sum_l[i]+=fx_scale(fx_left[i],(uint32_t)gain*2u);fx_bus_sum_r[i]+=fx_scale(fx_right[i],(uint32_t)gain*2u);
            }
            fx_bus_slot_mix[slot]=gain;
        }
        memcpy(fx_left,fx_bus_sum_l,fx_bus_n*sizeof *fx_left);memcpy(fx_right,fx_bus_sum_r,fx_bus_n*sizeof *fx_right);
    }else for(uint32_t slot=0;slot<FX_SLOTS;slot++)fx_bus_result|=fx_slot_run(&fx_bus_plan[slot],fx_bus_n);
#if MELODEE_AUDIO_PROFILE
    fx_audio_ticks=AUDIO_PROFILE_TICKS()-audio_fx_start;fx_audio_ready=1;
#endif
#if defined(MELODEE_FX_PROFILE) && MELODEE_FX_PROFILE && defined(FM1_IRQ_TARGET)
    if(on){uint32_t elapsed=fm1_ticks()-start+fx_profile_bus_setup;fx_profile_ticks[0]+=elapsed;if(elapsed>fx_profile_max[0])fx_profile_max[0]=elapsed;fx_profile_blocks[0]++;}
#endif
}
static int fx_bus_active(void)
{
    uint32_t base=0;
#if defined(MELODEE_FX_PROFILE) && MELODEE_FX_PROFILE && defined(FM1_IRQ_TARGET)
    if(fx_profile_control>>30u)return 1;
#endif
    for(uint32_t slot=0;slot<FX_SLOTS;slot++)if((song.g[G_MFXTYPE+slot*FX_PARAMS] && !fx_retired(song.g[G_MFXTYPE+slot*FX_PARAMS]) && song.g[G_MFXMIX+slot*FX_PARAMS]) || fx_state[base+slot].mix)return 1;
    for(uint32_t slot=0;slot<FX_SLOTS;slot++)if(fx_state[base+slot].type || resource[RES_FX0+base+slot].size){resource[RES_FX0+base+slot].size=0;fx_zero(&fx_state[base+slot]);}
    return 0;
}
static void fx_bus_start(uint32_t n)
{
#if MELODEE_AUDIO_PROFILE
    fx_audio_ready=0;fx_audio_ticks=0;
#endif
    shared_fx_normalize(song.g);
    fx_bus_select();
    fx_bus_result=fx_bus_pending=0;fx_bus_target=0;fx_bus_n=n;if(!fx_bus_active())return;
    AP_START(audio_start);
#if defined(MELODEE_FX_PROFILE) && MELODEE_FX_PROFILE && defined(FM1_IRQ_TARGET)
    uint32_t start=fm1_ticks();
#endif
    for(uint32_t slot=0;slot<FX_SLOTS;slot++)fx_slot_prepare(&fx_bus_plan[slot],slot,song.g+G_MFXTYPE+slot*FX_PARAMS);
    for(uint32_t slot=0;slot<FX_SLOTS;slot++)if(fx_bus_plan[slot].active && fx_bus_plan[slot].target){
        int32_t amount=clamp(song.g[G_FXRETURN],0,127);fx_bus_target=(amount+(amount==127))*256;break;
    }
    if((song.g[G_FXMODE]!=0)!=fx_bus_separate)fx_bus_target=0;
    /* A later allocation may compact earlier handles; snapshot pointers only
     * after all three preparations have completed. */
    for(uint32_t slot=0;slot<FX_SLOTS;slot++)if(fx_bus_plan[slot].active && fx_bus_plan[slot].f->type>=FX_ECHO)fx_bus_plan[slot].mem=resource[RES_FX0+slot].ptr;
    for(uint32_t i=0;i<n;i++){fx_left[i]=fx_send_l[i];fx_right[i]=fx_send_r[i];}
    AP_END(AP_FX,audio_start);
#if defined(MELODEE_FX_PROFILE) && MELODEE_FX_PROFILE && defined(FM1_IRQ_TARGET)
    fx_profile_bus_setup=fm1_ticks()-start;
#endif
#if MELODEE_DUAL_CORE && !MELODEE_HW_EQ
    if(audio_worker_submit(fx_bus_kernel,0)){fx_bus_pending=1;return;}
#endif
    fx_bus_kernel(0);
}
static void fx_bus_join(void)
{
#if MELODEE_DUAL_CORE
    if(fx_bus_pending && !audio_worker_join()){fx_bus_result=0;fx_bus_mix=0;for(uint32_t slot=0;slot<FX_SLOTS;slot++)fx_state[slot].epoch=fx_epoch-1u;}
#endif
    fx_bus_pending=0;
#if MELODEE_AUDIO_PROFILE
    if(fx_audio_ready){audio_profile_add(AP_FX,fx_audio_ticks);fx_audio_ready=0;}
#endif
}
