/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void setup(void)
{
    ui_power_on();track_defaults_steps(TSEL);TSEL->p[P_SLEN]=16;go_page(GR_ROLL);frame();
}
static void manual(uint32_t step,uint32_t note,uint32_t length)
{
    TSEL->step[step]=(step_t){{(uint8_t)note},1,ST_NOTE,0,96};step_note_resize(TSEL,step,(int32_t)length);
}
static void select_range(uint32_t start,int32_t delta)
{
    cursor_set((int32_t)start);btn_down(B_OCTUP);frame();frames(700);turn(EN_K1,delta);btn_up(B_OCTUP);frame();
}
static uint32_t duration(uint32_t i){return recording[i].duration*(1u<<(recording[i].owner>>5));}
int main(void)
{
    int bad=0;setup();manual(2,60,1);manual(5,72,1);cursor_set(2);frame();
    turn(EN_K3,-1);uint32_t i=notes_selected(TSEL);
    bad+=check("manual LENGTH becomes an exact 0.75-step event and leaves neighbours intact",i<RECORD_MAX&&duration(i)==3u*RECORD_UNIT/4u&&!recording[i].on&&TSEL->step[5].note[0]==72);
    hold(B_EDIT);bad+=check("undo restores the manual representation and clears converted events",notes_manual_start(TSEL)==2&&!recording[0].vel);
    btn_down(B_EDIT);frame();press(B_OCTUP);btn_up(B_EDIT);frame();
    bad+=check("redo restores fractional length and playback ownership",recording_active(TSEL,0)&&duration(0)==3u*RECORD_UNIT/4u);
    turn(EN_K3,2);bad+=check("fractional LENGTH lands on 1.25, never accumulates rounding",duration(0)==5u*RECORD_UNIT/4u);
    recording[0].duration=19500;recording[0].owner&=31u;ui.note_identity=recording[0];ui.note_generation=++recording_generation;frame();turn(EN_K3,1);
    bad+=check("off-grid captured durations snap to the next hard quarter",duration(0)==RECORD_UNIT/2u);
    setup();manual(1,60,2);manual(4,64,1);manual(8,72,1);select_range(1,3);
    bad+=check("OCT+ held beyond the sheet timeout then scrolling selects a range without a popup",bulk.on&&bulk_has(1)&&bulk_has(4)&&!bulk_has(8)&&!pop.on);
    turn(EN_K1,1);
    bad+=check("bulk quarter-step move converts selected notes and retains relative timing",bulk.on==2&&recording[0].on==RECORD_UNIT/4u&&recording[1].on==RECORD_UNIT/4u&&(recording[0].step&63u)==1&&(recording[1].step&63u)==4&&duration(0)==2u*RECORD_UNIT&&TSEL->step[8].note[0]==72);
    hold(B_EDIT);bad+=check("one undo restores both notes, their ties and untouched neighbour",TSEL->step[1].note[0]==60&&TSEL->step[2].time==ST_TIE&&TSEL->step[4].note[0]==64&&!recording[0].vel&&TSEL->step[8].note[0]==72);
    setup();manual(15,60,1);cursor_set(15);frame();note_copy_arm();
    turn(EN_K1,4);
    uint32_t count=0;for(uint32_t k=0;k<RECORD_MAX;k++)count+=recording_active(TSEL,k);
    bad+=check("copy wraps to zero, preserving the source and duration",count==2&&(recording[0].step&63u)==15&&(recording[1].step&63u)==0&&duration(0)==duration(1));
    hold(B_EDIT);count=0;for(uint32_t k=0;k<RECORD_MAX;k++)count+=recording_active(TSEL,k);
    bad+=check("copy undo restores the original manual note without a duplicate",count==0&&TSEL->step[15].note[0]==60&&TSEL->step[0].time==ST_REST);
    setup();manual(1,60,1);manual(2,62,1);cursor_set(1);frame();btn_down(B_ENV);btn_down(B_SCL);frame();turn(EN_SELECT,4);btn_up(B_SCL);btn_up(B_ENV);frame();
    bad+=check("fractional movement into an unselected manual note rolls back atomically",TSEL->step[1].note[0]==60&&TSEL->step[2].note[0]==62&&!recording[0].vel);
    setup();manual(1,60,1);cursor_set(1);frame();turn(EN_K3,1);transport_req=1;events_block(CTL);
    bad+=check("starting playback releases temporary editor history before synthesis",song.playing&&!resource[RES_NOTE_HISTORY].size);
    setup();recorded_note_t a={.step=2,.on=5000,.note7=60,.vel=70,.duration=RECORD_UNIT/2,.owner=(uint8_t)recording_owner(TSEL)};
    recorded_note_t b=a;b.step=4;b.on=9000;recorded_set_note(&b,64);b.vel=99;
    recording_restore_note(TSEL,0,a);recording_restore_note(TSEL,1,b);note_rebuild_all();TSEL->step[2].probability=73;
    select_range(2,2);note_move_snapped(1,RECORD_UNIT/4,0);
    bad+=check("recorded bulk movement snaps the first onset and preserves relative offsets",recording[0].on==RECORD_UNIT/4&&recording[1].on==RECORD_UNIT/4+4000&&recording[0].vel==70&&recording[1].vel==99&&TSEL->step[2].probability==73);
    setup();manual(1,67,2);manual(3,60,3);manual(10,72,3);
    TSEL->step[3].flags|=SF_RECORDED;
    a=(recorded_note_t){.step=3,.on=22000,.note7=60,.vel=83,.duration=40960,.owner=(uint8_t)(recording_owner(TSEL)|(2u<<5))};
    recording_restore_note(TSEL,0,a);select_range(1,2);
    btn_down(B_SCL);frame();turn(EN_SELECT,1);btn_up(B_SCL);frame();
    bad+=check("mixed bulk move can enter a recorded note's own rounded tie tail",!strcmp(ui.msg,"Moving..")&&recording[0].on==22000+RECORD_UNIT/4&&recording[1].on==RECORD_UNIT/4&&duration(0)==5u*RECORD_UNIT/2u&&duration(1)==2u*RECORD_UNIT);
    bad+=check("rebuilding recorded overviews clears their old ties and preserves manual neighbours",TSEL->step[5].time==ST_REST&&TSEL->step[10].note[0]==72&&TSEL->step[11].time==ST_TIE&&TSEL->step[12].time==ST_TIE);
    hold(B_EDIT);
    bad+=check("mixed move undo restores the original recorded tail and manual source",!memcmp(&recording[0],&a,sizeof a)&&!recording[1].vel&&TSEL->step[1].note[0]==67&&TSEL->step[2].time==ST_TIE&&TSEL->step[4].time==ST_TIE&&TSEL->step[5].time==ST_TIE);
    select_range(1,2);for(uint32_t k=0;k<8u;k++)note_move_snapped(1,RECORD_UNIT/4,0);
    bad+=check("repeated mixed bulk moves keep the exact event selection across rounded cells",bulk.on==2&&(recording[0].step&63u)==5&&recording[0].on==22000&&(recording[1].step&63u)==3&&!recording[1].on&&TSEL->step[10].note[0]==72);
    setup();manual(1,60,3);TSEL->step[1].flags|=SF_RECORDED;
    a.step=1;a.on=0;recording_restore_note(TSEL,0,a);cursor_set(1);frame();notes_event_edit(0,1);
    bad+=check("single recorded note can also move into its own old tie",(recording[0].step&63u)==2&&duration(0)==5u*RECORD_UNIT/2u);
    setup();manual(1,60,1);manual(5,72,3);select_range(1,0);note_move_snapped(20,RECORD_UNIT/4,0);
    bad+=check("bulk movement into an unselected manual tie still rolls back",!strcmp(ui.msg,"STEP OCCUPIED")&&TSEL->step[1].note[0]==60&&TSEL->step[5].note[0]==72&&TSEL->step[6].time==ST_TIE&&!recording[0].vel);
    setup();manual(2,60,1);cursor_set(2);frame();sheet_do("Move note");turn(EN_K1,1);
    bad+=check("Move note uses KNOB 1 directly with no held modifier",ui.note_move&&recording[0].on==RECORD_UNIT/4u&&!fm1_in.buttons);
    press(B_OCTDN);turn(EN_K1,1);
    bad+=check("OCT- ends move mode and KNOB 1 resumes note browsing",!ui.note_move&&!bulk.on&&ui.cursor==3&&recording[0].on==RECORD_UNIT/4u);
    cursor_set(2);frame();sheet_do("Copy note");turn(EN_K1,1);turn(EN_K1,1);
    bad+=check("Copy note duplicates once then KNOB 1 moves that duplicate",!ui.note_copy&&ui.note_move&&recording[0].on==RECORD_UNIT/4u&&recording[1].on==3u*RECORD_UNIT/4u);
    track_select(1);frame();bad+=check("switching tracks cancels a single-note move",!ui.note_move&&!ui.note_copy);
    setup();manual(1,67,2);manual(3,60,2);manual(10,72,2);TSEL->step[3].flags|=SF_RECORDED;
    a=(recorded_note_t){.step=3,.on=22000,.note7=60,.vel=83,.duration=40960,.owner=(uint8_t)(recording_owner(TSEL)|(2u<<5))};
    b=a;b.step=7|64u;b.on=40000;recorded_set_note(&b,65);b.vel=97;
    recording_restore_note(TSEL,0,a);recording_restore_note(TSEL,1,b);TSEL->step[8]=(step_t){{65},1,ST_NOTE,SF_RECORDED,97};
    select_range(1,2);hold(B_OCTUP);
    bad+=check("holding OCT+ with a range opens selected-note tools",pop.on==POP_SHEET&&!strcmp(pop.title,"Selected notes"));
    for(pop.sel=0;pop.sel<pop.n&&!str_eq(pop.rows[pop.sel].label,"Quantize");pop.sel++);
    turn(EN_K1,2);press(B_OCTUP);
    bad+=check("selected quantize snaps each onset to the chosen quarter grid only",!strcmp(ui.msg,"NOTES QUANTIZED")&&recording[0].on==RECORD_UNIT/4u&&!memcmp(&recording[1],&b,sizeof b)&&TSEL->step[10].note[0]==72&&recording[0].vel==83&&duration(0)==5u*RECORD_UNIT/2u&&bulk.on==2&&bulk_note_has(0)&&!bulk_note_has(1));
    hold(B_EDIT);
    bad+=check("quantize undo restores mixed precise events and manual tie data",!memcmp(&recording[0],&a,sizeof a)&&!memcmp(&recording[1],&b,sizeof b)&&TSEL->step[1].note[0]==67&&!(TSEL->step[1].flags&SF_RECORDED)&&TSEL->step[2].time==ST_TIE&&TSEL->step[4].time==ST_TIE&&!recording[2].vel);
    setup();manual(6,67,2);a.step=3;a.on=22000;b.step=15|64u|128u;b.on=50000;
    recording_restore_note(TSEL,0,a);recording_restore_note(TSEL,1,b);note_rebuild_all();cursor_set(3);frame();hold(B_OCTUP);sheet_do("Quantize");
    bad+=check("pattern Quantize defaults to nearest whole steps and wraps the final onset",!recording[0].on&&(recording[0].step&63u)==3&&!recording[1].on&&(recording[1].step&63u)==0&&recording[0].vel==83&&recording[1].vel==97&&duration(0)==5u*RECORD_UNIT/2u&&duration(1)==5u*RECORD_UNIT/2u&&recorded_note(&recording[2])==67&&duration(2)==2u*RECORD_UNIT);
    hold(B_EDIT);bad+=check("whole-pattern quantize is one undoable operation",!memcmp(&recording[0],&a,sizeof a)&&!memcmp(&recording[1],&b,sizeof b)&&!recording[2].vel&&TSEL->step[7].time==ST_TIE);
    note_quantize(RECORD_UNIT/2u);bad+=check("half-step quantize rounds independently and retains note lengths",recording[0].on==RECORD_UNIT/2u&&recording[1].on==0u&&(recording[1].step&63u)==0&&duration(0)==5u*RECORD_UNIT/2u);
    uint32_t gen=recording_generation;note_quantize(RECORD_UNIT/2u);
    bad+=check("already quantized notes leave events and undo history untouched",!strcmp(ui.msg,"ALREADY ON GRID")&&recording_generation==gen&&note_transaction.valid);
    setup();manual(1,60,1);a.step=4;a.on=22000;recording_restore_note(TSEL,0,a);note_rebuild_all();
    song.playing=1;note_quantize(RECORD_UNIT);
    bad+=check("quantize refuses playback before touching any event",!strcmp(ui.msg,"STOP TO EDIT")&&!memcmp(&recording[0],&a,sizeof a)&&TSEL->step[1].note[0]==60);
    setup();manual(2,60,1);manual(5,72,1);cursor_set(2);frame();song.playing=1;ui.msg[0]=0;
    turn(EN_K3,-1);i=notes_selected(TSEL);
    bad+=check("manual LENGTH while playing converts to a 0.75-step event without STOP TO EDIT",i<RECORD_MAX&&duration(i)==3u*RECORD_UNIT/4u&&strcmp(ui.msg,"STOP TO EDIT")&&TSEL->step[5].note[0]==72);
    turn(EN_K3,2);bad+=check("  further turns while playing edit that event (1.25 steps)",notes_selected(TSEL)==i&&duration(i)==5u*RECORD_UNIT/4u);
    song.playing=0;
    setup();manual(1,60,1);cursor_set(1);frame();
    for(uint32_t k=0;k<RECORD_MAX;k++)recording[k]=(recorded_note_t){.step=0,.note7=64,.vel=90,.owner=1,.duration=1000};recording_reindex();
    note_move_snapped(1,RECORD_UNIT/4,1);
    bad+=check("a full recording pool rejects conversion/copy before changing any source",TSEL->step[1].note[0]==60&&!(TSEL->step[1].flags&SF_RECORDED)&&recording[0].owner==1&&recording[RECORD_MAX-1].vel==90);
    setup();cursor_set(3);frame();ui.msg[0]=0;turn(EN_K2,1);
    bad+=check("PITCH on an empty step adds no note",!TSEL->step[3].n&&TSEL->step[3].time!=ST_NOTE&&!recording[0].vel&&!strcmp(ui.msg,"SELECT A NOTE"));
    setup();settings_preview=1;manual(2,60,1);cursor_set(2);frame();audition_request=0;turn(EN_K2,2);
    bad+=check("PITCH on a manual note previews the new pitch",TSEL->step[2].note[0]==62&&audition_request&&audition_count==1&&audition_notes[0]==62);
    a=(recorded_note_t){.step=6,.on=5000,.note7=64,.vel=90,.duration=RECORD_UNIT/2,.owner=(uint8_t)recording_owner(TSEL)};
    recording_restore_note(TSEL,0,a);note_rebuild_all();cursor_set(6);frame();audition_request=0;turn(EN_K2,-1);
    bad+=check("PITCH on a recorded note previews the new pitch",recorded_note(&recording[0])==63&&audition_request&&audition_notes[0]==63);
    settings_preview=0;
    setup();manual(1,60,1);manual(4,64,1);manual(8,72,1);TSEL->step[1].n=2;TSEL->step[1].note[1]=67;
    a=(recorded_note_t){.step=3,.on=5000,.note7=62,.vel=90,.duration=RECORD_UNIT/2,.owner=(uint8_t)recording_owner(TSEL)};
    recording_restore_note(TSEL,0,a);note_rebuild_all();select_range(1,3);
    turn(EN_K2,2);turn(EN_K2,3);
    bad+=check("range PITCH shifts every selected note together, the rest stays",TSEL->step[1].note[0]==65&&TSEL->step[1].note[1]==72&&recorded_note(&recording[0])==67&&recording[0].on==5000&&TSEL->step[4].note[0]==69&&TSEL->step[8].note[0]==72&&bulk.on==1);
    turn(EN_K2,100);
    bad+=check("range PITCH stops at the top edge with intervals intact",TSEL->step[1].note[1]==127&&TSEL->step[1].note[0]==120&&TSEL->step[4].note[0]==124&&recorded_note(&recording[0])==122);
    hold(B_EDIT);
    bad+=check("one undo restores the whole run of range PITCH turns",TSEL->step[1].note[0]==60&&TSEL->step[1].note[1]==67&&recorded_note(&recording[0])==62&&TSEL->step[4].note[0]==64&&TSEL->step[8].note[0]==72);
    setup();settings_preview=1;manual(1,60,1);manual(4,64,1);select_range(1,3);audition_request=0;turn(EN_K2,1);
    bad+=check("range PITCH over several steps shifts without a preview",TSEL->step[1].note[0]==61&&TSEL->step[4].note[0]==65&&!audition_request);
    setup();settings_preview=1;manual(1,60,1);TSEL->step[1].n=2;TSEL->step[1].note[1]=67;select_range(1,2);audition_request=0;turn(EN_K2,1);
    bad+=check("range PITCH on one step previews its notes together",audition_request&&audition_count==2&&audition_notes[0]==61&&audition_notes[1]==68);
    setup();settings_preview=1;manual(2,60,1);manual(6,72,1);cursor_set(2);frame();sheet_do("Copy note");audition_request=0;turn(EN_K2,2);
    count=0;for(uint32_t k=0;k<RECORD_MAX;k++)count+=recording_active(TSEL,k);
    bad+=check("PITCH with Copy armed copies in place and pitches the copy only",count==2&&recorded_note(&recording[0])==60&&recorded_note(&recording[1])==62&&recording[0].on==recording[1].on&&!ui.note_copy&&ui.note_move&&audition_request&&audition_count==1&&audition_notes[0]==62);
    turn(EN_K2,1);turn(EN_K1,1);
    bad+=check("..further PITCH and KNOB 1 move the copy, the source stays",recorded_note(&recording[0])==60&&recorded_note(&recording[1])==63&&!recording[0].on&&recording[1].on==RECORD_UNIT/4u);
    turn(EN_K2,1);hold(B_EDIT);
    bad+=check("..after a move a new PITCH run undoes alone",recorded_note(&recording[1])==63&&recording[1].on==RECORD_UNIT/4u);
    setup();manual(2,60,1);cursor_set(2);frame();sheet_do("Copy note");turn(EN_K2,2);turn(EN_K2,1);hold(B_EDIT);
    count=0;for(uint32_t k=0;k<RECORD_MAX;k++)count+=recording_active(TSEL,k);
    bad+=check("one undo takes back the in-place copy and its PITCH run",count==0&&TSEL->step[2].note[0]==60&&TSEL->step[2].n==1&&TSEL->step[6].note[0]==0);
    setup();manual(2,60,1);cursor_set(2);frame();sheet_do("Move note");turn(EN_K1,1);turn(EN_K2,3);
    bad+=check("PITCH while moving a note shifts that note",recorded_note(&recording[0])==63&&recording[0].on==RECORD_UNIT/4u&&ui.note_move);
    setup();manual(2,60,1);manual(5,64,1);select_range(2,3);hold(B_OCTUP);sheet_do("Copy: KNOB 1");turn(EN_K2,12);turn(EN_K1,4);
    bad+=check("range Copy armed: PITCH copies in place, the copies move, the sources stay",bulk.on==2&&recorded_note(&recording[0])==60&&recorded_note(&recording[1])==64&&!recording[0].on&&recorded_note(&recording[2])==72&&recorded_note(&recording[3])==76&&(recording[2].step&63u)==3&&(recording[3].step&63u)==6&&!recording[2].on&&bulk_note_has(2)&&!bulk_note_has(0));
    settings_preview=0;
    setup();manual(2,60,1);manual(5,64,1);select_range(2,3);turn(EN_K1,1);turn(EN_K2,-2);
    bad+=check("after a range move PITCH shifts the moved notes and keeps the range",bulk.on==2&&recorded_note(&recording[0])==58&&recorded_note(&recording[1])==62&&bulk_note_has(0)&&bulk_note_has(1));
    setup();set_engine_of(TSEL,ENGI_FM6);go_page(GR_USER);frame();
    native_put(ENGI_FM6,1,FM6_INIT);native_put(ENGI_FM6,2,FM6_INIT);native_put(ENGI_FM6,3,FM6_INIT);ui.uslot=1;
    btn_down(B_OCTUP);frame();turn(EN_K1,1);btn_up(B_OCTUP);frame();
    bad+=check("preset range selection reaches only the dragged slots",bulk.on&&bulk_has(1)&&bulk_has(2)&&!bulk_has(3));
    press(B_OCTUP);sheet_do("Erase selected");bad+=check("bulk erase presents one confirmation before writing",ui.confirm==CF_BULK_ERASE&&native_used(ENGI_FM6,1));press(B_OCTUP);
    bad+=check("bulk erase deletes selected presets and preserves the next slot",!native_used(ENGI_FM6,1)&&!native_used(ENGI_FM6,2)&&native_used(ENGI_FM6,3)&&!bulk.on);
    printf("Note/library gestures: %s\n",bad?"FAILED":"passed");return !!bad;
}
