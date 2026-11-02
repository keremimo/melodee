/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Ordered six-category matrix, real concurrent worker and real mixer.
 * Each assignment includes steady held voices, controls, release/retrigger,
 * spread and shared Chorus. Host elapsed time is not a device deadline. */
#define AUDIO_MATRIX_INCLUDE 1
#include "dual_core_test.c"
static const uint8_t matrix_engines[6]={ENGI_PROPHET,ENGI_FM6,ENGI_CZ,3,ENGI_DRUM,ENGI_DRUM};
static uint64_t matrix_hash;
static void matrix_setup(const uint32_t cat[4],uint32_t variant)
{
    memset(trk,0,sizeof trk);trk_voices_bind();memset(&song,0,sizeof song);eng_state_reset();
    memset(fm6_eff,0,sizeof fm6_eff);memset(p5_ready,0,sizeof p5_ready);cz_init();
    memset(mix_cache,0,sizeof mix_cache);memset(&mod,0,sizeof mod);memset(fx_state,0,sizeof fx_state);
    memset(midi_bend_q8,0,sizeof midi_bend_q8);memset(midi_bend_target,0,sizeof midi_bend_target);
    fx_bus_mix=fx_bus_target=fx_bus_pending=fx_bus_result=0;fx_epoch++;
    rng_state=0x12345678u;mod_seed=0x2545f491u;dith_r=DITH_SEED;vage=voice_kills=0;track_solo=0;
    lim_env=LIM_T;dc_l=dc_r=dce_l=dce_r=0;host_tracks_init();fm6_fn_reset();
    for(uint32_t p=0;p<NPART;p++){
        uint32_t c=cat[p];host_preset(&trk[p],matrix_engines[c],c==0?3:c==1?4:c==3?8:c==5?1:0);
        trk[p].p[P_VOICE]=V_POLY;trk[p].p[P_ALLOC]=0;trk[p].p[P_LEVEL]=40;
        if(c==0){uint8_t *b=p5_patch_of(&trk[p])->raw;b[P5_FILTER_REV]=variant&1u;b[P5_SYNC]=1;b[P5_POLY_FREQ]=b[P5_POLY_FILTER]=1;b[P5_POLY_B]=100;b[P5_SUSTAIN_AMP]=127;}
        if(c==1){uint8_t patch[FP_SIZE+1u];fm6_unpack(FM6_INIT,patch);patch[FP_ALG]=variant%32u;patch[FP_FB]=7;
            for(uint32_t k=0;k<6;k++){patch[k*FP_OP+FP_OL]=90;patch[k*FP_OP+FP_FC]=k+1;}
            fm6_put_patch(p,patch,1);FM6F(&trk[p])[FN_ENGINE]=variant%3u;}
        if(c==2)cz_patch_of(p)->raw[0]=3;
        if(c==3){trk[p].p[P_E0]=variant&1u;trk[p].p[P_E2]=(variant%3u)==0?SID_DETUNE:(variant%3u)==1?SID_SYNC:SID_RING;}
        for(uint32_t k=0;k<2;k++)trk_note_on(&trk[p],c>=4?(k?38:36):48+p*3+k*7,110);
        assert(active_voices(&trk[p])==2);
    }
}
static void matrix_render(const uint32_t cat[4],uint32_t variant,int parallel)
{
    audio_worker_online=1;serial_render=!parallel;matrix_setup(cat,variant);
    for(uint32_t block=0;block<96u;block++){
        int32_t out[CTL*2];
        if(block==16u)for(uint32_t p=0;p<4;p++){trk[p].p[P_SPRD]=42;trk[p].p[P_M1SRC]=MS_LFO;trk[p].p[P_M1DST]=MD_CUT;trk[p].p[P_M1AMT]=24;}
        if(block==32u)for(uint32_t p=0;p<4;p++)for(uint32_t k=0;k<2;k++)trk_note_off(&trk[p],cat[p]>=4?(k?38:36):48+p*3+k*7);
        if(block==40u)for(uint32_t p=0;p<4;p++)for(uint32_t k=0;k<2;k++)trk_note_on(&trk[p],cat[p]>=4?(k?38:36):48+p*3+k*7,100);
        if(block==56u){song.g[G_MFXTYPE]=FX_CHORUS;for(uint32_t p=0;p<4;p++)trk[p].p[P_FXSEND]=30;}
        if(block==72u)for(uint32_t p=0;p<4;p++){trk[p].p[P_E1]+=cat[p]==3?0:1;trk[p].p[P_LD_PIT]=4;}
        mix_block(out,CTL);
        assert(!audio_batch_count && AW_LOAD(&audio_worker.request)==AW_LOAD(&audio_worker.complete));
        assert(!resource_failures && !audio_worker.timeouts && !injected_faults);
        if(block<16u)for(uint32_t p=0;p<4;p++)assert(active_voices(&trk[p])==2);
        if(!parallel)memcpy(reference+block*CTL*2,out,sizeof out);
        else {if(memcmp(reference+block*CTL*2,out,sizeof out)){fprintf(stderr,"matrix mismatch %u/%u/%u/%u block %u\n",cat[0],cat[1],cat[2],cat[3],block);abort();}
            for(uint32_t i=0;i<CTL*2;i++)matrix_hash=matrix_hash*1099511628211ull^(uint32_t)out[i];}
    }
}
int main(void)
{
    pthread_t thread;assert(!pthread_create(&thread,0,worker_thread,0));while(!AW_LOAD(&audio_worker.ready))sched_yield();
    uint32_t cases=0;resource_failures=0;
    for(uint32_t a=0;a<6;a++)for(uint32_t b=0;b<6;b++)for(uint32_t c=0;c<6;c++)for(uint32_t d=0;d<6;d++){
        const uint32_t cat[4]={a,b,c,d};matrix_render(cat,cases,0);matrix_render(cat,cases,1);cases++;
    }
    assert(cases==1296u && audio_batch_pairs);
    assert(!pthread_cancel(thread));assert(!pthread_join(thread,0));
    printf("Audio matrix: 1296 ordered assignments, two voices per track, controls/releases/retriggers/spread/Chorus, serial/concurrent PCM identical; hash %llx: OK\n",(unsigned long long)matrix_hash);
    return 0;
}
