/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Original routing retained from 5da9f0d, Melodee changes 32e26696/fa64ef07.
 * The earlier Felucca C port derives from MSFA/Dexed (Copyright 2012 Google Inc.,
 * Copyright 2016-2025 Pascal Gauthier, Apache-2.0); see fm6_core.c and LICENSING.md.
 * Keep exact state transitions as the independent optimization oracle. */
#define MELODEE_FM6_FAST 1
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
    if (!proc_pid_rusage(getpid(),RUSAGE_INFO_V4,(rusage_info_t *)&r)) return r.ri_instructions;
#endif
    return 0;
}
static __attribute__((noinline)) void plan_reference(fm6_voice_t *s, const int32_t *lv, uint32_t alg, uint32_t eng, uint32_t fbshift)
{
    uint32_t k, has = 1u, fb_on = fbshift < 16u;
    /* MODERN gains are linear; MARK I/OPL gains are chip attenuation codes.
     * A live mode switch must not interpolate across these different units. */
    if(s->eng!=eng){
        for(k=0;k<6u;k++)s->gout[k]=0.0f;
        s->fb[0]=s->fb[1]=0.0f;
    }
    s->eng = (uint8_t)eng;
    s->loop = 0;
    for (k = 0; k < 6u; k++) {
        uint32_t f = FM6_ALG[alg][k], out = f & 3u, in = (f >> 4) & 3u, add = f & 4u, run;
        float g1, g2;
        if (eng == FM6_MARK1 && !k && fb_on && (alg == 3u || alg == 5u))
            f = 0xc4u, out = 0, in = 0, add = 4u;        /* MARK I: the loop runs from OP6 */
        if (eng == FM6_MODERN) {
            g1 = s->gout[k];
            g2 = fm_exp2f((float)lv[k] * (1.0f / 16777216.0f) - 14.0f);
            run = g1 >= 1120.0f / 16777216.0f || g2 >= 1120.0f / 16777216.0f;
        } else if (eng == FM6_MARK1) {
            g1 = s->gout[k] ? s->gout[k] : 16383;
            g2 = 16384 - (lv[k] >> 14);
            run = g1 <= 16284 || g2 <= 16284;
        } else {
            g1 = s->gout[k] ? s->gout[k] : 511;
            g2 = 512 - (lv[k] >> 19);
            run = g1 <= 507 || g2 <= 507;
        }
        s->gout[k] = g2;
        s->plan[k] = 0;
        if (!run) {
            if (!add)
                has &= ~(1u << out);
            s->ph[k] += (uint32_t)s->fq[k] << FM6_LG_N;
            continue;
        }
        if (!((has >> out) & 1u))
            add = 0;
        if (in && !((has >> in) & 1u))
            in = 0;
        s->g[k] = g1;
        s->dg[k] = eng == FM6_MODERN ? (g2 - g1) * (1.0f / FM6_N)
                     : (float)(((int32_t)(g2 - g1) + (FM6_N >> 1)) >> FM6_LG_N);
        s->plan[k] = (uint8_t)(FM6_P_RUN | out | add | in << 4);
        if (!in && (f & 0xc0u) == 0xc0u && fb_on) {
            s->plan[k] |= FM6_P_FB;
            s->fbs = (uint8_t)fbshift;
            if (eng == FM6_MARK1 && (alg == 3u || alg == 5u || alg == 31u))
                s->fbs = (uint8_t)(fbshift + 2u < 16u ? fbshift + 2u : 16u);
            if (eng == FM6_MARK1 && (alg == 3u || alg == 5u)) {   /* the loop's other operators: a fixed gain */
                uint32_t j;
                s->loop = (uint8_t)(alg == 3u ? 3 : 2);
                for (j = 1; j < s->loop; j++) {
                    s->gout[j] = 16384 - (lv[j] >> 14);
                    s->g[j] = s->gout[j] ? s->gout[j] : 16383;
                    s->dg[j] = 0;
                    s->plan[j] = 0;
                }
                k += s->loop - 1u;
            }
        }
        has |= 1u << out;
    }
}
static __attribute__((noinline)) void plan_candidate(fm6_voice_t *s, const int32_t *lv, uint32_t alg, uint32_t eng, uint32_t fbshift)
{ fm6_plan(s,lv,alg,eng,fbshift); }

