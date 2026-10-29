/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#ifndef MELODEE_RECORD_INDEX
#define MELODEE_RECORD_INDEX 1
#endif
#define RECORDING_NO_MAIN 1
#include "recording_test.c"
#undef __attribute__
#include <assert.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
static uint64_t instructions(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 r;
    if(!proc_pid_rusage(getpid(),RUSAGE_INFO_V4,(rusage_info_t *)&r))return r.ri_instructions;
#endif
    return 0;
}
/* Independent all-slot scanner: no production active/view/list helpers. */
static __attribute__((noinline)) uint32_t scanner(track_t *t,const step_t *s,uint32_t skip)
{
    if(!(s->flags&SF_RECORDED))return skip;
    for(uint32_t i=0;i<RECORD_MAX;i++){
        const recorded_note_t *r=&recording[i];
        uint32_t view=r->step&128u?0u:(r->step&63u)+!!(r->step&64u);
        if(!r->vel || (r->owner&31u)!=(uint32_t)(t-trk)*NPAT+t->pattern ||
           (r->step&63u)>=(uint32_t)t->p[P_SLEN] || view!=t->seq_idx ||
           t->step[view].time!=ST_NOTE || !(t->step[view].flags&SF_RECORDED))continue;
        for(uint32_t k=0;k<s->n;k++)if(s->note[k]==recorded_note(r))skip|=1u<<k;
        for(uint32_t k=0;k<NLANE;k++)if(DRUM_LANE_NOTE[k]==recorded_note(r))skip|=1u<<(8u+k);
    }
    return skip;
}
/* Original linked-bank comparator for fair query cost, separate from oracle. */
static __attribute__((noinline)) uint32_t reference(track_t *t,const step_t *s,uint32_t skip)
{
    if(s->flags&SF_RECORDED)
        for(uint32_t r=recording_head[recording_owner(t)];r<RECORD_MAX;r=recording_next[r])if(recording_active(t,r) && recording_view(t,&recording[r])==t->seq_idx){
            for(uint32_t k=0;k<s->n;k++)if(s->note[k]==recorded_note(&recording[r]))skip|=1u<<k;
            for(uint32_t k=0;k<NLANE;k++)if(DRUM_LANE_NOTE[k]==recorded_note(&recording[r]))skip|=1u<<(8u+k);
        }
    return skip;
}
static __attribute__((noinline)) uint32_t candidate(track_t *t,const step_t *s,uint32_t skip)
{return recording_step_skip(t,s,skip);}
static uint32_t random_state=0x537deb42u;
static uint32_t rand_u(void){random_state=random_state*1664525u+1013904223u;return random_state;}
static recorded_note_t event(uint32_t owner)
{
    recorded_note_t r={0};uint32_t actual=rand_u()%NSTEP,rounded=rand_u()>>31;
    r.step=(uint8_t)(actual|(rounded?64u:0u)|(rounded && actual==63u?128u:0u));
    recorded_set_note(&r,(uint8_t)(rand_u()%128u));r.vel=100;r.duration=1000;r.owner=(uint8_t)owner;
    return r;
}
static void fixture(uint32_t count,uint32_t dense)
{
    timing_reset(0,24000000u);seq_stop();song.rec=0;recording_reset();
    for(uint32_t tr=0;tr<NPART;tr++){
        trk[tr].pattern=0;trk[tr].p[P_SLEN]=64;
        for(uint32_t v=0;v<NSTEP;v++){
            step_t *s=&trk[tr].step[v];memset(s,0,sizeof *s);s->time=ST_NOTE;s->flags=SF_RECORDED;
            s->n=4;s->note[0]=0;s->note[1]=60;s->note[2]=127;s->note[3]=DRUM_LANE_NOTE[v%NLANE];
        }
    }
    for(uint32_t i=0;i<count;i++){
        recorded_note_t r=event(dense?0u:i%32u);
        /* Repeated pitches in different/same original cells. */
        if(dense){recorded_set_note(&r,(uint8_t)(i%128u));r.step=(uint8_t)(i%64u);}
        recording[i]=r;
    }
    recording_reindex();
}
static void counts_match(void)
{
#if MELODEE_RECORD_INDEX
    uint16_t count[NPART*NPAT]={0};
    for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel)count[recording[i].owner&31u]++;
    assert(!memcmp(count,recording_pitch_count,sizeof count));
