/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Exercise actual master DSP and keyboard ownership, not generated MIDI notes. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>
static int32_t l[CTL],r[CTL];
static uint32_t sample;
static void reset(void)
{
    ui_power_on();punch_reset();punch_panel_lock=1;sample=0;
    song.g[G_BPM]=120;song.playing=0;
}
static void test_signal(int silence)
{
    for(uint32_t i=0;i<CTL;i++,sample++){
        l[i]=silence?0:(int32_t)(10000*sin(6.28318530718*347*sample/FS)+4000*sin(6.28318530718*3209*sample/FS));
        r[i]=silence?0:(int32_t)(8000*sin(6.28318530718*523*sample/FS));
    }
}
static void tap_fast(uint32_t b){btn_down(b);frames(20);btn_up(b);frames(20);}
static void controls(void)
{
    ui_power_on();frame();
    int16_t before[P_COUNT];memcpy(before,TSEL->p,sizeof before);
    key_down(2);frame();assert(gates());
    btn_down(B_FX); /* key press reaches the ISR before the next UI frame */
    key_down(0);keyboard_block();assert(punch.req==PX_LOOP4 && punch_claimed==1u);
    frame();assert(ui.layer==LAYER_FX && gates());
    key_down(1);frame();assert(punch.req==PX_KLOW);
    key_up(0);frame();assert(punch.req==PX_KLOW); /* newest key owns the effect */
    btn_up(B_FX);frame();assert(ui.layer==LAYER_FX && punch.req==PX_KLOW);
    key_up(1);frames(100);assert(!ui.layer && punch.req==-1 && !punch_claimed);
    assert(gates());key_up(2);frame();assert(!gates());
    assert(!memcmp(before,TSEL->p,sizeof before));
    ui_power_on();tap_fast(B_FX);tap_fast(B_FX);assert(ui.lock==LAYER_FX && ui.layer==LAYER_FX);
    key_down(8);frame();assert(punch.req==PX_RWD && !gates());
    tap_fast(B_FX);assert(!ui.lock); /* releasing the lock does not turn its held key into a note */
    key_up(8);frames(100);assert(!punch_claimed && punch.req==-1 && !gates());
    for(uint32_t k=0;k<27;k++)assert(punch_key_of_fx((uint32_t)punch_fx_of_key(k))==k);
    puts("Punch UI: all 27 SLOOP key mappings, ISR ownership, original held notes, double-tap and releases passed");
}
static void effects(void)
{
    for(int32_t fx=0;fx<(int32_t)PUNCH_NFX;fx++){
        reset();
        for(uint32_t b=0;b<1500;b++){test_signal(0);punch_process(l,r,CTL);}
        assert(resource[RES_PUNCH].size==32768);
        punch.req=(int8_t)fx;
        uint64_t changed=0,energy=0;
        for(uint32_t b=0;b<3000;b++){
            test_signal(0);int32_t dryl[CTL],dryr[CTL];memcpy(dryl,l,sizeof l);memcpy(dryr,r,sizeof r);
            punch_process(l,r,CTL);
            for(uint32_t i=0;i<CTL;i++){
                changed+=l[i]!=dryl[i] || r[i]!=dryr[i];energy+=abs(l[i])+abs(r[i]);
                assert(abs(l[i])<524288 && abs(r[i])<524288);
            }
        }
        assert(changed>1000 && energy>1000 && punch.cur==fx);
        punch.req=-1;
        for(uint32_t b=0;b<4;b++){test_signal(0);punch_process(l,r,CTL);}
        assert(!punch.g && punch.cur==-1);
        test_signal(0);int32_t copy[CTL];memcpy(copy,l,sizeof l);punch_process(l,r,CTL);assert(!memcmp(copy,l,sizeof l));
        punch_panel_lock=0;punch_process(l,r,CTL);assert(!resource[RES_PUNCH].size);
        printf("Punch: %-10s changes audio, stays bounded and releases to exact dry\n",PUNCH_NAME[fx]);
    }
}
static void loops(void)
{
    const int32_t replay[]={PX_LOOP4,PX_LOOP8,PX_LOOP16,PX_LOOP32,PX_STUT,PX_REV,PX_HALF,PX_LOOP8T};
    static int16_t saved[PUNCH_N];
    for(uint32_t j=0;j<NELEM(replay);j++){
        reset();for(uint32_t b=0;b<1500;b++){test_signal(0);punch_process(l,r,CTL);}
        punch.req=(int8_t)replay[j];
        for(uint32_t b=0;b<1200;b++){test_signal(0);punch_process(l,r,CTL);}
        memcpy(saved,punch_ring,sizeof saved);
        for(uint32_t b=0;b<2400;b++){test_signal(0);punch_process(l,r,CTL);}
        for(uint32_t k=0;k<punch.len;k++){
            uint32_t pos=(punch.cur==PX_REV?punch.start-k:punch.start+k)&(PUNCH_N-1);
            assert(saved[pos]==punch_ring[pos]);
        }
    }
    puts("Punch loops: all eight captures survive multiple history wraps");
}
static void transitions(void)
{
    reset();
    for(int32_t fx=0;fx<(int32_t)PUNCH_NFX;fx++){
        punch.req=(int8_t)fx;
        for(uint32_t b=0;b<6;b++){test_signal(0);punch_process(l,r,CTL);}
        assert(punch.cur==fx && punch.g==32767);
    }
    punch.req=-1;test_signal(0);punch_process(l,r,CTL);assert(punch.g>0);
    test_signal(0);punch_process(l,r,CTL);assert(!punch.g && punch.cur==-1);
    punch.req=PX_MONO;test_signal(0);punch_process(l,r,1);assert(punch.g==512);
    seq_stop();assert(punch.req==-1);
    punch_panel_lock=0;fx_clear_all();punch_process(l,r,CTL);assert(punch.cur==-1 && !resource[RES_PUNCH].size);
    puts("Punch transitions: every effect handoff, 64-sample release, fresh fade-in, Stop and project reset passed");
}
static void freeze(void)
{
    reset();punch.req=PX_FREEZE;
    uint64_t early=0,late=0;
    for(uint32_t b=0;b<FS*8u/CTL;b++){
        test_signal(b*CTL>=PX_FRZ_CAP);punch_process(l,r,CTL);
        for(uint32_t i=0;i<CTL;i++){
            if(b*CTL>FS && b*CTL<2*FS)early+=abs(l[i])+abs(r[i]);
            if(b*CTL>7*FS)late+=abs(l[i])+abs(r[i]);
        }
    }
    assert(early>100000 && late>early/10 && late<early*3);
    assert(punch.freeze.type==FX_REVERB);
    puts("Punch FREEZE: SLOOP reverb holds captured audio for eight seconds after input ends");
}
static void tempo_memory(void)
{
    reset();midi_sync_active=1;midi_beat_samples=FS;beat_pos=MIDI_BEAT_UNITS/2;
    assert(punch_clock_samples()==FS/2);song.playing=1;punch.req=PX_LOOP8;
    test_signal(0);punch_process(l,r,CTL);assert(punch.len==PUNCH_N-1024u);
    reset();song.playing=1;beat_pos=(uint32_t)(((uint64_t)(beat_samples()/2-64)*MIDI_BEAT_UNITS+beat_samples()-1u)/beat_samples());punch.req=PX_LOOP8;
    test_signal(0);punch_process(l,r,CTL);assert(punch.pre==32);
    test_signal(0);punch_process(l,r,CTL);assert(!punch.pre);
    /* Shorter musical spans remain exact; long spans clamp before wrap. */
    for(uint32_t bpm=30;bpm<=300;bpm+=30){
        reset();song.g[G_BPM]=(int16_t)bpm;
        for(int32_t fx=PX_LOOP4;fx<=PX_HALF;fx++){
            punch_start(fx);assert(punch.len>=64u && punch.len<=PUNCH_N-1024u);
        }
    }
    reset();for(uint32_t id=0;id<RES_COUNT;id++)resource_release(id);
    assert(resource_get(RES_NOTE_HISTORY,RESOURCE_MAIN_CAPACITY));
    punch.req=PX_ECHO;test_signal(0);int32_t dry[CTL];memcpy(dry,l,sizeof l);punch_process(l,r,CTL);
    assert(punch_memory_failed && punch.cur==-1 && !memcmp(dry,l,sizeof l));
    uint32_t failures=resource_failures;for(uint32_t i=0;i<8;i++)punch_process(l,r,CTL);assert(failures==resource_failures);
    punch.req=PX_MONO;test_signal(0);punch_process(l,r,CTL);assert(punch.cur==PX_MONO);
    resource_release(RES_NOTE_HISTORY);punch_reset();punch_panel_lock=0;
    puts("Punch timing/memory: measured external tempo, grid wait, dry memory fallback and no allocation retry storm passed");
}
static void hot_mix(void)
{
    reset();
    for(int32_t fx=0;fx<(int32_t)PUNCH_NFX;fx++){
        punch.req=(int8_t)fx;
        for(uint32_t b=0;b<250;b++){
            for(uint32_t i=0;i<CTL;i++){l[i]=((i+b)&1u)?2097148:-2097148;r[i]=l[i]/2;}
            punch_process(l,r,CTL);
            for(uint32_t i=0;i<CTL;i++)assert(abs(l[i])<2200000 && abs(r[i])<2200000);
        }
    }
    puts("Punch headroom: all effects and transitions accept the maximum four-track master sum");
}
static void master(void)
{
    reset();trk_note_on(TSEL,60,100);
    song.master_q12=4096;fx_bus_mix=fx_bus_target=32768;fx_bus_result=1;
    int32_t out[2*CTL];punch.req=PX_FADE;
    for(uint32_t b=0;b<beat_samples()*3u/CTL;b++){
        test_signal(0);
        for(uint32_t i=0;i<CTL;i++){mix_l[i]=l[i];mix_r[i]=r[i];fx_left[i]=l[i];fx_right[i]=r[i];}
        master_samples(out,CTL);
    }
    int64_t energy=0;for(uint32_t i=0;i<2*CTL;i++)energy+=abs(out[i]);assert(energy<200);
    assert(gates());
    punch.req=-1;
    for(uint32_t b=0;b<100;b++){
        test_signal(0);
        for(uint32_t i=0;i<CTL;i++){mix_l[i]=l[i];mix_r[i]=r[i];fx_left[i]=l[i];fx_right[i]=r[i];}
        master_samples(out,CTL);
    }
    energy=0;for(uint32_t i=0;i<2*CTL;i++)energy+=abs(out[i]);assert(energy>200);
    puts("Punch master: dry mix AND shared returns fade while voices stay held; release restores audio");
}
int main(void){controls();effects();loops();transitions();freeze();tempo_memory();hot_mix();master();return 0;}
