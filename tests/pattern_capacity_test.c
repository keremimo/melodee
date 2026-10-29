/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Pool at 16 x 128: retain the old dense step capacity, refuse edits past the save's room (pattern_room)
 * atomically, and switch freely with every bank dense. */
#define NSTEP 128u
#define NPAT 16u
#define NMIDI 0u                               /* (the parts only: 16 x 128 x 8 tracks outgrow the 64 take owners) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint32_t room = ~0u;                    /* the save's room for steps (pattern_store.c on the device) */
static uint32_t pattern_room(void) { return room; }
static pattern_t model[NPART][NPAT];
static uint32_t random_state=0x78943b21u;
static uint32_t draw(void) { random_state=random_state*1664525u+1013904223u;return random_state; }
static void fill(pattern_t *p,uint32_t first,uint32_t end)
{
    for(uint32_t i=first;i<end;i++){
        step_t *s=&p->step[i];memset(s,0,sizeof *s);s->time=ST_NOTE;s->n=4;s->vel=100;
        for(uint32_t j=0;j<4;j++)s->note[j]=(uint8_t)((draw()>>24)&127u);
        s->probability=(uint8_t)(i%100u+1u);
    }
}
static void verify(void)
{
    uint8_t seen[PAT_CHUNKS]={0};uint32_t used=0,free=0;
    for(uint32_t k=0;k<NPART;k++)for(uint32_t bank=0;bank<NPAT;bank++){
        pattern_t actual;pattern_read(k,bank,&actual);assert(!memcmp(&actual,&model[k][bank],sizeof actual));
        for(uint32_t i=0;i<NSTEP;i++){
            step_t s=pattern_step(k,bank,i);assert(!memcmp(&s,&actual.step[i],sizeof s));
        }
        for(uint32_t b=0;b<PAT_BLOCKS;b++){
            const pattern_block_t *p=&pattern_blocks[k][bank][b];
            if(bank==trk[k].pattern)assert(p->head==PAT_NONE);
            for(uint32_t h=p->head&~PAT_REPEAT;h!=PAT_NONE;h=pattern_link[h]){
                assert(h<PAT_CHUNKS && !seen[h]++);used++;
            }
        }
    }
    for(uint32_t h=pattern_free;h!=PAT_NONE;h=pattern_link[h]){assert(h<PAT_CHUNKS && !seen[h]++);free++;}
    assert(used==pattern_pool_used && used+free==PAT_CHUNKS);
    assert(pattern_file_cost()<=pattern_room());
}
static void fresh(void)
{
    room=~0u;
    for(uint32_t k=0;k<NPART;k++)memset(trk[k].step,0,sizeof trk[k].step);
    host_tracks_init();
    for(uint32_t k=0;k<NPART;k++)for(uint32_t bank=0;bank<NPAT;bank++)pattern_read(k,bank,&model[k][bank]);
}
static void original_capacity(void)
{
    fresh();
    for(uint32_t k=0;k<NPART;k++)for(uint32_t b=0;b<8u;b++){
        fill(&model[k][b],0,64);assert(pattern_write(k,b,&model[k][b]));
    }
    assert(pattern_total_cost()==PAT_DENSE_BUDGET);verify();
    for(uint32_t round=0;round<32;round++){
        for(uint32_t k=0;k<NPART;k++){
            uint32_t bank=draw()%NPAT;
            if(round&1u)pattern_prepare(k,bank);
            pattern_switch(&trk[k],bank);
        }
        if(round%3u==0)for(uint32_t k=0;k<NPART;k++)pattern_flush(k);
        verify();
    }
    puts("shared pool: original 32 x 64 dense capacity survives prepared and fallback switches");
}
static void exhaustion(void)
{
    fresh();
    /* All the room cold, all four active patterns empty: no spare room to hide a partial-publication or
     * replacement-order bug. The pool keeps every bank dense, so encoding never runs out under it. */
    for(uint32_t k=0;k<NPART;k++)for(uint32_t b=1;b<=8;b++){
        fill(&model[k][b],64,128);assert(pattern_write(k,b,&model[k][b]));
    }
    room=pattern_file_cost();
    assert(room==NPART*8u*pattern_file_bytes(64) && pattern_pool_used==NPART*8u*4u*PAT_BLOCK_CHUNKS && pattern_free!=PAT_NONE);
    verify();
    pattern_prepare(0,1);pattern_t p=model[0][15];fill(&p,0,16);
    uint16_t used=pattern_pool_used,peak=pattern_pool_peak;
    assert(!pattern_write(0,15,&p) && pattern_full);
    assert(pattern_stage[0].bank==1 && pattern_pool_used==used && pattern_pool_peak==peak);verify();
    recorded_note_t r={.duration=1000,.vel=100,.note7=60};
    recorded_set_owner(&r,1);recorded_set_step(&r,100,0,0);recording[0]=r;recording_reindex();
    motion.count=1;motion.event[0]=(motion_event_t){0,P_PAN,12};motion_pattern[0]=1;
    uint32_t generation=recording_generation;
    assert(pattern_copy_at(&trk[0],1,15,0)==4);
    assert(!memcmp(&recording[0],&r,sizeof r) && recording_generation==generation);
    assert(motion.count==1 && motion_pattern[0]==1 && motion.event[0].value==12);verify();
    /* Move a cold bank's dense blocks from the end to the beginning, at the
     * same total cost. Freeing one old block at a time would run out here. */
    p=model[0][1];
    memcpy(p.step,p.step+64u,64u*sizeof(step_t));
    for(uint32_t i=64;i<128;i++)p.step[i]=(step_t){{0},0,ST_REST};
    assert(pattern_write(0,1,&p));model[0][1]=p;verify();
    p.timing[0]=64;assert(pattern_write(0,1,&p));model[0][1]=p;
    assert(pattern_double(&trk[0],1,0)==4 && pattern_double(&trk[0],1,1)==4);
    assert(!memcmp(&recording[0],&r,sizeof r) && recording_generation==generation);verify();
    for(uint32_t k=0;k<NPART;k++)pattern_prepare(k,1);
    for(uint32_t k=0;k<NPART;k++)pattern_switch(&trk[k],1);
    verify();
    uint32_t empty_step=64;step_t before=trk[0].step[empty_step],new_step={{60},1,ST_NOTE,0,100};
    assert(!pattern_set_step(&trk[0],empty_step,&new_step));
    assert(!memcmp(&before,&trk[0].step[empty_step],sizeof before));verify();
    new_step=trk[0].step[0];new_step.vel=92;
    assert(pattern_set_step(&trk[0],0,&new_step));model[0][1].step[0]=new_step;verify();
    for(uint32_t k=0;k<NPART;k++)pattern_switch(&trk[k],15);
    for(uint32_t k=0;k<NPART;k++)pattern_flush(k);
    assert(pattern_file_cost()==room && pattern_pool_used==NPART*8u*4u*PAT_BLOCK_CHUNKS);verify();
    puts("pool: refusal past the save's room is atomic; relocation, existing edits and all four switches preserve notes");
}
static void sparse_banks(void)
{
    fresh();
    for(uint32_t k=0;k<NPART;k++)for(uint32_t b=0;b<NPAT;b++){
        for(uint32_t i=0;i<NSTEP;i++)model[k][b].step[i]=(step_t){{60,64,67,71},4,ST_NOTE,0,100};
        assert(pattern_write(k,b,&model[k][b]));
    }
    assert(pattern_total_cost()==NPART*NPAT*PAT_BLOCKS);verify();
    for(uint32_t op=0;op<300;op++){
        uint32_t k=draw()%NPART,b=(draw()>>8)%NPAT;
        if(op%3u==0){pattern_prepare(k,b);pattern_switch(&trk[k],b);}
        else {
            pattern_t p=model[k][b];fill(&p,0,NSTEP);
            if(pattern_write(k,b,&p))model[k][b]=p;
        }
        if(op%7u==0)pattern_flush(k);
        verify();
    }
    puts("shared pool: all 64 banks support 128 repeated steps; seeded edits preserve ownership across exhaustion");
}
int main(void)
{
    original_capacity();exhaustion();sparse_banks();
    printf("16 x 128 host pool: %u payload chunks, %zu bytes payload+links; no general heap allocations\n",
        (unsigned)PAT_CHUNKS,sizeof pattern_chunks_retained+sizeof pattern_chunks_ram+sizeof pattern_link);
    return 0;
}
