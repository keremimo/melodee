/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Original performance events through real recording, panel, playback and storage paths. */
#define RECORDING_NO_MAIN 1
#include "recording_test.c"

static int selection_and_delete(void)
{
    raw_begin(); track_t *t = &trk[0];
    seq_advance(600); input_on(t,60,70); input_on(t,64,90);
    seq_advance(300); input_off(t,60);
    seq_advance(300); input_off(t,64);
    seq_advance(600); input_on(t,60,111);
    seq_advance(600); input_off(t,60);
    seq_advance(2400); input_on(t,67,95);
    seq_advance(600); input_off(t,67);
    seq_stop(); song.rec = 0;
    recorded_note_t saved[4]; memcpy(saved,recording,sizeof saved);
    go_page(GR_ROLL); cursor_set(0); frame();
    uint32_t count;
    int bad = check("NOTES selects chronological hits in their actual interval, including a hit rounded to the next overview step",
                    notes_selected(t)==0 && notes_rank(t,0,&count)==1 && count==4 && recording_view(t,&recording[3])==1);
    notes_cycle(2); frame();
    bad += check("HIT selects a repeated pitch independently of its chord neighbour",notes_selected(t)==2);
    recorded_note_t unchanged[RECORD_MAX]; memcpy(unchanged,recording,sizeof unchanged);
    turn(EN_PRESET,4);  t->p[P_RECQ]=3; frame();
    bad += check("zoom and QNT preserve selection and original events",
                 ui.note_zoom==4 && notes_span()==1 && notes_selected(t)==2 && !memcmp(unchanged,recording,sizeof unchanged));
    t->p[P_RECQ]=0;
    press(B_EDIT);
    bad += check("EDIT removes only the selected hit and preserves neighbouring event bytes",
                 !recording[2].vel && !memcmp(&saved[0],&recording[0],sizeof saved[0]) &&
                 !memcmp(&saved[1],&recording[1],sizeof saved[1]) && !memcmp(&saved[3],&recording[3],sizeof saved[3]) &&
                 notes_selected(t)==3 && ui.cursor==0 && (t->step[0].flags&SF_RECORDED));
    raw_replay(); seq_advance(600); int exact = gate_note(t,60)&&gate_note(t,64);
    seq_advance(300); exact &= !gate_note(t,60)&&gate_note(t,64);
    seq_advance(300); exact &= !gate_note(t,64);
    seq_advance(600); exact &= !gate_note(t,60);
    seq_advance(3000); exact &= gate_note(t,67);
    bad += check("deleted off-grid hits stay silent; neighbours retain exact onsets and releases",exact);
    seq_stop(); hold(B_EDIT);
    bad += check("undo restores timing, velocity, duration and the selected hit's focus",
                 !memcmp(saved,recording,sizeof saved) && notes_selected(t)==2 && ui.cursor==0);
    fm1_in.buttons |= 1u<<panel.btn[B_EDIT]; host_pressed |= 1u<<panel.btn[B_EDIT]; frame();
    press(B_OCTUP); fm1_in.buttons &= ~(1u<<panel.btn[B_EDIT]); frame();
    bad += check("EDIT plus OCT+ redoes one deletion without deleting its successor",!recording[2].vel&&recording[3].vel);
    project_capture(&proj_scratch);
    int packed=bank_pack(proj_wire_u.raw,&proj_scratch,1);
    project_restore_runtime(&proj_scratch); if(packed) bank_restore(proj_wire_u.raw);
    bad += check("one-hit deletion survives project save/load",packed&&!recording[2].vel&&recording[0].vel&&recording[1].vel&&recording[3].vel);
    return bad;
}

static int deletion_history(void)
{
    raw_begin(); track_t *t=&trk[0];
    for(uint32_t i=0;i<10;i++){seq_advance(180);input_on(t,60,70+i);seq_advance(60);input_off(t,60);}
    seq_stop(); song.rec=0; recorded_note_t saved[10];memcpy(saved,recording,sizeof saved);
    go_page(GR_ROLL);cursor_set(0);frame();
    for(uint32_t i=0;i<10;i++)press(B_EDIT);
    int ok=1;for(uint32_t i=0;i<10;i++)ok&=!recording[i].vel;
    int bad=check("same-pitch deletions each become an edit even when the overview is unchanged",ok&&!step_on(&t->step[0]));
    for(uint32_t i=0;i<8;i++) {step_history_apply(0);frame();}
    ok=!recording[0].vel&&!recording[1].vel;
    for(uint32_t i=2;i<10;i++)ok&=!memcmp(&saved[i],&recording[i],sizeof saved[i]);
    bad+=check("the latest eight deletions undo byte-for-byte, restoring the last hit's overview",ok&&!step_history_apply(0));
    for(uint32_t i=0;i<8;i++) {step_history_apply(1);frame();}
    ok=1;for(uint32_t i=0;i<10;i++)ok&=!recording[i].vel;
    bad+=check("eight deletions redo without resurrecting the oldest two",ok&&!step_history_apply(1));
    step_history_apply(0);frame();step_history_apply(0);frame();
    press(B_EDIT);
    bad+=check("a new one-hit deletion drops the old redo branch",!step_history_apply(1));
    recorded_note_t before_take[RECORD_MAX];memcpy(before_take,recording,sizeof before_take);
    song.rec=1;seq_start();seq_advance(0);seq_advance(180);input_on(t,60,120);seq_advance(60);input_off(t,60);seq_stop();song.rec=0;
    frame();bad+=check("slot reuse by recording undoes the take instead of stale deletions",step_history_apply(0)&&!memcmp(before_take,recording,sizeof before_take));
    bad+=check("redo restores the new take in its reused slot",step_history_apply(1)&&recording[0].vel==120);
    return bad;
}