static uint32_t random_state = 0x5783492u;
static uint32_t next_u(void) { random_state = random_state * 1664525u + 1013904223u; return random_state; }
static void parity(void)
{
    fm6_voice_t a={0},b={0}; int32_t lv[6];
    static const float previous[] = {0.0f, -0.0f, 1120.0f / 16777216.0f,
                                    1.0f / 16384.0f, 1.0f, 16383.0f, 511.0f};
    uint32_t cases = 0;
    assert(fm_exp2f(-14.0f) == 1.0f / 16384.0f);
    for (uint32_t alg=0;alg<32;alg++) for(uint32_t eng=0;eng<3;eng++)
        for(uint32_t fb=0;fb<=16;fb++) for(uint32_t pattern=0;pattern<8;pattern++)
            for(uint32_t edge=0;edge<NELEM(previous);edge++) {
                memset(&a,0,sizeof a);
                a.eng=(uint8_t)((eng+edge)%3);
                a.fb[0]=0.5f; a.fb[1]=-0.125f;
                for(uint32_t k=0;k<6;k++) {
                    a.gout[k]=previous[edge]; a.ph[k]=UINT32_MAX-k;
                    a.fq[k]=(int32_t)(next_u()&0x7fffffffu);
                    lv[k]=pattern==0?0:pattern==1?1:pattern==2?(20<<24):
                          pattern==3?((int32_t)(next_u()%(20u<<24))):
                          pattern==4?(k?0:14<<24):pattern==5?(k?14<<24:0):
                          pattern==6?(1638400+(int32_t)k-3):14<<24;
                }
                b=a;
                plan_reference(&a,lv,alg,eng,fb); plan_candidate(&b,lv,alg,eng,fb);
                assert(!memcmp(&a,&b,sizeof a)); cases++;
            }
    memset(&a,0,sizeof a); memset(&b,0,sizeof b);
    for (uint32_t k=0;k<6;k++) a.fq[k]=b.fq[k]=(int32_t)(next_u()&0x7fffffffu);
    for (uint32_t i=0;i<200000;i++) {
        uint32_t alg=next_u()%32, eng=next_u()%3, fb=next_u()%9;
        for(uint32_t k=0;k<6;k++) lv[k]=i%4?((int32_t)(next_u()%(20u<<24))):0;
        if(i%7==0) lv[i%6]=0;
        if(i%9==0) fb=16;
        plan_reference(&a,lv,alg,eng,fb); plan_candidate(&b,lv,alg,eng,fb);
        assert(!memcmp(&a,&b,sizeof a));
    }
    printf("FM6 zero-level plan: %u algorithm/mode/feedback/gain-boundary cases and 200000 changing blocks exact\n", cases);
}
static void benchmark(void)
{
    puts("pattern,reference_instructions_per_plan,candidate_instructions_per_plan");
    for(uint32_t mode=0;mode<7;mode++) {
        double cost[2];
        for(uint32_t fast=0;fast<2;fast++) {
            fm6_voice_t s={0};int32_t lv[6];
            void (*volatile run)(fm6_voice_t *,const int32_t *,uint32_t,uint32_t,uint32_t)=fast?plan_candidate:plan_reference;
            uint64_t start=instructions();
            for(uint32_t i=0;i<300000;i++) {
                for(uint32_t k=0;k<6;k++) lv[k]=mode==0?0:mode==1?(k?0:14<<24):mode==2?14<<24:mode==3?(int32_t)((i*65537u+k)%(20u<<24)):mode==4?(i&1?0:14<<24):14<<24;
                run(&s,lv,0,mode==5?1:mode==6?2:0,4);
            }
            cost[fast]=(double)(instructions()-start)/300000;
        }
        printf("%u,%.2f,%.2f\n",mode,cost[0],cost[1]);
        if(cost[0]) assert(cost[1] < cost[0]*(mode<2?.85:1.04));
    }
}
int main(int argc,char **argv) { parity(); if(argc==1)benchmark(); return 0; }
