/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) Devin Acker
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * License: LICENSES/BSD-3-Clause-uPD933.txt */
/* Retained original CZ phase kernel, plus a host-only window specialization. */
#define MELODEE_CZ_FAST 1
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
static __attribute__((noinline)) float wave_reference(const cz_hw_pd_t *b,uint32_t ph,uint32_t toggle)
{
    float pos=(float)ph*(2048.0f/4294967296.0f),half=pos>=1024.0f?pos-1024.0f:pos;
    float pivot=b->pivot,phase=0.0f,window=0.0f;
    switch(b->wave[toggle&1u]){
    case 0:phase=pos<pivot?pos*b->k0:1024.0f+(pos-pivot)*b->k1;break;
    case 1:phase=(half<pivot?half*b->k0:1024.0f)+(pos>=1024.0f?1024.0f:0.0f);break;
    case 2:phase=pos<pivot*2.0f?pos*b->k0:2048.0f;break;
    case 3:return 0.0f;
    case 4:phase=pos<pivot?pos*b->k0*2.0f:(pos-pivot)*b->k1*2.0f;break;
    case 5:phase=pos<1024.0f?pos:pos<pivot+1024.0f?1024.0f+half*b->k0:2048.0f;break;
    case 6:phase=pos*(1.0f+b->dcw*(1.0f/64.0f));break;
    default:phase=half<pivot?half*b->k0:2048.0f;break;
    }
    phase-=(float)(uint32_t)(phase*(1.0f/2048.0f))*2048.0f;
    switch(b->window){
    case 0:break;
    case 1:window=pos;break;
    case 2:window=pos<1024.0f?2048.0f-half*2.0f:half*2.0f;break;
    case 3:if(pos>=1024.0f)window=half*2.0f;break;
    case 4:window=pos<1024.0f?pos*2.0f:2048.0f;break;
    default:window=(1024.0f-half)*2.0f;break;
    }
    /* The normalized sine table is shared with FM6. Interpolate its cosine
     * quadrant in float: no chip-phase or PCM truncation before the mixer. */
    float x=phase*0.5f+256.0f;uint32_t i=(uint32_t)x;float f=x-(float)i;
    i&=1023u;
    float carrier=1.0f-(FM6_SIN[i]+(FM6_SIN[i+1u]-FM6_SIN[i])*f);
    return carrier*(1.0f-window*(1.0f/2048.0f))-1.0f;
}
static __attribute__((noinline)) float wave_probe(const cz_hw_pd_t *b,uint32_t ph,uint32_t toggle)
{
    float pos=(float)ph*(2048.0f/4294967296.0f),half=pos>=1024.0f?pos-1024.0f:pos;
    float pivot=b->pivot,phase=0.0f,window=0.0f;
    switch(b->wave[toggle&1u]){
    case 0:phase=pos<pivot?pos*b->k0:1024.0f+(pos-pivot)*b->k1;break;
    case 1:phase=(half<pivot?half*b->k0:1024.0f)+(pos>=1024.0f?1024.0f:0.0f);break;
    case 2:phase=pos<pivot*2.0f?pos*b->k0:2048.0f;break;
    case 3:return 0.0f;
    case 4:phase=pos<pivot?pos*b->k0*2.0f:(pos-pivot)*b->k1*2.0f;break;
    case 5:phase=pos<1024.0f?pos:pos<pivot+1024.0f?1024.0f+half*b->k0:2048.0f;break;
    case 6:phase=pos*(1.0f+b->dcw*(1.0f/64.0f));break;
    default:phase=half<pivot?half*b->k0:2048.0f;break;
    }
    if(b->window==4u && pos>=1024.0f)return -1.0f;
    phase-=(float)(uint32_t)(phase*(1.0f/2048.0f))*2048.0f;
    switch(b->window){
    case 0:break;
    case 1:window=pos;break;
    case 2:window=pos<1024.0f?2048.0f-half*2.0f:half*2.0f;break;
    case 3:if(pos>=1024.0f)window=half*2.0f;break;
    case 4:window=pos<1024.0f?pos*2.0f:2048.0f;break;
    default:window=(1024.0f-half)*2.0f;break;
    }
    /* The normalized sine table is shared with FM6. Interpolate its cosine
     * quadrant in float: no chip-phase or PCM truncation before the mixer. */
    float x=phase*0.5f+256.0f;uint32_t i=(uint32_t)x;float f=x-(float)i;
    i&=1023u;
    float carrier=1.0f-(FM6_SIN[i]+(FM6_SIN[i+1u]-FM6_SIN[i])*f);
    return carrier*(1.0f-window*(1.0f/2048.0f))-1.0f;
}