static int loop_drums_and_playback(void)
{
    raw_begin();track_t *t=&trk[0];
    seq_advance(15u*6144u+5004u);input_on(t,60,100);seq_advance(2004);input_off(t,60);seq_stop();song.rec=0;
    recorded_note_t saved=recording[0];
    go_page(GR_ROLL);cursor_set(15);frame();
    int bad=check("late-loop events select in their final step although the overview is step zero",
                  notes_selected(t)==0&&recording_view(t,&recording[0])==0);
    press(B_EDIT);bad+=check("late-loop deletion clears its step-zero overview",!recording[0].vel&&!step_on(&t->step[0]));
    hold(B_EDIT);bad+=check("late-loop undo restores the event and overview",!memcmp(&saved,&recording[0],sizeof saved)&&step_on(&t->step[0]));
    cursor_set(1);frame();press(B_EDIT);bad+=check("empty interval deletion leaves neighbouring notes and manual history alone",!memcmp(&saved,&recording[0],sizeof saved)&&!step_history.pending&&!step_history.pos);
    turn(EN_PRESET,100);bad+=check("zoom clamps at one step",ui.note_zoom==4);
    turn(EN_PRESET,-100);bad+=check("zoom clamps at sixteen steps",ui.note_zoom==0);

    raw_begin();t=&trk[0];set_engine_of(t,ENGI_DRUM);t->engine=t->eng_req;
    seq_advance(600);input_on(t,36,100);input_on(t,38,115);seq_advance(300);input_off(t,36);input_off(t,38);
    seq_advance(900);input_on(t,36,90);seq_advance(300);input_off(t,36);seq_stop();song.rec=0;
    go_page(GR_ROLL);cursor_set(0);frame();notes_cycle(2); frame();press(B_EDIT);
    bad+=check("drum deletion preserves repeated kicks and other lane hits",
               !recording[2].vel&&recording[0].vel&&recording[1].vel&&(t->step[0].hit&(1u<<drum_lane(36)))&&(t->step[0].hit&(1u<<drum_lane(38))));
    hold(B_EDIT);bad+=check("drum deletion undoes without opening the EDIT layer",recording[2].vel==90&&!ui.ly&&cur_page()->graph==GR_ROLL);

    raw_begin();t=&trk[0];t->p[P_RECQ]=3;seq_advance(600);input_on(t,60,70);seq_advance(100);input_off(t,60);
    seq_advance(100);input_on(t,60,90);seq_advance(100);input_off(t,60);seq_stop();song.rec=0;
    go_page(GR_ROLL);cursor_set(0);frame();
    t->p[P_RECQ]=3;raw_replay();
    int ok=recording_refs[0][60]==2&&gate_note(t,60);press(B_EDIT);
    ok&=recording_refs[0][60]==1&&gate_note(t,60)&&!recording[0].vel&&recording[1].vel;
    seq_advance(100);ok&=!gate_note(t,60)&&!recording_refs[0][60];
    bad+=check("sounding-event deletion preserves overlapping same-pitch ownership and its final release",ok);
    seq_stop();song.rec=1;seq_start();seq_advance(0);frame();recorded_note_t before=recording[1];press(B_EDIT);
    bad+=check("a live EDIT tap without audio passage does not delete a selected hit on release",!memcmp(&before,&recording[1],sizeof before)&&!seq_erase_active(t));
    seq_stop();song.rec=0;
    return bad;
}

static int timing_geometry_and_context(void)
{
    raw_begin();track_t *t=&trk[0];t->p[P_SSWING]=50;t->p[P_SLEN]=5;
    uint32_t period=seq_div_samples((uint32_t)t->p[P_SDIV]), span=step_samples(t,period,0);
    seq_advance(span/4u);input_on(t,61,100);seq_advance(span/8u);input_off(t,61);
    seq_advance(3u*span/8u);input_on(t,64,110);seq_advance(span/8u);input_off(t,64);seq_stop();song.rec=0;
    go_page(GR_ROLL);cursor_set(0);turn(EN_PRESET,4);
    uint32_t a,b;notes_window(t,period,&a,&b);
    int32_t x=notes_x(recording_raw_on(t,&recording[0],period),a,b), y=pr_row_y(61)+PR_TOP;
    int bad=check("swung off-grid notes draw at their original quarter-step position at maximum zoom",
                  x==PR_X0+4*PR_CW&&pr_is_bar((uint32_t)x+1u,(uint32_t)y+2u)&&!pr_is_bar((uint32_t)x-2u,(uint32_t)y+2u));
    notes_cycle(1); frame();uint16_t selection=ui.note_pick;
    t->p[P_RECQ]=3;frame();
    bad+=check("changing the next-take quantization leaves a completed take's display unchanged",
               ui.note_pick==selection&&notes_x(recording_raw_on(t,&recording[0],period),a,b)==x);
    t->p[P_RECQ]=0;cursor_set(4);turn(EN_PRESET,-3);notes_window(t,period,&a,&b);
    bad+=check("zoom windows clip safely to the last step of an odd-length loop",notes_base()==0&&b==recording_loop(t,period));
    turn(EN_PRESET,1);notes_window(t,period,&a,&b);
    bad+=check("a partially filled zoom window starts at its final bank and ends at the loop",notes_base()==4&&a==recording_prefix(t,period,4)&&b==recording_loop(t,period));
    cursor_set(0);frame();uint32_t saved=notes_selected(t);
    pattern_switch(t,7);frame();bad+=check("switching banks clears stale hit selection",notes_selected(t)==RECORD_MAX);
    pattern_switch(t,0);frame();bad+=check("returning to the recorded bank selects its first original hit",notes_selected(t)==saved);
    track_select(1);frame();bad+=check("switching tracks cannot select another track's recording",notes_selected(TSEL)==RECORD_MAX);
    track_select(0);frame();notes_cycle(1); frame();press(B_EDIT);
    bad+=check("event deletion rebuilds the surviving overview's pitch and accent",t->step[1].time==ST_REST&&t->step[0].note[0]==61&&!(t->step[0].flags&SF_ACCENT));
    return bad;
}

