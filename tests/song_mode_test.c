/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void arrangement(uint32_t count)
{
    chain_defaults(&chain_config); chain_config.count = (uint8_t)count;
    memset(chain_patterns, 0, sizeof chain_patterns);
    for (uint32_t r = 0; r < count; r++) {
        for (uint32_t k = 0; k < NTRK; k++) chain_patterns[r][k] = (uint8_t)(r % trk_npat(k));   /* (MIDI-only: 8 banks) */
        chain_config.row[r] = (chain_row_t){r % NPAT, 1};
    }
}
static void song_page(void) { go_page(GR_SONG); frame(); }
static void select_range(int32_t delta)
{
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP]; host_pressed |= 1u << panel.btn[B_OCTUP]; frame();
    turn(EN_K1, delta);
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]); frame();
}
static int cursor_playback(void)
{
    int bad = 0; ui_power_on();
    for (uint32_t k = 0; k < NPART; k++) {
        trk[k].p[P_SLEN] = 1;
        trk[k].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 96};
        pattern_commit(&trk[k]);
    }
    arrangement(64); memset(chain_patterns, 0, sizeof chain_patterns);
    for (uint32_t r = 0; r < CHAIN_ROWS; r++) chain_config.row[r].slot = 0;
    chain_config.row[62].repeat = 2;
    song_page(); ui.song_row = 62;
    press(B_PLAY);
    bad += check("SONG PLAY snapshots the selected section before the ISR starts", chain.armed && chain.start == 62);
    turn(EN_K1, -10); pattern_poll(); events_block(1);
    bad += check("SONG starts selected section at step zero with its full repeats", chain.running && chain.row == 62 && chain.remaining == 2 && trk[0].seq_idx == 0 && trk[0].seq_n == 1);
    uint32_t period = seq_div_samples(trk[0].p[P_SDIV]);
    events_block(period);
    bad += check("cursor movement does not seek a running song", chain.row == 62 && chain.remaining == 1 && ui.song_row == 52);
    events_block(period);
    bad += check("cursor playback continues into the following section", chain.row == 63 && chain.running);
    events_block(period);
    bad += check("cursor playback stops at the end and restores the original bank", !chain.running && !song.playing && trk[0].pattern == 0 && trk[0].step[0].note[0] == 60);
    ui.song_row = 20; press(B_OCTUP);
    bad += check("OCT+ opens cell actions and never starts playback", pop.on == POP_SHEET && !chain.armed && !song.playing);
    press(B_PLAY); events_block(1);
    bad += check("PLAY works while the song cell sheet is open", chain.running && chain.row == 20);
    press(B_PLAY); events_block(1); press(B_OCTDN);
    ui.song_row = 63; press(B_PLAY); press(B_PLAY); events_block(1);
    bad += check("PLAY can cancel a pending cursor start", !chain.armed && !chain.running && !song.playing);
    arrangement(2); ui.song_row = chain_config.count; press(B_PLAY); events_block(1);
    bad += check("PLAY on the add-section column starts at section one", chain.running && !chain.row);
    seq_stop();
    return bad;
}
static int rests(void)
{
    int bad = 0; ui_power_on();
    track_t *t = &trk[0]; t->p[P_SLEN] = 1;
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 96}; pattern_commit(t);
    motion_set_event(t, 0, P_LEVEL, 12);
    recording[0] = (recorded_note_t){.duration=RECORD_UNIT,.note7=72,.vel=96};
    recorded_set_owner(&recording[0], recording_owner(t)); recording_reindex();
    arrangement(3); memset(chain_patterns, 0, sizeof chain_patterns);
    for (uint32_t r = 0; r < 3; r++) chain_config.row[r].slot = 0;
    chain_patterns[0][0] = CHAIN_REST; chain_config.row[0].slot = CHAIN_REST;
    chain_patterns[1][1] = CHAIN_REST;
    chain_config.row[0].repeat = 2;
    int16_t level = t->p[P_LEVEL];
    bad += check("a song may start with a rest on track one", !chain_prepare_from(0));
    events_block(1);
    bad += check("REST suppresses manual notes, recorded notes and motion", chain.running && !t->seq_n && t->p[P_LEVEL] == level && !t->rh_n);
    uint32_t bar = 16u * seq_div_samples(2u);
    events_block(bar - 1u);
    bad += check("a resting track one keeps a complete four-beat section", !chain.row && chain.remaining == 2 && !t->seq_n);
    events_block(1);
    bad += check("resting sections use the normal repeat count", !chain.row && chain.remaining == 1);
    events_block(bar);
    bad += check("leaving a rest resumes the referenced pattern and motion", chain.row == 1 && t->p[P_LEVEL] == 12 && t->seq_n && !trk[1].seq_n);
    seq_stop();
    bad += check("STOP after rests restores timing, sound controls and pattern contents", t->p[P_SLEN] == 1 && t->step[0].note[0] == 60 && t->p[P_LEVEL] == level);
    arrangement(1); memset(chain_patterns[0], CHAIN_REST, NTRK); chain_config.row[0].slot = CHAIN_REST;
    chain_prepare(); events_block(1); events_block(bar);
    bad += check("an entirely resting song advances and stops normally", !song.playing && !chain.running);
    return bad;
}
static int edits(void)
{
    int bad = 0; ui_power_on(); arrangement(4); song_page(); ui.song_row = 1;
    turn(EN_K3, -2);
    bad += check("turning below P1 selects REST without consuming a pattern bank", chain_patterns[1][0] == CHAIN_REST && chain_config.row[1].slot == CHAIN_REST);
    hold(B_EDIT);
    bad += check("EDIT undoes a song cell edit", chain_patterns[1][0] == 1);
    edit_redo();
    bad += check("EDIT plus OCT+ redoes a song cell edit", chain_patterns[1][0] == CHAIN_REST);
    press(B_OCTUP); sheet_do("Copy to\x85"); turn(EN_K1, 2); turn(EN_K2, 1); press(B_OCTUP);
    bad += check("cell copy selects its destination on the grid and keeps its source", chain_patterns[3][1] == CHAIN_REST && chain_patterns[1][0] == CHAIN_REST && !se.pending);
    turn(EN_K3, 4); chain_patterns[0][1] = CHAIN_REST; song_edit_sync(); press(B_OCTUP); sheet_do("Move to\x85"); turn(EN_K1, -3); press(B_OCTUP);
    bad += check("cell move places its pattern reference and leaves a rest", chain_patterns[0][1] == 3 && chain_patterns[3][1] == CHAIN_REST);
    press(B_OCTUP); sheet_do("Copy to\x85"); turn(EN_K1, 1); press(B_OCTDN);
    bad += check("OCT- cancels choosing a destination without leaving SONG", !se.pending && !ui.home && chain_patterns[1][1] == 1);
    arrangement(4); song_edit_sync(); ui.song_row = 1;
    select_range(1);
    bad += check("held OCT+ and KNOB 1 select a range without starting transport", se.range && song_range_first() == 1 && song_range_last() == 2 && !pop.on && !song.playing);
    press(B_OCTUP); sheet_do("Copy sections"); turn(EN_K1, 2); press(B_OCTUP);
    bad += check("range copy inserts whole sections with all track assignments", chain_config.count == 6 && chain_patterns[4][0] == 1 && chain_patterns[5][0] == 2 && chain_patterns[3][0] == 3);
    hold(B_EDIT);
    bad += check("one undo restores the complete phrase before a range operation", chain_config.count == 4 && chain_patterns[3][0] == 3);
    edit_redo();
    ui.song_row = 1; select_range(1); press(B_OCTUP); sheet_do("Move sections"); turn(EN_K1, 4); press(B_OCTUP);
    bad += check("range move preserves order and removes the original phrase", chain_config.count == 6 && chain_patterns[0][0] == 0 && chain_patterns[1][0] == 3 && chain_patterns[4][0] == 1 && chain_patterns[5][0] == 2);
    press(B_OCTUP); sheet_do("Add empty section");
    bad += check("OCT+ menu adds an empty section with rests on every track", chain_config.count == 7 && chain_config.row[ui.song_row].repeat == 1 && chain_patterns[ui.song_row][NTRK-1] == CHAIN_REST);
    ui.song_row = 0; select_range(1); press(B_OCTUP); sheet_do("Delete sections");
    bad += check("range delete closes the gap and is undoable", chain_config.count == 5);
    hold(B_EDIT);
    bad += check("range-delete undo restores all removed sections", chain_config.count == 7 && chain_patterns[0][0] == 0 && chain_patterns[1][0] == 3);
    arrangement(CHAIN_ROWS); song_edit_sync(); ui.song_row = 0;
    song_sections_move(); turn(EN_K1, CHAIN_ROWS); song_apply_destination();
    bad += check("a full song can move its first section after the final section", chain_config.count == CHAIN_ROWS && chain_patterns[0][0] == 1 && chain_patterns[CHAIN_ROWS-1u][0] == 0 && ui.song_row == CHAIN_ROWS-1u);
    se.range = 1; se.anchor = 0; se.end = CHAIN_ROWS-1u; song_sections_delete();
    bad += check("selecting and deleting an entire 64-section song is bounded", !chain_config.count);
    song_edit_history(0);
    bad += check("undo restores an entire deleted 64-section song", chain_config.count == CHAIN_ROWS && chain_patterns[CHAIN_ROWS-1u][0] == 0);
    resource_release(RES_SONG_UNDO); song_edit_history(0);
    bad += check("reclaimed history refuses undo without touching the song", chain_config.count == CHAIN_ROWS && msg_is("NOTHING TO UNDO"));
    return bad;
}
static int block_spans(void)
{
    int bad = 0; ui_power_on();
    trk[0].p[P_SLEN] = 1; pattern_commit(&trk[0]);
    trk[1].p[P_SLEN] = 4;
    trk[1].step[0] = (step_t){{60},1,ST_NOTE,0,96};
    for (uint32_t j=1;j<4;j++) trk[1].step[j] = (step_t){{0},0,ST_TIE};
    pattern_commit(&trk[1]); motion_set_event(&trk[1],2,P_LEVEL,12);
    arrangement(5); memset(chain_patterns,CHAIN_REST,sizeof chain_patterns);
    for (uint32_t r=0;r<5;r++) { chain_patterns[r][0]=0; chain_config.row[r].slot=0; }
    chain_patterns[0][1]=0;
    song_page(); song.sel=1; ui.song_row=0; turn(EN_K4,3);
    bad += check("KNOB 4 extends only the selected block into following sections", chain_patterns[1][1]==CHAIN_HOLD && chain_patterns[3][1]==CHAIN_HOLD && chain_patterns[4][1]==CHAIN_REST && chain_config.row[0].repeat==1 && chain_config.count==5);
    bad += check("resizing leaves every other track assignment intact", chain_patterns[1][0]==0 && chain_patterns[1][2]==CHAIN_REST);
    chain_prepare(); events_block(1); uint32_t generation=trk[1].pattern_gen;
    uint32_t period=seq_div_samples(trk[0].p[P_SDIV]); events_block(period);
    bad += check("a spanning block keeps its step and held note across section boundaries", chain.row==1 && trk[1].seq_idx==1 && trk[1].seq_n==1 && trk[1].pattern_gen==generation);
    events_block(period);
    bad += check("automation follows the block's continuous phase", chain.row==2 && trk[1].seq_idx==2 && trk[1].p[P_LEVEL]==12);
    events_block(period); events_block(period);
    bad += check("the end of a resized block releases its notes into the following rest", chain.row==4 && !trk[1].seq_n);
    seq_stop(); ui.song_row=2; turn(EN_K4,-2);
    bad += check("shrinking from inside a block trims its right edge and leaves rests", chain_patterns[1][1]==CHAIN_HOLD && chain_patterns[2][1]==CHAIN_REST && chain_patterns[3][1]==CHAIN_REST && !ui.song_row);
    hold(B_EDIT);
    bad += check("undo restores the entire resized block", chain_patterns[3][1]==CHAIN_HOLD);
    ui.song_row=1; press(B_OCTUP); sheet_do("Set rest");
    bad += check("setting a continued block to rest clears its whole span", chain_patterns[0][1]==CHAIN_REST && chain_patterns[3][1]==CHAIN_REST);
    hold(B_EDIT); ui.song_row=2; press(B_PLAY); events_block(1);
    bad += check("PLAY inside a spanning block seeks its phase and chases held notes", chain.running && chain.row==2 && trk[1].pattern==0 && trk[1].seq_idx==2 && trk[1].seq_n && trk[1].p[P_LEVEL]==12);
    seq_stop();
    ui.song_row=0; chain_patterns[4][1]=1; song_edit_sync(); turn(EN_K4,1);
    bad += check("extending cannot overwrite the next block", chain_patterns[4][1]==1 && msg_is("NEXT BLOCK OCCUPIED"));
    ui.song_row=4; turn(EN_K4,2);
    bad += check("extending beyond the song appends empty sections for other tracks", chain_config.count==7 && chain_patterns[6][1]==CHAIN_HOLD && chain_patterns[6][0]==CHAIN_REST);
    ui.song_row=0; chain_patterns[0][0]=CHAIN_REST; chain_config.row[0].slot=CHAIN_REST;
    trk[0].p[P_SLEN]=16; trk[0].p[P_SDIV]=2; song.g[G_BPM]=120;
    char time[16]; sg_time_text(time,chain_row_samples(0));
    bad += check("the time ruler shows minutes and seconds instead of repeats", str_eq(time,"0:02"));
    uint32_t remainder=0; uint64_t long_time=UINT64_C(12345678901);
    uint32_t quotient=chain_div_samples(long_time,50321u,&remainder);
    bad += check("long-song sample arithmetic retains its full timeline precision", quotient==long_time/50321u && remainder==long_time%50321u);
    return bad;
}
static int conductor_resize(void)
{
    int bad=0;ui_power_on();trk[0].p[P_SLEN]=64;trk[0].p[P_SDIV]=2;pattern_commit(&trk[0]);
    arrangement(3);memset(chain_patterns,CHAIN_REST,sizeof chain_patterns);
    for(uint32_t r=0;r<3;r++)chain_config.row[r].slot=CHAIN_REST;
    chain_patterns[0][0]=0;chain_config.row[0].slot=0;chain_patterns[1][1]=0;
    uint64_t before=chain_row_samples(1);song_page();ui.song_row=0;turn(EN_K4,1);
    bad+=check("resizing track one preserves the timing and contents of following sections",chain_patterns[1][0]==CHAIN_HOLD&&chain_patterns[1][1]==0&&chain_row_samples(1)==before);
    chain_prepare_from(1);events_block(1);
    chain.rest_pos=(uint32_t)before-1u;trk[0].seq_idx=0;trk[0].seq_pos=0;events_block(1);
    bad+=check("a continued conductor advances sections independently of its pattern loop",chain.row==2&&chain.running);
    seq_stop();return bad;
}
static int recorded_block(void)
{
    int bad=0; ui_power_on(); trk[0].p[P_SLEN]=1; trk[1].p[P_SLEN]=8;
    pattern_commit(&trk[0]); pattern_commit(&trk[1]);
    recorded_note_t note={.duration=32768,.note7=72,.vel=96};
    recorded_set_exponent(&note,3); recorded_set_owner(&note,recording_owner(&trk[1]));
    recording_restore_note(&trk[1],0,note); recording_reindex();
    trk[1].step[0] = (step_t){{72},1,ST_NOTE,SF_RECORDED,96};
    arrangement(5); memset(chain_patterns,CHAIN_REST,sizeof chain_patterns);
    for(uint32_t r=0;r<5;r++){chain_patterns[r][0]=0;chain_config.row[r].slot=0;}
    chain_patterns[0][1]=0;chain_patterns[1][1]=chain_patterns[2][1]=CHAIN_HOLD;
    chain_prepare();events_block(1);uint32_t period=seq_div_samples(trk[0].p[P_SDIV]);
    events_block(period);events_block(period);
    bad+=check("recorded notes remain held through continued sections",chain.row==2&&recording_owns(&trk[1],72));
    events_block(period);
    bad+=check("a block's right edge releases its retained recorded notes",chain.row==3&&!recording_owns(&trk[1],72));
    seq_stop();chain_prepare_from(2);events_block(1);
    bad+=check("cursor playback inside a block chases an ongoing recorded note",chain.row==2&&trk[1].seq_idx==2&&recording_owns(&trk[1],72));
    seq_stop();events_block(FS);
    recorded_note_t past={.duration=8192,.note7=64,.vel=96};       /* one step: over before the cursor */
    recorded_set_exponent(&past,3); recorded_set_owner(&past,recording_owner(&trk[1]));
    recording_restore_note(&trk[1],1,past); recording_reindex();
    trk[1].step[0] = (step_t){{72,64},2,ST_NOTE,SF_RECORDED,96}; trk[1].p[P_VOICE]=V_POLY; pattern_commit(&trk[1]);
    chain_prepare_from(2);events_block(1);
    int stale=0; for(uint32_t v=0;v<NVOICE;v++) stale|=trk[1].v[v].active&&trk[1].v[v].note==64;
    bad+=check("cursor playback leaves recorded notes that ended before it silent",chain.row==2&&recording_owns(&trk[1],72)&&!stale);
    seq_stop();return bad;
}
static int song_looping(void)
{
    int bad=0;ui_power_on();
    bad+=check("song loop defaults off",!chain_config.rsv[CHAIN_LOOP]);
    for(uint32_t k=0;k<NPART;k++){trk[k].p[P_SLEN]=1;trk[k].step[0]=(step_t){{60},1,ST_NOTE,0,96};pattern_commit(&trk[k]);}
    pattern_request(&trk[0],2);trk[0].p[P_SLEN]=7;trk[0].step[0]=(step_t){{75},1,ST_NOTE,0,96};
    arrangement(2);memset(chain_patterns,0,sizeof chain_patterns);chain_config.row[1].slot=0;chain_config.row[0].repeat=2;
    song_page();press(B_OCTUP);sheet_do("Loop song");
    bad+=check("OCT+ offers Loop song with a persistent header state",chain_config.rsv[CHAIN_LOOP]&&str_eq(song_edit_status(),"LOOP"));
    chain_prepare_from(1);events_block(1);uint32_t period=seq_div_samples(trk[0].p[P_SDIV]);
    trk[0].seq_pos=period-8u+3u;events_block(8);
    bad+=check("a cursor start loops the complete song with fractional timing intact",chain.running&&song.playing&&!chain.row&&chain.remaining==2&&trk[0].seq_pos==3);
    events_block(period);bad+=check("looping retains the first section's complete repeat count",!chain.row&&chain.remaining==1);
    events_block(period);events_block(period);
    bad+=check("song looping survives multiple passes",chain.running&&!chain.row&&chain.remaining==2);
    song_loop_set(0);bad+=check("changing loop mode while playing is refused",chain.config.rsv[CHAIN_LOOP]&&chain_config.rsv[CHAIN_LOOP]&&msg_is("STOP TO EDIT"));
    press(B_PLAY);events_block(1);
    bad+=check("PLAY stops a loop and restores the original pattern and timing",!chain.running&&!song.playing&&trk[0].pattern==2&&trk[0].p[P_SLEN]==7&&trk[0].step[0].note[0]==75);
    bad+=check("loop mode saves with the project",!project_save(0));
    song_loop_set(0);project_load(0);
    bad+=check("project reload restores loop mode",chain_config.rsv[CHAIN_LOOP]==1);
    uint8_t *raw=proj_bank_slot[0];raw[BANKP_STEPS_OFF+1u]=2;bank_checksum(raw);
    bad+=check("invalid saved loop values are rejected before restoration",!bank_valid(raw,BANK_STORE_SIZE));
    song_loop_set(0);project_save(1);song_loop_set(1);project_load(1);
    bad+=check("ordinary older project records default to non-looping",!chain_config.rsv[CHAIN_LOOP]);
    arrangement(1);memset(chain_patterns[0],CHAIN_REST,NTRK);chain_config.row[0].slot=CHAIN_REST;song_loop_set(1);
    chain_prepare();events_block(1);uint32_t bar=16u*seq_div_samples(2u);chain.rest_pos=bar-8u+2u;events_block(8);
    bad+=check("a single resting section loops without hanging or losing time",chain.running&&!chain.row&&chain.rest_pos==2&&!trk[0].seq_n);
    seq_stop();return bad;
}
static int persistence(void)
{
    int bad = 0; ui_power_on(); arrangement(64);
    chain_config.row[63].repeat = 16; chain_patterns[63][2] = CHAIN_REST;
    chain_patterns[22][0] = CHAIN_REST; chain_config.row[22].slot = CHAIN_REST;
    chain_patterns[21][2] = CHAIN_HOLD; chain_patterns[22][2] = CHAIN_HOLD;
    chain_config_t expected = chain_config;
    uint8_t patterns[CHAIN_ROWS][NTRK]; memcpy(patterns, chain_patterns, sizeof patterns);
    bad += check("64 sections with rests save successfully", !project_save(0));
    uint32_t magic; memcpy(&magic, proj_bank_slot[0], 4);
    bad += check("extended songs save in the 16-pattern container", magic == BANKP_MAGIC);
    pattern_init(); chain_defaults(&chain_config); project_load(0);
    bad += check("all 64 sections, per-track rests and repeats survive reload", !memcmp(&expected, &chain_config, sizeof expected) && !memcmp(patterns, chain_patterns, sizeof patterns));
    uint8_t *raw = proj_bank_slot[0]; uint32_t offset = BANKP_STEPS_OFF + 5u + 63u * (NTRK + 1u) + 2u;
    raw[offset] = CHAIN_HOLD + 1u; bank_checksum(raw);
    bad += check("malformed extended assignments are rejected even with a valid checksum", !bank_valid(raw, BANK_STORE_SIZE));
    arrangement(2); bad += check("ordinary songs save too", !project_save(1));
    memcpy(&magic, proj_bank_slot[1], 4);
    bad += check("ordinary songs save in the same container (FBKK only as a dense fallback)", magic == BANKP_MAGIC);
    return bad;
}
int main(void)
{
    int bad = cursor_playback() + rests() + edits() + block_spans() + conductor_resize() + recorded_block() + song_looping() + persistence();
    printf("Song mode: %s\n", bad ? "FAILED" : "passed"); return !!bad;
}
