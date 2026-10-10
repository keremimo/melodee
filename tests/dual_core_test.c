/* SPDX-License-Identifier: GPL-3.0-only */
/* Run the real FM6/Prophet pairing on a concurrent host worker, compare the complete
 * mixer to serial output. Controls and lifecycle stay on the producer. */
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include <assert.h>
#include <sched.h>
static uint32_t worker_clock(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000u);
}
#define MELODEE_DUAL_CORE 1
#define AW_WORD _Atomic uint32_t
#define AW_LOAD(p) atomic_load_explicit(p, memory_order_acquire)
#define AW_STORE(p,v) atomic_store_explicit(p,v,memory_order_release)
#define AW_TICKS() worker_clock()
#define AW_TIMEOUT_TICKS 2000000u
#define AW_RAM_LOOP static
#define AW_IDLE() pthread_testcancel()
/* The host cannot stop the thread mid-job: holding waits until it touches nothing more. */
static void hold_worker(void);
static uint32_t injected_faults;
#define AW_HOLD() hold_worker()
#define AW_FAULTS() injected_faults
#include "../firmware/src/audio_worker.h"
static void hold_worker(void)
{
    while (AW_LOAD(&audio_worker.complete) != AW_LOAD(&audio_worker.request)) sched_yield();
}
/* Reference rendering keeps the same online allocator/caps as the parallel
 * run, but refuses submissions so the actual kernel executes on CPU0. */
static int serial_render;
#define audio_worker_submit(fn,context) (serial_render ? 0 : audio_worker_submit(fn,context))
#define main hostsim_main
#include "hostsim.c"
#undef main

#define FRAMES (CTL * 256u)
static int32_t reference[FRAMES * 2];
static p5_part_t prophet_state_reference[NPART][FRAMES/CTL];
static uint8_t prophet_state_present[NPART][FRAMES/CTL];
static _Atomic uint32_t held_job_entered, held_job_release;
static void held_job(void *context)
{
    assert(context == &held_job_release);
    atomic_store(&held_job_entered, 1);
    while (!atomic_load(&held_job_release)) sched_yield();
}

