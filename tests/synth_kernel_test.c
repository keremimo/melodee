/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Kernel parity against the unmodified 05a05cf sample ordering. The Prophet
 * reference below is retained Melodee code by Kerem Kilic (Ellic Studio).
 * FM6 uses its retained float-ramp operators and original bus ordering. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
static uint32_t random_state=0xa983671u;
static uint32_t next_u(void){random_state=random_state*1664525u+1013904223u;return random_state;}
static float unit(void){return (float)(next_u()>>8)*(1.0f/16777216.0f);}
static __attribute__((section(".dsp_text"))) void p5_samples_reference(p5_voice_t *restrict s,const uint8_t *restrict p,int32_t *restrict out,uint32_t n,const vmod_t *m,const p5_render_params_t *c)
{
    int32_t last=0;
    uint32_t fixed_pw_b=p5_pw(c->pwm_b);
    for(uint32_t i=0;i<n;i++){
        float time=(float)i*(1.0f/CTL);
        float fe=c->f0+(c->f1-c->f0)*time,ae=c->a0+(c->a1-c->a0)*time,sum=0.0f;
        for(uint32_t os=0;os<P5_OVERSAMPLE;os++){
            float noise=(float)(noise32(&s->noise)>>8)*(1.0f/16777216.0f)-0.5f;
            float lfo=c->wheel[i*P5_OVERSAMPLE+os],wheel_pitch=c->wheel_pitch[i*P5_OVERSAMPLE+os];
            uint32_t db=p[P5_WHEEL_FREQ_B]?p5_wheel_inc(c->ib,wheel_pitch):c->ib;
            float b=p5_wave(s->phase[1],db,(p[P5_WHEEL_PW_B]?p5_pw(c->pwm_b+lfo):fixed_pw_b),p[P5_SAW_B],p[P5_PULSE_B],p[P5_TRI_B],&s->triangle);
            float bn=b*2.0f;
            uint32_t da=p[P5_POLY_FREQ]?p5_inc(c->ia,fe*c->pa_env+bn*c->pa_b):c->ia;
            if(p[P5_WHEEL_FREQ_A])da=p5_wheel_inc(da,wheel_pitch);
            uint32_t pw=p5_pw(c->pwm_a+(p[P5_POLY_PW]?fe*c->pw_env+b*c->pw_b:0.0f)+(p[P5_WHEEL_PW_A]?lfo:0.0f));
            float unused=0.0f;
            float a=p5_wave(s->phase[0],da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused)+s->sync_tail;
            s->sync_tail=0.0f;
            uint32_t nb=s->phase[1]+db,na=s->phase[0]+da;
            if(p[P5_SYNC] && nb<s->phase[1] && db){
                float frac=p5_sync_fraction(0u-s->phase[1],db),remain=1.0f-frac;
                uint32_t at=s->phase[0]+(uint32_t)((float)da*frac);
                float before=p5_wave(at,da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused);
                float after=p5_wave(0,da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused);
                float jump=(after-before)*0.5f;
                a+=jump*remain*remain;s->sync_tail=-jump*frac*frac;
                na=(uint32_t)((float)da*remain);
            }
            s->phase[0]=na;s->phase[1]=nb;
            float x=a*c->la+b*c->lb+noise*c->ln;
            float fc=c->cut+fe*c->filter_velocity*c->env_amount;
            if(p[P5_POLY_FILTER])fc+=fe*c->pf_env+bn*c->pf_b;
            if(p[P5_WHEEL_FILTER])fc+=lfo*112.0f;
            sum+=p5_filter_core(s,p5_fast_softclip(x),fc,c->k,c->filter_gain,!!p[P5_FILTER_REV]);
        }
        sum*=1.0f/P5_OVERSAMPLE;
        float gain=ae*c->amp_velocity*(1.0f+(float)s->amp_drift/32768.0f);
        float amp=((float)m->amp0+(float)(m->amp1-m->amp0)*time)*(1.0f/32768.0f);
        last=dith_cutf(sum*gain*amp*(2.0f*VOICE_FS),i+(uint32_t)m->amp1);out[i]+=last;
    }
    s->last_pcm=last;
}