static track_t *clash_setup(void)
{
    raw_begin();track_t *t=&trk[0];
    seq_advance(600);input_on(t,60,100);seq_advance(300);input_off(t,60);
    seq_advance(900);input_on(t,64,90);seq_advance(300);input_off(t,64);
    seq_advance(6144u+600u-2100u);input_on(t,67,95);seq_advance(300);input_off(t,67);
    seq_stop();song.rec=0;go_page(GR_ROLL);cursor_set(0);frame();
    return t;
}
static int control_clashes(void)
{
    int bad=0;
    static const uint32_t enc[]={EN_K1,EN_K2,EN_K3,EN_K4,EN_SELECT,EN_PRESET,EN_ALGO};
    for(uint32_t k=0;k<NELEM(enc);k++) {
        track_t *t=clash_setup();
        fm1_in.buttons|=1u<<panel.btn[B_EDIT];host_pressed|=1u<<panel.btn[B_EDIT];frame();
        turn(enc[k],enc[k]==EN_SELECT?-1:1);
        fm1_in.buttons&=~(1u<<panel.btn[B_EDIT]);frame();
        bad+=check("EDIT held plus any encoder consumes deletion",recording[0].vel&&recording[1].vel&&recording[2].vel);
    }
    track_t *t=clash_setup();recorded_note_t saved[3];memcpy(saved,recording,sizeof saved);
    fm1_in.buttons|=1u<<panel.btn[B_EDIT];host_pressed|=1u<<panel.btn[B_EDIT];frame();
    host_enc[panel.enc[EN_K1]]+=panel.dir[EN_K1];fm1_in.buttons&=~(1u<<panel.btn[B_EDIT]);frame();
    bad+=check("release-frame selection consumes EDIT without deleting a note",notes_selected(t)==1&&!memcmp(saved,recording,sizeof saved));
    t=clash_setup();press(B_EDIT);
    fm1_in.buttons|=1u<<panel.btn[B_EDIT];host_pressed|=1u<<panel.btn[B_EDIT];frame();
    hold(B_EDIT);fm1_in.buttons&=~(1u<<panel.btn[B_EDIT]);frame();
    bad+=check("EDIT hold undo cannot delete the restored hit on release",recording[0].vel==100&&recording_active(t,0));
    t=clash_setup();notes_cycle(1); frame();uint16_t selection=ui.note_pick;
    song.rec=2;seq_start();seq_advance(0);seq_advance(600);input_on(&trk[1],65,100);frame();
    int stable=ui.note_pick==selection;
    seq_advance(300);input_off(&trk[1],65);frame();stable&=ui.note_pick==selection;
    bad+=check("recording and releasing notes on another track preserves the selected hit",stable);
    seq_stop();song.rec=0;
    t=clash_setup();press(B_EDIT);
    song.rec=2;seq_start();seq_advance(0);seq_advance(600);input_on(&trk[1],65,100);
    recorded_note_t replacement=recording[0];
    /* An ISR may reuse a freed slot after the frame's sync but before end. */
    step_history_end();int undone=step_history_apply(0);
    bad+=check("recording between history sync/end cannot be mistaken for an undoable UI edit",
               !undone&&!memcmp(&replacement,&recording[0],sizeof replacement)&&(recording[0].owner&31u)==NPAT);
    input_off(&trk[1],65);seq_stop();song.rec=0;
    t=clash_setup();memcpy(saved,recording,sizeof saved);
    queued(0x90,69,100,1);frame();int audition=gate_note(t,69)&&!ui.entry_open;
    queued(0x80,69,0,1);frame();audition&=!gate_note(t,69);
    bad+=check("MIDI audition on NOTES plays and releases normally without entering or replacing steps",audition&&!memcmp(saved,recording,sizeof saved));
    t=clash_setup();memcpy(saved,recording,sizeof saved);
    fm1_in.buttons|=1u<<panel.btn[B_EDIT];host_pressed|=1u<<panel.btn[B_EDIT];frame();
    press(B_PLAY);fm1_in.buttons&=~(1u<<panel.btn[B_EDIT]);frame();
    bad+=check("PLAY with EDIT held requests transport without deleting a note on release",transport_req==1&&!memcmp(saved,recording,sizeof saved));
    transport_req=0;
    t=clash_setup();memcpy(saved,recording,sizeof saved);chain.running=1;press(B_EDIT);
    bad+=check("song-chain protection also blocks individual note deletion",!memcmp(saved,recording,sizeof saved)&&msg_is("STOP TO EDIT"));
    chain.running=0;
    for(uint32_t layer=LAYER_GLO;layer<LAYER_SCL;layer++) {
        t=clash_setup();memcpy(saved,recording,sizeof saved);uint8_t zoom=ui.note_zoom;
        uint32_t b=LAYERS[layer].btn;
        fm1_in.buttons|=1u<<panel.btn[b];host_pressed|=1u<<panel.btn[b];frame();turn(EN_K4,1);
        int active=ui.ly==layer&&ui.layer==layer;
        fm1_in.buttons&=~(1u<<panel.btn[b]);frame();
        bad+=check("GLO layer keep their own knobs inside NOTES",active&&ui.note_zoom==zoom&&!memcmp(saved,recording,sizeof saved));
    }
    return bad;
}


