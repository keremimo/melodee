/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Synthetic CPU playback workloads. Reuse the actual serializer/validator;
 * no personal input, project slots or snapshot files. --emit writes only the
 * temporary FBKK runtime bytes to stdout for the guarded Python runner. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>

static const uint8_t cpu_engines[]={19,12,15,3,10,10};
static uint8_t fixture_raw[BANK_STORE_SIZE];
static recorded_note_t fixture_notes[RECORD_MAX];
/* Stopped, disarmed, empty four-track project with the assigned engines. */
static int fixture_tracks(uint32_t quant,const char *assignment)
{
    if(quant>1u || strlen(assignment)!=NTRK)return 0;
    for(uint32_t k=0;k<NTRK;k++)if(assignment[k]<'0' || assignment[k]>'5')return 0;
    ui_power_on();seq_stop();song.rec=song.rec_latch=0;recording_reset();
    song.ntrk=NTRK; /* unused tracks would take no notes or mixer CPU */
    for(uint32_t k=0;k<NTRK;k++){
        track_t *t=&trk[k];uint32_t category=(uint32_t)(assignment[k]-'0');
        host_preset(t,cpu_engines[category],0);
        if(category>=4u)t->p[P_E0]=(int16_t)(category==4u?4:5);
        t->p[P_VOICE]=V_POLY;t->p[P_LEVEL]=35;t->p[P_MUTE]=0;t->p[P_SLEN]=64;t->p[P_SDIV]=2;
        t->p[P_SSWING]=0;t->p[P_RECQ]=quant?3:0;t->p[P_AMODE]=0;t->p[P_QUANT]=0;
        t->p[P_FXTYPE]=t->p[P_FX2TYPE]=t->p[P_FX3TYPE]=0;
        t->p[P_FXSEND]=t->p[P_FXSEND2]=t->p[P_FXSEND3]=0;
        for(uint32_t v=0;v<NSTEP;v++){
            memset(&t->step[v],0,sizeof t->step[v]);t->step[v].time=ST_REST;
        }
    }
    song.g[G_BPM]=120;song.g[G_CLOCK]=0;song.g[G_SWING]=0;
    return 1;
}
static int make_fixture(uint32_t count,uint32_t quant,uint32_t spread,uint32_t tails,const char *assignment)
{
    if(count!=64u && count!=256u && count!=RECORD_MAX)return 0;
    if(spread>1u || tails>1u || !fixture_tracks(quant,assignment))return 0;
    for(uint32_t i=0;i<count;i++){
        uint32_t k=spread?i/(count/NTRK):0u,local=spread?i%(count/NTRK):i,cell=local%NSTEP,group=local/NSTEP;
        track_t *t=&trk[k];uint32_t category=(uint32_t)(assignment[k]-'0');
        uint32_t on=quant?0u:8192u+(local*1777u)%49152u;
        uint32_t round=!quant && on>=32768u,view=(cell+round)%NSTEP;
        recorded_note_t r={0};r.on=(uint16_t)on;r.duration=(uint16_t)(14000u+(i*109u)%42000u);
        r.note=category>=4u?DRUM_LANE_NOTE[(cell+group)%NLANE]:(uint8_t)(48u+(cell%12u)+group*3u);
        r.vel=(uint8_t)(70u+i%58u);r.owner=(uint8_t)(k*NPAT+(tails?3u<<5:0));
        r.step=(uint8_t)(cell|(round?64u:0u)|(round && cell==63u?128u:0u));
        recording[i]=r;
        step_t *s=&t->step[view];s->time=ST_NOTE;s->flags|=SF_RECORDED;s->vel=r.vel;
        uint32_t j=0;for(;j<s->n && s->note[j]!=r.note;j++);
        if(j==s->n && s->n<4u)s->note[s->n++]=r.note;
        if(category>=4u)for(uint32_t lane=0;lane<NLANE;lane++)if(DRUM_LANE_NOTE[lane]==r.note)s->hit|=1u<<lane;
        assert(recording_valid(&r));
    }
    recording_reindex();memcpy(fixture_notes,recording,sizeof recording);
    project_capture(&proj_scratch);
    if(!bank_pack(fixture_raw,&proj_scratch,1) || !bank_valid(fixture_raw,sizeof fixture_raw))return 0;
    if(memcmp(proj_scratch.recording,fixture_notes,sizeof fixture_notes))return 0;
    if(proj_scratch.parts!=NTRK || project_restore_runtime(&proj_scratch))return 0;
    bank_restore(fixture_raw);
    if(trk_n()!=NTRK)return 0;
    if(memcmp(recording,fixture_notes,sizeof fixture_notes))return 0;
    uint32_t active=0,owners=0;
    for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel){
        active++;owners|=1u<<((recording[i].owner&31u)/NPAT);
        if(!recording_present(&recording[i]))return 0;
        if((quant && recording[i].on) || (!quant && !recording[i].on))return 0;
    }
    return active==count && owners==(spread?15u:1u);
}
#ifndef AUDIO_CPU_FIXTURE_NO_MAIN
int main(int argc,char **argv)
{
    if(argc==7 && !strcmp(argv[1],"--emit")){
        /* Strict finite integer arguments; never print diagnostics into FBKK. */
        uint32_t values[4];
        for(uint32_t i=0;i<4;i++){
            char *end=0;unsigned long v=strtoul(argv[2u+i],&end,10);
            if(!argv[2u+i][0] || *end || v>RECORD_MAX)return 2;
            values[i]=(uint32_t)v;
        }
        if(!make_fixture(values[0],values[1],values[2],values[3],argv[6]))return 2;
        return fwrite(fixture_raw,1,sizeof fixture_raw,stdout)!=sizeof fixture_raw || fflush(stdout)?2:0;
    }
    if(argc!=1)return 2;
    static const uint32_t counts[]={64,256,RECORD_MAX};
    static const char *const assignments[]={"0124","0000","1111","2222","3333","4444","5555"};
    uint32_t cases=0;
    for(uint32_t e=0;e<NELEM(assignments);e++)for(uint32_t c=0;c<NELEM(counts);c++)
        for(uint32_t quant=0;quant<2;quant++)for(uint32_t spread=0;spread<2;spread++)for(uint32_t tails=0;tails<2;tails++){
            assert(make_fixture(counts[c],quant,spread,tails,assignments[e]));cases++;
        }
    assert(!make_fixture(65,0,0,0,"0124"));assert(!make_fixture(64,2,0,0,"0124"));
    assert(!make_fixture(64,0,0,0,"0126"));
    printf("CPU playback fixtures: %u density/timing/layout/tail/engine cases, exact recording roundtrip and overview ownership\n",cases);
    return 0;
}
#endif
