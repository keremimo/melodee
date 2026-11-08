/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Prototype DSP and exact native wire round trips. Pass real .syx libraries
 * as arguments; downloaded factory data stays outside the source repository. */
#define MELODEE_PROPHET_PROTOTYPE 1
#define main hostsim_main
#include "hostsim.c"
#undef main
static int failures;
static void check(const char *what, int ok)
{ printf("prophet: %s: %s\n",what,ok?"ok":"FAIL"); failures+=!ok; }
static void setup(void)
{
    memset(trk,0,sizeof trk);trk_voices_bind(); eng_state_reset();
    p5_parts_init();
    host_tracks_init(); host_preset(&trk[0],0,0);   /* (MELODEE_PROPHET_PROTOTYPE: engine 0 is ENG_P5_TEST) */
    trk[0].p[P_VOICE]=V_POLY;
    trk[0].p[P_RESERVED34]=trk[0].p[P_RESERVED35]=trk[0].p[P_RESERVED36]=0;
}
static uint32_t active(track_t *t)
{
    uint32_t n=0;for(uint32_t i=0;i<NVOICE;i++)n+=t->v[i].active&&t->v[i].stage!=4u;
    return n;
}
static int32_t tick(void)
{
    int32_t out[CTL], peak=0;track_render(&trk[0],out,CTL);
    for(uint32_t i=0;i<CTL;i++){int32_t a=out[i]<0?-out[i]:out[i];if(a>peak)peak=a;}
    return peak;
}
static void native_tests(void)
{
    p5_patch_t p,q,unchanged;
    uint8_t f[P5_FRAME_MAX],g[P5_FRAME_MAX];
    p5_patch_init(&p);
    int32_t seed=17385;
    int roundtrip=1;
    for(uint32_t len=128;len<=133;len+=5)for(uint32_t cmd=2;cmd<=3;cmd++) {
        p.size=(uint8_t)len;p.command=(uint8_t)cmd;p.group=9;p.program=39;
        for(uint32_t j=0;j<100;j++){
            for(uint32_t k=0;k<len;k++)p.raw[k]=(uint8_t)noise32(&seed);
            uint32_t n=p5_patch_encode(&p,f,sizeof f);
            roundtrip &= n && p5_patch_decode(&q,f,n) && q.size==p.size &&
                         !memcmp(q.raw,p.raw,len) && p5_patch_encode(&q,g,sizeof g)==n && !memcmp(f,g,n);
        }
    }
    check("random single/edit-buffer frames preserve every raw byte (both sizes)",roundtrip);
    uint32_t n=p5_patch_encode(&p,f,sizeof f);
    unchanged=q;
    int rejected=1;
    for(uint32_t k=0;k<n;k++) { q=unchanged; rejected &= !p5_patch_decode(&q,f,k) && !memcmp(&q,&unchanged,sizeof q); }
    uint8_t old=f[4];f[4]=0x80;rejected &= !p5_patch_decode(&q,f,n);f[4]=old;
    f[2]=0x33;rejected &= !p5_patch_decode(&q,f,n);f[2]=0x32;
    f[3]=5;rejected &= !p5_patch_decode(&q,f,n);f[3]=3;
    check("truncation, status bytes and unrelated commands rejected atomically",rejected);
    p.size=128;n=p5_patch_encode(&p,f,sizeof f);f[n-4u]|=4u;
    check("unused bits in the final partial group rejected",!p5_patch_decode(&q,f,n));
    p.command=2;p.group=10;
    check("invalid program address rejected",!p5_patch_encode(&p,f,sizeof f));
}
static void factory_file(const char *path)
{
    FILE *fp=fopen(path,"rb");if(!fp){perror(path);failures++;return;}
    fseek(fp,0,SEEK_END);long size=ftell(fp);rewind(fp);
    if(size<=0 || size>16*1024*1024){fclose(fp);failures++;return;}
    uint8_t *b=malloc((size_t)size);if(!b){fclose(fp);failures++;return;}
    int ok=fread(b,1,(size_t)size,fp)==(size_t)size;fclose(fp);
    uint32_t pos=0,count=0,rendered=0,audible=0;int safe=1;uint8_t encoded[P5_FRAME_MAX];p5_patch_t p;
    while(ok && pos<(uint32_t)size){
        uint32_t end=pos;while(end<(uint32_t)size&&b[end]!=0xF7)end++;
        uint32_t n=end-pos+1u;
        ok=end<(uint32_t)size&&p5_patch_decode(&p,b+pos,n)&&p5_patch_encode(&p,encoded,sizeof encoded)==n&&
           !memcmp(b+pos,encoded,n);
        if(ok){
            setup();memcpy(patch_of(0),p.raw,sizeof p.raw);patch_note(0,MOD3_PATCH_EDIT,0xFFFFu);p5_track_accept(&trk[0]);
            for(uint32_t j=0;j<3;j++)trk_note_on(&trk[0],48+j*7,100);
            int32_t peak=0;for(uint32_t j=0;j<FS/CTL;j++){int32_t x=tick();if(x>peak)peak=x;safe &= x<5*VOICE_FS*3;}
            for(uint32_t j=0;j<3;j++)trk_note_off(&trk[0],48+j*7);for(uint32_t j=0;j<200;j++)tick();
            rendered++;audible+=peak>16u;if(peak<=16){char name[21];p5_patch_name(name,&p);printf("prophet: slow/quiet factory program %u: %s (peak %d in first second)\n",count+1,name,peak); }
        }
        pos=end+1u;count++;
    }
    printf("prophet: real bank %s: %u exact frames\n",path,count);
    check("real bank-file byte-for-byte round trip",ok&&count==200u);
    printf("prophet: native factory playback: %u rendered, %u audible\n",rendered,audible);
    check("every factory program reaches the native renderer and stays bounded",safe&&rendered==count&&audible>count*9/10);
    free(b);
}
static void voice_tests(void)
{
    setup();track_t *t=&trk[0];
    for(uint32_t k=0;k<8;k++)trk_note_on(t,60+k,100);
    check("eight notes cap at five sounding Prophet voices",active(t)==5u);
    check("five Prophet voices use fifteen of sixteen shared units",voices_busy()==15u);
    for(uint32_t part=1;part<4;part++){
        host_preset(&trk[part],3,0);trk[part].p[P_VOICE]=V_POLY;
        trk_note_on(&trk[part],48+part,100);
    }
    check("shared allocation keeps three Prophet and all three other tracks",voices_busy()==15u&&active(t)==3u&&active(&trk[1])==1u&&active(&trk[2])==1u&&active(&trk[3])==1u);
    setup();t=&trk[0];p5_patch_t *p=p5_patch_of(t);
    p->raw[P5_RELEASE_AMP]=90;t->p[P_REL]=0;
    trk_note_on(t,60,100);for(uint32_t k=0;k<32;k++)tick();trk_note_off(t,60);
    tick();tick();check("common REL=0 does not terminate native release",active(t)==1u);
    for(uint32_t k=0;k<FS*8/CTL;k++)tick();
    check("native amp envelope frees the released voice",active(t)==0u);
    for(uint32_t mode=0;mode<2;mode++){
        setup();p=p5_patch_of(&trk[0]);p->raw[P5_LEVEL_A]=p->raw[P5_LEVEL_B]=p->raw[P5_NOISE]=0;
        p->raw[P5_RESONANCE]=120;p->raw[P5_CUTOFF]=60;p->raw[P5_FILTER_REV]=(uint8_t)mode;
        trk_note_on(&trk[0],60,100);int32_t peak=0;for(uint32_t k=0;k<FS/CTL;k++){int32_t x=tick();if(x>peak)peak=x;}
        check("native self-oscillation starts with no oscillator or mixer noise",peak>100);
    }
    setup();p=p5_patch_of(&trk[0]);p->raw[P5_VEL_AMP]=0;trk_note_on(&trk[0],60,30);
    int32_t lo=0;for(uint32_t k=0;k<100;k++){int32_t a=tick();if(a>lo)lo=a;}
    setup();trk_note_on(&trk[0],60,127);
    int32_t hi=0;for(uint32_t k=0;k<100;k++){int32_t a=tick();if(a>hi)hi=a;}
    check("native velocity disabled bypasses common velocity gain",lo==hi&&lo>100);
}
/* Measure the real renderer's phase advance and filter coefficient, rather
 * than duplicating the raw-to-DSP conversion in the expected values. */