static int property_edits_and_mixed_patterns(void)
{
    track_t *t=clash_setup();
    recorded_note_t original[3];memcpy(original,recording,sizeof original);
    uint32_t count=0;for(uint32_t p=0;p<NPAGES;p++)if(PAGES[p].fam==FAM_SEQ&&PAGES[p].scope==SC_STEP)count++;
    int bad=check("sequence navigation exposes exactly one note editor",count==1&&str_eq(cur_page()->title,"NOTES"));
    turn(EN_K1,1);uint32_t selected=notes_selected(t);
    turn(EN_K2,3);
    bad+=check("PITCH edits only the highlighted hit and keeps exact timing",selected==1&&recorded_note(&recording[1])==67&&recording[1].on==original[1].on&&recording[1].duration==original[1].duration&&!memcmp(&original[0],&recording[0],sizeof original[0])&&!memcmp(&original[2],&recording[2],sizeof original[2]));
    hold(B_EDIT);bad+=check("pitch undo restores exact event bytes and selection",!memcmp(original,recording,sizeof original)&&notes_selected(t)==1);
    turn(EN_K4,-20);bad+=check("VEL changes the highlighted hit's playback velocity",recording[1].vel==70&&recorded_note(&recording[1])==64&&recording[0].vel==100);
    uint32_t length=(uint32_t)recording[1].duration*(1u<<(recording[1].owner>>5));
    turn(EN_K3,1);
    bad+=check("LENGTH snaps one captured note to the next quarter without moving its onset",(uint32_t)recording[1].duration*(1u<<(recording[1].owner>>5))==(length/(RECORD_UNIT/4u)+1u)*(RECORD_UNIT/4u)&&recording[1].on==original[1].on&&recording[0].duration==original[0].duration);
    hold(B_EDIT);bad+=check("length undo keeps the preceding velocity edit",recording[1].duration==original[1].duration&&recording[1].vel==70);
    fm1_in.buttons|=1u<<panel.btn[B_ENV];host_pressed|=1u<<panel.btn[B_ENV];frame();turn(EN_K3,1);fm1_in.buttons&=~(1u<<panel.btn[B_ENV]);frame();
    bad+=check("ENV plus LENGTH gives a fine sixteenth-step duration edit",(uint32_t)recording[1].duration*(1u<<(recording[1].owner>>5))==(length/(RECORD_UNIT/16u)+1u)*(RECORD_UNIT/16u)&&cur_page()->graph==GR_ROLL);
    hold(B_EDIT);
    fm1_in.buttons|=1u<<panel.btn[B_SCL];host_pressed|=1u<<panel.btn[B_SCL];frame();turn(EN_K1,2);fm1_in.buttons&=~(1u<<panel.btn[B_SCL]);frame();
    bad+=check("MOVE preserves a captured note's fractional onset, duration and neighbours",ui.cursor==2&&notes_selected(t)==1&&(recording[1].step&63u)==2&&recording[1].on==original[1].on&&recording[1].duration==original[1].duration&&recording[0].vel==100&&recording[2].vel==95);
    hold(B_EDIT);bad+=check("move undo restores position and focus without dropping prior edits",ui.cursor==0&&notes_selected(t)==1&&!memcmp(&recording[1].on,&original[1].on,sizeof original[1].on)&&recording[1].vel==70);
    cursor_set(0);turn(EN_K1,-1);bad+=check("KNOB 1 back from a step's first note: the step before, an empty one too",ui.cursor==step_pattern_len(t)-1u&&notes_selected(t)>=RECORD_MAX);
    cursor_set(2);turn(EN_K1,-1);bad+=check("..the step before on its last recorded note",ui.cursor==1&&notes_selected(t)==2);
    cursor_set(4);frame();queued(0x90,72,100,1);frame();queued(0x80,72,0,1);frame();
    bad+=check("MIDI can add a manual note in free space beside a recorded take",t->step[4].n==1&&t->step[4].note[0]==72&&!(t->step[4].flags&SF_RECORDED)&&recording[0].vel==100&&recording[1].vel==70);
    cursor_set(5);turn(EN_K1,-1);int manual=ui.cursor==4&&notes_manual_start(t)==4;turn(EN_K1,-3);
    bad+=check("the same KNOB 1 jog reaches manual and recorded notes",manual&&ui.cursor==1&&notes_selected(t)==2);
    project_capture(&proj_scratch);int packed=bank_pack(proj_wire_u.raw,&proj_scratch,1);project_restore_runtime(&proj_scratch);if(packed)bank_restore(proj_wire_u.raw);
    bad+=check("property edits and mixed note types survive save/load",packed&&recording[1].vel==70&&t->step[4].note[0]==72&&recording_active(t,0)&&recording_active(t,1));
    raw_replay();seq_advance(600);bad+=check("neighbouring recorded pitch still plays after editing and reload",gate_note(t,60));seq_advance(1200);bad+=check("edited recorded note still plays on its original timeline",gate_note(t,64));seq_stop();
    return bad;
}
static int manual_chord_controls(void)
{
    raw_begin();seq_stop();song.rec=0;track_t *t=&trk[0];
    t->step[0]=(step_t){.time=ST_NOTE,.n=3,.note={60,64,67},.flags=SF_ACCENT};
    step_note_resize(t,0,3);go_page(GR_ROLL);cursor_set(0);frame();
    turn(EN_K1,1);turn(EN_K2,2);
    int bad=check("manual chords select and transpose one pitch at a time",ui.note_slot==1&&t->step[0].note[0]==60&&t->step[0].note[1]==66&&t->step[0].note[2]==67);
    press(B_EDIT);bad+=check("DELETE removes the highlighted chord pitch and retains other pitches and tails",t->step[0].n==2&&t->step[0].note[0]==60&&t->step[0].note[1]==67&&step_note_length(t,0)==3);
    hold(B_EDIT);bad+=check("chord deletion undo restores the selected pitch and its focus",ui.note_slot==1&&t->step[0].n==3&&t->step[0].note[1]==66);
    turn(EN_K4,-10);bad+=check("manual velocity edits control playback even after an accent",t->step[0].vel==117&&!(t->step[0].flags&SF_ACCENT));
    fm1_in.buttons|=1u<<panel.btn[B_ENV];host_pressed|=1u<<panel.btn[B_ENV];frame();turn(EN_K4,1);fm1_in.buttons&=~(1u<<panel.btn[B_ENV]);frame();
    bad+=check("ENV plus VEL retains manual slide editing without opening a page",(t->step[0].flags&SF_SLIDE)&&cur_page()->graph==GR_ROLL&&t->step[0].vel==117);
    turn(EN_PRESET,3);step_t saved=t->step[0];turn(EN_K1,1);turn(EN_K1,7);
    bad+=check("zoom and navigation never mutate a manual chord",ui.note_zoom==3&&!memcmp(&saved,&t->step[0],sizeof saved));
    return bad;
}

