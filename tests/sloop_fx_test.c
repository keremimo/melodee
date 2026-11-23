/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include "fx_kernel_fixture.h"
#include <assert.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
static uint64_t instructions(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 r;if(!proc_pid_rusage(getpid(),RUSAGE_INFO_V4,(rusage_info_t *)&r))return r.ri_instructions;
#endif
    return 0;
}
static int32_t b[CTL];
static void reset(void){ui_power_on();punch_reset();fx_clear_all();for(uint32_t k=0;k<FX_STATE_COUNT;k++)resource_release(RES_FX0+k);memset(fx_state,0,sizeof fx_state);spread_on=0;}
static uint64_t impulse(uint32_t size,uint32_t damping,uint32_t late,uint32_t *different)
{
    reset();TSEL->p[P_FXTYPE]=FX_REVERB;TSEL->p[P_FXMIX]=127;TSEL->p[P_FXRATE]=(int16_t)size;TSEL->p[P_FXCHAR]=(int16_t)damping;
    uint64_t energy=0,last=0;uint32_t diff=0;
    for(uint32_t at=0;at<10*FS;at+=CTL){
        memset(b,0,sizeof b);if(!at)b[0]=24000;
        assert(fx_kernel_process(TSEL,b,CTL));
        for(uint32_t i=0;i<CTL;i++){
            assert(abs(fx_left[i])<=262143 && abs(fx_right[i])<=262143);
            if(at>=late && at<late+FS)energy+=abs(fx_left[i])+abs(fx_right[i]);
            if(at>9*FS)last+=abs(fx_left[i])+abs(fx_right[i]);
            diff+=fx_left[i]!=fx_right[i];
        }
    }
    assert(last<1000);if(different)*different=diff;return energy;
}
static void reverb(void)
{
    uint32_t stereo=0;uint64_t short_tail=impulse(0,0,FS,&stereo),long_tail=impulse(127,0,FS,0);
    printf("SLOOP Reverb: %zu bytes; tail short %llu long %llu, stereo samples %u\n",sizeof(fx_sloop_reverb_t),(unsigned long long)short_tail,(unsigned long long)long_tail,stereo);
    assert(sizeof(fx_sloop_reverb_t)<20000 && stereo>1000 && long_tail>short_tail+10000);
    uint64_t dark=impulse(127,127,0,0),bright=impulse(127,0,0,0);assert(dark<bright);
    reset();TSEL->p[P_FXTYPE]=FX_REVERB;TSEL->p[P_FXMIX]=127;TSEL->p[P_FXRATE]=127;TSEL->p[P_FXCHAR]=0;
    for(uint32_t j=0;j<3000;j++){
        for(uint32_t i=0;i<CTL;i++)b[i]=((i+j)&1u)?1048575:-1048575;
        assert(fx_kernel_process(TSEL,b,CTL));
        for(uint32_t i=0;i<CTL;i++)assert(abs(fx_left[i])<=262143 && abs(fx_right[i])<=262143);
    }
    TSEL->p[P_FXMIX]=0;for(uint32_t j=0;j<16;j++)fx_kernel_process(TSEL,b,CTL);assert(!resource[RES_FX0].size);
    puts("SLOOP Reverb: size/damping, stereo decay, full-rate hot input and buffer release passed");
}
static void inserts(void)
{
    for(uint32_t type=FX_SOFT;type<=FX_PEDAL_CHORUS;type++){
        uint64_t hashes[3]={0};
        for(uint32_t setting=0;setting<3;setting++){
            reset();TSEL->p[P_FXTYPE]=(int16_t)type;TSEL->p[P_FXMIX]=127;TSEL->p[P_FXRATE]=setting==1?127:60;TSEL->p[P_FXCHAR]=setting==2?127:64;
            TSEL->p[P_FXTONE]=setting==2?63:0;
            for(uint32_t frame=0;frame<2000;frame++){
                for(uint32_t i=0;i<CTL;i++)b[i]=sine_i((frame*CTL+i)*0x01734567u)/2;
                assert(fx_kernel_process(TSEL,b,CTL));
                for(uint32_t i=0;i<CTL;i++){
                    assert(abs(fx_left[i])<1000000 && fx_left[i]==fx_right[i]);
                    hashes[setting]=(hashes[setting]^(uint32_t)fx_left[i])*1099511628211ull;
                }
            }
        }
        assert(hashes[0]!=hashes[1] && hashes[0]!=hashes[2]);
        /* Transition from folded mono to true stereo without resetting the left tail. */
        spread_on=1;TSEL->p[P_SPRD]=100;
        for(uint32_t i=0;i<CTL;i++){b[i]=10000;spread_buf[i]=-7000;}
        assert(fx_kernel_process(TSEL,b,CTL));assert(((fx_sloop_insert_t *)resource[RES_FX0].ptr)->stereo);
        for(int32_t feedback=-63;feedback<=63;feedback+=63){
            TSEL->p[P_FXTONE]=(int16_t)feedback;
            for(uint32_t k=0;k<300;k++){
                for(uint32_t i=0;i<CTL;i++){b[i]=((i+k)&1u)?262143:-262143;spread_buf[i]=b[i]/3;}
                assert(fx_kernel_process(TSEL,b,CTL));
                for(uint32_t i=0;i<CTL;i++)assert(abs(fx_left[i])<1000000 && abs(fx_right[i])<1000000);
            }
        }
        spread_on=0;
        for(uint32_t j=0;j<12000;j++){memset(b,0,sizeof b);fx_kernel_process(TSEL,b,CTL);}
        assert(((fx_sloop_insert_t *)resource[RES_FX0].ptr)->asleep);
        for(uint32_t i=0;i<CTL;i++)b[i]=10000;fx_kernel_process(TSEL,b,CTL);
        assert(!((fx_sloop_insert_t *)resource[RES_FX0].ptr)->asleep);
        TSEL->p[P_FXMIX]=0;for(uint32_t j=0;j<16;j++)fx_kernel_process(TSEL,b,CTL);assert(!resource[RES_FX0].size);
        puts(N_FX[type]);
    }
    printf("SLOOP inserts: all eight algorithms, controls, stereo transitions, +/- feedback and bypass; state %zu bytes\n",sizeof(fx_sloop_insert_t));
}
static void returns(void)
{
    for(uint32_t type=FX_SOFT;type<=FX_PEDAL_CHORUS;type++)for(uint32_t separate=0;separate<2;separate++){
        reset();song.g[G_FXMODE]=(int16_t)separate;fx_bus_mix=0;fx_bus_select();
        song.g[G_MFXTYPE]=(int16_t)type;song.g[G_MFXMIX]=127;song.g[G_MFXRATE]=60;song.g[G_MFXCHAR]=90;song.g[G_FXRETURN]=127;
        for(uint32_t j=0;j<40;j++){for(uint32_t i=0;i<CTL;i++){fx_send_l[i]=10000;fx_send_r[i]=-3000;}fx_bus_start(CTL);fx_bus_join();}
        assert(fx_bus_result && resource[RES_FX0].size==fx_sloop_insert_bytes(type));
    }
    /* At zero chorus depth/feedback, its own dry half must not leak into a
     * parallel return before the delayed copy arrives. */
    reset();fx_state_t f={.type=FX_PEDAL_CHORUS,.rate=60,.character=0,.mono=1,.mix=32768,.echo=1};
    struct {fx_sloop_insert_t s;int16_t lines[512];} state={0};int32_t l[CTL],r[CTL];
    for(uint32_t i=0;i<CTL;i++)fx_dry_l[i]=fx_dry_r[i]=l[i]=r[i]=10000;
    fx_sloop_insert_run(&f,&state.s,CTL,32768,l,r,0,1);
    for(uint32_t i=0;i<CTL;i++)assert(!l[i] && !r[i]);
    reset();go_title("SEND FX 1");TSEL->p[P_FXTYPE]=song.g[G_MFXTYPE]=FX_REVERB;
    assert(str_eq(track_desc(TSEL,P_FXRATE)->label,"SIZE") && str_eq(track_desc(TSEL,P_FXCHAR)->label,"DAMP"));
    TSEL->p[P_FXTYPE]=song.g[G_MFXTYPE]=FX_PEDAL_PHASER;int16_t *vp;
    assert(str_eq(page_desc(cur_page(),3,&vp)->label,"RESO") && vp==&song.g[G_MFXTONE]);
    TSEL->p[P_FXTYPE]=song.g[G_MFXTYPE]=FX_HARD;                         /* a DRIVE's KNOB 4: its SHAPE, the slot's effect */
    assert(str_eq(page_desc(cur_page(),3,&vp)->label,"SHAPE") && vp==&song.g[G_MFXTYPE]);
    TSEL->p[P_FXTYPE]=FX_CRUSH;
    {char v[8];const char *u;param_format(track_desc(TSEL,P_FXRATE),60,v,&u);assert(str_eq(v,"9") && str_eq(u,"bit"));
     param_format(track_desc(TSEL,P_FXCHAR),64,v,&u);assert(str_eq(v,"3.7") && str_eq(u,"kHz"));}
    project_t q;project_store_t p;
    for(uint32_t type=FX_SOFT;type<=FX_PEDAL_CHORUS;type++){
        TSEL->p[P_FXTYPE]=(int16_t)type;TSEL->p[P_FXTONE]=-63;song.g[G_MFXTYPE]=(int16_t)type;song.g[G_MFXTONE]=63;
        project_capture(&q);assert(proj_pack(&p,&q) && proj_import_same(&proj_scratch,&p,sizeof p));
        assert(proj_scratch.t[0].p[P_FXTYPE]==FX_OFF && proj_scratch.t[0].p[P_FXTONE]==-63);
        assert(proj_scratch.g[G_MFXTYPE]==type && proj_scratch.g[G_MFXTONE]==63);
    }
    puts("SLOOP routing: CHAIN/SPLIT sends, wet-only modulation return, rack labels and persistence passed");
}
static void benchmark(void)
{
    const uint32_t types[]={FX_PHASER,FX_PEDAL_PHASER,FX_CHORUS,FX_PEDAL_CHORUS,FX_REVERB,FX_CRUSH,FX_FLANGER};
    for(uint32_t k=0;k<NELEM(types);k++){
        reset();TSEL->p[P_FXTYPE]=(int16_t)types[k];TSEL->p[P_FXMIX]=127;TSEL->p[P_FXRATE]=60;TSEL->p[P_FXCHAR]=64;
        for(uint32_t i=0;i<CTL;i++)b[i]=10000;fx_kernel_process(TSEL,b,CTL);
        uint64_t begin=instructions();
        for(uint32_t j=0;j<4096;j++){for(uint32_t i=0;i<CTL;i++)b[i]=sine_i((j*CTL+i)*0x01734567u)/2;fx_kernel_process(TSEL,b,CTL);}
        uint64_t used=instructions()-begin;
        printf("Host benchmark %-9s %.2f instructions/frame, %u bytes (not device CPU)\n",N_FX[types[k]],(double)used/(4096*CTL),resource[RES_FX0].size);
    }
}
int main(void){reverb();inserts();returns();benchmark();return 0;}