static void fm6_reference(fm6_voice_t *s,uint32_t n,float bus[2][CTL],float *sum)
{
    for(uint32_t i=0;i<n;i++)sum[i]=0;
    for(uint32_t k=0;k<6;k++){
        uint32_t pl=s->plan[k],o=pl&3u,in=(pl>>4)&3u;float *out=o?bus[o-1u]:sum;
        if(!(pl&FM6_P_RUN))continue;
        if(!k && s->loop)fm6_op_loop(s,out,n);
        else if(pl&FM6_P_FB)fm6_op_fb(s,k,out,pl&FM6_P_ADD,s->eng,n);
        else fm6_op(s,k,out,in?bus[in-1u]:0,pl&FM6_P_ADD,s->eng,n);
    }
}
static void fm6_parity(void)
{
    uint32_t cases=0;
    for(uint32_t i=0;i<1024;i++){assert(FM6_MKI_LOG[i]<=FM6_MKI_LOG[0]);assert(FM6_MKI_EXP[i]<8192u);}
    const uint32_t sizes[]={0,1,7,31,32};
    for(uint32_t mode=0;mode<3;mode++)for(uint32_t alg=0;alg<32;alg++)for(uint32_t fb=0;fb<=16;fb+=2){
        fm6_voice_t a={0},b;
        for(uint32_t rep=0;rep<32;rep++){
            int32_t lv[6];
            for(uint32_t k=0;k<6;k++){
                lv[k]=(int32_t)(next_u()%(20u<<24));
                if(rep%4==0)lv[k]=0;
                if(rep%4==1)lv[k]=15<<24;
                a.fq[k]=(int32_t)(next_u()&0xffffffu);a.ph[k]=next_u();
            }
            fm6_plan(&a,lv,alg,mode,fb);b=a;
            for(uint32_t z=0;z<NELEM(sizes);z++){
                float ref_bus[2][CTL]={{0}},ref_sum[CTL]={0};
                memset(fm6_bus,0,sizeof fm6_bus);memset(fm6_sum,0,sizeof fm6_sum);
                fm6_reference(&b,sizes[z],ref_bus,ref_sum);fm6_run(&a,sizes[z]);
                assert(!memcmp(&a,&b,sizeof a));assert(!memcmp(fm6_sum,ref_sum,sizeof ref_sum));
                assert(!memcmp(fm6_bus,ref_bus,sizeof ref_bus));cases++;
            }
        }
    }
#if MELODEE_FM6_FAST
    const int32_t bounds[]={-4096,0,13311,13312,16284,16384,22170,22171,32767,32768};
    for(uint32_t e=0;e<NELEM(bounds);e++)for(int32_t d=-1;d<=1;d++)for(uint32_t p=0;p<4096;p++){
        fm6_voice_t a={0},b;float x[CTL],y[CTL],input[CTL];
        a.ph[0]=(p<<12)|(p*71u&4095u);a.fq[0]=278653;a.g[0]=(float)bounds[e];a.dg[0]=(float)d;a.eng=FM6_MARK1;b=a;
        for(uint32_t i=0;i<CTL;i++){x[i]=y[i]=0.25f;input[i]=(float)((int32_t)(p%1024)-512)*(1.0f/16.0f);}
        uint32_t n=p%33u;const float *in=p&1u?input:0;
        fm6_op(&a,0,x,in,1,FM6_MARK1,n);fm6_op_mark1(&b,0,y,in,1,n);
        assert(!memcmp(&a,&b,sizeof a));assert(!memcmp(x,y,sizeof x));cases++;
    }
#endif
    printf("FM6: %u exact operator-bus/state cases across all algorithms, modes, feedback and block lengths\n",cases);
}
static void prophet_parity(void)
{
    uint32_t cases=0;
    float wheel[CTL*P5_OVERSAMPLE],pitch[CTL*P5_OVERSAMPLE];
    for(uint32_t preset=0;preset<=P5_FACTORY_N;preset++){
        p5_patch_t patch;if(preset)p5_factory(preset-1,&patch);else p5_patch_init(&patch);
        p5_voice_t a={0},b={0};a.noise=b.noise=(int32_t)next_u();
        for(uint32_t edit=0;edit<64;edit++){
            if(edit){uint32_t k=next_u()%55u;patch.raw[k]=(uint8_t)(next_u()%128u);}
            p5_render_params_t c={0};vmod_t m={0};
            c.f0=unit();c.f1=unit();c.a0=unit();c.a1=unit();c.cut=unit()*170-20;
            c.k=unit()*5;c.filter_gain=unit();c.filter_velocity=unit();c.amp_velocity=unit();
            c.env_amount=unit()*100;c.pa_env=unit()*2;c.pa_b=unit();c.pf_env=unit()*100;c.pf_b=unit()*100;
            c.pw_env=unit();c.pw_b=unit();c.pwm_a=unit();c.pwm_b=unit();c.la=unit();c.lb=unit();c.ln=unit();
            c.ia=next_u()%1932735232u;c.ib=next_u()%1932735232u;
            for(uint32_t i=0;i<CTL*P5_OVERSAMPLE;i++){wheel[i]=unit()*2-1;pitch[i]=p5_pitch_ratio(wheel[i]);}
            c.wheel=wheel;c.wheel_pitch=pitch;m.amp0=(int32_t)(next_u()%32769u);m.amp1=(int32_t)(next_u()%32769u);
            uint32_t n=edit%33u;int32_t x[CTL],y[CTL];
            for(uint32_t i=0;i<CTL;i++)x[i]=y[i]=(int32_t)(next_u()%60001u)-30000;
            p5_samples_reference(&a,patch.raw,x,n,&m,&c);p5_samples(&b,patch.raw,y,n,&m,&c);
            assert(!memcmp(&a,&b,sizeof a));assert(!memcmp(x,y,sizeof x));cases++;
        }
    }
    printf("Prophet: %u exact PCM/full-state cases, all factory programs, live edits, sync, modulation and block lengths (OS %u)\n",cases,P5_OVERSAMPLE);
}
static uint64_t instructions(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 r;
    if(!proc_pid_rusage(getpid(),RUSAGE_INFO_V4,(rusage_info_t *)&r))return r.ri_instructions;
#endif
    return 0;
}
static volatile uint32_t checksum;
static void fm6_operator_reference(fm6_voice_t *s,float *out,const float *in,int add)
{fm6_op(s,0,out,in,add,s->eng,CTL);}
static void fm6_operator_candidate(fm6_voice_t *s,float *out,const float *in,int add)
{
#if MELODEE_FM6_FAST
    if(s->eng==FM6_MARK1)fm6_op_mark1(s,0,out,in,add,CTL);
    else if(s->eng==FM6_MODERN && s->dg[0]==0.0f)fm6_op_modern_flat(s,0,out,in,add,CTL);
    else
#endif
        fm6_op(s,0,out,in,add,s->eng,CTL);
}
static void benchmark(void)
{
    puts("operator_mode,modulated,ramping,quiet,reference_instructions_per_sample,candidate_instructions_per_sample");
    for(uint32_t mode=0;mode<3;mode++)for(uint32_t mod=0;mod<2;mod++)for(uint32_t ramp=0;ramp<2;ramp++)for(uint32_t quiet=0;quiet<(mode==FM6_MARK1 && !ramp?2u:1u);quiet++){
        double costs[2];
        for(uint32_t fast=0;fast<2;fast++){
            fm6_voice_t s={0};float out[CTL]={0},in[CTL];
            s.eng=(uint8_t)mode;s.ph[0]=0xa98371u;s.fq[0]=317001;s.g[0]=mode==FM6_MODERN?0.7f:quiet?15000.0f:12000.0f;
            s.dg[0]=ramp?(mode==FM6_MODERN?0.001f:1.0f):0.0f;
            for(uint32_t i=0;i<CTL;i++)in[i]=(float)(int32_t)(i*31987123u)*(1.0f/536870912.0f);
            void (*volatile run)(fm6_voice_t *,float *,const float *,int)=fast?fm6_operator_candidate:fm6_operator_reference;
            uint64_t start=instructions();
            for(uint32_t b=0;b<10000;b++){run(&s,out,mod?in:0,1);checksum+=s.ph[0];}
            costs[fast]=(double)(instructions()-start)/(10000*CTL);
        }
        printf("%u,%u,%u,%u,%.3f,%.3f\n",mode,mod,ramp,quiet,costs[0],costs[1]);
#if MELODEE_FM6_FAST
        if(quiet && costs[0])assert(costs[1]<costs[0]*0.5);
#endif
    }
}
int main(int argc,char **argv){fm6_parity();prophet_parity();if(argc==1)benchmark();return 0;}
