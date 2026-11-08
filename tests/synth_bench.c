/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Native host instructions over whole rendering runs, with counter reads
 * outside the measured loop. Counts are not FM-1 cycles. Each scenario forks
 * from untouched DSP state. PCM hashes allow before/after comparisons. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
static uint64_t instructions(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 r;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&r)) return r.ri_instructions;
#endif
    return 0;
}
static void measure(uint32_t engine, uint32_t preset, uint32_t mode, uint32_t voices, int mixer)
{
    host_tracks_init();host_preset(&trk[0],engine,preset);
    uint32_t tracks=mixer?4u:1u;
    for(uint32_t t=0;t<tracks;t++){
        if(t)host_preset(&trk[t],t==3?ENGI_808:ENGI_FM6,0);
        trk[t].p[P_VOICE]=V_POLY;trk[t].p[P_ALLOC]=voices;trk[t].p[P_SUS]=127;
        if(trk[t].engine==ENGI_FM6)fm6_fn[t][FN_ENGINE]=(uint8_t)mode;
        for(uint32_t v=0;v<voices;v++)trk_note_on(&trk[t],48u+t*5u+v*4u,100);
    }
    int32_t out[CTL*2];uint64_t hash=0xcbf29ce484222325ull;
    for(uint32_t phase=0;phase<3;phase++){
        if(phase==2)for(uint32_t t=0;t<tracks;t++)for(uint32_t v=0;v<voices;v++)trk_note_off(&trk[t],48u+t*5u+v*4u);
        uint32_t blocks=phase==1?4096u:512u;
        uint64_t start=instructions();
        for(uint32_t b=0;b<blocks;b++){
            if(mixer)mix_block_q15(out,CTL);
            else{memset(out,0,sizeof out);track_render(&trk[0],out,CTL);}
            hash=(hash^(uint32_t)out[b%CTL])*0x100000001b3ull;
        }
        uint64_t cost=instructions()-start;
        printf("%u,%u,%u,%u,%u,%u,%.3f,%016llx\n",engine,preset,mode,voices,mixer,phase,(double)cost/(blocks*CTL),(unsigned long long)hash);
    }
}
static void run(uint32_t e,uint32_t p,uint32_t m,uint32_t v,int mix)
{
    fflush(stdout);pid_t pid=fork();if(!pid){measure(e,p,m,v,mix);fflush(stdout);_exit(0);}
    int status;if(pid<0 || waitpid(pid,&status,0)<0 || !WIFEXITED(status) || WEXITSTATUS(status))exit(1);
}
static void measure_drum(uint32_t kit,uint32_t sound,uint32_t changing)
{
    host_tracks_init();track_t *t=&trk[0];host_kit_preset(t,kit);
    t->p[P_ALLOC]=8;t->p[P_VOICE]=V_POLY;
    int32_t out[CTL];uint64_t hash=0xcbf29ce484222325ull;
    uint64_t start=instructions();
    for(uint32_t b=0;b<8192;b++){
        if(!(b%128u)){
            if(changing){t->p[P_E1]=(int16_t)(32u+b/128u%65u);t->p[P_E3]=(int16_t)(32u+(b/128u*7u)%96u);}
            if(sound<16u)trk_note_on(t,pads_note(t,sound),80u+b/128u%48u);
            else{
                trk_note_on(t,42,100);
                if(!(b%512u))trk_note_on(t,36,110);
                if(b%512u==256u)trk_note_on(t,38,110);
                if(b%2048u>=1536u)trk_note_on(t,45u+(b/128u)%3u,100);
            }
        }
        memset(out,0,sizeof out);track_render(t,out,CTL);
        hash=(hash^(uint32_t)out[b%CTL])*0x100000001b3ull;
    }
    uint64_t cost=instructions()-start;
    printf("%u,%u,%u,%.3f,%016llx\n",kit,sound,changing,(double)cost/(8192u*CTL),(unsigned long long)hash);
}
static void drum_bench(void)
{
    puts("kit,sound,changing,host_instructions_per_frame,pcm_hash");
    for(uint32_t kit=0;kit<4;kit++)for(uint32_t changing=0;changing<2;changing++){
        uint32_t count=kit==1?D9_NUM:16u;
        for(uint32_t sound=0;sound<=count;sound++){
            fflush(stdout);pid_t pid=fork();if(!pid){measure_drum(kit,sound==count?16u:sound,changing);fflush(stdout);_exit(0);}
            int status;if(pid<0 || waitpid(pid,&status,0)<0 || !WIFEXITED(status) || WEXITSTATUS(status))exit(1);
        }
    }
}
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"drums")){drum_bench();return 0;}
    int full=argc>1 && !strcmp(argv[1],"full");
    puts("engine,preset,mode,voices,mixer,phase,host_instructions_per_frame,pcm_hash");
    const uint32_t engines[]={ENGI_PROPHET,ENGI_FM6,ENGI_CZ,ENGI_SID,ENGI_808};
    for(uint32_t k=0;k<NELEM(engines);k++){
        uint32_t e=engines[k],count=full?ENGINES[e]->npresets:4u;
        if(count>ENGINES[e]->npresets)count=ENGINES[e]->npresets;
        for(uint32_t p=0;p<count;p++)for(uint32_t m=0;m<(e==ENGI_FM6?3u:1u);m++)run(e,p,m,4,0);
    }
    for(uint32_t m=0;m<3;m++)run(ENGI_PROPHET,3,m,2,1);
    return 0;
}
