/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define RECORDING_NO_MAIN 1
#include "recording_test.c"
#if !MELODEE_RECORD_QUEUE
#error "This test exercises timestamped releases"
#endif
static void queue_fixture(uint32_t count)
{
    timing_reset(0,24000000u);seq_stop();song.rec=0;
    for(uint32_t tr=0;tr<NTRK;tr++){
        trk[tr].p[P_SLEN]=64;
        for(uint32_t s=0;s<NSTEP;s++){trk[tr].step[s].time=ST_NOTE;trk[tr].step[s].flags=SF_RECORDED;}
    }
    for(uint32_t i=0;i<count;i++){
        recorded_note_t r={0};r.duration=1000;r.note=(uint8_t)(i%128u);r.vel=100;r.step=(uint8_t)(i%64u);
        recording_restore_note(TSEL,i,r);
    }
    recording_reindex();
}
/* Every shared row is exactly once either a chunk some heap is using or free
 * on the stack past the taken mark. */
static int queue_chunks(void)
{
    uint8_t used[RECORDING_ROWS]={0};uint32_t taken=0;
    for(uint32_t tr=0;tr<NTRK;tr++){
        if(recording_heap_count[tr]>32u && recording_heap_chunk[tr][0]!=(int8_t)tr)return 0;
        for(uint32_t c=1;c<(recording_heap_count[tr]+31u)/32u;c++,taken++){
            uint32_t row=(uint32_t)(recording_heap_chunk[tr][c]+(int)c);
            if(row<NTRK || row>=RECORDING_ROWS || used[row]++)return 0;
        }
    }
    if(taken!=recording_heap_taken)return 0;
    for(uint32_t k=taken;k<RECORDING_SHARED;k++){
        uint32_t row=NTRK+(recording_heap_free[k]^k);
        if(row>=RECORDING_ROWS || used[row]++)return 0;
    }
    return 1;
}
static int queue_invariants(void)
{
    uint8_t seen[RECORD_MAX]={0};
    if(!queue_chunks())return 0;
    for(uint32_t tr=0;tr<NTRK;tr++){
        uint16_t refs[128]={0},watch[NSTEP]={0};uint32_t bits[2]={0};
        for(uint32_t at=0;at<recording_heap_count[tr];at++){
            uint32_t i=RQ(tr,at);
            if(i>=RECORD_MAX || seen[i]++ || !recording_sounding(i) ||
               (recording[i].owner&31u)/NPAT!=tr || (recording_heap_pos[i]&1023u)!=at ||
               (at && recording_before(i,RQ(tr,(at-1u)/2u))))return 0;
            uint32_t view=recording_heap_pos[i]>>10;
            watch[view]++;bits[view>>5]|=1u<<(view&31u);refs[recording_message[i]&127u]++;
        }
        if(memcmp(refs,recording_refs[tr],sizeof refs) || memcmp(watch,recording_watch[tr],sizeof watch) ||
           memcmp(bits,recording_watch_bits[tr],sizeof bits))return 0;
    }
    for(uint32_t i=0;i<RECORD_MAX;i++)if((seen[i]!=0)!=recording_sounding(i))return 0;
    return 1;
}
static int rollover_and_retrigger(void)
{
    queue_fixture(2);track_t *t=TSEL;
    recording[0].note=0;recording[1].note=127;recording[0].duration=recording[1].duration=100;
    recording_clock[0]=UINT32_MAX-99u;
    recording_fire(t,0,RECORD_UNIT,0);recording_fire(t,1,RECORD_UNIT,0);
    int bad=check("pitch 0/127 release tags and a zero deadline remain active",recording_run[0].left==0 && recording_sounding(0) && recording_message[0]==128 && recording_message[1]==255 && queue_invariants());
    recording_queue_tick(t,99);
    bad+=check("clock rollover preserves the final unit of both gates",recording_left(0)==1 && recording_left(1)==1 && recording_owns(t,0) && recording_owns(t,127));
    recording_flags[0]&=(uint8_t)~4u;recording_fire(t,0,RECORD_UNIT,0);
    recording_queue_tick(t,1);
    bad+=check("retrigger cancels its old release while the other pitch ends",recording_left(0)==99 && !recording_sounding(1) && recording_owns(t,0) && !recording_owns(t,127) && queue_invariants());
    recording_queue_tick(t,99);
    bad+=check("the replacement gate releases exactly once",!recording_sounding(0) && !recording_owns(t,0) && queue_invariants());
    return bad;
}
static int dense_cancellation(void)
{
    queue_fixture(RECORD_MAX);track_t *t=TSEL;
    /* All 1024 slots on one track, with many overlapping equal pitches and
     * unsorted deadlines. Exercise indexed deletion at every heap depth. */
    uint32_t random=0x0f721a39u;
    for(uint32_t i=0;i<RECORD_MAX;i++){
        random=random*1664525u+1013904223u;
        recording[i].duration=(uint16_t)(1000u+random%60000u);recording_fire(t,i,RECORD_UNIT,0);
    }
    int ok=recording_heap_count[0]==RECORD_MAX && queue_invariants();
    for(uint32_t k=0;k<RECORD_MAX && ok;k++){
        uint32_t i=(k*197u)%RECORD_MAX;
        if(k&1u){recording_flags[i]&=(uint8_t)~4u;recording_fire(t,i,RECORD_UNIT,k%300u);}
        else recording_remove(t,i);
        if(k%31u==0u)recording_reindex();
        recording_queue_tick(t,1);ok=queue_invariants();
    }
    int bad=check("1024 overlapping events survive cancellation, retrigger and reindex",ok);
    t->p[P_SLEN]=1;recording_queue_tick(t,1);
    for(uint32_t i=0;i<RECORD_MAX;i++)if(recording_sounding(i) && (recording[i].step&63u)>=1u)ok=0;
    bad+=check("shortening LEN cancels buried releases immediately",ok && queue_invariants());
    t->step[0].flags=0;recording_queue_tick(t,1);
    bad+=check("overwriting an overview cancels all its remaining releases",!recording_heap_count[0] && queue_invariants());
    seq_stop();return bad;
}
static int changed_overview(void)
{
    queue_fixture(2);track_t *t=TSEL;
    recording_fire(t,0,RECORD_UNIT,0);recording_fire(t,1,RECORD_UNIT,0);
    /* Punch-out quantization moves a sounding event without changing its
     * stored gate. Its old overview cell may become a tie or empty cell. */
    recording[0].step=2;t->step[0].flags=0;recording_schedule[0].period=recording_schedule[0].ordered_period=0;
    recording_sync(t,RECORD_UNIT);
    int ok=recording_sounding(0) && recording_watch[0][2]==1 && !recording_watch[0][0] && queue_invariants();
    t->step[2].flags=0;recording_queue_tick(t,1);
    return check("quantization moves the ownership watch; deleting its new cell releases it",ok && !recording_sounding(0) && recording_sounding(1) && queue_invariants());
}
static int independent_tracks(void)
{
    queue_fixture(4);
    for(uint32_t tr=0;tr<NTRK;tr++){
        recorded_note_t r=recording[tr];recording_remove(TSEL,tr);r.owner=(uint8_t)(tr*NPAT);r.duration=(uint16_t)(tr+1u);
        recording_restore_note(&trk[tr],tr,r);recording_clock[tr]=UINT32_MAX-tr;
        recording_fire(&trk[tr],tr,RECORD_UNIT,0);
    }
    int ok=queue_invariants();recording_reindex();
    for(uint32_t tr=0;tr<NTRK;tr++){
        recording_queue_tick(&trk[tr],tr+1u);
        if(recording_sounding(tr) || !queue_invariants())ok=0;
    }
    return check("track clocks and rollover deadlines remain independent across reindex",ok);
}
/* share[tr] events owned by track tr, all sounding, with unsorted deadlines. */
static void spread_fixture(const uint32_t share[NTRK])
{
    recording_reset();queue_fixture(0);
    for(uint32_t tr=0,i=0;tr<NTRK;tr++)for(uint32_t k=0;k<share[tr];k++,i++){
        recorded_note_t r={0};r.duration=(uint16_t)(1000u+(i*7919u)%50000u);r.note=(uint8_t)(i%128u);r.vel=100;
        r.step=(uint8_t)(i%64u);r.owner=(uint8_t)recording_owner(&trk[tr]);
        recording_restore_note(&trk[tr],i,r);
    }
    recording_reindex();
    for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel)recording_fire(&trk[(recording[i].owner&31u)/NPAT],i,RECORD_UNIT,0);
}
/* Each block releases exactly the events whose deadline it reached, through
 * every chunk boundary, and the store is whole again once all have ended. */