static int dense_selection_and_protection(void)
{
    raw_begin();track_t *t=&trk[0];
    for(uint32_t n=0;n<40;n++){seq_advance(60);input_on(t,60,100);seq_advance(60);input_off(t,60);}
    seq_advance(6144u+600u-4800u);input_on(t,67,100);seq_advance(300);input_off(t,67);seq_stop();song.rec=0;
    go_page(GR_ROLL);cursor_set(1);frame();turn(EN_K1,-1);
    int bad=check("reverse note navigation reaches the final hit in dense intervals",notes_selected(t)==39&&ui.cursor==0);
    recorded_note_t saved[41];memcpy(saved,recording,sizeof saved);
    t->step[3].time=ST_NOTE;t->step[3].n=1;t->step[3].note[0]=72;step_note_resize(t,3,3);frame();
    fm1_in.buttons|=1u<<panel.btn[B_SCL];host_pressed|=1u<<panel.btn[B_SCL];frame();turn(EN_K1,4);fm1_in.buttons&=~(1u<<panel.btn[B_SCL]);frame();
    bad+=check("moving a recorded note into a manual tie is refused without changing the take",!memcmp(saved,recording,sizeof saved)&&t->step[4].time==ST_TIE&&msg_is("STEP OCCUPIED"));
    cursor_set(2);frame();
    t->step[2].time=ST_NOTE;t->step[2].n=1;t->step[2].note[0]=75;t->step[2].flags=SF_RECORDED;frame();
    step_t overview=t->step[2];turn(EN_K2,1);turn(EN_K3,1);
    bad+=check("an overview cell with no event onset cannot overwrite a recording",!memcmp(&overview,&t->step[2],sizeof overview)&&!memcmp(saved,recording,sizeof saved));
    return bad;
}

static int drum_recording_layout(void)
{
    raw_begin();track_t *t=&trk[0];set_engine_of(t,ENGI_DRUM);t->engine=t->eng_req;
    go_page(GR_ROLL);frame();seq_advance(600);input_on(t,36,100);seq_advance(100);input_off(t,36);frame();
    int bad=check("the first recorded drum hit keeps live lane-key mapping and the grid",grid_on()&&song.grid==1u);
    seq_advance(500);input_on(t,38,100);seq_advance(100);input_off(t,38);frame();
    recorded_note_t first=recording[0];press(B_EDIT);
    bad+=check("a live drum EDIT tap consumes release without deleting the overview",recording_active(t,0)&&!memcmp(&first,&recording[0],sizeof first)&&!seq_erase_active(t));
    seq_stop();song.rec=0;frame();
    bad+=check("stopping drum recording reveals precise note editing in the same page",!grid_on()&&!song.grid&&cur_page()->graph==GR_ROLL&&notes_selected(t)<RECORD_MAX);
    return bad;
}

