/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Rejected general pitch cache: independent arithmetic oracle and reproducible
 * routing-free host benchmark. This does not enable a firmware cache. */
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
static __attribute__((noinline)) uint32_t reference(uint32_t pitch,int32_t fine)
{
    uint32_t inc=pitch_inc(pitch);
    if(tuning_a4!=440)inc=(uint32_t)(((uint64_t)inc*A4_RATIO[tuning_a4-A4_MIN]+8388608u)>>24);
    if(fine)inc+=(uint32_t)((int32_t)(inc>>12)*fine);
    return inc;
}
/* Rejected experiment retained only for reproducible host comparison. */
static struct { uint32_t key, inc; int32_t fine; } cache;
static __attribute__((noinline)) uint32_t candidate(uint32_t pitch,int32_t fine)
{
    uint32_t key=pitch|((uint32_t)tuning_a4<<12);
    if(cache.key==key && cache.fine==fine)return cache.inc;
    uint32_t inc=tuned_pitch_inc(pitch);
    if(fine)inc+=(uint32_t)((int32_t)(inc>>12)*fine);
    cache.key=key;cache.fine=fine;cache.inc=inc;return inc;
}
static void parity(void)
{
    for(int32_t a4=A4_MIN;a4<=A4_MAX;a4++)for(uint32_t p=0;p<2048;p++){
        tuning_a4=(int16_t)a4;
        for(int32_t f=-96;f<=96;f+=16)for(uint32_t repeat=0;repeat<2;repeat++)
            assert(candidate(p,f)==reference(p,f));
    }
    puts("pitch cache experiment: full pitch/A4/residual oracle parity");
}
static volatile uint32_t checksum;
static void benchmark(void)
{
    puts("a4,moving,reference_instructions_per_lookup,cached_instructions_per_lookup");
    for(uint32_t hz=432;hz<=440;hz+=8)for(uint32_t moving=0;moving<3;moving++){
        double cost[2];tuning_a4=(int16_t)hz;
        for(uint32_t cached=0;cached<2;cached++){
            uint64_t start=instructions();
            for(uint32_t k=0;k<1000000;k++){
                uint32_t p=1000+(moving==1?(k&1):0);int32_t f=3+(moving==2?(k&1):0);
                checksum+=cached?candidate(p,f):reference(p,f);
            }
            cost[cached]=(double)(instructions()-start)/1000000;
        }
        printf("%u,%u,%.1f,%.1f\n",hz,moving,cost[0],cost[1]);
    }
    printf("proposed per-voice cache bytes: %zu\n",sizeof cache*NTRK*NVOICE);
}
int main(int argc,char **argv)
{
    assert(argc==1 || (argc==2 && !strcmp(argv[1],"--parity-only")));
    parity();if(argc==1)benchmark();return 0;
}