static void *worker_thread(void *unused)
{
    (void)unused;
    audio_worker_loop();
    return 0;
}
static void setup_case(uint32_t alg, uint32_t eng, uint32_t voices)
{
    memset(trk, 0, sizeof trk);
    eng_state_reset();
    memset(fm6_eff, 0, sizeof fm6_eff);
    memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target);
    memset(&mod, 0, sizeof mod);
    memset(&fx, 0, sizeof fx);
    memset(resource, 0, sizeof resource);
    cho_buf = rev_comb = 0; rev_memory = 0;
    dly_buf = 0; memset(&dl, 0, sizeof dl);           /* (the delay: no line, every state at rest) */
    cho_idle = rev_scan = 0; rev_hold_dc = 0;
    memset(rev_hold_comb, 0, sizeof rev_hold_comb);
    memset(rev_hold_ap, 0, sizeof rev_hold_ap);
    memset(mix_cache, 0, sizeof mix_cache);
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    rng_state = 0x12345678u;
    mod_seed = 0x2545F491u;
    vage = 0; panic_req = 0;
    memset(&pf, 0, sizeof pf);
    pf.src = pf.next = PF_N; pf.lc = PF_TOP;
    for (uint32_t i = 0; i < NTRK; i++) pf.mg[i] = 32768;
    perf_held = perf_act = perf_mask = perf_solo = perf_kill = 0;
    memset((void *)perf_k, 0, sizeof perf_k);
    host_tracks_init();
    fm6_fn_reset();
    for (uint32_t i = 0; i < NTRK; i++) {
        host_preset(&trk[i], ENGI_FM6, i);
        uint8_t patch[FP_SIZE + 1];
        fm6_unpack(FM6_INIT, patch);
        /* Exercise every operator bus and feedback route, not just the init
         * patch's single audible carrier. */
        for (uint32_t op = 0; op < 6; op++) {
            uint8_t *p = patch + op * FP_OP;
            p[FP_OL] = 75u + op * 4u; p[FP_FC] = 1u + op % 3u;
            p[FP_R1] = 90; p[FP_R1 + 1] = 75; p[FP_R1 + 2] = 70; p[FP_R1 + 3] = 80;
            p[FP_L1] = 99; p[FP_L1 + 1] = 80; p[FP_L1 + 2] = 72; p[FP_L1 + 3] = 0;
        }
        patch[FP_ALG] = alg; patch[FP_FB] = 7;
        fm6_put_patch(i, patch, 1);
        FM6F(&trk[i])[FN_ENGINE] = eng;
        trk[i].p[P_LEVEL] = 70;
        trk[i].p[P_CHOR] = 24; trk[i].p[P_REV] = 36;
        trk[i].p[P_M1SRC] = MS_LFO; trk[i].p[P_M1DST] = MD_PITCH; trk[i].p[P_M1AMT] = 12;
        trk[i].p[P_M2SRC] = MS_VEL; trk[i].p[P_M2DST] = MD_PAN; trk[i].p[P_M2AMT] = 7;
        trk[i].p[P_VOICE] = voices == 8 ? V_UNISON : V_POLY;
    }
    for (uint32_t i = 0; i < voices; i++) trk_note_on(&trk[i % NTRK], 48u + i, 70u + i * 3u);
}
static void render_case(int parallel, uint32_t alg, uint32_t eng, uint32_t voices)
{
    setup_case(alg, eng, voices);
    audio_worker_online = parallel;
    for (uint32_t pos = 0; pos < FRAMES; pos += CTL) {
        int32_t out[2 * CTL];
        if (pos == CTL * 32u) midi_bend_target[0] = 256;
        if (pos == CTL * 64u) for (uint32_t i = 0; i < voices; i++) trk_note_off(&trk[i % NTRK], 48u + i);
        if (pos == CTL * 96u) for (uint32_t i = 0; i < voices; i++) trk_note_on(&trk[i % NTRK], 60u + i, 110u);
        if (pos == CTL * 160u) { trk[0].eng_req = 0; trk[0].p[P_E0] = 1; }
        mix_block(out, CTL);
        assert(!fm6_pending);
        assert(AW_LOAD(&audio_worker.request) == AW_LOAD(&audio_worker.complete));
        if (!parallel) memcpy(reference + 2u * pos, out, sizeof out);
        else if (memcmp(reference + 2u * pos, out, sizeof out)) {
            fprintf(stderr, "difference: algorithm %u engine %u voices %u block %u\n", alg, eng, voices, pos/CTL);
            abort();
        }
    }
}
static uint32_t active_voices(const track_t *t)
{
    uint32_t count=0;
    for(uint32_t i=0;i<NVOICE;i++)count+=t->v[i].active && t->v[i].stage!=4u;
    return count;
}
static void prophet_allocation(void)
{
    setup_case(0,0,0);
    memset(p5_ready,0,sizeof p5_ready);
    host_preset(&trk[0],ENGI_PROPHET,0);
    audio_worker_online=0;
    for(uint32_t i=0;i<8;i++)trk_note_on(&trk[0],48+i,100);
    assert(active_voices(&trk[0])==5 && voices_busy()==15);
    setup_case(0,0,0);
    memset(p5_ready,0,sizeof p5_ready);
    host_preset(&trk[0],ENGI_PROPHET,0);
    audio_worker_online=1;
    for(uint32_t i=0;i<8;i++)trk_note_on(&trk[0],48+i,100);
    assert(active_voices(&trk[0])==8 && voices_busy()==VBUDGET);
    for(uint32_t i=1;i<NTRK;i++)trk_note_on(&trk[i],60+i,100);
    assert(active_voices(&trk[0])==6 && voices_busy()<=VBUDGET);
    for(uint32_t i=1;i<NTRK;i++)assert(active_voices(&trk[i])==1);
    /* Native factory unison stays at its requested five; an explicit count
     * of eight is allowed only with an online worker. */
    setup_case(0,0,0);memset(p5_ready,0,sizeof p5_ready);
    host_preset(&trk[0],ENGI_PROPHET,0);
    p5_patch_t *p=p5_patch_of(&trk[0]);p->raw[P5_UNISON]=1;p->raw[P5_UNISON_COUNT]=5;
    p5_track_accept(&trk[0]);trk_note_on(&trk[0],60,100);
    assert(active_voices(&trk[0])==5);
    panic_req=1;events_block(CTL);
    p->raw[P5_UNISON_COUNT]=8;trk_note_on(&trk[0],64,100);
    assert(active_voices(&trk[0])==8 && voices_busy()==VBUDGET);
    audio_worker_online=0;
    assert(p5_cap(&trk[0])==5 && p5_units(&trk[0])==3);
    audio_worker_online=1;
}
/* A CPU1 fault mid-song, as the exception handler reports it: a join still
 * waiting drops the paired voice's block, else the next block's check (as
 * audio.c) retires the worker. CPU0 then renders all eight sounding voices;
 * new notes get the five-voice cap. */
