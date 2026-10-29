/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Real sequencer/recording sources at the proposed capacity. This deliberately
 * excludes project persistence and the UI: neither is enabled at 16 x 128 yet. */
#define NSTEP 128u
#define NPAT 16u
#define NMIDI 0u                               /* (the parts only: 16 x 128 x 8 tracks outgrow the 64 take owners) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

static void fixture(void)
{
    host_tracks_init();song.ntrk=NPART;song.sel=0;song.rec=0;
    for(uint32_t k=0;k<NPART;k++){
        host_preset(&trk[k],ENGI_FM6,0);
        pattern_switch(&trk[k],15);
        trk[k].p[P_SLEN]=NSTEP;
        for(uint32_t s=0;s<NSTEP;s++){
            step_t *v=&trk[k].step[s];memset(v,0,sizeof *v);
            v->time=ST_NOTE;v->flags=SF_RECORDED;v->n=1;v->note[0]=60;v->vel=100;
        }
    }
}
static void queues_and_positions(void)
{
    fixture();
    for(uint32_t i=0;i<RECORD_MAX;i++){
        uint32_t k=i%NPART,step=i/NPART%NSTEP;
        recorded_note_t r={.on=60000,.duration=(uint16_t)(1000+i),.note7=(uint8_t)(i%128u),.vel=100};
        recorded_set_owner(&r,k*NPAT+15u);recorded_set_step(&r,step,1,step==127u);
        recording_restore_note(&trk[k],i,r);
        assert(recording_valid(&recording[i]));
        assert(recording_view(&trk[k],&recording[i])==(step+1u)%128u);
        assert(recording_active(&trk[k],i));
    }
    recording_reindex();
    for(uint32_t k=0;k<NPART;k++){
        uint32_t count=0;
        for(uint32_t i=recording_head[k*NPAT+15u];i<RECORD_MAX;i=recording_next[i])count++;
        assert(count==RECORD_MAX/NPART);
    }
    for(uint32_t i=0;i<RECORD_MAX;i++)recording_fire(&trk[i%NPART],i,RECORD_UNIT,0);
    for(uint32_t i=0;i<RECORD_MAX;i++){
        assert(recording_sounding(i));
        assert(recording_queue_view(i)==(i/NPART+1u)%128u);
    }
    /* Reindex sounding notes, then remove upper-half overview ownership.
     * A lost seventh bit would cancel/watch a different cell here. */
    recording_reindex();
    for(uint32_t k=0;k<NPART;k++){
        assert(recording_heap_count[k]==RECORD_MAX/NPART);
        assert(recording_watch[k][0]==2 && recording_watch[k][64]==2 && recording_watch[k][127]==2);
        trk[k].step[127].flags=0;recording_queue_tick(&trk[k],1);
        assert(recording_heap_count[k]==RECORD_MAX/NPART-2u);
    }
    for(uint32_t i=0;i<RECORD_MAX;i++)assert(recording_sounding(i)==((i/NPART+1u)%128u!=127u));
    /* Dense heap cancellation clears ownership in every upper cell. */
    for(uint32_t k=0;k<NPART;k++){
        recording_queue_tick(&trk[k],3000);
        assert(!recording_heap_count[k]);
        for(uint32_t s=0;s<NSTEP;s++)assert(!recording_watch[k][s]);
    }
}
static void edits_and_cache(void)
{
    fixture();
    recorded_note_t r={.on=100,.duration=25000,.note7=60,.vel=100};
    recorded_set_owner(&r,63);recorded_set_step(&r,127,0,0);recorded_set_exponent(&r,7);
    const uint32_t places[4]={127,64,95,96};
    for(uint32_t i=0;i<4;i++){
        recorded_set_step(&r,places[i],0,0);recording_restore_note(&trk[3],i,r);
    }
    recording_reindex();
    for(uint32_t i=0;i<4;i++){
        trk[3].seq_idx=(uint16_t)places[i];
        assert(recording_step_skip(&trk[3],&trk[3].step[places[i]],0)&1u);
    }
    r=recording[2];recorded_set_note(&r,61);recording_unlink(2);recording_restore_note(&trk[3],2,r);
    trk[3].seq_idx=95;
    assert(!(recording_step_skip(&trk[3],&trk[3].step[95],0)&1u));
    assert(!recording_copy(&trk[3],15,&trk[3],0,1));recording_reindex();
    int found=0;
    for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel && recorded_owner(&recording[i])==48u){
        assert(recorded_step(&recording[i])>=64 && recorded_exponent(&recording[i])==7);
        assert(!recorded_gate(&recording[i]));found++;
    }
    assert(found==4);
    /* A bank/length cache key must distinguish bank 8 from a one-step LEN
     * change; the old key's three-bit shift collided for 16-bank projects. */
    trk[3].pattern=0;trk[3].p[P_SLEN]=127;uint32_t key=recording_pitch_current(&trk[3]);
    trk[3].pattern=8;trk[3].p[P_SLEN]=126;assert(key!=recording_pitch_current(&trk[3]));
    recorded_note_t out;
    recording_quant_position(&r,127u*1000u+750u,1000,1000,128,&out);
    assert(recorded_owner(&out)==63 && recorded_step(&out)==127 && recorded_wrapped(&out));
    assert(recording_view(&trk[3],&out)==0 && recorded_exponent(&out)==7 && !recorded_gate(&out));
    recording_drop(&trk[3],63);recording_reindex();assert(recording_head[63]==RECORD_MAX);
}
static void capture_and_double(void)
{
    fixture();track_t *t=&trk[3];uint32_t period=seq_div_samples((uint32_t)t->p[P_SDIV]);
    t->seq_idx=127;t->seq_pos=step_samples(t,period,127)*3u/4u;
    rec_note(t,60,99);recording_reindex();
    uint32_t i=recording_head[63];assert(i<RECORD_MAX);
    assert(recorded_owner(&recording[i])==63 && recorded_step(&recording[i])==127);
    assert(recorded_rounded(&recording[i]) && recorded_wrapped(&recording[i]));
    assert(recording_view(t,&recording[i])==0);
    recording_run[i].held=700000;rec_release(t,60);
    assert(recorded_owner(&recording[i])==63 && recorded_exponent(&recording[i])>0);
    assert(recorded_step(&recording[i])==127 && !recorded_gate(&recording[i]));
    recording_reset();
    recorded_note_t r={.duration=1000,.vel=100,.note7=60};
    recorded_set_owner(&r,63);recorded_set_step(&r,63,1,1);
    recording_restore_note(t,0,r);recording_reindex();
    assert(!recording_double(t,15,64,0) && !recording_double(t,15,64,1));
    recording_reindex();uint32_t count=0;
    for(i=recording_head[63];i<RECORD_MAX;i=recording_next[i]){
        if(recorded_step(&recording[i])==127){assert(recorded_wrapped(&recording[i]));count++;}
    }
    assert(count==1);
}
int main(void)
{
    queues_and_positions();edits_and_cache();capture_and_double();
    puts("16 x 128 recording fixture: 64 owners, precise positions, 1024 releases, copy, quantization and pitch cache passed");
    return 0;
}