static void mapping_probe(uint8_t coarse, int key_b, int note, int key_filter,
                          uint32_t step[2], double *filter_hz)
{
    setup();track_t *t=&trk[0];p5_patch_t *patch=p5_patch_of(t);
    uint8_t *p=patch->raw;
    p[P5_FREQ_A]=p[P5_FREQ_B]=coarse;p[P5_KEY_B]=(uint8_t)key_b;
    p[P5_CUTOFF]=60;p[P5_KEY_FILTER]=(uint8_t)key_filter;p[P5_ENV_FILTER]=0;
    p[P5_LFO_INITIAL]=p[P5_PRESS_FILTER]=0;
    p5_patch_t original=*patch;
    trk_note_on(t,(uint32_t)note,100);p5_block(t);
    voice_t *v=0;for(uint32_t j=0;j<NVOICE;j++)if(t->v[j].gate){v=&t->v[j];break;}
    if(!v){failures++;return;}
    p5_voice_t *s=p5_voice(t,v);uint32_t before[2]={s->phase[0],s->phase[1]};
    vmod_t m={0};m.pitch16=note*16;m.shape=64<<8;
    int32_t out[1]={0};p5_render(t,v,out,1,&m);
    for(uint32_t j=0;j<2;j++)step[j]=s->phase[j]-before[j];
    double g=s->coeff_g[0];
    *filter_hz=atan(g/(1-g))*(FS*P5_OVERSAMPLE)/3.141592653589793;
    if(memcmp(patch,&original,sizeof original))failures++;
}
static void mapping_tests(void)
{
    int detents=1,anchors=1;double fc;
    uint32_t a[2],b[2];
    for(uint32_t raw=0;raw<96;raw+=2){
        mapping_probe((uint8_t)raw,1,69,0,a,&fc);
        mapping_probe((uint8_t)(raw+1),1,69,0,b,&fc);
        detents &= a[0]==b[0]&&a[1]==b[1];
    }
    for(uint32_t octave=0;octave<5;octave++){
        mapping_probe((uint8_t)(octave*24),1,57,0,a,&fc);
        uint32_t want=pitch_inc((45+octave*12)*16);
        anchors &= a[0]==want&&a[1]==want;
    }
    mapping_probe(25,1,69,0,a,&fc);
    check("factory raw 25 renders oscillator A/B at A4=440, without a half-semitone offset",a[0]==pitch_inc(69*16)&&a[1]==a[0]);
    check("all keyed coarse-tuning raw pairs select identical semitone detents",detents);
    check("keyed coarse-tuning octave anchors span exactly four octaves",anchors);
    mapping_probe(96,1,57,0,a,&fc);int limit=1;
    for(uint32_t raw=97;raw<=127;raw++){
        mapping_probe((uint8_t)raw,1,57,0,b,&fc);limit &= a[0]==b[0]&&a[1]==b[1];
    }
    check("keyed coarse tuning saturates at the top octave without rewriting native bytes",limit);
    mapping_probe(24,0,57,0,a,&fc);mapping_probe(25,0,57,0,b,&fc);
    int continuous=a[0]==b[0]&&a[1]!=b[1];
    mapping_probe(127,0,57,0,a,&fc);mapping_probe(127,0,69,0,b,&fc);
    check("keyboard-off B remains continuous and independent of the played note",continuous&&a[1]==b[1]&&a[1]==pitch_inc(120*16));
    int tracking=1;
    for(int mode=0;mode<=2;mode++){
        double lo,hi;mapping_probe(24,1,60,mode,a,&lo);mapping_probe(24,1,72,mode,b,&hi);
        double ratio=hi/lo,want=mode==2?2:mode==1?sqrt(2):1;
        printf("prophet: filter tracking %d: octave cutoff ratio %.5f (expected %.5f)\n",mode,ratio,want);
        tracking &= fabs(ratio/want-1)<.01;
    }
    check("rendered filter coefficient follows off/half/full keyboard octave ratios",tracking);
}
static uint32_t wheel_probe(uint8_t initial,uint32_t phase,uint32_t osc)
{
    setup();track_t *t=&trk[0];uint8_t *p=p5_patch_of(t)->raw;
    p[P5_LFO_INITIAL]=initial;p[P5_LFO_TRI]=1;p[P5_LFO_SAW]=p[P5_LFO_PULSE]=0;
    p[P5_WHEEL_FREQ_A]=p[P5_WHEEL_FREQ_B]=1;p[P5_WHEEL_MIX]=0;
    trk_note_on(t,69,100);p5_part(0)->lfo=phase;p5_block(t);
    voice_t *v=0;for(uint32_t j=0;j<NVOICE;j++)if(t->v[j].gate){v=&t->v[j];break;}
    if(!v){failures++;return 0;}
    p5_voice_t *s=p5_voice(t,v);uint32_t before=s->phase[osc];
    vmod_t m={0};m.pitch16=69*16;m.shape=64<<8;
    int32_t out[1]={0};p5_render(t,v,out,1,&m);
    return s->phase[osc]-before;
}
static void wheel_pitch_tests(void)
{
    int centered=1,neutral=1,positive=1,measured=1;uint32_t base=pitch_inc(69*16);
    /* Rev-4 factory demo: initial 18 / 30 / 42 / 55 vibrato peaks at about
     * 7.5 / 20 / 36 / 50 cents (0.0519 * raw^1.737, within 11 %). */
    static const struct { uint8_t amount; double cents; } rev4[]={{18,7.5},{30,20},{42,36},{55,50}};
    for(uint32_t osc=0;osc<2;osc++)for(uint32_t amount=0;amount<=127;amount++){
        uint32_t down=wheel_probe((uint8_t)amount,0,osc);
        uint32_t up=wheel_probe((uint8_t)amount,0x80000000u,osc);
        double flat=1200*log2((double)down/base),sharp=1200*log2((double)up/base);
        centered &= fabs(flat+sharp)<.2;
        positive &= down>=base/2&&up>=base&&up<base*2;
        if(!amount)neutral &= down==base&&up==base;
        for(uint32_t k=0;k<NELEM(rev4);k++)if(rev4[k].amount==amount)measured &= fabs(sharp/rev4[k].cents-1)<.15;
        if(amount==18&&osc==0)printf("prophet: first-preset triangle pitch: %.2f / +%.2f cents\n",flat,sharp);
    }
    check("triangle wheel pitch goes equally sharp/flat in cents for both oscillators",centered);
    check("zero initial wheel depth leaves oscillator pitch exact",neutral);
    check("maximum wheel pitch depth remains positive and does not stall an oscillator",positive);
    check("triangle vibrato depth follows the measured Rev-4 amount curve",measured);
    /* LFO rate: raw 74/80/89/94/97 measured at 2.40/3.37/5.48/7.10/8.39 Hz;
     * the documented ends are .022 and 500 Hz. */
    static const struct { uint8_t raw; double hz; } rate[]={{0,.022},{74,2.40},{80,3.37},{89,5.48},{94,7.10},{97,8.39},{127,500}};
    int rates=1;
    for(uint32_t k=0;k<NELEM(rate);k++){
        double hz=(double)P5_LFO_INC[rate[k].raw]*FS/4294967296.0;
        rates &= fabs(1200*log2(hz/rate[k].hz))<60;
    }
    check("LFO rate follows the measured Rev-4 curve and documented range",rates);
}
static void stolen_tail_test(void)
{
    setup();track_t *t=&trk[0];trk_note_on(t,69,100);
    for(uint32_t k=0;k<100;k++)tick();
    voice_t *v=0;for(uint32_t k=0;k<NVOICE;k++)if(t->v[k].gate){v=&t->v[k];break;}
    if(!v){failures++;return;}
    p5_voice_t *s=p5_voice(t,v);s->last_pcm=24000;
    uint32_t phase[2]={s->phase[0],s->phase[1]};
    voice_kill(v);env_tick(t,v);
    vmod_t m={0};m.amp0=32767;m.amp1=0;
    int32_t out[CTL]={0};p5_render(t,v,out,CTL,&m);
    int smooth=out[0]==24000&&out[CTL-1]==0;
    for(uint32_t k=1;k<CTL;k++)smooth &= out[k]>=0&&out[k]<=out[k-1]&&out[k-1]-out[k]<=800;
    check("stolen Prophet voice fades continuously to zero without advancing its expensive synth",smooth&&s->phase[0]==phase[0]&&s->phase[1]==phase[1]&&s->last_pcm==0);
}
static void filter_tests(void)
{
    int bounded=1, distinct=0;
    int32_t seed=42;
    for(uint32_t cut=0;cut<=127;cut+=7)for(uint32_t res=0;res<=127;res+=7){
        p5_voice_t a={0},b={0};
        for(uint32_t k=0;k<2048;k++){
            float x=((int32_t)(noise32(&seed)>>16)-32768)/32768.0f;
            float ya=p5_filter(&a,x,(float)cut,(float)res,1);
            float yb=p5_filter(&b,x,(float)cut,(float)res,0);
            bounded &= isfinite(ya)&&isfinite(yb)&&fabsf(ya)<=1.0f&&fabsf(yb)<=1.0f;
            distinct |= ya!=yb;
            for(uint32_t j=0;j<4;j++)bounded &= isfinite(a.z[j])&&isfinite(b.z[j])&&fabsf(a.z[j])<=60000.0f/32768.0f&&fabsf(b.z[j])<=60000.0f/32768.0f;
        }
    }
    check("both four-pole modes stable across cutoff/resonance sweep",bounded);
    check("Curtis and SSI character models produce distinct output",distinct);
    for(uint32_t mode=0;mode<=1;mode++){
        p5_voice_t s={0};double power=0;
        for(uint32_t k=0;k<FS*2;k++){
            double y=32768.0*p5_filter(&s,k==0?10000.0f/32768.0f:0.0f,82.0f,127.0f,(int)mode);
            if(k>FS)power+=(double)y*y;
        }
        printf("prophet: %s self-oscillation RMS %.1f\n",mode?"Curtis":"SSI",sqrt(power/FS));
        check("resonance can sustain self-oscillation without oscillators",power/FS>10000);
    }
    setup();p5_patch_t *p=p5_patch_of(&trk[0]);
    p->raw[P5_RESONANCE]=127;p->raw[P5_SYNC]=1;p->raw[P5_POLY_B]=127;
    p->raw[P5_POLY_FREQ]=p->raw[P5_POLY_PW]=p->raw[P5_POLY_FILTER]=1;
    p->raw[P5_SAW_B]=p->raw[P5_PULSE_A]=p->raw[P5_PULSE_B]=p->raw[P5_TRI_B]=1;
    p->raw[P5_LEVEL_B]=127;
    for(uint32_t k=0;k<5;k++)trk_note_on(&trk[0],84+k,127);
    int32_t peak=0;
    uint64_t start=now_ns();
    for(uint32_t k=0;k<FS/CTL;k++){int32_t a=tick();if(a>peak)peak=a;}
    printf("prophet: five-voice host stress %.1f ms per rendered second, peak %d\n",(now_ns()-start)/1e6,peak);
    check("sync/audio-rate Poly-Mod at high pitch remains bounded",peak>100&&peak<5*VOICE_FS*3);
    /* Include the 127 endpoints and ultrasonic Poly-Mod rates in UBSan runs. */
    int extremes=1;
    for(uint32_t mode=0;mode<2;mode++)for(uint32_t cut=0;cut<=127;cut+=127)
    for(uint32_t res=0;res<=127;res+=127){
        setup();p=p5_patch_of(&trk[0]);
        p->raw[P5_FILTER_REV]=(uint8_t)mode;p->raw[P5_CUTOFF]=(uint8_t)cut;
        p->raw[P5_RESONANCE]=(uint8_t)res;p->raw[P5_FREQ_A]=p->raw[P5_FREQ_B]=127;
        p->raw[P5_SAW_B]=p->raw[P5_PULSE_A]=p->raw[P5_PULSE_B]=p->raw[P5_TRI_B]=1;
        p->raw[P5_SYNC]=p->raw[P5_POLY_FREQ]=p->raw[P5_POLY_PW]=p->raw[P5_POLY_FILTER]=1;
        p->raw[P5_POLY_B]=p->raw[P5_POLY_ENV]=127;
        for(uint32_t k=0;k<5;k++)trk_note_on(&trk[0],127-k,127);
        for(uint32_t k=0;k<256;k++)extremes &= tick()<5*VOICE_FS*3;
    }
    check("maximum pitch and cutoff/resonance endpoints remain bounded",extremes);

}
static void demos(void)
{
    const char *dir=getenv("P5_DEMOS");if(!dir)return;
    const char *names[]={"pad","brass","bass","sync","polymod"};
    for(uint32_t sound=0;sound<5;sound++)for(uint32_t mode=0;mode<2;mode++){
        setup();p5_patch_t *p=p5_patch_of(&trk[0]);uint8_t *r=p->raw;
        r[P5_FILTER_REV]=(uint8_t)mode;r[P5_SAW_B]=1;r[P5_LEVEL_B]=100;
        r[P5_CUTOFF]=65;r[P5_RESONANCE]=50;r[P5_ENV_FILTER]=70;r[P5_SUSTAIN_FILTER]=30;
        r[P5_ATTACK_AMP]=sound==0?65:0;r[P5_ATTACK_FILTER]=sound==1?42:0;
        r[P5_RELEASE_AMP]=sound==0?80:50;r[P5_FREQ_B]=25;
        if(sound==2){r[P5_CUTOFF]=45;r[P5_RESONANCE]=70;r[P5_SUSTAIN_AMP]=40;}
        if(sound>=3){r[P5_FREQ_A]=64;r[P5_SYNC]=sound==3;r[P5_POLY_B]=90;
                    r[P5_POLY_FREQ]=1;r[P5_POLY_ENV]=100;r[P5_POLY_FILTER]=sound==4;}
        uint32_t count=sound<2?5u:1u;
        for(uint32_t k=0;k<count;k++)trk_note_on(&trk[0],(sound==2?36:60)+k*3,100);
        char path[512];snprintf(path,sizeof path,"%s/%s-%s.wav",dir,names[sound],mode?"curtis":"ssi");
        FILE *f=fopen(path,"wb");if(!f){perror(path);failures++;continue;}wav_hdr(f,FS*3);
        for(uint32_t frame=0;frame<FS*3;frame+=CTL){
            if(frame>=FS&&frame<FS+CTL)for(uint32_t k=0;k<count;k++)trk_note_off(&trk[0],(sound==2?36:60)+k*3);
            int32_t out[CTL];track_render(&trk[0],out,CTL);
            for(uint32_t j=0;j<CTL&&frame+j<FS*3;j++)wav_put(f,out[j]/4,out[j]/4);
        }
        fclose(f);
    }
}
int main(int argc,char **argv)
{
    int blep_ok=1;
    for(uint32_t inc=1;inc<100000;inc++){
        uint32_t phase[]={0,inc/2,inc-1,0xFFFFFFFFu-inc+1,0xFFFFFFFFu};
        for(uint32_t j=0;j<NELEM(phase);j++){
            float correction=p5_fast_blep(phase[j],inc);
            blep_ok &= isfinite(correction)&&correction>=-1.0f&&correction<=1.0f;
        }
    }
    check("low-rate fp32 oscillator discontinuity correction stays bounded",blep_ok);
    int high_blep_ok=1;uint32_t rng=57;double max_blep_error=0;
    for(uint32_t k=0;k<10000;k++){
        rng=rng*1664525u+1013904223u;
        uint32_t inc=(1u<<27)+rng%(1932735280u-(1u<<27)),distance=rng%inc;
        double x=(double)distance/inc;
        double want=32768*(2*x-x*x-1);
        double error=fabs(p5_fast_blep(distance,inc)*32768.0-want);
        if(error>max_blep_error)max_blep_error=error;
        high_blep_ok &= error<=0.02;
    }
    printf("prophet: high-rate BLEP maximum error %.3f Q15 units\n",max_blep_error);
    check("fp32 high-rate oscillator correction stays within 0.02 Q15 units of the normalized curve",high_blep_ok);
    p5_part_t wheel_state={0};p5_patch_t wheel_patch;p5_patch_init(&wheel_patch);
    int ratio_ok=1;
    for(uint32_t wave=0;wave<2;wave++){
        wheel_patch.raw[P5_LFO_TRI]=!wave;wheel_patch.raw[P5_LFO_PULSE]=(uint8_t)wave;
        for(uint32_t ph=0;ph<=65535;ph++){
            wheel_state.lfo=ph<<16;
            p5_mod_samples(&wheel_state,wheel_patch.raw,0,1.0f,0.0f);
            double cents=wheel_state.wheel[0]*470.0;
            double expected=pow(2,cents/1200);
            ratio_ok &= fabs(wheel_state.wheel_pitch[0]/expected-1.0)<=1e-6;
        }
    }
    check("fp32 shared wheel pitch ratio stays within 1 ppm of 2^(cents/1200)",ratio_ok);
    int exp_ok=1;
    for(int32_t x=-8*4096;x<=6*4096;x+=7){
        uint32_t base=pitch_inc(60*16),y=p5_inc(base,(float)x/4096.0f);
        double want=base*pow(2,x/4096.0);if(want>1932735280.0)want=1932735280.0;
        exp_ok &= fabs(y/want-1)<2e-4;
    }
    check("exponential Poly-Mod frequency stays within 0.35 cent across its range",exp_ok);
    int sat_ok=1;float previous=-1.0f;
    for(int32_t x=0;x<=131072;x++){
        float input=(float)x/32768.0f,actual=p5_fast_softclip(input);
        double expected=tanh((double)input);
        sat_ok &= actual>=previous && fabs(actual-expected)<=6e-6 && p5_fast_softclip(-input)==-actual;
        previous=actual;
    }
    check("fp32 saturation stays odd, monotonic and within 6e-6 of tanh",sat_ok);
    int fraction_ok=1,wheel_ok=1;uint32_t seed=43;
    for(uint32_t k=0;k<10000;k++){
        seed=seed*1664525u+1013904223u;uint32_t inc=(seed>>1)+1u,distance=seed%inc;
        double exact=(double)distance/inc;
        fraction_ok &= fabs(p5_sync_fraction(distance,inc)-exact)<1.5e-7;
        uint32_t base=seed%1932735281u;float ratio=(16384u+seed%49152u)/32768.0f;
        exact=(double)base*ratio;if(exact>1932735232.0)exact=1932735232.0;
        wheel_ok &= fabs(p5_wheel_inc(base,ratio)-exact)<=256;
    }
    check("fp32 hard-sync fraction stays within 1.5e-7 of exact division",fraction_ok);
    check("fp32 wheel multiplication stays within 256 phase units across pitch/depth limits",wheel_ok);
    native_tests();for(int k=1;k<argc;k++)factory_file(argv[k]);
    mapping_tests();wheel_pitch_tests();stolen_tail_test();voice_tests();filter_tests();demos();
    printf("prophet: %d failure(s); patch %zu B, state %zu B per track\n",failures,sizeof(p5_patch_t),sizeof(p5_part_t));
    return failures?1:0;
}