#endif
}
static void all_queries(void)
{
    for(uint32_t tr=0;tr<NPART;tr++)for(uint32_t bank=0;bank<NPAT;bank++){
        track_t *t=&trk[tr];t->pattern=(uint8_t)bank;
        for(uint32_t len=1;len<=64;len+=7){
            t->p[P_SLEN]=(int16_t)len;
            for(uint32_t view=0;view<NSTEP;view++){
                t->seq_idx=(uint16_t)view;step_t *s=&t->step[view];uint32_t skip=rand_u();
                s->time=view%5u?ST_NOTE:ST_REST;s->flags=(uint8_t)(view%7u?SF_RECORDED:0u);
                s->note[1]=(uint8_t)(rand_u()%128u);
                assert(scanner(t,s,skip)==candidate(t,s,skip));
                /* A caller may ask about a separate overview, whose pitches
                 * are deliberately unrelated to the cached current cell. */
                step_t alternate=*s;alternate.note[0]^=127u;alternate.note[1]^=63u;
                assert(scanner(t,&alternate,skip)==candidate(t,&alternate,skip));
            }
        }
    }
}
static void parity(void)
{
    for(uint32_t note=0;note<128u;note++)for(uint32_t value=0;value<256u;value++){
        uint32_t key=value|((value^255u)<<8)|(((value+1u)&255u)<<16)|(((value+127u)&255u)<<24),expected=0;
        for(uint32_t k=0;k<4u;k++)if(((key>>(8u*k))&255u)==note)expected|=1u<<k;
        for(uint32_t k=0;k<NLANE;k++)if(DRUM_LANE_NOTE[k]==note)expected|=1u<<(8u+k);
        assert(recording_pitch_mask(note,key)==expected);
    }
    fixture(RECORD_MAX,0);all_queries();
    /* Interleave capture/removal/replacement, original-to-rounded and wrapped
     * edits, live flags/time and LEN/bank changes. Query every pitch for the
     * edited cell in groups of four, plus every drum lane. */
    for(uint32_t n=0;n<20000;n++){
        uint32_t i=rand_u()%RECORD_MAX,owner=(rand_u()>>8)%32u;
        track_t *t=&trk[owner/NPAT];t->pattern=(uint8_t)(owner%NPAT);t->p[P_SLEN]=(int16_t)(1u+(rand_u()>>8)%64u);
        if(recording[i].vel)recording_remove(&trk[(recording[i].owner&31u)/NPAT],i);
        if(n%5u){recorded_note_t r=event(owner);recording_restore_note(t,i,r);t->seq_idx=(uint16_t)(r.step&128u?0u:(r.step&63u)+!!(r.step&64u));}
        else t->seq_idx=(uint16_t)(rand_u()%NSTEP);
        step_t *s=&t->step[t->seq_idx];s->time=ST_NOTE;s->flags=SF_RECORDED;
        for(uint32_t pitch=0;pitch<128;pitch+=4){
            for(uint32_t k=0;k<4;k++)s->note[k]=(uint8_t)(pitch+k);
            uint32_t skip=rand_u();assert(scanner(t,s,skip)==candidate(t,s,skip));
        }
        if(n%73u==0u){recording_reindex();counts_match();}
    }
    fixture(RECORD_MAX,1);track_t *t=&trk[0];t->seq_idx=0;
    /* 1024 duplicate same-cell/same-pitch owners: deleting one must preserve
     * the bit until the last one ends, including a single coalesced rebuild. */
    for(uint32_t i=0;i<RECORD_MAX;i++){recording[i].step=0;recorded_set_note(&recording[i],60);}
    recording_reindex();counts_match();assert(candidate(t,&t->step[0],0)&2u);
    for(uint32_t i=0;i<RECORD_MAX;i++){
        recording_remove(t,i);
        counts_match();
        if(i%97u==0u || i+1u==RECORD_MAX){uint32_t got=candidate(t,&t->step[0],0);assert(got==scanner(t,&t->step[0],0));assert(!!(got&2u)==(i+1u<RECORD_MAX));}
    }
    /* Wrapped cells retain original-onset eligibility when LEN shrinks. */
    fixture(8,1);t=&trk[0];t->seq_idx=0;t->p[P_SLEN]=16;
    for(uint32_t i=0;i<8;i++){recorded_set_note(&recording[i],60);recording[i].step=(uint8_t)((i<4?15u:31u)|192u);}
    recording_reindex();assert(candidate(t,&t->step[0],0)&2u);
    for(uint32_t i=0;i<4;i++)recording_remove(t,i);
    assert(!(candidate(t,&t->step[0],0)&2u));t->p[P_SLEN]=32;assert(candidate(t,&t->step[0],0)&2u);
    puts("Recording ownership index: all banks/tracks/LEN/cells, 640,000 pitch queries, 1024 duplicate removals exact");
}
static volatile uint32_t checksum;
static void benchmark(void)
{
    puts("events,query_kind,reference_instructions,candidate_instructions");
    for(uint32_t count=0;count<=RECORD_MAX;count=count<4?count+1:count==4?64:count==64?256:count==256?1024:1025){
        fixture(count,1);track_t *t=&trk[0];recording_pitch_prepare(t);
        double cost[2];
        for(uint32_t fast=0;fast<2;fast++){
            uint32_t (*volatile run)(track_t *,const step_t *,uint32_t)=fast?candidate:reference;
            uint64_t start=instructions();
            for(uint32_t k=0;k<20000;k++){t->seq_idx=(uint16_t)(k%NSTEP);checksum+=run(t,&t->step[t->seq_idx],0);}
            cost[fast]=(double)(instructions()-start)/20000;
        }
        printf("%u,steady,%.1f,%.1f\n",count,cost[0],cost[1]);
#if MELODEE_RECORD_INDEX
        if(count>=64 && cost[0])assert(cost[1]<cost[0]*.25);
        if(count<4 && cost[0])assert(cost[1]-cost[0]<24.0);
        uint64_t start=instructions();
        for(uint32_t k=0;k<1000;k++){recording_pitch_invalidate();recording_pitch_prepare(t);checksum+=recording_pitch[0][0];}
        printf("%u,full_rebuild,,%.1f\n",count,(double)(instructions()-start)/1000);
        start=instructions();
        for(uint32_t k=0;k<1000;k++){recording_pitch_dirty[0][0]=1;recording_pitch_prepare(t);checksum+=recording_pitch[0][0];}
        printf("%u,cell_rebuild,,%.1f\n",count,(double)(instructions()-start)/1000);
        /* Existing release-queue overview polling, separate from synthesis,
         * onset dispatch and the new pitch query. No deadlines expire. */
        for(uint32_t i=0;i<count;i++)recording_queue_add(t,i,1000000u);
        recording_queue_tick(t,0);start=instructions();
        for(uint32_t k=0;k<1000;k++){recording_queue_tick(t,0);checksum+=recording_heap_count[0];}
        printf("%u,release_overview_poll,,%.1f\n",count,(double)(instructions()-start)/1000);
#endif
    }
}
int main(int argc,char **argv)
{
    parity();if(argc==1)benchmark();return 0;
}
