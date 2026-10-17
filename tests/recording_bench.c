/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Build with MELODEE_RECORD_CURSOR=0 and =1. Measures the real sequencer,
 * excluding synthesis; host instruction counts are not FM-1 CPU percentages. */
#define MIDI_TIMING_NO_MAIN 1
#include "midi_timing_test.c"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
static uint64_t bench_instructions(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 r;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&r)) return r.ri_instructions;
#endif
    return 0;
}
#ifndef MELODEE_RECORD_QUEUE
#define MELODEE_RECORD_QUEUE 0
#endif
static volatile uint32_t bench_checksum;
int main(int argc, char **argv)
{
    int sustained=argc>1 && !strcmp(argv[1],"sustained");
    puts("cursor,queue,sustained,quant,tracks,notes_per_step,stored_records,event_hash,capture_instructions_per_note,instructions_per_tick,ns_per_tick");
    for(uint32_t q=0;q<=3;q+=3) for (uint32_t tracks=1; tracks<=4; tracks*=4) for(uint32_t notes=1;notes<=4;notes*=4) {
        timing_reset(0,24000000u);song.rec=(uint8_t)((1u<<tracks)-1u);
        for(uint32_t k=0;k<NTRK;k++){
            track_t *t=&trk[k];t->p[P_RECQ]=(int16_t)q;t->p[P_SLEN]=64;t->p[P_VOICE]=V_POLY;
            memset(t->step,0,sizeof t->step);for(uint32_t i=0;i<NSTEP;i++)t->step[i].time=ST_REST;
        }
        seq_start();seq_advance(0);song.rec_pass=0;
        uint32_t period=seq_div_samples(TSEL->p[P_SDIV]);
        uint64_t capture_start=bench_instructions();
        for(uint32_t i=0;i<64;i++){
            seq_advance(period/4u+512u);
            for(uint32_t k=0;k<tracks;k++)for(uint32_t n=0;n<notes;n++)input_on(&trk[k],48+n*4,90+n*8);
            seq_advance(period/8u);
            for(uint32_t k=0;k<tracks;k++)for(uint32_t n=0;n<notes;n++)input_off(&trk[k],48+n*4);
            seq_advance(period-period/4u-512u-period/8u);
        }
        seq_stop();song.rec=0;
        double capture_cost=(double)(bench_instructions()-capture_start)/(64u*tracks*notes);
        /* Stress the active-list bound too: keep events sounding for nearly
         * two complete loops rather than the phrase's short captured gates. */
        if(sustained)for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel){recording[i].duration=65535;recording[i].owner|=224u;}
        uint32_t stored=0;for(uint32_t i=0;i<RECORD_MAX;i++)stored+=recording[i].vel!=0;
        uint32_t event_hash=fnv(2166136261u,recording,sizeof recording);
        /* Optional canonical fixture dump for the host-only storage probe.
         * Explicit fields avoid dependence on C padding or host byte order. */
        if(argc>2){
            char path[512];snprintf(path,sizeof path,"%s/q%u-t%u-n%u-s%d.bin",argv[2],q,tracks,notes,sustained);
            FILE *f=fopen(path,"wb");if(!f)return 2;
            for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel){
                const recorded_note_t *r=&recording[i];
                uint8_t b[10]={r->on,r->on>>8,r->duration,r->duration>>8,r->note,r->vel,r->owner,r->step,(uint8_t)r->pitch,r->length};
                if(fwrite(b,1,sizeof b,f)!=sizeof b){fclose(f);return 2;}
            }
            if(fclose(f))return 2;
        }
        seq_start();for(uint32_t i=0;i<1024;i++)seq_advance(CTL);
        uint32_t ticks=50000;
        uint64_t c0=bench_instructions(),n0=now_ns();
        for(uint32_t i=0;i<ticks;i++)seq_advance(CTL);
        uint64_t ns=now_ns()-n0,ins=bench_instructions()-c0;
        bench_checksum^=trk[0].seq_pos+trk[0].seq_n;
        printf("%u,%u,%d,%u,%u,%u,%u,%08x,%.1f,%.1f,%.1f\n",MELODEE_RECORD_CURSOR,MELODEE_RECORD_QUEUE,sustained,q,tracks,notes,stored,event_hash,capture_cost,(double)ins/ticks,(double)ns/ticks);
        seq_stop();
    }
    return 0;
}
