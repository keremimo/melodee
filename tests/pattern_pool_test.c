/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Dense-capacity and seeded state-machine oracle for the actual compressed
 * store, staging handoff and point queries. Host timing is not device timing. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>
static pattern_t model[NTRK][NPAT];
static uint32_t seed = 0x91740a5u;
static uint32_t random_word(void) { seed = seed * 1664525u + 1013904223u; return seed; }
static void dense(pattern_t *p)
{
    for (uint32_t i=0;i<NSTEP;i++) {
        step_t *s=&p->step[i];
        /* any step the device makes: 7-bit notes and velocity, chance up to 101, no n on a rest or tie
         * (the pool's packing, step_packed_put) and accents a step can hold */
        for(uint32_t n=0;n<4;n++)s->note[n]=(uint8_t)(random_word()>>25);
        s->time=(uint8_t)(random_word()%3u);s->n=(uint8_t)(s->time==ST_NOTE?random_word()%5u:0u);
        s->flags=(uint8_t)(random_word()%8u);s->vel=(uint8_t)(random_word()>>25);
        s->hit=(uint8_t)(random_word()>>24);s->acc=(uint8_t)(random_word()>>24);s->probability=(uint8_t)(random_word()%102u);
        if(s->hit)s->acc&=s->hit;else if(s->time==ST_REST)s->acc=0;
    }
    p->timing[0]=(int16_t)(1u+random_word()%NSTEP);p->timing[1]=2;p->timing[2]=7;p->timing[3]=90;
}
static void check_pool(void)
{
    uint8_t seen[PAT_CHUNKS]={0};uint32_t used=0,free=0;
    for(uint32_t k=0;k<NTRK;k++)for(uint32_t bank=0;bank<trk_npat(k);bank++){
        pattern_t actual;pattern_read(k,bank,&actual);assert(!memcmp(&actual,&model[k][bank],sizeof actual));
        for(uint32_t i=0;i<NSTEP;i++){
            step_t s=pattern_step(k,bank,i);assert(!memcmp(&s,&actual.step[i],sizeof s));
        }
        for(uint32_t b=0;b<PAT_BLOCKS;b++){
            pattern_block_t *block=&pattern_blocks[k][bank][b];uint32_t n=0;
            if(bank==trk[k].pattern)assert(block->head==PAT_NONE);
            for(uint32_t h=block->head&~PAT_REPEAT;h!=PAT_NONE;h=pattern_link[h]){
                assert(h<PAT_CHUNKS && !seen[h]);seen[h]=1;used++;assert(++n<=PAT_BLOCK_CHUNKS);
            }
        }
    }
    for(uint32_t h=pattern_free;h!=PAT_NONE;h=pattern_link[h]){assert(h<PAT_CHUNKS && !seen[h]);seen[h]=1;assert(++free<=PAT_CHUNKS);}
    assert(used==pattern_pool_used && used+free==PAT_CHUNKS && pattern_pool_peak<=PAT_CHUNKS);
}
static void fresh(void)
{
    ui_power_on();
    pattern_loading=1;          /* (the pool on its own: what a save has room for is pattern_capacity_test's) */
    for(uint32_t k=0;k<NTRK;k++)for(uint32_t b=0;b<trk_npat(k);b++)pattern_read(k,b,&model[k][b]);
    assert(!pattern_pool_used);check_pool();
}
static void dense_capacity(void)
{
    fresh();
    for(uint32_t k=0;k<NTRK;k++)for(uint32_t b=0;b<trk_npat(k);b++){dense(&model[k][b]);assert(pattern_write(k,b,&model[k][b]));}
    assert(pattern_pool_used==PAT_CHUNKS);check_pool();
    /* A full store can prepare all four incoming patterns without allocating.
     * A last-moment edit belongs to the outgoing bank, including hidden steps. */
    uint32_t fallbacks=pattern_fallbacks;
    for(uint32_t k=0;k<NTRK;k++){
        pattern_prepare(k,1);trk[k].step[NSTEP-1].vel^=3u;model[k][0].step[NSTEP-1].vel^=3u;
        pattern_switch(&trk[k],1);assert(pattern_stage[k].pending);
    }
    assert(pattern_fallbacks==fallbacks);check_pool();
    pattern_poll();assert(pattern_pool_used==PAT_CHUNKS);check_pool();
    for(uint32_t round=0;round<64;round++){
        for(uint32_t k=0;k<NTRK;k++)pattern_switch(&trk[k],(trk[k].pattern+1u)%trk_npat(k));
        check_pool();
    }
    assert(pattern_fallbacks>fallbacks);pattern_poll();check_pool();
    puts("pattern pool: every bank dense, all four prepared/fallback switches, late edits and hidden steps: OK");
}
static void codecs_and_mutations(void)
{
    fresh();
    pattern_t p=model[0][1];
    for(uint32_t i=0;i<NSTEP;i++)p.step[i]=(step_t){{60,64,67,71},4,ST_NOTE,SF_SLIDE,100,0,0,80};
    model[0][1]=p;pattern_write(0,1,&p);assert(pattern_pool_used==PAT_BLOCKS);
    for(uint32_t b=0;b<PAT_BLOCKS;b++)assert(pattern_blocks[0][1][b].head&PAT_REPEAT);
    check_pool();
    for(uint32_t op=0;op<600;op++){
        uint32_t k=random_word()%NTRK,bank=(random_word()>>8)%trk_npat(k),action=(random_word()>>16)%5u;
        if(action<2){
            if(action)pattern_prepare(k,bank);
            pattern_switch(&trk[k],bank);
        }else{
            pattern_read(k,bank,&p);
            uint32_t at=(random_word()>>9)%NSTEP;
            if(action==2)dense(&p);
            else if(action==3)p.step[at]=(step_t){{0},0,ST_REST};
            else{                                   /* (a rest or tie as the device makes it: no n, no gate) */
                step_t *s=&p.step[at];s->note[3]^=31u;s->time=(uint8_t)(random_word()%3u);
                if(s->time!=ST_NOTE)s->n=0;
                if(!s->hit && s->time==ST_REST)s->acc=0;
            }
            pattern_prepare(k,bank); /* publication must invalidate a prepared destination */
            model[k][bank]=p;assert(pattern_write(k,bank,&p));
        }
        if(op%7u==0)pattern_poll();
        check_pool();
    }
    puts("pattern pool: empty/repeated/sparse/dense codecs, destination edits, seeded ownership oracle: OK");
}
static void timing(void)
{
    /* Same release/motion work, empty timed-note store, dense step banks.
     * The reference below is the previous commit + load operation. */
    pattern_t a,b;dense(&a);dense(&b);uint64_t start;
    fresh();dense(&a);dense(&b);pattern_write(0,0,&a);pattern_write(0,1,&b);
    uint64_t prepare_ns=0,switch_ns=0;
    for(uint32_t n=0;n<10000;n++){
        uint32_t bank=1u-trk[0].pattern;
        start=now_ns();pattern_prepare(0,bank);prepare_ns+=now_ns()-start;
        start=now_ns();pattern_switch(&trk[0],bank);switch_ns+=now_ns()-start;
    }
    uint64_t fallback_ns=0,reference_ns=0;
    for(uint32_t n=0;n<10000;n++){
        start=now_ns();pattern_switch(&trk[0],1u-trk[0].pattern);fallback_ns+=now_ns()-start;
    }
    fresh();pattern_t banks[2];dense(&banks[0]);dense(&banks[1]);
    for(uint32_t n=0;n<10000;n++){
        track_t *t=&trk[0];uint32_t bank=1u-t->pattern;start=now_ns();
        memcpy(banks[t->pattern].step,t->step,sizeof t->step);
        memcpy(banks[t->pattern].timing,&t->p[P_SLEN],sizeof banks[0].timing);
        seq_release(t);recording_stop(t);motion_restore(t);
        memcpy(t->step,banks[bank].step,sizeof t->step);
        memcpy(&t->p[P_SLEN],banks[bank].timing,sizeof banks[0].timing);
        t->pattern=(uint8_t)bank;t->pattern_next=0xff;t->rh_n=t->rskip_n=0;t->pattern_gen++;
        t->seq_idx=(uint16_t)(t->p[P_SLEN]-1);reference_ns+=now_ns()-start;
    }
    printf("host only, ns/switch: previous expanded %.0f, prepared %.0f, foreground flush+prepare %.0f, unprepared fallback %.0f\n",
        (double)reference_ns/10000,(double)switch_ns/10000,(double)prepare_ns/10000,(double)fallback_ns/10000);
    printf("pattern RAM: %zu B chunks, %zu B links, %zu B directory, %zu B timings, %zu B staging; capacity %u chunks\n",
        sizeof pattern_chunks_retained+sizeof pattern_chunks_ram,sizeof pattern_link,sizeof pattern_blocks,sizeof pattern_timings,sizeof pattern_stage,
        (unsigned)PAT_CHUNKS);
}
int main(void)
{
    dense_capacity();codecs_and_mutations();timing();return 0;
}
