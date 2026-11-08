/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Shared rack and DSP kernel matrix; host counts are not FM-1 CPU percentages. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "fx_kernel_fixture.h"
#include <assert.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
static volatile uint32_t checksum;
static uint64_t instructions(void){
#ifdef __APPLE__
    struct rusage_info_v4 r;if(!proc_pid_rusage(getpid(),RUSAGE_INFO_V4,(rusage_info_t *)&r))return r.ri_instructions;
#endif
    return 0;
}
static void setup(const uint8_t modes[3],int tracks,int setting,int mixer){
    for(uint32_t id=0;id<RES_COUNT;id++)resource_release(id);
    memset(trk,0,sizeof trk);trk_voices_bind();memset(&song,0,sizeof song);memset(fx_state,0,sizeof fx_state);host_tracks_init();spread_on=0;
    for(int t=0;t<tracks;t++){
        if(mixer){host_preset(&trk[t],t==0?ENGI_PROPHET:t==3?ENGI_DRUM:ENGI_FM6,t==0?3:0);trk[t].p[P_SUS]=127;trk_note_on(&trk[t],48+t*7,100);if(t<3)trk_note_on(&trk[t],52+t*7,100);}
        for(int s=0;s<3;s++){int b=P_FXTYPE+s*FX_PARAMS;trk[t].p[b]=modes[s];trk[t].p[b+1]=100;trk[t].p[b+2]=setting?127:60;trk[t].p[b+3]=setting?127:64;trk[t].p[P_FXTONE+s]=setting?FX_TONE_MAX:0;}
    }
    {for(uint32_t k=0;k<tracks;k++){trk[k].p[P_FXSEND]=127;trk[k].p[P_FXSEND2]=trk[k].p[P_FXSEND3]=127;}
        for(uint32_t k=0;k<3;k++){song.g[G_MFXTYPE+k*4]=modes[k];song.g[G_MFXMIX+k*4]=127;song.g[G_MFXRATE+k*4]=setting?127:60;song.g[G_MFXCHAR+k*4]=setting?127:64;song.g[G_MFXTONE+k]=setting?FX_TONE_MAX:0;}song.g[G_FXRETURN]=100;song.g[G_FXMODE]=mixer==3;fx_bus_mix=0;fx_bus_select();
        shared_fx_normalize(song.g);
        for(int t=0;t<tracks;t++)for(uint32_t s=0;s<FX_SLOTS;s++)trk[t].p[P_FXTYPE+s*FX_PARAMS]=song.g[G_MFXTYPE+s*FX_PARAMS];
    }
}
static void block(int tracks,int mixer,uint32_t frame){
    int32_t out[CTL*2],b[CTL];
    if(mixer){mix_block_q15(out,CTL);checksum^=(uint32_t)out[0];return;}
    for(uint32_t i=0;i<CTL;i++)b[i]=sine_i((frame*CTL+i)*0x01734567u)/2;
    for(int t=0;t<tracks;t++){int active=fx_kernel_process(&trk[t],b,CTL);checksum^=(uint32_t)(active?fx_left[0]:b[0]);}
}
static double run(const char *kind,const uint8_t modes[3],int tracks,int setting,int mixer,int frames){
    setup(modes,tracks,setting,mixer);for(uint32_t i=0;i<256;i++)block(tracks,mixer,i);
    uint64_t i0=instructions(),t0=now_ns();
    for(int i=0;i<frames;i++)block(tracks,mixer,i+256);
    double ns=(double)(now_ns()-t0)/(frames*CTL),ipc=(double)(instructions()-i0)/(frames*CTL);
    for(uint32_t s=0;s<FX_SLOTS;s++){int16_t type=song.g[G_MFXTYPE+s*FX_PARAMS];if(type)assert(fx_state[s].type==type && (!fx_state[s].failed || type==FX_REVERB));else assert(!fx_state[s].type);}
    uint32_t rejected=0;
    for(uint32_t si=0;si<FX_STATE_COUNT;si++)if(fx_state[si].failed){assert(fx_state[si].type==FX_REVERB && !resource[RES_FX0+si].size);rejected++;}
    printf("%s,%u,%u,%u,%d,%d,%.3f,%.3f,%u,%u\n",kind,modes[0],modes[1],modes[2],tracks,setting,ipc,ns,resource_used(),rejected);
    return ipc;
}
int main(int argc,char **argv){
    uint32_t matrix_rows=0;int full=argc>1;puts("kind,slot1,slot2,slot3,tracks,extreme,host_instructions_per_frame,host_ns_per_frame,allocated_bytes,rejected_reverb_slots");
    for(int setting=0;setting<2;setting++)for(int tracks=1;tracks<=4;tracks+=3)for(int type=0;type<FX_AUDIO_COUNT;type++){
        if(fx_retired(type))continue;
        uint8_t modes[3]={type,0,0};double cost=run("single",modes,tracks,setting,0,2048);
        /* Apple instruction counters are deterministic; leave 20% headroom
         * above the measured ~112 instructions per stereo sample frame.
         * Other hosts return zero rather than a target CPU estimate. */
        if(type==FX_CHORUS && tracks==1 && cost>0)assert(cost<=135.0);
        run("mixer",modes,tracks,setting,1,2048);run("send",modes,tracks,setting,2,2048);run("separate-send",modes,tracks,setting,3,2048);
        if(type){modes[1]=modes[2]=type;run("triple",modes,tracks,setting,0,2048);run("mixer-triple",modes,tracks,setting,1,2048);run("send-triple",modes,tracks,setting,2,2048);run("separate-send-triple",modes,tracks,setting,3,2048);}
    }
    if(full)for(int a=1;a<FX_AUDIO_COUNT;a++)for(int b=1;b<FX_AUDIO_COUNT;b++)for(int c=1;c<FX_AUDIO_COUNT;c++)for(int setting=0;setting<2;setting++){
        if(fx_retired(a)||fx_retired(b)||fx_retired(c))continue;
        matrix_rows++;uint8_t modes[3]={a,b,c};run("matrix",modes,4,setting,0,256);
    }
    fprintf(stderr,"FX performance: all effects, three-slot/four-track chains, mixer and %u-row matrix passed (%u)\n",matrix_rows,checksum);return 0;
}
