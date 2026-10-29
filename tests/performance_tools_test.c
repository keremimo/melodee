/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>

static void reset_tools(void)
{
    seq_stop(); momentary_restore(); ui_power_on(); set_engine_of(&trk[0], 3); recording_reset();   /* (SID: knobs and CC macros on p[]) */ memset(&midi_clock,0,sizeof midi_clock); song.master_q12=4096;
    settings_click=settings_countin=settings_preview=settings_chord_add=0;
    settings_click_level=CLICK_LEVEL_DEFAULT; memset(cin_n,0,sizeof cin_n); clk.env=click_req=0;
}
static void advance(uint32_t samples)
{
    while(samples){uint32_t n=samples>CTL?CTL:samples;events_block(n);samples-=n;}
}
static void page_named(const char *name)
{
    for(uint32_t i=0;i<NPAGES;i++)if(!strcmp(PAGES[i].title,name)){ui.home=0;ui.page=i;return;}
    assert(0);
}
static void test_recording_tools(void)
{
    reset_tools();settings_countin=1;press(B_REC);assert(song.rec==1 && !transport_req && !song.playing);
    press(B_REC);assert(!song.rec && !transport_req);press(B_REC);press(B_PLAY);events_block(0);
    assert(seq_counting() && !song.playing && transport_busy() && click_req==2);
    uint32_t beat=beat_samples();advance(3*beat+beat/2);assert(cin_left==1 && !song.playing);
    input_on(TSEL,60,100);assert(recording_head[0]>=RECORD_MAX);
    advance(beat-beat/2-1);assert(!song.playing);
    advance(1);assert(song.playing && !seq_counting() && TSEL->seq_idx==0);
    assert(TSEL->rh_n==1);input_off(TSEL,60);seq_stop();
    transport_req=1;events_block(0);assert(seq_counting());transport_req=2;events_block(1);assert(!seq_counting() && !song.playing);
    settings_countin=2;transport_req=1;events_block(0);advance(8*beat-1);assert(seq_counting());advance(1);assert(song.playing);
    seq_stop();song.g[G_CLOCK]=1;transport_req=1;events_block(0);assert(seq_counting() && !song.playing);
    midi_enqueue(0xfau<<8|15u,1);events_block(0);assert(!seq_counting() && song.playing);seq_stop();
    reset_tools();settings_click=CLICK_ON;seq_start();assert(click_req==2);
    int32_t audio[2*CTL]={0};click_render(audio,CTL);int energy=0;
    for(uint32_t i=0;i<2*CTL;i++)energy|=audio[i];assert(energy);
    seq_advance(beat_samples());assert(click_req==1);
    seq_stop();settings_click=CLICK_OFF;seq_start();assert(!click_req);seq_stop();
}
static void test_click_volume(void)
{
    reset_tools();
    int32_t reference[2*CTL], actual[2*CTL];
    static const uint8_t PCT[]={0,1,12,25,50,75,100};
    for(uint32_t beat=1;beat<=2;beat++) {
        uint32_t previous_peak=0;
        for(uint32_t l=0;l<sizeof PCT;l++) {
            settings_click_level=PCT[l];song.master_q12=4096;
            memset(reference,0,sizeof reference);click_req=beat;click_render(reference,CTL);
            uint32_t peak=0;
            for(uint32_t i=0;i<2*CTL;i++) { uint32_t a=(uint32_t)abs(reference[i]);if(a>peak)peak=a; }
            assert(PCT[l]?peak>previous_peak:!peak);previous_peak=peak;
            /* the level is its own: MASTER (the mixer) leaves it as it is */
            for(uint32_t master=0;master<=4096;master+=1024) {
                song.master_q12=master;memset(actual,0,sizeof actual);click_req=beat;click_render(actual,CTL);
                assert(!memcmp(actual,reference,sizeof actual));
            }
        }
        /* 100 % reaches (near) DAC full scale on the downbeat, half of it on a beat */
        assert(beat==2?previous_peak>30000u:previous_peak>15000u && previous_peak<16500u);
    }
    /* A sounding tail keeps its level when MASTER changes. */
    settings_click_level=CLICK_LEVEL_DEFAULT;song.master_q12=4096;click_req=2;
    memset(reference,0,sizeof reference);click_render(reference,CTL);
    song.master_q12=0;
    memset(actual,0,sizeof actual);click_render(actual,CTL);
    int energy=0;for(uint32_t i=0;i<2*CTL;i++)energy|=actual[i];assert(energy);
    /* Count-in sounds at the click level even when AUDIO CLICK is off, whatever MASTER is. */
    for(uint32_t master=0;master<=4096;master+=1024) {
        seq_stop();settings_click=CLICK_OFF;settings_countin=1;song.rec=1;song.master_q12=master;
        transport_req=1;events_block(0);assert(seq_counting() && click_req==2);
        memset(actual,0,sizeof actual);click_render(actual,CTL);
        assert(!memcmp(actual,reference,sizeof actual));
    }
    seq_stop();song.master_q12=4096;
}
static void test_editing_tools(void)
{
    reset_tools();go_page(GR_ROLL);TSEL->p[P_VOICE]=V_POLY;settings_chord_add=1;
    uint8_t a=60,b=64;seq_entry_notes(&a,1);seq_entry_finish();assert(ui.cursor==0 && TSEL->step[0].n==1);
    seq_entry_notes(&b,1);seq_entry_finish();assert(TSEL->step[0].n==2);
    seq_entry_notes(&a,1);seq_entry_finish();assert(TSEL->step[0].n==1 && TSEL->step[0].note[0]==64);
    settings_chord_add=0;seq_entry_notes(&a,1);seq_entry_finish();assert(ui.cursor==1);
    settings_preview=1;cursor_set(0);assert(audition_request);events_block(1);assert(audition_active && recording_head[0]>=RECORD_MAX);
    advance(FS/6);assert(!audition_active);input_on(TSEL,60,90);cursor_set(0);events_block(1);
    assert(!audition_n);advance(FS/6);assert(live_held[0][60>>5]&(1u<<(60&31u)));input_off(TSEL,60);
    reset_tools();ui.home=1;uint32_t param=ENGINES[TSEL->eng_req]->knob[0];int16_t before=TSEL->p[param];btn_down(B_LFO);turn(EN_K1,5);
    assert(momentary.active && TSEL->p[param]!=before);fm1_in.buttons&=~(1u<<panel.btn[B_LFO]);frame();
    assert(!momentary.active && TSEL->p[param]==before && ui.home);
    btn_down(B_LFO);turn(EN_K1,-4);int16_t kept=TSEL->p[param];press(B_OCTUP);
    fm1_in.buttons&=~(1u<<panel.btn[B_LFO]);frame();assert(TSEL->p[param]==kept && song.octave==0);
    set_engine_of(TSEL,ENGI_FM6);TSEL->engine=TSEL->eng_req;page_named("ALGO");
    uint8_t patch[FP_SIZE+1];memcpy(patch,fm6_patch_of(0),sizeof patch);
    btn_down(B_LFO);turn(EN_K1,1);assert(momentary.active);fm1_in.buttons=0;frame();assert(!memcmp(patch,fm6_patch_of(0),sizeof patch));
    set_engine_of(TSEL,ENGI_CZ);TSEL->engine=TSEL->eng_req;
    page_named("CZ LINE");
    cz_patch_t cz=(*cz_patch_of(0));btn_down(B_LFO);turn(EN_K1,1);assert(momentary.active);fm1_in.buttons=0;frame();assert(!memcmp(&cz,cz_patch,sizeof cz));
    set_engine_of(TSEL,ENGI_PROPHET);TSEL->engine=TSEL->eng_req;page_named("P5 FILTER");
    p5_patch_t p5=*p5_patch_of(TSEL);TSEL->p[P_PRIO]=2;btn_down(B_LFO);turn(EN_K1,-1);assert(momentary.active);fm1_in.buttons=0;frame();
    assert(!memcmp(&p5,p5_patch_of(TSEL),sizeof p5) && TSEL->p[P_PRIO]==2);
}
static void test_midi_and_levels(void)
{
    reset_tools();midi_event(0xb0,0,7,127);assert(TSEL->p[P_LEVEL]==127);
    midi_event(0xb0,0,10,64);assert(TSEL->p[P_PAN]==0);
    midi_event(0xb0,0,74,0);assert(TSEL->p[P_E4]==0);
    midi_event(0xb0,0,71,127);assert(TSEL->p[P_E5]==ENGINES[TSEL->eng_req]->edit[5].max);   /* (SID's RES: 0..15) */
    song.g[G_ROUTE]=0;midi_event(0xb0,1,91,88);assert(!trk[1].p[P_RESERVED36] && !trk[0].p[P_RESERVED36]);
    set_engine_of(TSEL,ENGI_PROPHET);TSEL->engine=TSEL->eng_req;
    midi_event(0xb0,0,73,70);assert(p5_patch_of(TSEL)->raw[P5_ATTACK_AMP]==70);
    reset_tools();song.rec=1;seq_start();seq_advance(0);midi_event(0xb0,0,7,17);
    assert(motion.count==1 && motion.event[0].param==P_LEVEL && motion.event[0].value==17);seq_stop();
    reset_tools();track_select(3);TSEL->p[P_LN0]=0;trk_note_on(TSEL,36,127);
    int32_t out[2*CTL];mix_block(out,CTL);for(uint32_t i=0;i<2*CTL;i++)assert(out[i]==0);
    TSEL->p[P_LN0]=127;mix_block(out,CTL);int energy=0;for(uint32_t i=0;i<2*CTL;i++)energy|=out[i];assert(energy);
}
static void test_compatibility(void)
{
    reset_tools();settings_click=2;settings_click_level=15;settings_countin=2;settings_preview=settings_chord_add=1;
    persist_t p={0};settings_export(&p);settings_click=settings_countin=settings_preview=settings_chord_add=0;settings_click_level=CLICK_LEVEL_DEFAULT;
    assert(settings_import(&p,sizeof p));assert(settings_click==2 && settings_click_level==15 && settings_countin==2 && settings_preview && settings_chord_add);
    /* older builds read the nearest LOW/MID/HIGH from the zoom word: 15 % is MID (0) */
    assert(((p.zoom>>10)&3u)==0u);
    settings_click_level=0;settings_export(&p);assert(((p.zoom>>10)&3u)==1u);
    settings_click_level=100;settings_export(&p);assert(((p.zoom>>10)&3u)==2u);
    /* records from before the percentage: LOW/MID/HIGH become 6/12/25 %, as heard at the default MASTER */
    for(uint32_t old=0;old<3;old++) {
        memset(p.favorites.factory[1]+24,0,4);p.zoom=0x52500000u|((old<<2)|1u)<<8;
        assert(settings_import(&p,sizeof p));assert(settings_click_level==(old==1u?6u:old==2u?25u:12u));
    }
    p.zoom=1;assert(settings_import(&p,sizeof p));assert(!settings_click && settings_click_level==CLICK_LEVEL_DEFAULT && !settings_countin && !settings_preview && !settings_chord_add);
    project_t q,back;project_store_t current;project_capture(&q);q.t[0].p[P_E4]=53;
    for(uint32_t t=0;t<NPART;t++)q.t[t].p[P_RESERVED35]=77;   /* delay sends and their automation (a FUN16's are kept) */
    q.g[G_RESERVED25]=3;q.g[G_RESERVED26]=9;q.g[G_RESERVED4]=2;q.motion.count=1;q.motion.event[0]=(motion_event_t){0,P_RESERVED35,100};q.sum=proj_sum(&q);
    assert(proj_pack(&current,&q));
    assert(proj_import(&back,&current,PROJ_STORE_SIZE) && back.t[1].p[P_RESERVED35]==77 && back.g[G_RESERVED25]==3 && back.g[G_RESERVED26]==9 &&
           back.motion.count==1 && back.motion.event[0].value==100);
    {   /* Both current and earlier user presets ignore retired effects in their live values. */
        up_rec_t r;int16_t v[P_COUNT];memset(&r,0,sizeof r);r.used=UP_USED;r.ver=UP_VER_GRID;r.engine=3;r.np=P_COUNT;memcpy(r.name,"OLD",3);
        for(uint32_t i=0;i<P_COUNT;i++)up_set_value(&r,i,param_desc_of(0,i)->def);
        up_set_value(&r,P_RESERVED35,77);up_values(&r,v);assert(v[P_RESERVED35]==0);
        r.np=100;up_values(&r,v);assert(v[P_RESERVED35]==0);
    }
    {   /* a factory preset: no delay send (its sends predate the delay coming back) */
        TSEL->eng_req=3;TSEL->p[P_RESERVED35]=77;apply_preset_to(TSEL,8);assert(TSEL->p[P_RESERVED35]==0 && TSEL->p[P_RESERVED36]==0);
    }
}
int main(void)
{
    test_recording_tools();test_click_volume();test_editing_tools();test_midi_and_levels();test_compatibility();
    puts("performance tools: count-in, click, additive entry, audition, momentary restore/keep, MIDI, drum levels and inert legacy effect values passed");return 0;
}