static __attribute__((noinline)) void cz_render_reference(track_t *t,voice_t *v,int32_t *out,uint32_t n,const vmod_t *m)
{
    if(!n)return;
    const uint8_t *b=cz_patch[(uint32_t)(t-trk)%NTRK].raw;
    cz_voice_t *c=cz_voice(t,v);const cz_native_def_t (*defs)[3]=cz_part((uint32_t)(t-trk)%NTRK)->native_def;cz_hw_pd_t pd[2];
    uint32_t inc[2],ph[2]={v->ph[0],v->ph[1]},tg[2]={(uint32_t)v->s[0],(uint32_t)v->s[1]};
    float amp[2][2],step[2];
    uint32_t ls=b[0]&3u,first=ls==1u?1u:0u,last=ls>=2u?1u:first;
    float vib=cz_native_vibrato(c,b),oct=(b[0]>>2)==1u?12.0f:(b[0]>>2)==2u?-12.0f:0.0f;
    for(uint32_t l=0;l<2u;l++){
        uint32_t src=ls==2u?0u:l,off=src*57u;
        uint32_t av=15u-(b[20u+off]>>4),al=b[16u+off]>>4;
        int heard=l>=first && l<=last;
        if(heard)amp[l][0]=cz_native_amp(c->native_eg[l][2].level,al,av,v->vel);
        float pitch=cz_native_env_tick(&c->native_eg[l][0],&defs[src][0],v->gate);
        float depth=cz_native_env_tick(&c->native_eg[l][1],&defs[src][1],v->gate);
        uint32_t kfn=v->note>36u?v->note-36u:0u;
        float rate_scale=1.0f+(float)((b[16u+off]&15u)*kfn)*(1.0f/96.0f);
        cz_native_env_scaled(&c->native_eg[l][2],&defs[src][2],v->gate,rate_scale);
        /* Unheard envelopes keep advancing. Their stateless waveform,
         * amplitude and frequency preparation cannot affect next-note state. */
        if(!heard)continue;
        amp[l][1]=cz_native_amp(c->native_eg[l][2].level,al,av,v->vel);
        float difference=amp[l][1]-amp[l][0];
        step[l]=n==CTL?difference*(1.0f/CTL):difference/(float)n;
        uint32_t pv=15u-(b[54u+off]>>4),wv=15u-(b[37u+off]>>4);
        pitch*=1.0f-(float)((127u-v->vel)*pv)*(1.0f/(127.0f*15.0f));
        depth*=1.0f-(float)((127u-v->vel)*wv)*(1.0f/(127.0f*15.0f));
        uint32_t kf=b[18u+off],kfnote=v->note>36u?v->note-36u:0u;
        depth*=(!kf || !kfnote)?1.0f:96.0f/(float)(96u+kf*kfnote);
        float det=l?((float)b[3]+(float)(b[2]>>2)*(1.0f/64.0f))*(b[1]?-1.0f:1.0f):0.0f;
        float nt=fm_clampf((float)m->pitch16*(1.0f/16.0f)+oct+vib+pitch+det,0.0f,2047.0f/16.0f);
        cz_native_freq_t *f=&c->native_freq;
        if(!((f->valid>>l)&1u) || f->note[l]!=nt || f->fine[l]!=m->fine || f->a4[l]!=tuning_a4){
            f->note[l]=nt;f->fine[l]=m->fine;f->a4[l]=tuning_a4;f->inc[l]=cz_native_inc(nt,m->fine);f->valid|=1u<<l;
        }
        inc[l]=f->inc[l];
        uint32_t word=(uint32_t)b[14u+off]<<8|b[15u+off];
        float dep=depth+(float)(m->cutoff+m->shape-(64<<8))*(1.0f/32.0f);
        cz_native_pd(&pd[l],word,dep);
    }
    v->s[4]=(int32_t)(c->native_eg[first][2].level*256.0f);
    uint32_t modulation=(b[15]>>3)&7u;
    for(uint32_t i=0;i<n;i++){
        float line[2]={0.0f,0.0f};
        for(uint32_t l=first;l<=last;l++){
            uint32_t old=ph[l],delta=inc[l];
            if(l && modulation==3u){
                c->noise^=c->noise<<13;c->noise^=c->noise>>17;c->noise^=c->noise<<5;
                if(c->noise&1u)delta=(uint32_t)fm_minf((float)delta*(1625.0f/256.0f),2147483520.0f);
            }
            float raw=wave_reference(&pd[l],ph[l],tg[l]);ph[l]+=delta;
            if(ph[l]<old)tg[l]^=1u;
            c->native_dc[l]+=(raw-c->native_dc[l])*(1.0f/1024.0f);
            raw=(raw-c->native_dc[l])*0.5f;
            line[l]=raw*(amp[l][0]+step[l]*(float)i);
        }
        float sample=first==last?line[first]:modulation==4u?line[0]*line[1]*2.0f:(line[0]+line[1])*0.5f;
        float gain=((float)m->amp0+(float)(m->amp1-m->amp0)*(float)i*(1.0f/CTL))*(1.0f/32768.0f);
        out[i]+=(int32_t)(sample*gain*(4.0f*VOICE_FS));
    }
    v->ph[0]=ph[0];v->ph[1]=ph[1];v->s[0]=(int32_t)tg[0];v->s[1]=(int32_t)tg[1];
}