static void erase_down(void)
{
    fm1_in.buttons |= 1u << panel.btn[B_EDIT];
    host_pressed |= 1u << panel.btn[B_EDIT];
    frame();
}
static void erase_up(void)
{
    fm1_in.buttons &= ~(1u << panel.btn[B_EDIT]);
    frame();
}
static void manual_hit(track_t *t, uint32_t s, uint8_t note)
{
    t->step[s] = (step_t){0};
    t->step[s].time = ST_NOTE;
    t->step[s].n = 1;
    t->step[s].note[0] = note;
    t->step[s].vel = 96;
}
static int live_erase_manual(void)
{
    raw_begin(); track_t *t = TSEL;
    for (uint32_t s = 0; s < 5; s++) manual_hit(t,s,60+s);
    manual_hit(&trk[1],0,72);
    step_t other = trk[1].step[0];
    go_page(GR_ROLL); cursor_set(4); frame();
    erase_down(); seq_advance(1);
    int bad = check("holding EDIT during REC erases the playhead step, not the editor cursor",!step_on(&t->step[0])&&step_on(&t->step[4])&&seq_erase_active(t));
    seq_advance(3u*6144u);
    bad += check("an erase pass removes every crossed step without waiting for screen refresh",!step_on(&t->step[1])&&!step_on(&t->step[2])&&!step_on(&t->step[3])&&step_on(&t->step[4])&&!memcmp(&other,&trk[1].step[0],sizeof other));
    fm1_in.buttons &= ~(1u<<panel.btn[B_EDIT]);
    seq_advance(6144); // physical release reaches audio before the next UI frame
    frame();
    bad += check("physical EDIT release stops immediately and consumes the delete tap",step_on(&t->step[4])&&!seq_erase_active(t));
    erase_down(); seq_advance(1);
    bad += check("a new EDIT press erases the current step again",!step_on(&t->step[4]));
    erase_up(); seq_stop(); song.rec=0;

    raw_begin();t=TSEL;manual_hit(t,0,60);step_note_resize(t,0,4);
    go_page(GR_ROLL);seq_advance(2u*6144u+100);erase_down();seq_advance(1);
    bad += check("erasing inside a sounding manual tie removes its onset and complete tail",t->step[0].time==ST_REST&&t->step[1].time==ST_REST&&t->step[2].time==ST_REST&&t->step[3].time==ST_REST&&!t->seq_n);
    erase_up();seq_stop();song.rec=0;

    raw_begin();t=TSEL;t->p[P_SLEN]=1;manual_hit(t,0,60);go_page(GR_ROLL);erase_down();seq_advance(1);
    manual_hit(t,0,61);seq_advance(6144);
    bad += check("a single-step loop is erased on every passage",!step_on(&t->step[0]));
    erase_up();seq_stop();song.rec=0;
    return bad;
}
static int live_erase_raw(void)
{
    raw_begin();track_t *t=TSEL;
    seq_advance(5000);input_on(t,60,80);seq_advance(100);input_off(t,60);
    seq_advance(1200);input_on(t,64,100);seq_advance(100);input_off(t,64);
    seq_advance(2u*6144u);input_on(t,67,90);seq_advance(100);input_off(t,67);
    seq_stop();song.rec=0;recorded_note_t later=recording[1], untouched=recording[2];
    go_page(GR_ROLL);raw_replay();song.rec=1;frame();erase_down();seq_advance(1);
    int bad=check("erase uses actual intervals and preserves an unvisited hit sharing the rounded cell",!recording[0].vel&&!memcmp(&later,&recording[1],sizeof later)&&recording_view(t,&later)==1&&t->step[1].n==1&&t->step[1].note[0]==64);
    seq_advance(6144);
    bad+=check("the next interval erases its own recorded hit and leaves later event bytes intact",!recording[1].vel&&!step_on(&t->step[1])&&!memcmp(&untouched,&recording[2],sizeof untouched));
    erase_up();seq_stop();song.rec=0;frame();
    project_capture(&proj_scratch);int packed=bank_pack(proj_wire_u.raw,&proj_scratch,1);
    project_restore_runtime(&proj_scratch);if(packed)bank_restore(proj_wire_u.raw);
    bad+=check("an erased recording pass survives save/load without resurrecting hits",packed&&!recording[0].vel&&!recording[1].vel&&recording[2].vel&&!step_history_apply(0));

    raw_begin();t=TSEL;seq_advance(100);input_on(t,60,100);seq_advance(4u*6144u);input_off(t,60);seq_stop();song.rec=0;
    go_page(GR_ROLL);raw_replay();seq_advance(2u*6144u);song.rec=1;frame();erase_down();seq_advance(1);
    bad+=check("starting erase inside a sustained recorded note removes its event and sounding ownership",!recording[0].vel&&!recording_refs[0][60]&&!gate_note(t,60));
    input_on(t,65,100);seq_advance(6144);input_off(t,65);
    int empty=1;for(uint32_t i=0;i<RECORD_MAX;i++)empty&=!recording[i].vel;
    bad+=check("keys still audition during erase without recording notes or recreating ties",empty&&!t->rh_n&&!t->seq_n&&!gate_note(t,65));
    erase_up();input_on(t,67,110);seq_advance(100);input_off(t,67);
    bad+=check("recording resumes normally after EDIT release",recording[0].vel==110&&recorded_note(&recording[0])==67);
    seq_stop();song.rec=0;

    raw_begin();t=TSEL;seq_advance(100);input_on(t,60,80);seq_advance(100);input_off(t,60);
    seq_advance(15u*6144u+4800u);input_on(t,72,100);seq_advance(100);input_off(t,72);seq_stop();song.rec=0;
    recorded_note_t late=recording[1];go_page(GR_ROLL);raw_replay();song.rec=1;frame();erase_down();seq_advance(1);
    bad+=check("step-zero erase retains a final-interval hit rounded across the loop",!recording[0].vel&&!memcmp(&late,&recording[1],sizeof late)&&step_on(&t->step[0]));
    erase_up();seq_advance(15u*6144u);erase_down();seq_advance(1);
    bad+=check("erasing the final interval removes the wrapped overview without ghost notes",!recording[1].vel&&!step_on(&t->step[0]));
    erase_up();seq_stop();song.rec=0;
    return bad;
}
static int live_erase_scope_and_drums(void)
{
    int bad=0;
    for(uint32_t mode=0;mode<5;mode++) {
        raw_begin();track_t *t=TSEL;go_page(GR_ROLL);erase_down();seq_advance(1);manual_hit(t,1,60);
        if(mode==0)song.rec=0;
        if(mode==1){song.sel=1;song.rec=3;manual_hit(TSEL,1,64);}
        if(mode==2)pattern_switch(t,1);
        if(mode==3)go_page(GR_STEPS);
        if(mode==4){seq_stop();seq_start();seq_advance(0);}
        frame();seq_advance(6144);
        bad+=check("disarm, track/bank/page changes and stop/restart cancel the captured erase gesture",!seq_erase_active(TSEL)&&((mode==2)||step_on(&t->step[1]))&&(mode!=1||step_on(&TSEL->step[1])));
        if(mode==0){song.rec=1;frame();seq_advance(1);bad+=check("rearming REC while EDIT remains held requires a new erase press",step_on(&t->step[1])&&!seq_erase_active(t));}
        erase_up();seq_stop();song.rec=0;
    }
    raw_begin();track_t *t=TSEL;t->p[P_SSWING]=50;
    for(uint32_t s=0;s<4;s++)manual_hit(t,s,60+s);
    uint32_t period=seq_div_samples((uint32_t)t->p[P_SDIV]),span=step_samples(t,period,0);
    go_page(GR_ROLL);erase_down();seq_advance(span-1);
    bad+=check("swung erase leaves the upcoming step until its actual boundary",!step_on(&t->step[0])&&step_on(&t->step[1]));
    seq_advance(1);bad+=check("swung erase follows the extended step's exact boundary",!step_on(&t->step[1]));
    erase_up();seq_stop();song.rec=0;

    /* QNT OFF, played on the straight grid: step 3's note lands in step 2's stretched interval */
    raw_begin();t=TSEL;t->p[P_SSWING]=50;
    for(uint32_t s=0;s<4;s++){input_on(t,60+s,100);seq_advance(1536);input_off(t,60+s);seq_advance(period-1536);}
    seq_stop();song.rec=0;
    bad+=check("fixture: a straight odd-step note is stored in the swung even interval",recorded_step(&recording[3])==2&&recording_view(t,&recording[3])==3);
    go_page(GR_ROLL);raw_replay();seq_advance(2u*period+600u);song.rec=1;frame();erase_down();seq_advance(1);erase_up();
    bad+=check("a swung EDIT tap erases its step's note, not the next step's",!recording[2].vel&&recording[3].vel&&step_on(&t->step[3]));
    erase_down();seq_advance(span-602u);
    bad+=check("a held swung erase keeps the odd step's note until that step's boundary",recording[3].vel&&step_on(&t->step[3]));
    seq_advance(1);bad+=check("the odd step's boundary erases its straight-played note",!recording[3].vel&&!step_on(&t->step[3]));
    erase_up();seq_stop();song.rec=0;

    raw_begin();t=TSEL;set_engine_of(t,ENGI_DRUM);t->engine=t->eng_req;
    seq_advance(100);input_on(t,36,100);input_on(t,38,115);seq_advance(100);input_off(t,36);input_off(t,38);
    seq_advance(6144);input_on(t,42,90);seq_advance(100);input_off(t,42);seq_stop();song.rec=0;
    go_page(GR_ROLL);raw_replay();song.rec=1;frame();erase_down();seq_advance(1);frame();
    bad+=check("live drum erase removes all current lane hits while keeping the drum grid",!recording[0].vel&&!recording[1].vel&&recording[2].vel&&!t->step[0].hit&&grid_on()&&song.grid&&seq_erase_active(t)&&!ui.ly);
    seq_advance(6144);erase_up();
    bad+=check("live drum erase crosses lane hits and releases without an extra grid delete",!recording[2].vel&&!step_on(&t->step[1])&&!ui.ly);
    seq_stop();song.rec=0;
    return bad;
}


