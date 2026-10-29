/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define RECORDING_NO_MAIN 1
#include "recording_test.c"

static void undo_fixture(uint32_t source,uint32_t q)
{
    pass_begin(source,4,q);seq_stop();song.rec=0;
    track_t *t=TSEL;
    recorded_note_t r={0};recorded_set_note(&r,60);r.vel=71;r.duration=12345;r.owner=(uint8_t)recording_owner(t);
    t->step[0].time=ST_NOTE;t->step[0].flags=SF_RECORDED;t->step[0].n=1;t->step[0].note[0]=60;t->step[0].vel=71;
    recording_restore_note(t,0,r);
    t->step[1].time=ST_NOTE;t->step[1].n=2;t->step[1].note[0]=64;t->step[1].note[1]=67;t->step[1].vel=90;
    t->step[2].time=ST_TIE;
    motion_set_event(t,0,P_LEVEL,42);
    motion_set_event(&trk[1],0,P_LEVEL,33);
}
static void undo_take_start(void)
{
    rec_set(TSEL,1);seq_start();seq_advance(0);
}
static int whole_take(void)
{
    int bad=0;
    for(uint32_t source=0;source<3;source++)for(uint32_t q=0;q<=3;q+=3){
        undo_fixture(source,q);track_t *t=TSEL;
        recorded_note_t before[RECORD_MAX];memcpy(before,recording,sizeof before);
        step_t steps[NSTEP];memcpy(steps,t->step,sizeof steps);
        motion_store_t motion_before;motion_snapshot_track(t,&motion_before);
        undo_take_start();input_on(t,60,111);motion_capture(t,P_LEVEL,78);
        seq_advance(300);input_off(t,60);
        seq_advance(step_samples(t,seq_div_samples(t->p[P_SDIV]),0)-300u);
        seq_advance(step_samples(t,seq_div_samples(t->p[P_SDIV]),1));
        seq_advance(step_samples(t,seq_div_samples(t->p[P_SDIV]),2));
        seq_advance(1700);input_on(t,72,99);seq_advance(200);input_off(t,72);
        seq_advance(step_samples(t,seq_div_samples(t->p[P_SDIV]),3)-1900u);
        bad+=check("completed take owns one undo slot while playback continues",song.playing&&!rec_on(t)&&recording_undo_state()&&!recording_undo_state()->active);
        recorded_note_t after[RECORD_MAX];memcpy(after,recording,sizeof after);
        step_t after_steps[NSTEP];memcpy(after_steps,t->step,sizeof after_steps);
        motion_store_t after_motion;motion_snapshot_track(t,&after_motion);
        /* Audio blocks used to discard edit snapshots here. */
        events_block(0);
        if(q)go_page(GR_ROLL);else go_home();hold(B_EDIT);
        motion_store_t actual;motion_snapshot_track(t,&actual);
        bad+=check("EDIT undoes a whole take: overwritten note, chord/ties, exact timing and motion",
            !memcmp(before,recording,sizeof before)&&!memcmp(steps,t->step,sizeof steps)&&!memcmp(&motion_before,&actual,sizeof actual)&&song.playing);
        bad+=check("whole-take undo preserves another track's automation",motion_count(&trk[1])==1);
        edit_redo();motion_snapshot_track(t,&actual);
        bad+=check("EDIT + OCT+ restores the complete finalized and quantized take",
            !memcmp(after,recording,sizeof after)&&!memcmp(after_steps,t->step,sizeof after_steps)&&!memcmp(&after_motion,&actual,sizeof actual));
        bad+=check("one slot cannot undo or redo past either end",recording_history(1)==0);
        seq_stop();led_pos_init();
    }
    return bad;
}
static int lifecycle(void)
{
    int bad=0;
    for(uint32_t stop=0;stop<2;stop++){
        undo_fixture(0,0);recorded_note_t original=recording[0];undo_take_start();
        input_on(TSEL,60,113);seq_advance(800);
        bad+=check("undo during capture is refused without interrupting the take",recording_history(0)&&recording[0].vel==113&&rec_on(TSEL));
        if(stop)seq_stop();else rec_set(TSEL,0);
        recorded_note_t finished=recording[0];
        bad+=check("early stop/disarm seals held duration before undo",!(recording_flags[0]&1u)&&finished.duration>1u);
        bad+=check("early take undo restores the overwritten event",recording_history(0)&&!memcmp(&original,&recording[0],sizeof original));
        bad+=check("early take redo restores the sealed duration",recording_history(1)&&!memcmp(&finished,&recording[0],sizeof finished));
        input_off(TSEL,60);seq_stop();
    }
    undo_fixture(0,0);undo_take_start();input_on(TSEL,60,100);seq_advance(100);input_off(TSEL,60);rec_set(TSEL,0);
    recording_undo_t *slot=recording_undo_state();uint32_t signature=slot->signature;
    rec_set(TSEL,1);rec_set(TSEL,0);
    bad+=check("arm/cancel leaves the previous take undo available",recording_undo_state()==slot&&slot->signature==signature);
    rec_set(TSEL,1);seq_stop();seq_start();seq_advance(0);
    seq_advance(recording_loop(TSEL,seq_div_samples(TSEL->p[P_SDIV])));
    bad+=check("a pass without input keeps the previous take undo",recording_undo_state()==slot&&slot->signature==signature);
    rec_set(TSEL,1);seq_stop();seq_start();seq_advance(0);
    recorded_note_t previous=recording[0];input_on(TSEL,60,120);seq_advance(200);input_off(TSEL,60);rec_set(TSEL,0);
    bad+=check("a new take replaces the single slot",recording_history(0)&&!memcmp(&previous,&recording[0],sizeof previous));
    bad+=check("there is no second recording undo level",!recording_history(0));
    recording_history(1);seq_stop();pattern_switch(TSEL,1);
    bad+=check("a bank switch prevents undo from writing to the wrong pattern",!recording_history(0));
    pattern_switch(TSEL,0);bad+=check("switching back does not resurrect an old take undo",!recording_history(0));
    undo_fixture(0,0);undo_take_start();input_on(TSEL,60,110);seq_advance(100);input_off(TSEL,60);seq_stop();
    project_capture(&proj_scratch);project_restore_runtime(&proj_scratch);
    bad+=check("project restore discards recording history",!recording_history(0));
    led_pos_init();return bad;
}
static int motion_only(void)
{
    int bad=0;
    for(uint32_t native=0;native<2;native++){
        undo_fixture(0,0);track_t *t=TSEL;
        if(native){set_engine_of(t,ENGI_FM6);apply_preset_to(t,0);motion_set_event(t,0,MO_NATIVE,(int16_t)(MO_TAG_FM6<<8|50u));}
        motion_store_t before;motion_snapshot_track(t,&before);
        recorded_note_t notes[RECORD_MAX];memcpy(notes,recording,sizeof notes);
        uint8_t patch[FP_SIZE];memcpy(patch,fm6_patch_of(0),sizeof patch);
        undo_take_start();
        if(native){uint8_t old[MO_NATIVE_N];motion_native_peek(t,old);fm6_patch_of(0)[0]=80;fm6_patch_of(0)[1]=81;motion_native_edited(t,old);}
        else motion_capture(t,P_LEVEL,77);
        seq_advance(500);motion_capture(t,P_PAN,17);rec_set(t,0);
        motion_store_t after;motion_snapshot_track(t,&after);
        bad+=check("motion-only take is undoable without changing notes",recording_history(0)&&!memcmp(notes,recording,sizeof notes));
        motion_store_t actual;motion_snapshot_track(t,&actual);
        bad+=check("motion undo restores overwritten points and removes all new lanes",!memcmp(&before,&actual,sizeof actual));
        if(native)bad+=check("native motion undo restores the synth's base patch",!memcmp(patch,fm6_patch_of(0),sizeof patch));
        bad+=check("motion-only redo restores every recorded lane",recording_history(1));motion_snapshot_track(t,&actual);
        bad+=check("motion redo reproduces the finished automation",!memcmp(&after,&actual,sizeof actual));
        seq_stop();
    }
    led_pos_init();return bad;
}
static int other_banks_and_memory(void)
{
    int bad=0;undo_fixture(0,0);undo_take_start();input_on(TSEL,60,110);seq_advance(100);input_off(TSEL,60);
    recording_remove(TSEL,0);seq_erase_clear(&TSEL->step[0]);rec_set(TSEL,0);
    /* The take's original slot was legitimately reused by an unrelated bank. */
    recorded_note_t other={0};other.owner=NPAT;recorded_set_note(&other,79);other.vel=100;other.duration=200;
    recording_restore_note(&trk[1],0,other);trk[1].step[0].time=ST_NOTE;trk[1].step[0].flags=SF_RECORDED;
    bad+=check("undo relocates saved events without overwriting another bank's reused slot",recording_history(0)&&!memcmp(&other,&recording[0],sizeof other)&&recording[1].vel==71);
    bad+=check("redo also preserves unrelated events after relocation",recording_history(1)&&!memcmp(&other,&recording[0],sizeof other)&&!recording[1].vel);
    seq_stop();
    undo_fixture(0,0);undo_take_start();input_on(TSEL,60,105);seq_advance(100);input_off(TSEL,60);rec_set(TSEL,0);
    bad+=check("sparse recording snapshot keeps only its occupied slots",resource[RES_RECORD_UNDO].size<sizeof(recording_undo_t)-9000u&&recording_undo_state()->note_count==1u);
    recording_undo_clear();
    /* Fill all resource banks, then fail the first snapshot allocation. */
    memset(resource,0,sizeof resource);
    for(uint32_t b=0;b<RESOURCE_BANKS;b++)if(b!=RESOURCE_CACHE_BANK)
        resource_get(b,(uint32_t)(resource_banks[b].end-resource_banks[b].start));
    rec_set(TSEL,1);seq_stop();seq_start();seq_advance(0);input_on(TSEL,60,119);seq_advance(100);input_off(TSEL,60);
    bad+=check("exhausted undo memory leaves recording operational and reports the limitation",recording[0].vel==119&&!recording_undo_state()&&recording_undo_lost);
    memset(resource,0,sizeof resource);input_on(TSEL,62,95);input_off(TSEL,62);rec_set(TSEL,0);
    bad+=check("memory returning mid-take cannot produce a partial-take undo",!recording_undo_state());
    seq_stop();
    undo_fixture(0,0);memset(resource,0,sizeof resource);
    for(uint32_t b=0;b<3u;b++)resource_get(RES_ENGINE0+1u+b,(uint32_t)(resource_banks[b].end-resource_banks[b].start));
    undo_take_start();input_on(TSEL,60,105);seq_advance(100);input_off(TSEL,60);
    void *audio=resource_get(RES_ENGINE0,RESOURCE_MAIN_CAPACITY);
    bad+=check("audio memory can reclaim the snapshot rather than losing engine allocation",audio&&resource[RES_ENGINE0].size==RESOURCE_MAIN_CAPACITY&&!recording_undo_state()&&recording_undo_lost);
    memset(resource,0,sizeof resource);input_on(TSEL,62,99);input_off(TSEL,62);rec_set(TSEL,0);
    bad+=check("a reclaimed active snapshot cannot restart halfway through the take",!recording_undo_state());
    seq_stop();led_pos_init();return bad;
}
static int capacity_and_edit_order(void)
{
    int bad=0;undo_fixture(0,0);undo_take_start();motion_capture(TSEL,P_PAN,12);seq_advance(100);rec_set(TSEL,0);
    bad+=check("motion take undo frees the new lane",recording_history(0)&&motion_count(TSEL)==1);
    for(uint32_t s=1;motion.count<MOTION_MAX;s++)motion_set_event(&trk[1],s,P_LEVEL,40);
    recorded_note_t before[RECORD_MAX];memcpy(before,recording,sizeof before);
    motion_store_t motion_before=motion;step_t steps[NSTEP];memcpy(steps,TSEL->step,sizeof steps);
    bad+=check("redo refuses full motion capacity without changing notes or automation",recording_history(1)&&
        !memcmp(before,recording,sizeof before)&&!memcmp(&motion_before,&motion,sizeof motion)&&!memcmp(steps,TSEL->step,sizeof steps));
    motion_clear(&trk[1]);bad+=check("redo remains available after motion capacity returns",recording_history(1)&&motion_count(TSEL)==2);
    seq_stop();
    undo_fixture(0,0);go_page(GR_ROLL);frame();
    /* An older bulk transaction must not outrank a new motion-only take. */
    note_transaction_begin();recording[0].vel=72;recording_generation++;note_transaction_end();
    undo_take_start();motion_capture(TSEL,P_LEVEL,79);seq_advance(100);rec_set(TSEL,0);
    hold(B_EDIT);
    motion_store_t actual;motion_snapshot_track(TSEL,&actual);
    bad+=check("motion recording takes priority over an older bulk note undo",recording[0].vel==72&&actual.count==1&&actual.event[0].value==42);
    edit_redo();seq_stop();
    /* A later manual motion edit undoes first; the take can then be undone. */
    mo_edit_begin();motion_set_event(TSEL,0,P_LEVEL,90);load_end(TSEL);
    go_home();hold(B_EDIT);motion_snapshot_track(TSEL,&actual);
    bad+=check("a newer manual motion edit undoes before the recording",actual.event[0].value==79);
    hold(B_EDIT);motion_snapshot_track(TSEL,&actual);
    bad+=check("recording undo remains available after undoing the newer edit",actual.event[0].value==42);
    led_pos_init();return bad;
}
static int note_capacity(void)
{
    int bad=0;undo_fixture(0,0);undo_take_start();
    input_on(TSEL,60,110);seq_advance(100);input_off(TSEL,60);
    seq_advance(100);input_on(TSEL,62,99);seq_advance(100);input_off(TSEL,62);rec_set(TSEL,0);
    recorded_note_t finished[2];memcpy(finished,recording,sizeof finished);
    bad+=check("undo removes all new events before a shared-pool capacity check",recording_history(0)&&recording[0].vel==71&&!recording[1].vel);
    for(uint32_t i=1;i<RECORD_MAX;i++){
        recorded_note_t r={0};r.owner=NPAT;recorded_set_note(&r,(uint8_t)(i%128u));r.vel=90;r.duration=100;r.on=(uint16_t)i;
        recording_restore_note(&trk[1],i,r);
    }
    recorded_note_t before[RECORD_MAX];memcpy(before,recording,sizeof before);
    bad+=check("full note pool refuses redo without overwriting unrelated events",recording_history(1)&&!memcmp(before,recording,sizeof before));
    recording_remove(&trk[1],1);
    bad+=check("redo retries successfully when a note slot is freed",recording_history(1)&&!memcmp(finished,recording,sizeof finished));
    seq_stop();
    undo_fixture(0,0);
    for(uint32_t i=1;i<RECORD_MAX;i++){
        recorded_note_t r={0};recorded_set_note(&r,(uint8_t)(i%128u));r.vel=90;r.duration=100;r.on=(uint16_t)(i*60u);
        recording_restore_note(TSEL,i,r);
    }
    memcpy(before,recording,sizeof before);undo_take_start();input_on(TSEL,60,120);seq_advance(100);input_off(TSEL,60);rec_set(TSEL,0);
    recorded_note_t replacement=recording[0];
    bad+=check("one slot can restore a full 1024-event pattern byte-for-byte",recording_history(0)&&!memcmp(before,recording,sizeof before));
    bad+=check("full-pattern redo restores the replacement without losing any event",recording_history(1)&&!memcmp(&replacement,&recording[0],sizeof replacement)&&!memcmp(before+1,recording+1,sizeof before-sizeof before[0]));
    seq_stop();led_pos_init();return bad;
}
static int latched_take(void)
{
    undo_fixture(0, 3);
    recorded_note_t before[RECORD_MAX]; memcpy(before, recording, sizeof before);
    step_t steps[NSTEP]; memcpy(steps, TSEL->step, sizeof steps);
    motion_store_t previous; motion_snapshot_track(TSEL, &previous);
    press(B_REC); seq_start(); seq_advance(0);
    input_on(TSEL, 60, 110); seq_advance(300); input_off(TSEL, 60);
    uint32_t loop = recording_loop(TSEL, seq_div_samples(TSEL->p[P_SDIV]));
    seq_advance(loop);
    input_on(TSEL, 72, 94); seq_advance(300); input_off(TSEL, 72);
    motion_capture(TSEL, P_LEVEL, 88);
    int bad = check("latched recording keeps one undo snapshot across multiple loops",
        rec_on(TSEL) && recording_undo_state() && recording_undo_state()->active);
    press(B_REC);
    bad += check("manual REC disarm finalizes the complete multi-loop undo slot",
        !rec_on(TSEL) && recording_undo_state() && !recording_undo_state()->active);
    int undone = recording_history(0);
    motion_store_t actual; motion_snapshot_track(TSEL, &actual);
    bad += check("undo restores notes and motion from before all latched loops",
        undone && !memcmp(before, recording, sizeof before) && !memcmp(steps, TSEL->step, sizeof steps) &&
        !memcmp(&previous, &actual, sizeof actual));
    seq_stop(); led_pos_init(); return bad;
}
static int sparse_storage(void)
{
    int bad=0;recorded_note_t source[RECORD_MAX],packed[RECORD_MAX];
    memset(source,0,sizeof source);
    for(uint32_t n=0;n<=RECORD_MAX;n++){
        memcpy(packed,source,sizeof packed);uint32_t count=note_history_pack(packed);
        note_history_expand(packed,count);
        if(memcmp(packed,source,sizeof source)){bad+=check("all sparse/dense counts preserve every slot byte",0);break;}
        if(n<RECORD_MAX){uint32_t at=n*37u%RECORD_MAX;source[at]=(recorded_note_t){.on=(uint16_t)n, .duration=65535u, .note7=127, .vel=127, .owner=255, .step=255, .pitch=-24, .length=1, .extended=3};}
    }
    bad+=check("dense fallback and every sparse count round trip",!memcmp(packed,source,sizeof source));
    undo_fixture(0,0);undo_take_start();input_on(TSEL,60,110);seq_advance(100);input_off(TSEL,60);seq_stop();
    memcpy(source,recording,sizeof source);recording_undo_t *u=recording_undo_state();uint32_t size=resource[RES_RECORD_UNDO].size,signature=u->signature;
    for(uint32_t b=0;b<RESOURCE_BANKS;b++)if(b!=RESOURCE_CACHE_BANK || RESOURCE_CACHE_AVAILABLE){
        uint32_t bytes=(uint32_t)(resource_banks[b].end-resource_banks[b].start);
        if((uintptr_t)u>=(uintptr_t)resource_banks[b].start&&(uintptr_t)u<(uintptr_t)resource_banks[b].end)bytes-=size;
        resource_allocate(b,bytes);
    }
    bad+=check("failed sparse expansion preserves the take and its retryable undo",recording_undo_apply(0)==4&&!memcmp(source,recording,sizeof source)&&resource[RES_RECORD_UNDO].size==size&&recording_undo_state()->signature==signature);
    for(uint32_t b=0;b<RESOURCE_BANKS;b++)resource_release(b);
    bad+=check("sparse undo retries after working memory returns",recording_undo_apply(0)==1&&recording[0].vel==71);
    led_pos_init();return bad;
}

int main(void)
{
    int bad=latched_take()+whole_take()+lifecycle()+motion_only()+other_banks_and_memory()+capacity_and_edit_order()+note_capacity()+sparse_storage();
    printf("%s\n",bad?"RECORDING UNDO TEST FAILED":"Recording undo tests passed");return !!bad;
}