static uint32_t random_state=0x8179352u;
static uint32_t next_u(void){random_state=random_state*1664525u+1013904223u;return random_state;}
static void parity(void) {
 static const float depths[]={-32.0f,-0.0f,0.0f,.5f,1.0f,511.0f,1023.0f,2048.0f};
 static const uint32_t phases[]={0,0x7fffffffu,0x80000000u,0xffffffffu};
 uint32_t cases=0;
 for(uint32_t word=0;word<65536;word++)for(uint32_t d=0;d<NELEM(depths);d++) {
   cz_hw_pd_t pd;cz_native_pd(&pd,word,depths[d]);
   for(uint32_t toggle=0;toggle<2;toggle++)for(uint32_t p=0;p<5;p++) {
      uint32_t ph=p<4?phases[p]:next_u();
      float a=wave_reference(&pd,ph,toggle),b=wave_probe(&pd,ph,toggle);
      assert(!memcmp(&a,&b,sizeof a));
      float c=pd.window==4u?cz_native_wave_window4(&pd,ph,toggle):wave_reference(&pd,ph,toggle);
      assert(!memcmp(&a,&c,sizeof a));cases++;
   }
 }
 printf("CZ window probe: %u word/depth/toggle/phase combinations exact\n",cases);
}
static __attribute__((noinline)) void block_reference(const cz_hw_pd_t *pd,uint32_t ph,uint32_t mode,float *out) {
 for(uint32_t i=0;i<32;i++) {
   ph+=0x9573519u;uint32_t pos=mode==0?ph&0x7fffffffu:mode==1?ph|0x80000000u:ph;
   out[i]=wave_reference(pd,pos,i&1u);
 }
}
static __attribute__((noinline)) void block_split(const cz_hw_pd_t *pd,uint32_t ph,uint32_t mode,float *out) {
 float (*volatile run)(const cz_hw_pd_t *,uint32_t,uint32_t)=pd->window==4u?cz_native_wave_window4:wave_reference;
 for(uint32_t i=0;i<32;i++) {
   ph+=0x9573519u;uint32_t pos=mode==0?ph&0x7fffffffu:mode==1?ph|0x80000000u:ph;
   out[i]=run(pd,pos,i&1u);
 }
}
static volatile float checksum;
static void benchmark(void) {
 puts("wave,window,phase,reference_instructions_per_sample,split_instructions_per_sample");
 for(uint32_t wave=0;wave<8;wave++)for(uint32_t window=0;window<8;window++)for(uint32_t mode=0;mode<3;mode++) {
    cz_hw_pd_t pd;cz_native_pd(&pd,wave<<13|window<<6,700.5f);double cost[2];
    for(uint32_t fast=0;fast<2;fast++) {
      void (*volatile run)(const cz_hw_pd_t *,uint32_t,uint32_t,float *)=fast?block_split:block_reference;
      uint64_t start=instructions();float out[32];
      for(uint32_t i=0;i<4000;i++) {
        run(&pd,i*0x1265849u,mode,out);checksum+=out[i%32];
      }
      cost[fast]=(double)(instructions()-start)/128000;
    }
    printf("%u,%u,%u,%.2f,%.2f\n",wave,window,mode,cost[0],cost[1]);
 }
}

