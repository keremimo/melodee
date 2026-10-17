/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define RECORDING_NO_MAIN 1
#include "recording_test.c"

static struct { uint32_t held, left; uint8_t flags; } expected[RECORD_MAX];
static uint32_t expected_fraction;
static uint32_t test_random = 0x1745ab39u;
static uint32_t next_random(void)
{
    test_random ^= test_random << 13; test_random ^= test_random >> 17; test_random ^= test_random << 5;
    return test_random;
}
/* The original scan's state transition, independent of the new sorted index
 * and runtime list. Chance is 100% in these fixtures. Voice behavior is also
 * covered by the existing recording, MIDI, undo and note-editor suites. */
static void expected_fire(track_t *t, uint32_t i, uint32_t period, uint32_t late)
{
    if (expected[i].flags & 4u) return;
    expected[i].flags |= 4u;
    if ((expected[i].flags & 1u) || ((seq_hush >> trk_index(t)) & 1u)) return;
    uint32_t gate = (uint32_t)(((uint64_t)recording[i].duration << (recording[i].owner >> 5)) * period / RECORD_UNIT);
    if (!gate) gate = 1;
    expected[i].left = gate > late ? gate - late : 0;
}
static void expected_zero(track_t *t)
{
    uint32_t period=seq_div_samples(t->p[P_SDIV]);
    uint32_t phase=recording_prefix(t,period,t->seq_idx)+t->seq_pos;
    for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel && (recording[i].owner&31u)==recording_owner(t)){
        expected[i].flags &= (uint8_t)~4u;
        if(recording_active(t,i)){
            uint32_t on=recording_raw_on(t,&recording[i],period);
            if(on<=phase)expected_fire(t,i,period,phase-on);
        }
    }
}
static void expected_tick(track_t *t,uint32_t n)
{
    uint32_t period=seq_div_samples(t->p[P_SDIV]);
    uint32_t a=recording_prefix(t,period,t->seq_idx)+t->seq_pos,b=a+n;
    uint64_t ticks=(uint64_t)n*RECORD_UNIT+expected_fraction;
    uint32_t elapsed=(uint32_t)(ticks/period);expected_fraction=(uint32_t)(ticks%period);
    for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel && (recording[i].owner&31u)==recording_owner(t)){
        if(expected[i].flags&1u)expected[i].held=(uint32_t)clamp((int32_t)(expected[i].held+elapsed),0,8388607);
        if(expected[i].left)expected[i].left=!recording_active(t,i)||expected[i].left<=n?0:expected[i].left-n;
        if(!(expected[i].flags&4u)){
            uint32_t on=recording_raw_on(t,&recording[i],period);
            if(on>a && on<=b && recording_active(t,i))expected_fire(t,i,period,b-on);
        }
    }
}
static int state_matches(track_t *t)
{
    uint16_t refs[128]={0};
    for(uint32_t i=0;i<RECORD_MAX;i++){
        if(recording_run[i].held!=expected[i].held || recording_left(i)!=expected[i].left ||
           (recording_flags[i]&~2u)!=expected[i].flags)return 0;
        if(expected[i].left && recording_active(t,i))refs[recording[i].note]++;
    }
    if(recording_fraction[trk_index(t)]!=expected_fraction || memcmp(refs,recording_refs[trk_index(t)],sizeof refs))return 0;
#if MELODEE_RECORD_CURSOR
    uint8_t seen[RECORD_MAX]={0};uint32_t n=0;
    for(uint32_t i=recording_live_head[trk_index(t)];i<RECORD_MAX;i=recording_live_next[i]){
        if(++n>RECORD_MAX || seen[i] || !(recording_flags[i]&2u))return 0;
        seen[i]=1;
    }
    for(uint32_t i=0;i<RECORD_MAX;i++)if((recording_flags[i]&2u)!=seen[i]*2u)return 0;
#endif
    return 1;
}
static int oracle_scenario(uint32_t count,uint32_t div,int long_gates)
{
    timing_reset(0,24000000u);track_t *t=TSEL;song.rec=0;
    t->p[P_SLEN]=16;t->p[P_SDIV]=(int16_t)div;t->seq_idx=0;t->seq_pos=0;song.playing=1;
    for(uint32_t i=0;i<NSTEP;i++){memset(&t->step[i],0,sizeof t->step[i]);t->step[i].time=ST_NOTE;t->step[i].flags=SF_RECORDED;}
    memset(expected,0,sizeof expected);expected_fraction=0;
    for(uint32_t i=0;i<count;i++){
        recorded_note_t r={0};r.on=(uint16_t)next_random();r.duration=(uint16_t)(1u+next_random()%65535u);
        r.owner=(uint8_t)(long_gates?(i%8u)<<5:0u);
        r.step=(uint8_t)(next_random()%16u);r.note=(uint8_t)(48u+next_random()%12u);r.vel=(uint8_t)(60u+next_random()%68u);
        recording_restore_note(t,i,r);recording_flags[i]=(uint8_t)(i%13u==0u?1u:i%3u==0u?8u:0u);
        expected[i].flags=recording_flags[i];
    }
    recording_reindex();expected_zero(t);recording_zero(t);
    int ok=state_matches(t);
    for(uint32_t tick=0;tick<2048 && ok;tick++){
        if(tick==311 && long_gates){song.g[G_BPM]=173;t->p[P_SDIV]=3;}
        if(tick==513){t->p[P_SSWING]=61;t->p[P_RECQ]=4;}
        if(tick==701)t->step[5].flags=0;
        if(tick==901)t->step[5].flags=SF_RECORDED;
        if(tick==1025){
            uint32_t i=count/2u;recorded_note_t r=recording[i];r.on^=0x8000u;
            recording_remove(t,i);recording_restore_note(t,i,r);memset(&expected[i],0,sizeof expected[i]);
        }
        if(tick==1401)recording_reindex();
        uint32_t period=seq_div_samples(t->p[P_SDIV]),span=step_samples(t,period,t->seq_idx);
        uint32_t n=(tick%19u==0u?70000u:1u+next_random()%127u);
        if(t->seq_pos>=span){t->seq_pos=0;t->seq_idx=(uint16_t)((t->seq_idx+1u)%16u);if(!t->seq_idx){expected_zero(t);recording_zero(t);}}
        span=step_samples(t,period,t->seq_idx);if(n>span-t->seq_pos)n=span-t->seq_pos;
        expected_tick(t,n);recording_tick(t,n);t->seq_pos+=n;
        if(t->seq_pos==span){t->seq_pos=0;t->seq_idx=(uint16_t)((t->seq_idx+1u)%16u);if(!t->seq_idx){expected_zero(t);recording_zero(t);}}
        ok=state_matches(t);
        if(!ok)fprintf(stderr,"scheduler oracle mismatch: count=%u div=%u tick=%u\n",count,div,tick);
    }
    seq_stop();
    return check("cursor matches scan state through wrap, swing, pending QNT, edits, holds and large batches",ok);
}
static int cursor_slot_reuse(void)
{
    raw_begin();track_t *t=TSEL;seq_stop();song.rec=0;
    recorded_note_t r={0};r.on=30000;r.duration=1000;r.note=60;r.vel=90;
    recording_restore_note(t,0,r);t->step[0].time=ST_NOTE;t->step[0].flags=SF_RECORDED;
    seq_start();seq_advance(0);recording_remove(t,0);
    r.owner=NPAT;r.note=67;recording_restore_note(&trk[1],0,r);
    trk[1].step[0].time=ST_NOTE;trk[1].step[0].flags=SF_RECORDED;
    seq_advance(3000);
    int bad=check("reusing a pending slot on another track cannot redirect the old cursor",!gate_note(t,67) && !recording_refs[0][67]);
    seq_stop();
#if MELODEE_RECORD_CURSOR
    for(uint32_t k=0;k<NTRK;k++)bad+=check("STOP empties every runtime list",recording_live_head[k]==RECORD_MAX);
#endif
    return bad;
}
int main(void)
{
    int bad=oracle_scenario(64,2,0)+oracle_scenario(1024,2,0)+oracle_scenario(64,9,0)+oracle_scenario(1024,2,1)+cursor_slot_reuse();
    if(!bad)puts("Recording scheduler tests passed");return bad!=0;
}