static void prophet_retire(void)
{
    setup_case(0,0,0);
    memset(p5_ready,0,sizeof p5_ready);
    audio_worker_online=1;serial_render=0;
    host_preset(&trk[0],ENGI_PROPHET,0);
    for(uint32_t i=0;i<8;i++)trk_note_on(&trk[0],48+i*3,100);
    assert(active_voices(&trk[0])==8);
    uint32_t jobs=0;int32_t peak=0;
    for(uint32_t pos=0;pos<CTL*64u;pos+=CTL){
        int32_t out[2*CTL];
        audio_worker_check();
        if(pos==CTL*16u)injected_faults=1;
        mix_block(out,CTL);
        assert(!p5_pending && AW_LOAD(&audio_worker.request)==AW_LOAD(&audio_worker.complete));
        if(pos==CTL*17u){assert(!audio_worker_online);jobs=audio_worker.jobs;}
        if(pos>CTL*16u)for(uint32_t i=0;i<2u*CTL;i++)if(abs(out[i])>peak)peak=abs(out[i]);
    }
    assert(audio_worker.jobs==jobs && peak>0 && active_voices(&trk[0])==8);
    assert(p5_cap(&trk[0])==5 && p5_units(&trk[0])==3);
    panic_req=1;events_block(CTL);
    for(uint32_t i=0;i<8;i++)trk_note_on(&trk[0],60+i,100);
    uint32_t held=0;
    for(uint32_t i=0;i<NVOICE;i++)held+=trk[0].v[i].active && trk[0].v[i].gate;
    assert(held==5);
    injected_faults=0;audio_worker_online=1;
}
static void prophet_case(int parallel,uint32_t preset,uint32_t mode,uint32_t voices,int mixed)
{
    setup_case(0,0,0);
    memset(p5_ready,0,sizeof p5_ready);
    audio_worker_online=1;
    serial_render=!parallel;
    host_preset(&trk[0],ENGI_PROPHET,preset);
    p5_patch_t *patch=p5_patch_of(&trk[0]);
    if(!preset){
        uint8_t *p=patch->raw;
        p[P5_FILTER_REV]=mode&1u;
        p[P5_RESONANCE]=mode>=2?127:80;p[P5_SYNC]=mode>=2;
        p[P5_POLY_B]=110;p[P5_POLY_ENV]=127;
        p[P5_POLY_FREQ]=p[P5_POLY_PW]=p[P5_POLY_FILTER]=mode>=4;
        p[P5_SAW_A]=p[P5_PULSE_A]=p[P5_SAW_B]=p[P5_PULSE_B]=p[P5_TRI_B]=1;
        p[P5_NOISE]=65;p[P5_LEVEL_A]=p[P5_LEVEL_B]=110;
        p[P5_VINTAGE]=127;p[P5_RELEASE_ON]=1;p[P5_RELEASE_AMP]=80;
        p[P5_LOW_B]=mode>=6;p[P5_GLIDE]=60;
        p[P5_WHEEL_FREQ_A]=p[P5_WHEEL_FREQ_B]=p[P5_WHEEL_PW_A]=p[P5_WHEEL_PW_B]=p[P5_WHEEL_FILTER]=1;
        p[P5_LFO_TRI]=1;p[P5_LFO_RATE]=80;p[P5_LFO_INITIAL]=100;
    }
    /* Ensure requested polyphony independent of native factory unison. */
    if(mode>=8){patch->raw[P5_UNISON]=1;patch->raw[P5_UNISON_COUNT]=voices;}
    trk[0].p[P_VOICE]=mode>=8?V_UNISON:V_POLY;
    trk[0].p[P_CHOR]=24;trk[0].p[P_REV]=36;trk[0].p[P_DLY]=20;
    trk[0].mw=90;trk[0].at=64;
    trk[0].p[P_M1SRC]=MS_LFO;trk[0].p[P_M1DST]=MD_PITCH;trk[0].p[P_M1AMT]=12;
    for(uint32_t i=0;i<voices;i++)trk_note_on(&trk[0],48+i*3,70+i*7);
    if(mixed)for(uint32_t i=1;i<NTRK;i++){
        if(mixed==2){host_preset(&trk[i],ENGI_PROPHET,0);*p5_patch_of(&trk[i])=*patch;}
        trk_note_on(&trk[i],60+i,100);
        if(mixed==2)trk_note_on(&trk[i],67+i,90);
    }
    assert(voices_busy()<=VBUDGET);
    if(!mixed)assert(active_voices(&trk[0])==voices);
    for(uint32_t pos=0;pos<FRAMES;pos+=CTL){
        int32_t out[2*CTL];
        if(pos==CTL*32u)midi_bend_target[0]=256;
        if(pos==CTL*64u)for(uint32_t i=0;i<voices;i++)trk_note_off(&trk[0],48+i*3);
        if(pos==CTL*96u)for(uint32_t i=0;i<voices;i++)trk_note_on(&trk[0],60+i,110);
        if(pos==CTL*128u){patch->raw[P5_FILTER_REV]^=1;patch->raw[P5_CUTOFF]=110;}
        if(pos==CTL*160u)trk[0].eng_req=ENGI_FM6;
        if(pos==CTL*192u){panic_req=1;events_block(CTL);}
        mix_block(out,CTL);
        assert(!p5_pending && !fm6_pending);
        assert(AW_LOAD(&audio_worker.request)==AW_LOAD(&audio_worker.complete));
        if(!parallel)memcpy(reference+2u*pos,out,sizeof out);
        else if(memcmp(reference+2u*pos,out,sizeof out)){
            fprintf(stderr,"Prophet difference: preset %u mode %u voices %u mixed %d block %u\n",preset,mode,voices,mixed,pos/CTL);
            abort();
        }
        /* Slow factory attacks may still be silent: compare every oscillator,
         * filter and noise state too, so zero PCM cannot hide a lost voice. */
        for(uint32_t part=0;part<NPART;part++){
            uint32_t block=pos/CTL;
            int present=trk[part].engine==ENGI_PROPHET;
            if(!parallel){
                prophet_state_present[part][block]=present;
                if(present)prophet_state_reference[part][block]=*p5_part(part);
            }else{
                assert(present==prophet_state_present[part][block]);
                if(present)assert(!memcmp(&prophet_state_reference[part][block],p5_part(part),sizeof(p5_part_t)));
            }
        }
    }
    serial_render=0;
}
int main(void)
{
    pthread_t thread;
    assert(!pthread_create(&thread, 0, worker_thread, 0));
    while (!AW_LOAD(&audio_worker.ready)) sched_yield();
    audio_worker_online = 1;
    assert(audio_worker_submit(held_job, &held_job_release));
    while (!atomic_load(&held_job_entered)) sched_yield();
    assert(!audio_worker_submit(fm6_worker_kernel, 0));
    assert(audio_worker.context == &held_job_release);
    atomic_store(&held_job_release, 1);
    audio_worker_join();
    for (uint32_t eng = 0; eng < 3; eng++)
        for (uint32_t alg = 0; alg < 32; alg++)
            for (uint32_t voices = 1; voices <= 16; voices++) {
                render_case(0, alg, eng, voices);
                render_case(1, alg, eng, voices);
            }
    prophet_allocation();
    uint32_t prophet_cases=0;
    for(uint32_t mode=0;mode<8;mode++)for(uint32_t voices=1;voices<=8;voices++)for(int mixed=0;mixed<2;mixed++){
        prophet_case(0,0,mode,voices,mixed);prophet_case(1,0,mode,voices,mixed);prophet_cases++;
    }
    for(uint32_t preset=1;preset<=P5_FACTORY_N;preset++){
        prophet_case(0,preset,0,8,0);prophet_case(1,preset,0,8,0);prophet_cases++;
    }
    for(uint32_t mode=8;mode<10;mode++)for(uint32_t voices=5;voices<=8;voices+=3){
        prophet_case(0,0,mode,voices,0);prophet_case(1,0,mode,voices,0);prophet_cases++;
    }
    for(uint32_t mode=0;mode<8;mode++)for(uint32_t voices=1;voices<=2;voices++){
        prophet_case(0,0,mode,voices,2);prophet_case(1,0,mode,voices,2);prophet_cases++;
    }
    assert(p5_pairs>0);
    prophet_retire();
    audio_worker_online = 0;
    assert(!audio_worker_submit(fm6_worker_kernel, 0));
    assert(audio_worker.jobs > 0);
    assert(!pthread_cancel(thread));
    assert(!pthread_join(thread, 0));
    /* Recreate an idle worker at the sequence boundary: 0 is a valid wrapped
     * request, not an empty-job sentinel. */
    AW_STORE(&audio_worker.ready, 0);
    AW_STORE(&audio_worker.request, UINT32_MAX);
    AW_STORE(&audio_worker.complete, UINT32_MAX);
    atomic_store(&held_job_entered, 0);
    assert(!pthread_create(&thread, 0, worker_thread, 0));
    while (!AW_LOAD(&audio_worker.ready)) sched_yield();
    audio_worker_online = 1;
    assert(audio_worker_submit(held_job, &held_job_release));
    audio_worker_join();
    assert(atomic_load(&held_job_entered));
    assert(AW_LOAD(&audio_worker.request) == 0);
    assert(!pthread_cancel(thread));
    assert(!pthread_join(thread, 0));
    printf("dual-core: 1536 FM6 + %u Prophet serial/concurrent cases identical; %u FM6 / %u Prophet pairs; eight-voice allocation, retire mid-song, busy/offline/wrap ok\n", prophet_cases,fm6_pairs,p5_pairs);
    return 0;
}
