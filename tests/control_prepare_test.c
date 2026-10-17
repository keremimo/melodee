/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Centered Prophet wheel: independent oracle and isolated control cost. */
#define MELODEE_CONTROL_FAST 1
#define main hostsim_main
#include "hostsim.c"
#undef main
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
static __attribute__((noinline)) void reference(track_t *t)
{
    uint32_t ti=(uint32_t)(t-trk);
    int32_t range=(clamp(p5_patch_of(t)->raw[P5_BEND],0,11)+1)*256;
    midi_bend_target[ti]=t->bend_raw*range/(t->bend_raw<0?8192:8191);
}
static __attribute__((noinline)) void candidate(track_t *t) { p5_update_bend(t); }
static void parity(void)
{
    host_tracks_init();
    for(uint32_t ti=0;ti<NTRK;ti++){
        track_t *t=&trk[ti];host_preset(t,ENGI_PROPHET,0);
        for(uint32_t range=0;range<128;range++)for(int32_t raw=-8192;raw<=8191;raw++){
            p5_patch_of(t)->raw[P5_BEND]=(uint8_t)range;t->bend_raw=(int16_t)raw;
            int32_t expected=raw*((clamp((int32_t)range,0,11)+1)*256)/(raw<0?8192:8191);
            midi_bend_target[ti]=123456;p5_update_bend(t);assert(midi_bend_target[ti]==expected);
        }
    }
    puts("control preparation: exact 14-bit bend at every native byte range, all tracks");
}
static volatile uint32_t checksum;
static void benchmark(void)
{
    track_t *t=&trk[0];
    puts("wheel,reference_instructions_per_block,fast_instructions_per_block");
    for(uint32_t moving=0;moving<3;moving++){
        double cost[2];
        for(uint32_t fast=0;fast<2;fast++){
            uint64_t start=instructions();
            for(uint32_t k=0;k<1000000;k++){
                t->bend_raw=(int16_t)(moving==0?0:moving==1?4096:(int32_t)(k&16383u)-8192);
                if(fast)candidate(t);else reference(t);
                checksum+=(uint32_t)midi_bend_target[0];
            }
            cost[fast]=(double)(instructions()-start)/1000000;
        }
        printf("%u,%.1f,%.1f\n",moving,cost[0],cost[1]);
    }
}
int main(int argc,char **argv)
{
    assert(argc==1 || (argc==2 && !strcmp(argv[1],"--parity-only")));
    parity();if(argc==1)benchmark();return 0;
}