static int erase_edit_hold_conflicts(void)
{
    int bad=0;
    for(uint32_t context=0;context<3;context++) {
        raw_begin();track_t *t=TSEL;manual_hit(t,0,60);
        if(context==0)go_home();
        if(context==1)go_page(GR_STEPS);
        if(context==2){set_engine_of(t,ENGI_DRUM);t->engine=t->eng_req;seq_stop();song.rec=0;go_page(GR_ROLL);}
        frame();undo_clear();recording_undo_clear();step_t before=t->step[0];erase_down();frames(800);
        int normal=!layer_open()&&!seq_erase_active(t);
        seq_advance(6144);erase_up();
        bad+=check("EDIT hold outside live NOTES never opens a navigation layer or deletes on release",normal&&!memcmp(&before,&t->step[0],sizeof before));
        seq_stop();song.rec=0;
    }
    for(uint32_t drum=0;drum<2;drum++) {
        raw_begin();track_t *t=TSEL;
        if(drum){set_engine_of(t,ENGI_DRUM);t->engine=t->eng_req;}
        manual_hit(t,0,60);manual_hit(t,1,64);go_page(GR_ROLL);frame();
        uint32_t engine=t->eng_req;erase_down();frames(500);seq_advance(1);
        int erase=seq_erase_active(t)&&!ui.ly&&!layer_open()&&!step_on(&t->step[0]);
        erase_up();
        bad+=check("a long live NOTES erase never also opens EDIT's engine layer or deletes on release",erase&&t->eng_req==engine&&step_on(&t->step[1])&&!seq_erase_active(t));
        seq_stop();song.rec=0;
    }
    raw_begin();track_t *t=TSEL;manual_hit(t,0,60);go_page(GR_ROLL);frame();
    btn_down(B_GLO);frame();frames(500);erase_down();seq_advance(1);
    bad+=check("an already held GLO layer keeps EDIT from starting an erase gesture",ui.ly==LAYER_GLO&&!seq_erase_active(t)&&step_on(&t->step[0]));
    erase_up();btn_up(B_GLO);frame();seq_stop();song.rec=0;
    return bad;
}

