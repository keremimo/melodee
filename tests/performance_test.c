/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Optimized mixer transitions: capture off/on, live parameter changes and
 * silent clocks. Audio equivalence is separately enforced by golden renders. */
#define MELODEE_USB_AUDIO 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static int32_t out[CTL*2u];
static void poison_capture(void){for(uint32_t i=0;i<CTL*NPART;i++)track_capture[i]=123456;}
static void assert_capture_poison(void){for(uint32_t i=0;i<CTL*NPART;i++)assert(track_capture[i]==123456);}
static void setup(void)
{
    memset(trk,0,sizeof trk);trk_voices_bind();memset(&song,0,sizeof song);host_tracks_init();
    for(uint32_t k=0;k<NPART;k++){host_preset(&trk[k],HOST_SYNTH,0);trk[k].p[P_RESERVED34]=trk[k].p[P_RESERVED36]=trk[k].p[P_RESERVED33]=0;}
    usb.up=usb.config=1;usb.suspended=0;ua_reset();ua_cap_channels=4;
    assert(ua_tracks_memory_prepare(1));
}
static void capture_transitions(void)
{
    setup();trk_note_on(&trk[0],60,100);poison_capture();mix_block_q15(out,CTL);
    assert(!track_capture_on);assert_capture_poison();
    ua.cap_alt=1;usb.config=0;mix_block_q15(out,CTL);assert(!track_capture_on);assert_capture_poison();
    usb.config=1;mix_block_q15(out,CTL);assert(track_capture_on);
    int sounding=0;
    for(uint32_t i=0;i<CTL;i++){
        sounding|=track_capture[i*NPART];
        for(uint32_t k=1;k<NPART;k++)assert(!track_capture[i*NPART+k]);
    }
    ua.cap_alt=2;assert(sounding);ua_audio(out,track_capture,CTL,4096);assert(ua.cw==CTL);   /* (PCM24: the stems as they are) */
    for(uint32_t i=0;i<CTL*NPART;i++){
        int32_t x=ua_clip24(track_capture[i]);
        assert((i%4u<2u?ua.cap[i/4u*2u+i%4u]:ua_track_extra[i/4u*2u+i%4u-2u])==(int16_t)(x>>8));
        assert((i%4u<2u?ua.cap_lo[i/4u*2u+i%4u]:ua_track_extra_lo[i/4u*2u+i%4u-2u])==(uint8_t)x);
    }
    ua.cap_alt=0;poison_capture();mix_block_q15(out,CTL);assert_capture_poison();
    for(uint32_t k=0;k<NPART;k++)for(uint32_t v=0;v<NVOICE;v++)trk[k].v[v].active=0;
    ua.cap_alt=1;mix_block_q15(out,CTL);
    for(uint32_t i=0;i<CTL*NPART;i++)assert(!track_capture[i]);
    ua_audio(out,track_capture,CTL,4096);assert(ua.cw==2u*CTL);
    for(uint32_t i=CTL*NPART;i<2u*CTL*NPART;i++)assert(!(i%4u<2u?ua.cap[i/4u*2u+i%4u]:ua_track_extra[i/4u*2u+i%4u-2u]) &&
        !(i%4u<2u?ua.cap_lo[i/4u*2u+i%4u]:ua_track_extra_lo[i/4u*2u+i%4u-2u]));
    usb.suspended=1;poison_capture();mix_block_q15(out,CTL);assert(!track_capture_on);assert_capture_poison();
    uint32_t written=ua.cw;ua_audio(out,0,CTL,4096);assert(ua.cw==written);
    puts("performance: capture off/configuration/suspend/resume produces no stale stems");
}
static void cache_transitions(void)
{
    setup();track_t *t=&trk[0];trk_note_on(t,60,100);
    for(int32_t k=-64;k<=64;k+=16){
        t->p[P_LEVEL]=(int16_t)(k+64);t->p[P_PAN]=(int16_t)k;
        t->p[P_RESERVED34]=(int16_t)(k+64);t->p[P_RESERVED36]=(int16_t)(64-k);song.g[G_TUNE]=(int16_t)k;
        mix_block_q15(out,CTL);
        assert(mix_cache[0].lvl==LEVEL_Q12[t->p[P_LEVEL]&127]);
        assert(mix_cache[0].gl==4096-(k>0?k*64:0) && mix_cache[0].gr==4096+(k<0?k*64:0));
        int32_t pitch=k>=0?k*16/100:-((-k*16+99)/100);
        assert(tune_cache.pitch==pitch && tune_cache.fine==(k*16-pitch*100)*2367/16000);
    }
    t->p[P_PAN]=0;t->p[P_M1SRC]=MS_MODW;t->p[P_M1DST]=MD_PAN;t->p[P_M1AMT]=63;t->mw=127;
    mix_block_q15(out,CTL);assert(mix_cache[0].pan>0 && t->p[P_PAN]==0);
    t->mw=0;mix_block_q15(out,CTL);assert(mix_cache[0].pan==0 && t->p[P_PAN]==0);
    puts("performance: derived caches follow edits and effective matrix parameters");
}
static void silent_clocks(void)
{
    setup();track_t *t=&trk[0];int32_t b[CTL];
    for(uint32_t i=0;i<CTL;i++)b[i]=123456;
    uint32_t phase=t->lfo_ph;midi_bend_q8[0]=0;midi_bend_target[0]=12;
    assert(!track_render_audio(t,b,CTL,0));
    assert(t->lfo_ph==phase+LFO_INC[t->p[P_LRATE]&127] && midi_bend_q8[0]==3);
    for(uint32_t i=0;i<CTL;i++)assert(b[i]==123456);
    t->xf_on=1;t->xf=3;memcpy(t->pe_old,t->p+P_E0,sizeof t->pe_old);
    track_render_audio(t,b,CTL,0);assert(t->xf==2);t->xf_on=0;
    track_render(t,b,CTL);for(uint32_t i=0;i<CTL;i++)assert(!b[i]);
    t->p[P_RESERVED33]=100;
    for(uint32_t i=0;i<CTL;i++)part_buf[i]=123456;
    mix_part(t,CTL);for(uint32_t i=0;i<CTL;i++)assert(part_buf[i]==123456);
    host_preset(t,ENGI_FM6,0);fm6_part_t *p=fm6_part(0);p->dc_x1=123;p->dc_y1=456;
    uint32_t tick=p->pt.tick;track_render_audio(t,b,CTL,0);
    assert(p->pt.tick!=tick && !p->dc_x1 && !p->dc_y1);
    host_preset(t,7,0);t->p[P_E7]=2;drw_part_t *wheel=drw_of(t);
    phase=wheel->t.rph[0];track_render_audio(t,b,CTL,0);assert(wheel->t.rph[0]!=phase);
    puts("performance: silent tracks retain LFO/bend/rotor/FM6 clocks, fades and DIST zeros");
}
int main(void){capture_transitions();cache_transitions();silent_clocks();puts("Performance transition tests passed");return 0;}