static int drains_in_order(void)
{
    uint32_t deadline[RECORD_MAX];int ok=1;
    for(uint32_t i=0;i<RECORD_MAX;i++)deadline[i]=recording_run[i].left;
    for(uint32_t block=0;block<60 && ok;block++)for(uint32_t tr=0;tr<NTRK && ok;tr++){
        recording_queue_tick(&trk[tr],997u);
        for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel && (recording[i].owner&31u)/NPAT==tr &&
            recording_sounding(i)!=((int32_t)(deadline[i]-recording_clock[tr])>0))ok=0;
        ok=ok && queue_invariants();
    }
    for(uint32_t tr=0;tr<NTRK;tr++)if(recording_heap_count[tr])ok=0;
    return ok && !recording_heap_taken;
}
static int shared_chunks(void)
{
    static const uint32_t one[NTRK]={RECORD_MAX,0,0,0},even[NTRK]={256,256,256,256},tight[NTRK]={961,33,15,15};
    spread_fixture(one);
    int bad=check("1024 sounding notes on one track take its row and 31 shared chunks",
                  recording_heap_count[0]==RECORD_MAX && recording_heap_taken==31u && queue_invariants());
    bad+=check("one track's 1024 releases drain in deadline order across its chunks",drains_in_order());
    spread_fixture(even);
    bad+=check("256 notes on each track take 7 shared chunks each",recording_heap_taken==28u && queue_invariants());
    bad+=check("four tracks' releases drain in order and return every chunk",drains_in_order());
    spread_fixture(tight);
    bad+=check("the most chunk-hungry split of 1024 notes fits (31 shared chunks)",
               recording_heap_taken==31u && recording_heap_count[1]==33u && queue_invariants());
    /* Removals in random order: chunks leave from every depth and track. */
    uint16_t order[RECORD_MAX];uint32_t random=0x5eed1234u;int ok=1;
    for(uint32_t i=0;i<RECORD_MAX;i++)order[i]=(uint16_t)i;
    for(uint32_t i=RECORD_MAX;i-- > 1;){random=random*1664525u+1013904223u;uint32_t j=(random>>8)%(i+1u);uint16_t s=order[i];order[i]=order[j];order[j]=s;}
    for(uint32_t k=0;k<RECORD_MAX && ok;k++){
        uint32_t i=order[k];recording_remove(&trk[(recording[i].owner&31u)/NPAT],i);
        ok=queue_invariants();
    }
    bad+=check("random-order removals return every chunk to the free stack",ok && !recording_heap_taken &&
               !recording_heap_count[0] && !recording_heap_count[1] && !recording_heap_count[2] && !recording_heap_count[3]);
    /* Notes move between tracks while the store is full: chunks change hands
     * in an interleaved order, retriggers and reindexes included. */
    spread_fixture(one);
    for(uint32_t k=0;k<4096 && ok;k++){
        random=random*1664525u+1013904223u;
        uint32_t i=(random>>8)%RECORD_MAX,tr=(random>>20)%NTRK;
        if(k%3u==0u){recording_flags[i]&=(uint8_t)~4u;recording_fire(&trk[(recording[i].owner&31u)/NPAT],i,RECORD_UNIT,k%500u);}
        else{
            recorded_note_t r=recording[i];recording_remove(&trk[(r.owner&31u)/NPAT],i);
            r.owner=(uint8_t)recording_owner(&trk[tr]);r.duration=(uint16_t)(1000u+random%40000u);
            recording_restore_note(&trk[tr],i,r);recording_fire(&trk[tr],i,RECORD_UNIT,0);
        }
        if(k%127u==0u)recording_reindex();
        ok=queue_invariants();
    }
    uint32_t sounding=0;for(uint32_t i=0;i<RECORD_MAX;i++)sounding+=(uint32_t)recording_sounding(i);
    bad+=check("1024 notes moving between tracks keep every chunk accounted for",ok && sounding==RECORD_MAX);
    bad+=check("the moved notes still release in order and free the whole store",drains_in_order());
    seq_stop();return bad;
}
int main(void)
{
    int bad=rollover_and_retrigger()+dense_cancellation()+changed_overview()+independent_tracks()+shared_chunks();
    if(!bad)puts("Recording release queue tests passed");return bad!=0;
}