static void delete_range(uint32_t first,uint32_t last)
{
    cursor_set((int32_t)first);frame();
    bulk.on=1;bulk.track=song.sel;bulk.generation=TSEL->pattern_gen;
    bulk.engine=(uint8_t)library_engine();bulk.pattern=TSEL->pattern;
    bulk.anchor=(uint16_t)first;bulk.end=(uint16_t)last;
    press(B_EDIT);
}
static int bulk_and_pattern_clears(void)
{
    raw_begin();track_t *t=TSEL;
    seq_advance(600);input_on(t,60,80);seq_advance(300);input_off(t,60);
    seq_advance(3300);input_on(t,64,90);seq_advance(300);input_off(t,64);
    seq_advance(2244);input_on(t,67,100);seq_advance(300);input_off(t,67);
    seq_advance(15u*6144u+5004u-7044u);input_on(t,72,110);seq_advance(300);input_off(t,72);
    seq_stop();song.rec=0;go_page(GR_ROLL);frame();
    recorded_note_t saved[4];memcpy(saved,recording,sizeof saved);
    int bad=check("bulk fixture includes late notes rounded outside their onset intervals",
        recording_view(t,&recording[1])==1&&recording_view(t,&recording[3])==0);
    delete_range(0,0);
    bad+=check("EDIT deletes every selected onset, including late notes, and preserves neighbours sharing its overview",
        !recording[0].vel&&!recording[1].vel&&!memcmp(&recording[2],&saved[2],sizeof saved[2])&&
        !memcmp(&recording[3],&saved[3],sizeof saved[3])&&t->step[0].note[0]==72&&t->step[1].note[0]==67&&!bulk.on);
    hold(B_EDIT);
    bad+=check("bulk delete undo restores all precise events byte-for-byte",!memcmp(saved,recording,sizeof saved));
    bad+=check("bulk delete redo removes both events again",note_tools_history(1)&&!recording[0].vel&&!recording[1].vel&&recording[2].vel&&recording[3].vel);
    delete_range(15,15);
    bad+=check("bulk delete at loop end removes the late event's step-zero overview only",!recording[3].vel&&recording[2].vel&&!step_on(&t->step[0]));
    hold(B_EDIT);
    bulk.on=2;bulk.track=song.sel;bulk.generation=recording_generation;
    bulk.engine=(uint8_t)library_engine();bulk.pattern=TSEL->pattern;
    memset(bulk.notes,0,sizeof bulk.notes);bulk.notes[0]=1u<<3;
    cursor_set(1);frame();press(B_EDIT);
    bad+=check("exact selection after a move deletes selected events rather than the cursor's neighbour",!recording[3].vel&&recording[2].vel);

    raw_begin();t=TSEL;
    seq_advance(600);input_on(t,60,80);seq_advance(300);input_off(t,60);
    seq_advance(3600);input_on(t,64,90);seq_advance(300);input_off(t,64);
    seq_stop();song.rec=0;go_page(GR_ROLL);frame();
    recorded_note_t other=recording[0];other.owner=(uint8_t)(1u+NPAT);recorded_set_note(&other,75);
    recording_restore_note(&trk[1],2,other);
    saved[0]=recording[0];saved[1]=recording[1];
    motion_set_event(t,0,P_LEVEL,80);motion_store_t before;motion_snapshot_track(t,&before);
    go_title("PATTERN");frame();
    confirm_open(CF_CLEAR_SEQ,0);press(B_OCTUP);
    bad+=check("Clear pattern physically erases the bank's precise events and preserves other owners",
        !recording[0].vel&&!recording[1].vel&&!memcmp(&recording[2],&other,sizeof other)&&seq_is_empty(t)&&!t->rh_n&&!motion.count);
    hold(B_EDIT);motion_store_t after;motion_snapshot_track(t,&after);
    bad+=check("pattern clear undo restores exact events and motion together",!memcmp(&recording[0],&saved[0],sizeof saved[0])&&
        !memcmp(&recording[1],&saved[1],sizeof saved[1])&&!memcmp(&before,&after,sizeof before));
    undo_step(1);
    bad+=check("pattern clear redo from the pattern page deletes events and motion again",!recording[0].vel&&!recording[1].vel&&!motion.count);
    song.rec=1;seq_start();seq_advance(0);seq_advance(600);input_on(t,77,100);
    seq_advance(3u*6144u);input_off(t,77);seq_stop();song.rec=0;
    uint32_t count=0;int only=1;
    for(uint32_t i=recording_head[recording_owner(t)];i<RECORD_MAX;i=recording_next[i]){count++;only&=recorded_note(&recording[i])==77;}
    bad+=check("a long note recorded after clearing cannot revive old notes",count==1&&only&&!note_tools_history(0));
    raw_replay();seq_advance(600);int clean=gate_note(t,77)&&!gate_note(t,60)&&!gate_note(t,64);
    seq_advance(4000);clean&=!gate_note(t,60)&&!gate_note(t,64);
    seq_advance(3u*6144u-4000u);clean&=!gate_note(t,77);
    bad+=check("playback after clearing sounds only the newly recorded long note",clean);
    seq_stop();project_capture(&proj_scratch);int packed=bank_pack(proj_wire_u.raw,&proj_scratch,1);
    project_restore_runtime(&proj_scratch);if(packed)bank_restore(proj_wire_u.raw);
    count=0;only=1;for(uint32_t i=recording_head[recording_owner(t)];i<RECORD_MAX;i=recording_next[i]){count++;only&=recorded_note(&recording[i])==77;}
    bad+=check("cleared events stay deleted through project save/load",packed&&count==1&&only);

    raw_begin();t=TSEL;seq_advance(600);input_on(t,60,80);input_on(t,64,90);
    seq_advance(300);input_off(t,60);input_off(t,64);seq_stop();song.rec=0;
    /* Simulate a step replacement that invalidates the rounded recording. */
    step_clear(&t->step[0]);song.rec=1;seq_start();seq_advance(0);seq_advance(1800);input_on(t,79,100);
    seq_advance(3u*6144u);input_off(t,79);seq_stop();song.rec=0;
    count=0;only=1;for(uint32_t i=recording_head[recording_owner(t)];i<RECORD_MAX;i=recording_next[i]){count++;only&=recorded_note(&recording[i])==79;}
    bad+=check("overwritten dormant events are retired before new recording can reactivate their overview",count==1&&only);
    raw_begin();t=TSEL;seq_advance(12u*6144u+600u);input_on(t,60,80);seq_advance(300);input_off(t,60);
    seq_stop();song.rec=0;t->p[P_SLEN]=8;go_title("PATTERN");frame();
    confirm_open(CF_CLEAR_SEQ,0);press(B_OCTUP);
    bad+=check("full pattern clear also deletes retained events beyond a shortened LEN",!recording[0].vel&&recording_head[recording_owner(t)]==RECORD_MAX);
    return bad;
}

int main(void)
{
    int bad=selection_and_delete()+deletion_history()+loop_drums_and_playback()+timing_geometry_and_context()+control_clashes()+property_edits_and_mixed_patterns()+manual_chord_controls()+dense_selection_and_protection()+drum_recording_layout()+live_erase_manual()+live_erase_raw()+live_erase_scope_and_drums()+erase_edit_hold_conflicts()+bulk_and_pattern_clears();
    printf("Recorded note editing: %s\n",bad?"FAILED":"all passed");
    return !!bad;
}
