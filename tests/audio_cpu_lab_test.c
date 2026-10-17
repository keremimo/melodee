/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Real editor wire plus native recording/transport. HAL/audio timing is host
 * simulation; no hardware CPU or musical-input acceptance is implied. */
#define EDITOR_TEST_NO_MAIN 1
#define fm1_ticks() (0u)
#include "editor_test.c"
#include <assert.h>

static uint32_t control(uint32_t op,uint32_t mask)
{
    uint8_t a[]={(uint8_t)op,(uint8_t)mask};
    host_irq_nested=0;
    uint32_t n=request(101,a,sizeof a);
    assert(!host_irq_nested && !host_irq_masked); /* one coherent section */
#if MELODEE_AUDIO_PROFILE && MELODEE_AUDIO_BENCH
    assert(n==16u && host_wire[6]==1u);
    return host_wire[5];
#else
    assert(n==0u);return 2;
#endif
}
#if MELODEE_AUDIO_PROFILE && MELODEE_AUDIO_BENCH
static void prepare(void)
{
    reset();seq_stop();recording_reset();song.rec=song.rec_latch=0;
    memset(&midi_clock,0,sizeof midi_clock);song.g[G_CLOCK]=0;song.g[G_BPM]=120;
    for(uint32_t k=0;k<NTRK;k++){
        trk[k].p[P_SLEN]=4;trk[k].p[P_SDIV]=2;trk[k].p[P_SSWING]=0;trk[k].p[P_RECQ]=0;
        trk[k].p[P_QUANT]=0;trk[k].p[P_AMODE]=0;
        for(uint32_t s=0;s<NSTEP;s++){memset(&trk[k].step[s],0,sizeof(step_t));trk[k].step[s].time=ST_REST;}
    }
    settings_countin=0;audio_lab_guard=1;song.ntrk=NTRK;
}
static void note(uint32_t k,uint32_t pitch,uint32_t on)
{
    midi_enqueue((on?9u:8u)|((on?0x90u:0x80u)|k)<<8|pitch<<16|(on?100u:0u)<<24,1);
    events_block(0);
}
static uint32_t count(void)
{
    uint32_t n=0;for(uint32_t k=0;k<RECORD_MAX;k++)n+=recording[k].vel!=0;return n;
}
static void lifecycle(void)
{
    for(uint32_t quant=0;quant<2;quant++)for(uint32_t latch=0;latch<2;latch++){
        prepare();trk[0].p[P_RECQ]=quant?3:0;
        assert(!control(latch?3:4,1));assert(song.rec==1 && song.rec_latch==(latch?1:0));
        assert(!control(latch?4:3,1));assert(song.rec_latch==(latch?1:0)); /* retries cannot change the take */
        assert(!control(1,0));assert(!song.playing && transport_req==1);
        events_block(0);assert(song.playing && song.rec_pass==1 && !song.rec_wait && !chain_busy());
        seq_advance(700);note(0,60,1);seq_advance(1100);note(0,60,0);
        assert(count()==1 && recording[0].duration);
        uint32_t beat=seq_beat_samples(),loop=4u*(beat>>2);
        for(uint32_t n=0;n<loop+CTL;n+=CTL)seq_advance(CTL);
        assert(song.rec==(latch?1:0));
        /* Mid-loop arming preserves native WAIT LOOP, then punch-in at zero. */
        assert(!control(3,2));assert(song.rec_wait==2);
        note(1,67,1);
        for(uint32_t n=0;n<loop+CTL;n+=CTL)seq_advance(CTL);
        assert(!(song.rec_wait&2) && (song.rec_pass&2));
        note(1,67,0);seq_advance(300);note(1,69,1);seq_advance(800);
        assert(!control(2,0));events_block(0);assert(!song.playing && !seq_counting());
        assert(!control(5,15));assert(!song.rec && !song.rec_wait && !song.rec_pass && !song.rec_latch);
        assert(count()>=2);
        for(uint32_t k=0;k<RECORD_MAX;k++)if(recording[k].vel)assert(recording_valid(&recording[k]) && recording[k].duration);
        recorded_note_t saved[RECORD_MAX];memcpy(saved,recording,sizeof saved);
        project_capture(&proj_scratch);assert(bank_pack(proj_wire_u.raw,&proj_scratch,1));
        assert(bank_valid(proj_wire_u.raw,BANK_STORE_SIZE));
        recording_reset();assert(!project_restore_runtime(&proj_scratch));bank_restore(proj_wire_u.raw);
        /* Serializer groups owners; compare after its canonical restoration. */
        assert(count()>=2 && !memcmp(recording,saved,sizeof saved));project_capture(&proj_scratch);
        assert(!memcmp(recording,proj_scratch.recording,sizeof recording));
    }
}
#endif
int main(void)
{
    reset();
#if MELODEE_AUDIO_PROFILE && MELODEE_AUDIO_BENCH
    prepare();audio_lab_guard=0;
    uint8_t tune[]={5,112,46,0,0,0}; /* 6000, five seven-bit bytes */
    assert(request(98,tune,6)==7 && host_wire[5]==3 && audio_batch_service_allowance==AUDIO_BATCH_SERVICE_AUTO);
    audio_lab_guard=1;
    assert(request(98,tune,6)==13 && host_wire[5]==0 && audio_batch_service_allowance==6000);
    assert(request(98,tune,1)==13 && host_wire[5]==0);
    tune[5]=16;assert(request(98,tune,6)==7 && host_wire[5]==1 && audio_batch_service_allowance==6000);
    tune[5]=0;tune[1]=127;tune[2]=127;tune[3]=127;
    assert(request(98,tune,6)==7 && host_wire[5]==1 && audio_batch_service_allowance==6000);
    assert(request(98,tune,2)==7 && host_wire[5]==1);
    uint8_t automatic[]={5,127,127,127,127,15};
    assert(request(98,automatic,6)==13 && host_wire[5]==0 && audio_batch_service_allowance==AUDIO_BATCH_SERVICE_AUTO);
    audio_lab_guard=0;
    for(uint32_t op=0;op<=5;op++)assert(control(op,op>2?1:0)==3 && !transport_req && !song.rec);
    audio_lab_guard=1;
    for(uint32_t op=0;op<9;op++)for(uint32_t mask=0;mask<32;mask++)
        if(op>5 || mask>15 || (op<=2?mask!=0:mask==0))assert(control(op,mask)==1 && !transport_req && !song.rec);
    uint8_t bad[]={1,0,0};assert(request(101,bad,3)==16 && host_wire[5]==1);
    assert(request(101,bad,0)==16 && host_wire[5]==1);
    song.g[G_CLOCK]=1;assert(control(1,0)==3 && !transport_req);song.g[G_CLOCK]=0;
    chain.armed=1;assert(control(1,0)==3 && control(3,1)==3);assert(!control(2,0));events_block(0);
    momentary.active=1;assert(control(1,0)==3 && control(3,1)==3);momentary.active=0;
    /* Tracks not in use cannot be armed; cleanup disarm still accepts all. */
    song.ntrk=2;assert(control(3,4)==3 && control(4,8)==3 && control(3,7)==3 && !song.rec);
    assert(!control(4,3) && song.rec==3);assert(!control(5,15) && !song.rec);song.ntrk=NTRK;
    assert(!control(1,0));assert(control(1,0)==3);assert(!control(2,0));events_block(0);assert(!song.playing);
    settings_countin=1;assert(!control(3,1));assert(!control(1,0));events_block(0);
    assert(seq_counting() && !song.playing);assert(!control(2,0));events_block(0);assert(!seq_counting());
    lifecycle();
    puts("CPU lab controls: guarded wire validation, native count-in/WAIT/latched/one-pass/seal/roundtrip passed");
#else
    assert(control(1,0)==2 && !transport_req && !song.rec);
    uint8_t tune[]={5,112,46,0,0,0};
    assert(request(98,tune,6)==7);
#if MELODEE_AUDIO_PROFILE
    assert(host_wire[5]==1);
#else
    assert(host_wire[5]==2);
#endif
    puts("CPU lab controls absent without both profiling and benchmark support");
#endif
    return 0;
}