static track_t *setup(void) {
 memset(trk,0,sizeof trk);eng_state_reset();host_tracks_init();cz_init();
 track_t *t=&trk[0];host_preset(t,ENGI_CZ,0);trk_note_on(t,60,100);cz_native_block(t);return t;
}
static void renderer_parity(void) {
 track_t *t=setup();voice_t *v=&t->v[0];cz_voice_t *c=cz_voice(t,v);vmod_t m={0};
 uint8_t *b=cz_patch[0].raw;int32_t a[CTL],z[CTL];
 for(uint32_t i=0;i<20000;i++) {
   b[0]=(uint8_t)((i%4)|((i%3)<<2));
   for(uint32_t l=0;l<2;l++) {
     uint32_t off=l*57,word=next_u();
     b[14+off]=(uint8_t)(word>>8);b[15+off]=(uint8_t)word;
     b[16+off]=(uint8_t)(next_u()>>24);b[18+off]=(uint8_t)(next_u()>>24);
     b[20+off]=(uint8_t)(next_u()>>24);b[37+off]=(uint8_t)(next_u()>>24);b[54+off]=(uint8_t)(next_u()>>24);
   }
   b[4]=(uint8_t)(next_u()>>24);b[12]=(uint8_t)(next_u()>>24);
   b[1]=(uint8_t)(i&1u);b[2]=(uint8_t)(i%49);b[3]=(uint8_t)(i%128);
   v->note=(uint8_t)(i%128);v->vel=(uint8_t)(1+i%127);v->gate=i%19<14;
   if(i%71==0)cz_native_note_on(t,v);
   if(i%11==0) {v->ph[0]=0x7fffffffu;v->ph[1]=UINT32_MAX;}
   tuning_a4=400+i%101;cz_native_block(t);
   m.pitch16=(int32_t)(next_u()%2048);m.fine=(int32_t)(next_u()%201)-100;
   m.shape=(int32_t)(next_u()%(128u<<8));m.cutoff=(int32_t)(next_u()%(128u<<8))-(64<<8);
   m.amp0=(int32_t)(next_u()%32769);m.amp1=(int32_t)(next_u()%32769);
   uint32_t n=i%33;
   for(uint32_t k=0;k<CTL;k++)a[k]=z[k]=(int32_t)((i+k)%127)-63;
   voice_t old=*v;cz_voice_t old_c=*c;
   cz_render_reference(t,v,a,n,&m);voice_t want=*v;cz_voice_t want_c=*c;
   *v=old;*c=old_c;cz_native_render(t,v,z,n,&m);
   assert(!memcmp(a,z,sizeof a));assert(!memcmp(&want,v,sizeof want));assert(!memcmp(&want_c,c,sizeof want_c));
 }
 tuning_a4=440;
 puts("CZ full renderer: 20000 variable blocks, both lines, ring/noise, gates/retriggers, pitch/A4, edits and DC state exact");
}
static void renderer_benchmark(void) {
 puts("renderer_pattern,reference_instructions_per_sample,candidate_instructions_per_sample");
 for(uint32_t mode=0;mode<7;mode++) {
   double cost[2];
   for(uint32_t fast=0;fast<2;fast++) {
     track_t *t=setup();voice_t *v=&t->v[0];uint8_t *b=cz_patch[0].raw;
     b[0]=(uint8_t)(mode<2?0:3);
     uint32_t window=mode<4?(mode&1?4:0):4;
     uint32_t word=((mode==6?3:0)<<13)|(window<<6);
     b[14]=b[71]=(uint8_t)(word>>8);b[15]=b[72]=(uint8_t)word;
     if(mode==4)b[15]=(uint8_t)((b[15]&~0x38u)|3u<<3);
     if(mode==5)b[15]=(uint8_t)((b[15]&~0x38u)|4u<<3);
     cz_native_block(t);
     vmod_t m={0};m.pitch16=60*16;m.shape=64<<8;m.amp0=m.amp1=32768;
     void (*volatile run)(track_t *,voice_t *,int32_t *,uint32_t,const vmod_t *)=fast?cz_native_render:cz_render_reference;
     int32_t out[CTL];uint64_t start=instructions();
     for(uint32_t i=0;i<20000;i++) {memset(out,0,sizeof out);run(t,v,out,CTL,&m);checksum+=(float)out[i%CTL];}
     cost[fast]=(double)(instructions()-start)/(20000*CTL);
   }
   printf("%u,%.2f,%.2f\n",mode,cost[0],cost[1]);
   if(cost[0])assert(cost[1]<cost[0]*(mode==0||mode==2||mode==6?1.04:.9));
 }
}

int main(int argc,char **argv){parity();renderer_parity();if(argc==1){benchmark();renderer_benchmark();}return 0;}
