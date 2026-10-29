/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Real UI/mixer/storage integration plus deterministic DSP stress. */
#include <assert.h>
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include "fx_kernel_fixture.h"
static void reset_fx(void)
{
    fx_clear_all();memset(fx_state,0,sizeof fx_state);
    for(uint32_t k=0;k<FX_STATE_COUNT;k++)resource_release(RES_FX0+k);
    memset(trk,0,sizeof trk);trk_voices_bind();spread_on=0;trk[0].p[P_FXRATE]=60;trk[0].p[P_FXCHAR]=64;
}
static uint64_t fx_signal(uint32_t type,int rate,int character,int mix,const char *demo)
{
    reset_fx();track_t *t=&trk[0];t->p[P_FXTYPE]=(int16_t)type;t->p[P_FXRATE]=(int16_t)rate;
    t->p[P_FXCHAR]=(int16_t)character;t->p[P_FXMIX]=(int16_t)mix;
    uint32_t phase=0;uint64_t h=1469598103934665603ull,energy=0;int32_t b[CTL];
    FILE *w=demo?fopen(demo,"wb"):0;if(demo)assert(w);if(w)wav_hdr(w,2u*FS/CTL*CTL);
    for(uint32_t frame=0;frame<2u*FS/CTL;frame++){
        for(uint32_t i=0;i<CTL;i++){
            phase+=0x02347120u;
            b[i]=((sine_i(phase)+sine_i(phase*3u)/2)/4)*2;
            if(frame%173u==0u && !i)b[i]+=18000;
        }
        int on=fx_kernel_process(t,b,CTL);
        assert(resource[RES_FX0].size==((type==FX_CHORUS || type==FX_ROOM) && mix?2048u:type==FX_REVERB && mix?sizeof(fx_sloop_reverb_t):0u));
        for(uint32_t i=0;i<CTL;i++){
            int32_t l=on?fx_left[i]:b[i],r=on?fx_right[i]:b[i];
            assert(l>=-1000000 && l<=1000000 && r>=-1000000 && r<=1000000);
            if(type==FX_WIDTH)assert(r==b[i]);
            h=(h^(uint32_t)l)*1099511628211ull;h=(h^(uint32_t)r)*1099511628211ull;
            energy+=(uint64_t)(l<0?-l:l)+(uint64_t)(r<0?-r:r);
            if(w)wav_put(w,clamp(l,-32767,32767),clamp(r,-32767,32767));
        }
    }
    if(w)fclose(w);assert(energy>1000000u);return h;
}
static void legacy(void)
{
    static uint8_t b[BANK_STORE_SIZE];
    const char *names[]={"fun16.bin","fbkk.bin","tplf.bin","fun17.bin","fbkk17.bin","tplg.bin"};
    for(uint32_t k=0;k<6u;k++){
        char path[96];snprintf(path,sizeof path,"tests/fixtures/fx/%s",names[k]);
        FILE *f=fopen(path,"rb");assert(f);uint32_t n=(uint32_t)fread(b,1,sizeof b,f);fclose(f);
        ui_power_on();
        if(k%3u==2u){assert(tmpl_take(b,n));template_load();}
        else{
            assert(k%3u?bank_valid(b,n):proj_import(&proj_scratch,b,(int)n));
            assert(proj_scratch.t[0].p[P_E0]==17 && proj_scratch.motion.event[0].param==P_E0);
            assert(proj_scratch.recording[0].on==55 && proj_scratch.recording[0].duration==217 && recorded_owner(&proj_scratch.recording[0])==1u*NPAT+3u);   /* (track 2, bank 4: owner 11 in the 8-bank files) */
            assert(!project_restore_runtime(&proj_scratch));if(k%3u)bank_restore(b);
            assert(TSEL->step[0].n==3 && TSEL->step[0].note[2]==67);
            if(k%3u)assert(pattern_at(1,3)->step[7].note[0]==72);
        }
        assert(TSEL->p[P_E0]==17);
        assert(TSEL->p[P_FXTYPE]==FX_OFF && TSEL->p[P_FXMIX]==(k<3u?0:91));
        assert(TSEL->p[P_FXRATE]==(k<3u?60:83) && TSEL->p[P_FXCHAR]==(k<3u?64:77));
        for(uint32_t slot=1;slot<FX_SLOTS;slot++)assert(!TSEL->p[P_FXTYPE+slot*FX_PARAMS]);
    }
    puts("Frozen FUN16/FUN17, FBKK, TPLF/TPLG: notes, banks, timing, motion, engine IDs and retired inserts OFF: OK");
}
static void frozen_three_slots(void)
{
    static uint8_t b[BANK_STORE_SIZE];const char *names[]={"fun18.bin","fbkk18.bin","tplh.bin"};
    const int16_t chain[12]={0,80,30,71,0,95,55,65,0,105,88,99};
    for(uint32_t k=0;k<3;k++){
        char path[96];snprintf(path,sizeof path,"tests/fixtures/fx/%s",names[k]);FILE *f=fopen(path,"rb");assert(f);uint32_t n=fread(b,1,sizeof b,f);fclose(f);ui_power_on();
        if(k==2){assert(tmpl_take(b,n));template_load();}else {assert(k?bank_valid(b,n):proj_import(&proj_scratch,b,n));assert(!project_restore_runtime(&proj_scratch));}
        if(k==0){ /* Old engine ID 112 shifts, a tagged native 112 must not. */
            motion_store_t m={.count=2,.on=1,.event={{0,112,10},{0,112,(MO_TAG_FM6<<8)|80}}};
            uint32_t off=68u+NPART*(116u+2u+NSTEP*8u)+sizeof(chain_legacy_t);memcpy(b+off,&m,sizeof m);uint32_t h=proj_hash(b,n-4u);memcpy(b+n-4u,&h,4);
            assert(proj_import(&proj_scratch,b,n));assert(proj_scratch.motion.event[0].param==P_E4 && proj_scratch.motion.event[1].param==112 && proj_scratch.motion.event[1].value==((MO_TAG_FM6<<8)|80));
        }
        assert(!memcmp(trk[0].p+P_FXTYPE,chain,sizeof chain));assert(!trk[0].p[P_FXSEND]);
        for(uint32_t j=0;j<3;j++)assert(!song.g[G_MFXTYPE+j*4] && song.g[G_MFXMIX+j*4]==127);
        assert(song.g[G_FXRETURN]==100);if(k!=2)assert(trk[0].step[0].note[0]==60);
    }
    puts("Frozen FUN18/FBKK18/TPLH: retired slots OFF; remaining insert and send defaults preserved: OK");
}
static void send_return(void)
{
    ui_power_on();int32_t l[CTL],r[CTL];memset(mix_l,0,sizeof mix_l);memset(mix_r,0,sizeof mix_r);memset(fx_send_l,0,sizeof fx_send_l);memset(fx_send_r,0,sizeof fx_send_r);
    mix_add(4000,-3000,0,65536);mix_add(2000,6000,0,32768);assert(mix_l[0]==6000 && mix_r[0]==3000 && fx_send_l[0]==5000 && fx_send_r[0]==0);
    fx_bus_start(CTL);fx_bus_join();assert(!fx_bus_result); /* no doubled dry when OFF */
    song.g[G_MFXTYPE]=FX_ROOM;for(uint32_t frame=0;frame<400;frame++){
        for(uint32_t i=0;i<CTL;i++){fx_send_l[i]=frame<20?6000:0;fx_send_r[i]=frame<20?-5000:0;}
        fx_bus_start(CTL);fx_bus_join();assert(fx_bus_result);memcpy(l,fx_left,sizeof l);memcpy(r,fx_right,sizeof r);
    }
    assert(resource[RES_FX0].size==2048 && !fx_state[0].failed);
    uint64_t energy=0;for(uint32_t i=0;i<CTL;i++)energy+=abs(l[i])+abs(r[i]);assert(energy>10);
    song.g[G_MFXTYPE]=FX_CHORUS;song.g[G_MFX2TYPE]=FX_ROOM;song.g[G_MFX3TYPE]=FX_ROOM;
    for(uint32_t frame=0;frame<64;frame++){fx_bus_start(CTL);fx_bus_join();for(uint32_t j=0;j<3;j++)if(fx_bus_plan[j].active && fx_bus_plan[j].f->type>=FX_CHORUS)assert(fx_bus_plan[j].mem==resource[RES_FX0+j].ptr);}
    assert(fx_state[0].mix==32768); /* fully wet is exact on a send */
    song.g[G_MFXTYPE]=FX_ROOM;song.g[G_MFX2TYPE]=song.g[G_MFX3TYPE]=FX_OFF;
    trk[0].p[P_FXSEND]=81;apply_preset_to(&trk[0],0);assert(trk[0].p[P_FXSEND]==81);sound_init_of(TSEL,eng_idx(TSEL->eng_req));assert(trk[0].p[P_FXSEND]==81);
    song.g[G_FXRETURN]=73;project_t q;project_capture(&q);project_store_t wire;assert(proj_pack(&wire,&q) && proj_import(&proj_scratch,&wire,sizeof wire));assert(proj_scratch.g[G_FXRETURN]==73 && proj_scratch.g[G_MFXTYPE]==FX_ROOM && proj_scratch.t[0].p[P_FXSEND]==81);
    template_save();memset(song.g+G_MFXTYPE,0,(G_COUNT-G_MFXTYPE)*sizeof *song.g);trk[0].p[P_FXSEND]=0;template_load();assert(song.g[G_FXRETURN]==73 && song.g[G_MFXTYPE]==FX_ROOM && trk[0].p[P_FXSEND]==81);
    go_title("FX");fx_rack_row[0]=3;turn(EN_K4,1);assert(trk[0].p[P_FXSEND]>81);   /* the rack's shared row: SEND */
    go_title("SHARED");fx_rack_row[1]=3;turn(EN_K4,1);assert(song.g[G_FXRETURN]>73);  /* SHARED's last row: RETURN */
    wire.raw[PROJ_FX_WIRE_OFF+3u]=128;uint32_t hash=proj_hash(wire.raw,sizeof wire-4u);memcpy(wire.raw+sizeof wire-4u,&hash,4);assert(!proj_import(&proj_scratch,&wire,sizeof wire));
    /* OFF and a type change fade the parallel return to silence, while an
     * insert bypass stays dry. RETURN edits also ramp rather than step. */
    song.g[G_MFXTYPE]=FX_ROOM;song.g[G_MFXMIX]=127;song.g[G_FXRETURN]=127;
    memset(mix_l,0,sizeof mix_l);memset(mix_r,0,sizeof mix_r);int32_t out[CTL*2];
    for(uint32_t k=0;k<24;k++){fx_bus_start(CTL);fx_bus_join();master_samples(out,CTL);}assert(fx_bus_mix==32768);
    song.g[G_MFXTYPE]=0;for(uint32_t k=0;k<24;k++){fx_bus_start(CTL);fx_bus_join();master_samples(out,CTL);}
    assert(!fx_bus_target && !fx_bus_mix && !fx_bus_result);
    song.g[G_MFXTYPE]=FX_ROOM;for(uint32_t k=0;k<16;k++){fx_bus_start(CTL);fx_bus_join();master_samples(out,CTL);}assert(fx_bus_mix==32768);
    song.g[G_MFXTYPE]=FX_CHORUS;fx_bus_start(CTL);fx_bus_join();assert(!fx_bus_target);master_samples(out,CTL);assert(fx_bus_mix==32768-CTL*128);
    song.g[G_FXRETURN]=0;for(uint32_t k=0;k<24;k++){fx_bus_start(CTL);fx_bus_join();master_samples(out,CTL);}assert(!fx_bus_mix);
    puts("Shared send/return: post-fader sum, no dry doubling when OFF, one 2KiB Room buffer, preset independence, UI and project/template persistence: OK");
}
static void separate_sends(void)
{
    /* Frozen records are written by the preceding firmware, not this decoder. */
    uint8_t b[PROJ_STORE_SIZE];
    const char *paths[]={"tests/fixtures/fun19-send-chain.bin","tests/fixtures/tpli-send-chain.bin"};
    for(uint32_t k=0;k<2;k++){
        FILE *f=fopen(paths[k],"rb");assert(f);uint32_t n=fread(b,1,sizeof b,f);fclose(f);ui_power_on();
        if(k){assert(tmpl_take(b,n));template_load();}else{assert(proj_import(&proj_scratch,b,n));assert(!project_restore_runtime(&proj_scratch));}
        assert(!song.g[G_FXMODE] && TSEL->p[P_FXSEND]==81 && !TSEL->p[P_FXSEND2] && !TSEL->p[P_FXSEND3]);
        assert(TSEL->p[P_E3]==clamp(42,track_desc(TSEL,P_E3)->min,track_desc(TSEL,P_E3)->max) && song.g[G_FXRETURN]==73 && song.g[G_MFXTYPE]==FX_REVERB);
    }
    ui_power_on();fx_bus_mix=0;song.g[G_FXMODE]=1;fx_bus_select();fx_send_on=1;
    memset(mix_l,0,sizeof mix_l);memset(mix_r,0,sizeof mix_r);
    memset(fx_send_l,0,sizeof fx_send_l);memset(fx_send_r,0,sizeof fx_send_r);
    memset(fx_send_extra_l,0,sizeof fx_send_extra_l);memset(fx_send_extra_r,0,sizeof fx_send_extra_r);
    memset(fx_bus_sum_l,0,sizeof fx_bus_sum_l);memset(fx_bus_sum_r,0,sizeof fx_bus_sum_r);
    fx_extra_gain[0]=32768;fx_extra_gain[1]=65536;mix_add(4000,-3000,0,0);mix_extra_sends(CTL);
    assert(mix_l[0]==4000 && mix_r[0]==-3000 && !fx_send_l[0] && !fx_send_r[0]);
    assert(fx_send_extra_l[0][0]==2000 && fx_send_extra_r[0][0]==-1500 && fx_send_extra_l[1][0]==4000 && fx_send_extra_r[1][0]==-3000);
    /* Each slot must see its own input and return exactly once, even with an
     * inactive middle slot. Compare with separately rendered reference outputs. */
    int32_t expected_l[CTL]={0},expected_r[CTL]={0};fx_state_t before[FX_SLOTS];
    for(uint32_t slot=0;slot<FX_SLOTS;slot++){
        song.g[G_MFXTYPE+slot*FX_PARAMS]=slot==1?FX_OFF:FX_PHASER;
        int32_t *l=slot?fx_send_extra_l[slot-1]:fx_send_l,*r=slot?fx_send_extra_r[slot-1]:fx_send_r;
        for(uint32_t i=0;i<CTL;i++){l[i]=(int32_t)(i*97+slot*303);r[i]=-(int32_t)(i*53+slot*197);}
        fx_slot_prepare(&fx_bus_plan[slot],slot,song.g+G_MFXTYPE+slot*FX_PARAMS);
        before[slot]=fx_state[slot];
        if(!fx_bus_plan[slot].active)continue;
        memcpy(fx_left,l,sizeof fx_left);memcpy(fx_right,r,sizeof fx_right);assert(fx_slot_run(&fx_bus_plan[slot],CTL));
        for(uint32_t i=0;i<CTL;i++){expected_l[i]+=fx_scale(fx_left[i],(i+1u)*256u);expected_r[i]+=fx_scale(fx_right[i],(i+1u)*256u);}
    }
    for(uint32_t slot=0;slot<FX_SLOTS;slot++)fx_state[slot]=before[slot];
    fx_bus_start(CTL);fx_bus_join();assert(fx_bus_result);
    assert(!memcmp(expected_l,fx_left,sizeof expected_l) && !memcmp(expected_r,fx_right,sizeof expected_r));
    /* The real mixer routes only this track to slot 2 after volume and pan. */
    ui_power_on();fx_bus_mix=0;song.g[G_FXMODE]=1;song.g[G_MFX2TYPE]=FX_PHASER;
    host_preset(TSEL,ENGI_FM6,8);TSEL->p[P_SUS]=127;TSEL->p[P_LEVEL]=127;TSEL->p[P_PAN]=-64;
    TSEL->p[P_FXSEND2]=127;trk_note_on(TSEL,60,100);int32_t probe[CTL*2];uint64_t send_energy=0;
    for(uint32_t k=0;k<32;k++){
        mix_block(probe,CTL);
        for(uint32_t i=0;i<CTL;i++){
            assert(!fx_send_l[i] && !fx_send_r[i] && !fx_send_extra_r[0][i] && !fx_send_extra_l[1][i] && !fx_send_extra_r[1][i]);
            send_energy+=abs(fx_send_extra_l[0][i]);assert(probe[2*i]>=-32767 && probe[2*i]<=32767);
        }
    }
    assert(send_energy>1000);TSEL->p[P_LEVEL]=0;
    mix_block(probe,CTL);
    for(uint32_t i=0;i<CTL;i++)assert(!fx_send_extra_l[0][i]);
    TSEL->p[P_LEVEL]=127;
    TSEL->p[P_FXSEND]=20;TSEL->p[P_FXSEND2]=71;TSEL->p[P_FXSEND3]=99;
    apply_preset_to(TSEL,0);sound_init_of(TSEL,eng_idx(TSEL->eng_req));
    assert(TSEL->p[P_FXSEND]==20 && TSEL->p[P_FXSEND2]==71 && TSEL->p[P_FXSEND3]==99);
    project_t q;project_store_t wire;project_capture(&q);assert(proj_pack(&wire,&q) && proj_import(&proj_scratch,&wire,sizeof wire));
    assert(proj_scratch.g[G_FXMODE]==1 && proj_scratch.t[0].p[P_FXSEND2]==71 && proj_scratch.t[0].p[P_FXSEND3]==99);
    template_save();song.g[G_FXMODE]=0;TSEL->p[P_FXSEND2]=TSEL->p[P_FXSEND3]=0;template_load();
    assert(song.g[G_FXMODE]==1 && TSEL->p[P_FXSEND2]==71 && TSEL->p[P_FXSEND3]==99);
    go_title("SHARED");fx_rack_row[1]=1;turn(EN_K4,1);turn(EN_K2,1);turn(EN_K4,-1);   /* SPLIT: SHARED's rows send */
    assert(TSEL->p[P_FXSEND2]>71 && TSEL->p[P_FXSEND3]<99 && fx_rack_row[1]==2);
    go_title("FX");fx_rack_row[0]=3;turn(EN_K4,1);assert(TSEL->p[P_FXSEND]==20);       /* (FX's shared row: no SEND) */
    go_title("SHARED");page_sheet_open();assert(pop.on==POP_SHEET && pop.rows==SHEET_SHARED);
    turn(EN_K1,-1);assert(!song.g[G_FXMODE]);pop_close();                               /* the sheet's Routing */
    uint32_t inserts_epoch=fx_state[0].epoch;fx_bus_mix=32768;fx_bus_select();assert(fx_bus_separate);
    int32_t out[CTL*2];for(uint32_t k=0;k<32;k++){fx_bus_start(CTL);fx_bus_join();master_samples(out,CTL);}
    assert(!fx_bus_separate && TSEL->p[P_FXSEND2]>71 && TSEL->p[P_FXSEND2]>71);
    wire.raw[PROJ_FX_MODE_OFF]=2;uint32_t h=proj_hash(wire.raw,sizeof wire-4u);memcpy(wire.raw+sizeof wire-4u,&h,4);assert(!proj_import(&proj_scratch,&wire,sizeof wire));
    ui_power_on();fx_bus_mix=0;fx_bus_select();
    puts("Separate sends: independent inputs/returns, inactive-slot silence, routing fade, old chain imports, UI and project/template persistence: OK");
}
static void fx_chain_test(void)
{
    ui_power_on();track_t *t=&trk[0];
    int16_t settings[FX_SLOTS*FX_PARAMS]={FX_PHASER,80,30,71,FX_CHORUS,95,55,65,FX_ROOM,105,88,99};
    memcpy(song.g+G_MFXTYPE,settings,sizeof settings);song.g[G_FXRETURN]=127;
    uint64_t energy=0;
    for(uint32_t frame=0;frame<1600;frame++){
        for(uint32_t i=0;i<CTL;i++)fx_send_l[i]=fx_send_r[i]=frame<80?8000*sine_i((frame*CTL+i)*12345678u)/32768:0;
        fx_bus_start(CTL);fx_bus_join();assert(fx_bus_result);
        for(uint32_t i=0;i<CTL;i++)if(frame>80)energy+=abs(fx_left[i])+abs(fx_right[i]);
    }
    assert(energy>10000 && fx_state[0].type==FX_PHASER && fx_state[1].type==FX_CHORUS && fx_state[2].type==FX_ROOM);
    assert(!resource[RES_FX0].size && resource[RES_FX0+1].size==2048 && resource[RES_FX0+2].size==2048);
    apply_preset_to(t,2);set_engine_of(t,ENGI_FM6);apply_preset_to(t,8);assert(!memcmp(song.g+G_MFXTYPE,settings,sizeof settings));
    project_t q;project_store_t p;project_capture(&q);assert(proj_pack(&p,&q) && proj_import(&proj_scratch,&p,sizeof p));
    for(uint32_t slot=0;slot<FX_SLOTS;slot++)assert(proj_scratch.t[0].p[P_FXTYPE+slot*FX_PARAMS]==FX_OFF);
    assert(!memcmp(proj_scratch.g+G_MFXTYPE,settings,sizeof settings));
    template_save();memset(song.g+G_MFXTYPE,0,sizeof settings);template_load();assert(!memcmp(song.g+G_MFXTYPE,settings,sizeof settings));
    song.g[G_MFX2MIX]=0;for(uint32_t k=0;k<24;k++){fx_bus_start(CTL);fx_bus_join();}
    assert(!resource[RES_FX0+1].size && resource[RES_FX0+2].size==2048);
    puts("Three shared slots: serial order, independent bypass, tails and project/template persistence: OK");
}
static void rate_and_pan(void)
{
    /* Cached block conversion must match the separately verified FIR model,
     * including arbitrary block boundaries, stereo differences and wraps. */
    fx_state_t actual={0},reference[2]={{0}};actual.type=FX_TAPE;int32_t rng=13;
    for(uint32_t block=0;block<10000u;block++){
        uint32_t n=1u+block%CTL,phase=actual.decim,count=0;int32_t expected[2][CTL/4u+1u];
        for(uint32_t i=0;i<n;i++){
            fx_left[i]=(int32_t)(noise32(&rng)%2000001u)-1000000;fx_right[i]=-fx_left[i];
            int32_t l=fx_filter_input(&reference[0],0,fx_left[i],phase),r=fx_filter_input(&reference[1],0,fx_right[i],phase);
            if(!phase){expected[0][count]=l;expected[1][count++]=r;}phase=(phase+1u)&3u;
            fx_dry_l[i]=fx_dry_r[i]=0;
        }
        assert(fx_down(&actual,n)==count);
        for(uint32_t i=0;i<count;i++)assert(fx_low_l[i]==expected[0][i] && fx_low_r[i]==expected[1][i]);
        fx_up(&actual,n,0);assert(actual.decim==phase);
        for(uint32_t ch=0;ch<2u;ch++)for(uint32_t k=0;k<4u;k++)assert(actual.rev_damp[ch*4u+k]==reference[ch].rev_damp[k]);
    }
    for(uint32_t shift=1;shift<16u;shift++)for(uint32_t i=0;i<5000u;i++){
        int32_t x=(int32_t)noise32(&rng);assert(fx_div_pow2(x,shift)==x/(int32_t)(1u<<shift));
    }
    for(uint32_t type=FX_PHASER;type<=FX_SPIT;type++){
        reset_fx();track_t *t=&trk[0];t->p[P_FXTYPE]=type;t->p[P_FXMIX]=127;t->p[P_PAN]=1;
        int32_t b[CTL];for(uint32_t i=0;i<CTL;i++)b[i]=4000;
        for(uint32_t k=0;k<50u;k++)fx_kernel_process(t,b,CTL);
        assert(type==FX_WIDTH || fx_state[0].mono); /* PAN alone adds no stereo filter work */
        t->p[P_PAN]=63;fx_kernel_process(t,b,CTL);for(uint32_t i=0;i<CTL;i++)assert(type==FX_WIDTH?fx_right[i]==4000:fx_left[i]==fx_scale(fx_right[i],1024u));
        t->p[P_PAN]=-64;fx_kernel_process(t,b,CTL);for(uint32_t i=0;i<CTL;i++)assert(!fx_right[i]);
        t->p[P_PAN]=0;spread_on=1;t->p[P_SPRD]=127;
        for(uint32_t i=0;i<CTL;i++)spread_buf[i]=i&1u?-3000:3000;
        fx_kernel_process(t,b,CTL);assert(!fx_state[0].mono);
    }
    puts("FX performance safety: cached stereo conversion, arbitrary blocks/wraps, exact shifts, mono pan and genuine spread: OK");
}
static void tape_smooth(void)
{
    for(uint32_t hz=0;hz<=220;hz+=220)for(int rate=60;rate<=127;rate+=67){
        reset_fx();track_t *t=&trk[0];t->p[P_FXTYPE]=FX_TAPE;t->p[P_FXMIX]=127;t->p[P_FXRATE]=rate;t->p[P_FXCHAR]=127;
        int32_t b[CTL]={0};fx_kernel_process(t,b,CTL);fx_state[0].mix=127*256;
        uint32_t phase=0,inc=(uint32_t)((uint64_t)hz*0x100000000ull/FS),jump=0;int32_t prev=0;
        for(uint32_t frame=0;frame<6u*FS/CTL;frame++){
            for(uint32_t i=0;i<CTL;i++){phase+=inc;b[i]=hz?8000*sine_i(phase)/32768:8000;}
            if(frame>FS/CTL && frame%401u==0u)t->p[P_FXCHAR]=t->p[P_FXCHAR]?0:127;
            fx_kernel_process(t,b,CTL);
            for(uint32_t i=0;i<CTL;i++){
                int32_t y=fx_left[i];uint32_t delta=(uint32_t)abs(y-prev);prev=y;
                if(frame>FS/CTL && delta>jump)jump=delta;
                assert(abs(y)<24000 && fx_left[i]==fx_right[i]);
            }
        }
        /* A steady input used to jump by ~1000 samples when random targets
         * changed; a 220 Hz tone jumped by ~1500..2000 instead of its 251. */
        assert(jump<=(hz?600u:2u));assert(!resource[RES_FX0].size);
        t->p[P_FXCHAR]=0;fx_kernel_process(t,b,CTL);t->p[P_FXCHAR]=127;fx_kernel_process(t,b,CTL);
        for(uint32_t k=0;k<4000;k++){memset(b,0,sizeof b);fx_kernel_process(t,b,CTL);}
        for(uint32_t ch=0;ch<2;ch++)for(uint32_t k=0;k<3;k++)assert(!fx_state[0].history[ch][k].z1 && !fx_state[0].history[ch][k].z2);
    }
    puts("Tape: maximum jitter/rate have smooth phase/filter transitions, bounded stereo output, no allocation and no denormal tail: OK");
}
static void chorus_test(void)
{
    _Static_assert(FX_ROOM==7 && FX_CHORUS==9,"stored FX IDs stay fixed");
    /* A fully wet, depth-zero chorus preserves upper audio frequencies and
     * independent stereo channels; unlike the other time FX it is full-rate. */
    fx_state_t f={.type=FX_CHORUS,.rev_l=256*65536,.rev_r=256*65536,.mix=32768};
    int16_t mem[FX_CHORUS_BYTES/2u]={0};int32_t step[2]={0},rng=31;
    uint64_t dry=0,wet=0;
    for(uint32_t block=0;block<3000u;block++){
        uint32_t n=1u+block%CTL;
        for(uint32_t i=0;i<n;i++){
            int32_t x=8000*sine_i((block*CTL+i)*0x2e709de5u)/32768;
            fx_left[i]=fx_dry_l[i]=x;fx_right[i]=fx_dry_r[i]=-x;
            dry+=(int64_t)x*x;
        }
        fx_chorus(&f,mem,n,step,32768);
        for(uint32_t i=0;i<n;i++){assert(abs(fx_left[i]+fx_right[i])<=4);wet+=(int64_t)fx_left[i]*fx_left[i];}
    }
    assert(wet>dry*98u/100u && wet<dry*102u/100u);
    /* Hot output from preceding slots stays bounded, and no feedback means
     * silence becomes exact within one ring, even after repeated wraps. */
    for(uint32_t block=0;block<1000;block++){
        for(uint32_t i=0;i<CTL;i++){fx_left[i]=fx_dry_l[i]=(int32_t)(noise32(&rng)%2000001u)-1000000;fx_right[i]=fx_dry_r[i]=-fx_left[i];}
        fx_chorus(&f,mem,CTL,step,32768);
        for(uint32_t i=0;i<CTL;i++)assert(abs(fx_left[i])<=131068 && abs(fx_right[i])<=131068);
    }
    for(uint32_t block=0;block<512u/CTL+2u;block++){
        memset(fx_left,0,sizeof fx_left);memset(fx_right,0,sizeof fx_right);fx_chorus(&f,mem,CTL,step,32768);
    }
    for(uint32_t i=0;i<CTL;i++)assert(!fx_left[i] && !fx_right[i]);
    ui_power_on();
    for(uint32_t slot=0;slot<FX_SLOTS;slot++){
        uint32_t b=P_FXTYPE+slot*FX_PARAMS,g=G_MFXTYPE+slot*FX_PARAMS;
        trk[0].p[b]=song.g[g]=FX_CHORUS;trk[0].p[b+1]=100;
        assert(str_eq(track_desc(&trk[0],b+2)->label,"RATE") && track_desc(&trk[0],b+2)->fmt==F_LFOHZ);
        assert(str_eq(track_desc(&trk[0],b+3)->label,"DEPTH"));
        assert(str_eq(global_desc(g+2)->label,"RATE") && global_desc(g+2)->fmt==F_LFOHZ);
        assert(str_eq(global_desc(g+3)->label,"DEPTH"));
    }
    project_t q;project_store_t p;project_capture(&q);assert(proj_pack(&p,&q) && proj_import(&proj_scratch,&p,sizeof p));
    template_save();memset(trk[0].p+P_FXTYPE,0,FX_SLOTS*FX_PARAMS*sizeof(int16_t));memset(song.g+G_MFXTYPE,0,FX_SLOTS*FX_PARAMS*sizeof(int16_t));template_load();
    int32_t b[CTL]={0};fx_bus_start(CTL);fx_bus_join();assert(fx_bus_result);
    for(uint32_t slot=0;slot<FX_SLOTS;slot++){
        assert(trk[0].p[P_FXTYPE+slot*FX_PARAMS]==FX_OFF && song.g[G_MFXTYPE+slot*FX_PARAMS]==FX_CHORUS);
        assert(resource[RES_FX0+slot].size==FX_CHORUS_BYTES);
        trk[0].p[P_FXTYPE+slot*FX_PARAMS]=song.g[G_MFXTYPE+slot*FX_PARAMS]=FX_OFF;
    }
    for(uint32_t k=0;k<24;k++){fx_bus_start(CTL);fx_bus_join();}
    for(uint32_t slot=0;slot<FX_SLOTS;slot++)assert(!resource[RES_FX0+slot].size && !resource[RES_FX0+slot].size);
    puts("Chorus: full-band independent stereo, bounded hot inputs, exact silent tail, 2 KiB/slot, RATE/DEPTH, insert/send persistence and bypass release: OK");
}
static uint64_t tone_energy(uint32_t type,int tone,uint32_t hz)
{
    ui_power_on();reset_fx();track_t *t=&trk[0];
    t->p[P_FXTYPE]=type;t->p[P_FXMIX]=127;t->p[P_FXRATE]=70;t->p[P_FXCHAR]=60;t->p[P_FXTONE]=tone;
    int32_t b[CTL]={0};assert(fx_kernel_process(t,b,CTL));fx_state[0].mix=32768;
    uint32_t phase=0,inc=(uint32_t)((uint64_t)hz*0x100000000ull/FS);uint64_t energy=0;
    for(uint32_t frame=0;frame<3u*FS/CTL;frame++){
        for(uint32_t i=0;i<CTL;i++){phase+=inc;b[i]=2000*sine_i(phase)/32768;}
        assert(fx_kernel_process(t,b,CTL));
        for(uint32_t i=0;i<CTL;i++){
            assert(abs(fx_left[i])<32768 && abs(fx_right[i])<32768);
            if(frame>=FS/CTL)energy+=(int64_t)fx_left[i]*fx_left[i]+(int64_t)fx_right[i]*fx_right[i];
        }
    }
    return energy;
}
static void tone_test(void)
{
    /* TONE's two sides: a low cut 150 .. 1280 Hz above 0, a high cut 2640 .. 120 Hz below, FULL at 0; ECHO keeps a
     * 100 Hz low cut of its own, ROOM and REVERB 150 Hz (FULL, DARK) */
    assert(!fx_tone_hz(0) && fx_tone_hz(1)==150 && fx_tone_hz(FX_TONE_MAX)==1280 && fx_tone_hz(-1)==2640 && fx_tone_hz(-FX_TONE_MAX)==120);
    for(int v=2;v<=FX_TONE_MAX;v++)assert(fx_tone_hz(v)>=fx_tone_hz(v-1) && fx_tone_hz(-v)<fx_tone_hz(-v+1));
    assert(fx_low_cut_hz(FX_ROOM,-20)==150 && fx_low_cut_hz(FX_ROOM,1)==150 && fx_low_cut_hz(FX_ROOM,24)==313);
    const int thin=24,edge=40,dark=-39;assert(fx_tone_hz(thin)==313 && fx_tone_hz(edge)==605 && fx_tone_hz(dark)==516);
    for(uint32_t type=FX_ROOM;type<=FX_ROOM;type++){
        uint64_t bass=tone_energy(type,0,40),cut=tone_energy(type,edge,40);
        uint64_t mids=tone_energy(type,0,2000),preserved=tone_energy(type,edge,2000);
        assert(cut<bass/12u); /* >10 dB less sub-bass at 605 Hz than at the floor */
        assert(preserved>mids*8u/10u && preserved<mids*12u/10u);
        assert(tone_energy(type,thin,40)<bass && tone_energy(type,FX_TONE_MAX,40)<bass/32u);
        uint64_t at_cut=tone_energy(type,edge,605),reference=tone_energy(type,0,605);
        assert(at_cut>reference*4u/10u && at_cut<reference*6u/10u); /* -3 dB (the floor's own loss there: ~0.3 dB) */
        uint64_t bright=tone_energy(type,0,2000),darker=tone_energy(type,dark,2000);
        uint64_t body=tone_energy(type,0,200),warm=tone_energy(type,dark,200);
        assert(bright>10000u && darker<bright/10u && warm>body*7u/10u);
        assert(tone_energy(type,-FX_TONE_MAX,2000)<darker/8u);
        uint64_t at_high=tone_energy(type,dark,500),flat_high=tone_energy(type,0,500);
        assert(at_high>flat_high*4u/10u && at_high<flat_high*6u/10u);
        trk[0].p[P_FXMIX]=0;reset_fx();int32_t dry[CTL];
        for(uint32_t i=0;i<CTL;i++)dry[i]=(int32_t)i*91-8000;
        trk[0].p[P_FXTONE]=FX_TONE_MAX;assert(!fx_kernel_process(&trk[0],dry,CTL) && !resource[RES_FX0].size);
    }
    for(uint32_t type=FX_PHASER;type<FX_AUDIO_COUNT;type++)if(!fx_retired(type) && !fx_has_tone((int32_t)type))
        assert(tone_energy(type,0,1000)==tone_energy(type,FX_TONE_MAX,1000) && tone_energy(type,0,1000)==tone_energy(type,-FX_TONE_MAX,1000));
    /* A constant input exposes discontinuities when moving the cutoff, crossing FULL from one side to the other and
     * switching the high cut off; test both processing rates. */
    for(uint32_t rev=0;rev<1;rev++)for(uint32_t sweep=0;sweep<3;sweep++){
        ui_power_on();reset_fx();int16_t p[4]={FX_ROOM,127,60,64};fx_plan_t plan;
        int32_t previous=8000;uint32_t rate=FS/4u;
        for(uint32_t block=0;block<600;block++){
            int16_t thin_side=(int16_t)(block<100?0:block<200?thin:block<300?FX_TONE_MAX:block<400?1:0);
            trk[0].p[P_FXTONE]=sweep==0?thin_side:sweep==1?(int16_t)-thin_side:(int16_t)(block<100?-FX_TONE_MAX:block<300?FX_TONE_MAX:block<400?-1:0);
            fx_slot_prepare(&plan,0,p);assert(plan.active);
            for(uint32_t i=0;i<CTL;i++){
                int32_t y=fx_tone_input(plan.f,8000,1.0f/(0.010f*rate));
                assert(abs(y-previous)<200 && abs(y)<=9000);previous=y;
            }
        }
        /* the high cut gone exactly; the low cut stays at the floor, so the constant input has died away */
        assert(!plan.f->time_tone[1].b1 && !plan.f->time_tone[1].z1 && plan.f->time_tone[0].a2==(float)fx_floor_hz(p[0]) && abs(previous)<100);
    }
    /* Each slot's TONE targets its own track/slot and section, shared slots too. */
    ui_power_on();reset_fx();fx_plan_t plan;
    for(uint32_t ti=0;ti<=NPART;ti++)for(uint32_t slot=0;slot<FX_SLOTS;slot++){
        int16_t *p=ti==NPART?song.g:trk[ti].p,v=(int16_t)((ti+slot)&1u?1+ti*3+slot:-(int)(1+ti*3+slot));
        uint32_t base=(ti==NPART?G_MFXTYPE:P_FXTYPE)+slot*FX_PARAMS,tone=(ti==NPART?G_MFXTONE:P_FXTONE)+slot;
        p[base]=FX_ROOM;p[base+1]=127;p[tone]=v;
        if(ti!=NPART)continue;
        fx_slot_prepare(&plan,slot,p+base);
        assert(plan.active && plan.f->time_tone[0].a2==(float)fx_low_cut_hz(FX_ROOM,v) && plan.f->time_tone[1].a2==(v<0?(float)fx_tone_hz(v):0.0f));
    }
    project_t q;project_store_t wire;project_capture(&q);assert(proj_pack(&wire,&q) && proj_import(&proj_scratch,&wire,sizeof wire));
    for(uint32_t ti=0;ti<=NPART;ti++)for(uint32_t slot=0;slot<FX_SLOTS;slot++){
        int16_t v=(int16_t)((ti+slot)&1u?1+ti*3+slot:-(int)(1+ti*3+slot));
        assert((ti==NPART?proj_scratch.g[G_MFXTONE+slot]:proj_scratch.t[ti].p[P_FXTONE+slot])==v);
    }
    template_save();memset(trk[0].p+P_FXTONE,0,6);memset(song.g+G_MFXTONE,0,6);template_load();
    for(uint32_t slot=0;slot<FX_SLOTS;slot++){
        assert(trk[0].p[P_FXTONE+slot]==(slot&1u?1+(int)slot:-(1+(int)slot)));
        assert(song.g[G_MFXTONE+slot]==((NPART+slot)&1u?1+NPART*3+(int)slot:-(int)(1+NPART*3+slot)));
    }
    int16_t kept[FX_SLOTS];memcpy(kept,TSEL->p+P_FXTONE,sizeof kept);
    apply_preset_to(TSEL,0);sound_init_of(TSEL,eng_idx(TSEL->eng_req));undo_swap();assert(!memcmp(TSEL->p+P_FXTONE,kept,sizeof kept));
    /* the slot pages: KNOB 4 TONE (ECHO, ROOM, REVERB only), its words */
    song.g[G_MFXTONE]=-2;go_title("SEND FX 1");turn(EN_K4,1);assert(song.g[G_MFXTONE]>-2);
    song.g[G_MFXTONE]=0;go_title("SEND FX 1");turn(EN_K4,1);assert(song.g[G_MFXTONE]>0);
    for(uint32_t k=0;k<2;k++){                          /* the picture follows TONE while its knob turns, shared too */
        go_title("SEND FX 1");turn(EN_K4,1);frame();uint32_t drawn=pv.panel;
        turn(EN_K4,1);frame();assert(pv.panel!=drawn);
    }
    int16_t *vp;TSEL->p[P_FXTYPE]=FX_CHORUS;assert(!page_desc(&PAGES[page_titled("FX 1")],3,&vp) && !vp);
    TSEL->p[P_FXTYPE]=FX_OFF;assert(!page_visible(page_titled("FX 1")));TSEL->p[P_FXTYPE]=FX_ROOM;assert(page_visible(page_titled("FX 1")));
    char val[32];const char *unit;
    param_format(track_desc(TSEL,P_FXTONE),0,val,&unit);assert(str_eq(val,"FULL"));
    param_format(track_desc(TSEL,P_FXTONE),-5,val,&unit);assert(str_eq(val,"DARK"));
    param_format(global_desc(G_MFX3TONE),5,val,&unit);assert(str_eq(val,"THIN"));
    /* corrupt records */
    wire.raw[PROJ_TONE_WIRE]=0;uint32_t hash=proj_hash(wire.raw,sizeof wire-4u);memcpy(wire.raw+sizeof wire-4u,&hash,4);assert(!proj_import(&proj_scratch,&wire,sizeof wire));
    wire.raw[PROJ_TONE_WIRE]=128;hash=proj_hash(wire.raw,sizeof wire-4u);memcpy(wire.raw+sizeof wire-4u,&hash,4);assert(!proj_import(&proj_scratch,&wire,sizeof wire));
    wire.raw[PROJ_TONE_WIRE]=64;wire.raw[PROJ_LP_WIRE]=1;hash=proj_hash(wire.raw,sizeof wire-4u);memcpy(wire.raw+sizeof wire-4u,&hash,4);assert(!proj_import(&proj_scratch,&wire,sizeof wire));
    template_save();assert(tmpl_blob_valid((const uint8_t *)&tmpl,sizeof tmpl));
    tmpl.master[14]=0;assert(!tmpl_blob_valid((const uint8_t *)&tmpl,sizeof tmpl));
    tmpl.master[14]=64;tmpl.master[17]=1;assert(!tmpl_blob_valid((const uint8_t *)&tmpl,sizeof tmpl));
    /* frozen FUN20 / TPLJ (before the cuts): TONE FULL everywhere */
    const char *paths[]={"tests/fixtures/fun20-split.bin","tests/fixtures/tplj-split.bin"};uint8_t old[PROJ_STORE_SIZE];
    for(uint32_t k=0;k<2;k++){
        FILE *f=fopen(paths[k],"rb");assert(f);uint32_t n=fread(old,1,sizeof old,f);fclose(f);ui_power_on();
        if(k){assert(tmpl_take(old,n));template_load();}else{assert(proj_import(&proj_scratch,old,n));assert(!project_restore_runtime(&proj_scratch));}
        assert(song.g[G_FXMODE]==1 && song.g[G_MFXTYPE]==FX_REVERB && song.g[G_FXRETURN]==73 && TSEL->p[P_FXTYPE]==FX_OFF && TSEL->p[P_FXMIX]==91 && TSEL->p[P_FXSEND2]==71);
        assert(TSEL->p[P_E3]==clamp(42,track_desc(TSEL,P_E3)->min,track_desc(TSEL,P_E3)->max));
        for(uint32_t slot=0;slot<FX_SLOTS;slot++){assert(!song.g[G_MFXTONE+slot]);for(uint32_t ti=0;ti<NPART;ti++)assert(!trk[ti].p[P_FXTONE+slot]);}
    }
    /* frozen FUN21 / TPLK (written by 201cbbd: two cuts a slot): each slot's cut becomes the TONE nearest it, the low
     * cut when both were set (one at or under 150 Hz: the floor's now); routing, the sound and engine motion (E3
     * shifted with P_E0) stay */
    const char *cuts[]={"tests/fixtures/fun21-cuts.bin","tests/fixtures/tplk-cuts.bin"};
    for(uint32_t k=0;k<2;k++){
        FILE *f=fopen(cuts[k],"rb");assert(f);uint32_t n=fread(old,1,sizeof old,f);fclose(f);ui_power_on();
        if(k){assert(tmpl_take(old,n));template_load();}else{assert(proj_import(&proj_scratch,old,n));assert(proj_scratch.motion.count==1 && proj_scratch.motion.event[0].param==P_E3);assert(!project_restore_runtime(&proj_scratch));}
        assert(song.g[G_FXMODE]==1 && song.g[G_MFXTYPE]==FX_REVERB && song.g[G_MFX2TYPE]==FX_OFF && song.g[G_FXRETURN]==73);
        assert(TSEL->p[P_FXTYPE]==FX_OFF && TSEL->p[P_FXMIX]==91 && TSEL->p[P_FX2TYPE]==FX_OFF && TSEL->p[P_FXSEND2]==71);
        assert(TSEL->p[P_E3]==clamp(42,track_desc(TSEL,P_E3)->min,track_desc(TSEL,P_E3)->max));
        assert(TSEL->p[P_FXTONE]>0 && abs(fx_tone_hz(TSEL->p[P_FXTONE])-200)<10);     /* LO CUT 200 Hz */
        assert(TSEL->p[P_FX2TONE]<0 && abs(fx_tone_hz(TSEL->p[P_FX2TONE])-500)<20);   /* HI CUT 500 Hz */
        assert(TSEL->p[P_FX3TONE]<0 && abs(fx_tone_hz(TSEL->p[P_FX3TONE])-1300)<40);  /* both, LO CUT 80 Hz: the floor's, HI CUT 1300 Hz */
        assert(song.g[G_MFXTONE]>0 && abs(fx_tone_hz(song.g[G_MFXTONE])-300)<10);     /* shared LO CUT 300 Hz */
        assert(song.g[G_MFX2TONE]<0 && abs(fx_tone_hz(song.g[G_MFX2TONE])-1000)<20);  /* shared HI CUT 1 kHz */
        assert(!song.g[G_MFX3TONE] && !trk[1].p[P_FXTONE]);
    }
    puts("FX tone: bass/treble rejection, -3 dB cutoffs, smooth edits across FULL, per-slot editor/routing, persistence and frozen FUN20/TPLJ, FUN21/TPLK imports: OK");
}
static void grouped_fx_test(void)
{
    static const uint8_t order[] = {FX_OFF, FX_ROOM, FX_REVERB, FX_WIDTH, FX_PHASER, FX_CHORUS,
        FX_FLANGER, FX_PEDAL_PHASER, FX_PEDAL_CHORUS, FX_SOFT, FX_CRUSH, FX_FORMANT, FX_TAPE, FX_SPIT,
        FX_MIDIDELAY, FX_MIDIVERB};
    ui_power_on();
    for (uint32_t shared = 1; shared < 2; shared++) {
        const param_desc_t *d = shared ? &GP[G_MFXTYPE] : &TP[P_FXTYPE];
        uint32_t n = NELEM(order) - (shared ? 2u : 0u);
        assert(enum_rows(d) == n);
        for (uint32_t i = 0; i < n; i++) {
            assert(enum_value(d, i) == order[i] && enum_rank(d, order[i]) == i);
            assert(fx_step(d, order[i], 1) == order[i + (i + 1u < n)]);
            assert(fx_step(d, order[i], -1) == order[i - (i > 0)]);
        }
        assert(fx_step(d, FX_HARD, 0) == FX_HARD && fx_step(d, FX_FUZZ, 1) == FX_CRUSH);
        go_title(shared ? "SHARED" : "FX");
        fx_rack_row[shared] = 0;
        vlist_open(ui.page, 0);assert(vl.sel == FX_ROOM);
        turn(EN_K1, 1);assert(vl.sel == FX_PHASER);   /* Space -> Motion */
        turn(EN_K2, 1);assert(vl.sel == FX_CHORUS);
        press(B_OCTDN);assert(!pop.on);              /* cancel leaves the empty slot alone */
        int16_t *p = shared ? song.g + G_MFXTYPE : TSEL->p + P_FXTYPE;
        assert(!p[0]);
        vlist_open(ui.page, 0);
        for (uint32_t f = 0; f < 8; f++) turn(EN_K1, 1);
        assert(vl.sel == (shared ? FX_FORMANT : FX_MIDIDELAY));
        turn(EN_K2, 1);
        assert(vl.sel == (shared ? FX_TAPE : FX_MIDIVERB));
        press(B_OCTUP);assert(!pop.on && p[0] == (shared ? FX_TAPE : FX_MIDIVERB));
        /* Every slot's SHAPE edits its type, retaining the hidden signed tone
         * and all other controls. SHAPE's names use absolute stored IDs. */
        for (uint32_t slot = 0; slot < FX_SLOTS; slot++) {
            int16_t *type = p + slot * FX_PARAMS;
            int16_t *tone = shared ? &song.g[G_MFXTONE + slot] : &TSEL->p[P_FXTONE + slot];
            *type = FX_SOFT;*tone = -31;
            type[1] = 91;type[2] = 53;type[3] = 87;
            fx_go(fx_slot_page(shared, slot));
            int16_t *vp;const param_desc_t *shape = page_desc(cur_page(), 3, &vp);
            assert(vp == type && enum_rows(shape) == 4);
            for (uint32_t s = 0; s < 4; s++) {
                char value[8];const char *unit;
                assert(*type == FX_SOFT + s && enum_value(shape, s) == *type && enum_rank(shape, *type) == s);
                assert(str_eq(enum_name(shape, *type), N_SHAPE[*type]));
                param_format(shape, *type, value, &unit);assert(str_eq(value, N_SHAPE[*type]));
                edit_param(3, 1);
            }
            assert(*type == FX_FUZZ);edit_param(3, -99);assert(*type == FX_SOFT);
            assert(*tone == -31 && type[1] == 91 && type[2] == 53 && type[3] == 87);
        }
    }
    puts("FX redesign: grouped order, family browser, cancel/confirm, shared exclusion and all-slot DRIVE shapes: OK");
}
static void rack_test(void)
{
    ui_power_on();char chip[16];
    press(B_FX);assert(!ui.home && cur_page()->graph==GR_FXRACK && cur_page()->scope==SC_TRACK && str_eq(cur_page()->title,"FX"));
    for(uint32_t i=0;i<NPAGES;i++)if(PAGES[i].fam==FAM_FX && PAGES[i].graph!=GR_FXRACK)assert(!page_visible(i));   /* (all empty) */
    fx_rack_row[0]=0;turn(EN_K1,1);assert(TSEL->p[P_FXTYPE]!=FX_OFF);turn(EN_K4,5);assert(TSEL->p[P_FXMIX]>0);
    for(uint32_t k=0;k<6;k++)turn(EN_K2,1);
    assert(fx_rack_row[0]==3);for(uint32_t k=0;k<6;k++)turn(EN_K2,-1);
    assert(fx_rack_row[0]==0);
    TSEL->p[P_FXTYPE]=FX_MIDIDELAY;press(B_OCTUP);assert(cur_page()->id[0]==P_FXMIX);
    head_chip_text(chip);assert(str_eq(chip,"NOTE ECHO"));
    press(B_OCTDN);assert(str_eq(cur_page()->title,"FX") && fx_rack_row[0]==0);
    turn(EN_K2,1);press(B_OCTUP);assert(pop.on==POP_LIST);                         /* an empty slot: the list */
    turn(EN_K2,1);turn(EN_K2,1);press(B_OCTUP);assert(pop.on==POP_NONE && TSEL->p[P_FX2TYPE]==FX_MIDIDELAY);
    press(B_OCTUP);assert(cur_page()->id[0]==P_FX2MIX);head_chip_text(chip);assert(str_eq(chip,"NOTE ECHO"));
    press(B_FX);assert(str_eq(cur_page()->title,"FX"));                            /* FX from a slot: the rack */
    turn(EN_SELECT,1);assert(cur_page()->id[0]==P_FXMIX);turn(EN_SELECT,1);assert(cur_page()->id[0]==P_FX2MIX);
    turn(EN_SELECT,1);assert(str_eq(cur_page()->title,"SHARED"));                  /* (slot 3 empty: none) */
    press(B_OCTDN);assert(str_eq(cur_page()->title,"FX") && fx_rack_row[0]==3);
    TSEL->p[P_FXSEND]=10;turn(EN_K4,1);assert(TSEL->p[P_FXSEND]>10);              /* the shared row: SEND */
    press(B_OCTUP);assert(str_eq(cur_page()->title,"SHARED"));
    fx_rack_row[1]=0;turn(EN_K1,1);assert(song.g[G_MFXTYPE]!=FX_OFF);song.g[G_MFXTYPE]=FX_ROOM;
    song.g[G_MFXMIX]=100;turn(EN_K4,-1);assert(song.g[G_MFXMIX]<100);             /* CHAIN: the slot's MIX */
    song.g[G_FXMODE]=1;TSEL->p[P_FXSEND]=30;turn(EN_K4,1);assert(TSEL->p[P_FXSEND]>30 && song.g[G_MFXMIX]<100);   /* SPLIT */
    turn(EN_K3,-1);assert(!song.g[G_FXMODE]);turn(EN_K3,1);assert(song.g[G_FXMODE]==1);   /* KNOB 3: ROUTE */
    {int16_t *vp;const param_desc_t *d=page_desc(cur_page(),2,&vp);assert(d && vp==&song.g[G_FXMODE] && str_eq(d->label,"ROUTE"));
     go_title("FX");assert(!page_desc(cur_page(),2,&vp));go_title("SHARED");}                 /* (FX's KNOB 3: none) */
    press(B_OCTUP);assert(cur_page()->scope==SC_GLOBAL && cur_page()->id[0]==G_MFXMIX);head_chip_text(chip);assert(str_eq(chip,"ROOM"));
    press(B_OCTDN);assert(str_eq(cur_page()->title,"SHARED") && fx_rack_row[1]==0);
    press(B_OCTDN);assert(str_eq(cur_page()->title,"FX"));press(B_OCTDN);assert(ui.home);
    /* SHARED's sheet: Clear asks, then every shared slot is off */
    press(B_FX);fx_rack_row[0]=3;press(B_OCTUP);fx_rack_row[1]=3;press(B_OCTUP);assert(pop.on==POP_SHEET && pop.rows==SHEET_SHARED);
    turn(EN_K2,1);press(B_OCTUP);assert(ui.confirm==CF_CLEAR_SHARED);ui_confirm_accept();
    assert(!song.g[G_MFXTYPE] && !song.g[G_MFX2TYPE] && !song.g[G_MFX3TYPE] && song.g[G_MFXMIX]==GP[G_MFXMIX].def);
    /* the editor empties the slot shown: its rack */
    TSEL->p[P_FXTYPE]=FX_MIDIDELAY;go_title("FX 1");TSEL->p[P_FXTYPE]=FX_OFF;frame();assert(str_eq(cur_page()->title,"FX"));
    puts("FX rack: rows, effect list, slot pages named after their effect, OCT+/OCT-/FX/SELECT navigation, SHARED, SPLIT sends and its sheet: OK");
}
int main(int argc,char **argv)
{
    /* Full PCM from f1be889, before the mutually exclusive history union.
     * A lifetime/offset regression must not silently change wet audio. */
    static const uint64_t original_pcm[FX_COUNT]={
        0xdcbb3a74d3a1ba7bull,0x31f2c466625c315full,0xd6837fa20c478664ull,
        0x9582f216361a2b8bull,0xabf785bd7bd51881ull,0xddbbf39f09c8baa3ull,
        0x7705dc943db0ff6bull,0x5cc781ef5a3621a3ull,0xef747476fc21a401ull,
        0xd4c3687f410daf9cull
    };
    legacy();frozen_three_slots();send_return();separate_sends();fx_chain_test();rate_and_pan();tape_smooth();chorus_test();tone_test();grouped_fx_test();rack_test();
    uint64_t dry=fx_signal(FX_OFF,60,64,0,0);
    assert(dry==original_pcm[FX_OFF]);
    for(uint32_t mode=FX_PHASER;mode<FX_AUDIO_COUNT;mode++){
        if(fx_retired(mode))continue;
        assert(fx_signal(mode,60,64,0,0)==dry);
        uint64_t a=fx_signal(mode,60,64,127,0);assert(a!=dry && a==fx_signal(mode,60,64,127,0));
        if(mode!=FX_REVERB && a!=original_pcm[mode])fprintf(stderr,"%s Classic PCM: %llx, expected %llx\n",N_FX[mode],(unsigned long long)a,(unsigned long long)original_pcm[mode]);
        if(mode!=FX_REVERB)assert(a==original_pcm[mode]);
        assert(a!=fx_signal(mode,127,64,127,0));assert(a!=fx_signal(mode,60,127,127,0));
        for(int rate=0;rate<=127;rate+=127)for(int cha=0;cha<=127;cha+=127)fx_signal(mode,rate,cha,127,0);
        if(argc>1){char path[256];snprintf(path,sizeof path,"%s/%s.wav",argv[1],N_FX[mode]);fx_signal(mode,60,64,100,path);}
        printf("%s: deterministic, audible controls, bounded extremes, zero-mix bypass: OK\n",N_FX[mode]);
    }
    /* Time tails survive voice completion, bypass releases their allocation. */
    for(uint32_t mode=FX_ROOM;mode<=FX_ROOM;mode++){
        reset_fx();track_t *t=&trk[0];t->p[P_FXTYPE]=(int16_t)mode;t->p[P_FXMIX]=127;t->p[P_FXRATE]=0;
        t->p[P_FXCHAR]=80;fx_state[0].mix=32768;int32_t b[CTL]={20000};
        assert(fx_kernel_process(t,b,CTL));uint64_t tail=0;
        for(uint32_t k=0;k<1500;k++){memset(b,0,sizeof b);assert(fx_kernel_process(t,b,CTL));for(uint32_t i=0;i<CTL;i++)tail+=(uint32_t)abs(fx_left[i]);}
        assert(tail>1000);t->p[P_FXMIX]=0;
        for(uint32_t k=0;k<16;k++)fx_kernel_process(t,b,CTL);assert(!resource[RES_FX0].size);
    }
    /* Exhaust all usable banks: fail dry, with no per-block allocation retry. */
    reset_fx();for(uint32_t id=0;id<RES_COUNT;id++)resource_release(id);
    for(uint32_t id=0;id<RES_FX0;id++)assert(id==4u || resource_get(id,id==0?163840u:id==1?20480u:id==2?4096u:2048u));
    int32_t b[CTL]={0};
    for(uint32_t mode=0;mode<2;mode++){
        reset_fx();trk[0].p[P_FXTYPE]=mode?FX_CHORUS:FX_ROOM;trk[0].p[P_FXMIX]=127;
        assert(!fx_kernel_process(&trk[0],b,CTL));uint32_t fail=resource_failures;assert(fx_state[0].failed);
        for(uint32_t k=0;k<100;k++)assert(!fx_kernel_process(&trk[0],b,CTL));assert(resource_failures==fail);
    }
    for(uint32_t id=0;id<RES_COUNT;id++)resource_release(id);
    ui_power_on();press(B_FX);assert(!ui.home && cur_page()->fam==FAM_FX && !ui.layer);
    fx_rack_row[0]=0;turn(EN_K1,1);turn(EN_K1,1);assert(TSEL->p[P_FXTYPE]==FX_MIDIDELAY);turn(EN_K4,50);assert(TSEL->p[P_FXMIX]>0);
    project_t q;project_capture(&q);project_store_t p;assert(proj_pack(&p,&q));
    assert(proj_import(&proj_scratch,&p,sizeof p));assert(proj_scratch.t[0].p[P_FXTYPE]==FX_MIDIDELAY);
    frames(400);press(B_OCTUP);assert(cur_page()->id[0]==P_FXMIX);press(B_OCTUP);assert(pop.on==POP_SHEET);
    press(B_OCTDN);assert(!pop.on);press(B_OCTDN);assert(str_eq(cur_page()->title,"FX"));press(B_OCTDN);assert(ui.home);
    /* Four live tracks through the actual mixer, then mute a sounding insert. */
    ui_power_on();int32_t out[CTL*2];
    for(uint32_t k=0;k<NPART;k++){
        set_engine_of(&trk[k],ENGI_FM6);apply_preset_to(&trk[k],8);
        trk[k].p[P_FXTYPE]=(int16_t)(FX_PHASER+k);trk[k].p[P_FXMIX]=100;trk_note_on(&trk[k],48+k*7u,100);
    }
    for(uint32_t k=0;k<256u;k++){
        mix_block(out,CTL);for(uint32_t i=0;i<CTL*2u;i++)assert(out[i]>=-32767 && out[i]<=32767);
    }
    for(uint32_t k=0;k<NPART;k++)trk[k].p[P_MUTE]=1;
    for(uint32_t k=0;k<2048u;k++)mix_block(out,CTL);
    for(uint32_t i=0;i<CTL*2u;i++)assert(abs(out[i])<=1);
    puts("FX: tails, allocator exhaustion/recovery, FX button/knobs/Esc and project round trip: OK");return 0;
}
