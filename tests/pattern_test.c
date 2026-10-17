/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int banks_and_chords(void)
{
    int bad = 0; ui_power_on(); track_t *t = TSEL;
    t->p[P_SLEN] = 64;
    t->step[63] = (step_t){{60,64,67,71},4,ST_NOTE,SF_ACCENT,112,0x81,0x80,41};
    t->step[0] = (step_t){{0},0,ST_TIE};
    motion_set_event(t, 63, P_LEVEL, 83);
    bad += check("bank copy preserves four-note chords, wrapped ties, grid, chance and automation", !pattern_copy(t, 0, 7) &&
        !pattern_request(t, 7) && t->step[63].n == 4 && t->step[63].note[3] == 71 && t->step[0].time == ST_TIE &&
        t->step[63].hit == 0x81 && t->step[63].acc == 0x80 && step_chance(&t->step[63]) == 41 && motion_count(t) == 1);
    motion_step(t, 63, &motion);
    bad += check("copied bank plays its own automation", t->p[P_LEVEL] == 83);
    motion_restore(t); motion_clear(t); t->step[63].note[3] = 72;
    pattern_request(t, 0);
    bad += check("editing or clearing a copy leaves the source chord and motion intact", t->step[63].note[3] == 71 && motion_count(t) == 1);
    step_history_sync(); uint32_t gen = t->pattern_gen;
    pattern_copy(t, 0, 1); pattern_request(t, 1); step_history_sync();
    bad += check("identical-bank switches invalidate manual undo history", t->pattern_gen != gen && step_history.pattern_gen == t->pattern_gen && step_history.count == 1);
    return bad;
}
static int independent_switches(void)
{
    int bad = 0; ui_power_on();
    for (uint32_t k = 0; k < 2; k++) {
        track_t *t = &trk[k]; track_defaults_steps(t); t->p[P_SLEN] = (int16_t)(2+k);
        t->step[0] = (step_t){{60,64,67,71},4,ST_NOTE,0,100};
        pattern_request(t, 1); t->p[P_SLEN] = 4; t->step[0] = (step_t){{62,65,69,72},4,ST_NOTE,0,100};
        pattern_request(t, 0);
    }
    seq_start(); events_block(32);
    pattern_request(&trk[0], 1); pattern_request(&trk[1], 1);
    uint32_t period = div_samples(trk[0].p[P_SDIV]);
    bad += check("playing bank choices queue without replacing the sounding chord", trk[0].pattern == 0 && trk[0].pattern_next == 1 && trk[0].step[0].note[0] == 60);
    events_block(period); events_block(period);
    bad += check("shorter track switches at its own loop end while the longer track keeps playing", trk[0].pattern == 1 && trk[1].pattern == 0 && trk[0].seq_idx == 0 && trk[0].seq_n == 4 && trk[0].seq_notes[3] == 72);
    events_block(period);
    bad += check("longer track switches at its own boundary with its four-note chord", trk[1].pattern == 1 && trk[1].seq_idx == 0 && trk[1].seq_n == 4 && trk[1].seq_notes[3] == 72);
    pattern_request(&trk[0], 3); pattern_request(&trk[0], 1);
    bad += check("selecting the active bank cancels a pending switch", trk[0].pattern_next == 0xff);
    pattern_request(&trk[1], 7); seq_stop();
    bad += check("STOP applies queued banks and releases sequenced notes", trk[1].pattern == 7 && trk[1].pattern_next == 0xff && !trk[1].seq_n);
    return bad;
}
static int all_banks_persist(void)
{
    int bad = 0, ok = 1; ui_power_on();
    for (uint32_t k = 0; k < NTRK; k++) for (uint32_t b = 0; b < NPAT; b++) {
        track_t *t = &trk[k]; pattern_request(t, b);
        t->p[P_SLEN] = (int16_t)(32+b); t->p[P_SSWING] = (int16_t)b;
        t->step[0] = (step_t){{(uint8_t)(40+k*8+b),60,64,67},4,ST_NOTE,SF_SLIDE,100,(uint8_t)(1u<<b),(uint8_t)(1u<<b),(uint8_t)(20+b)};
        t->step[1] = (step_t){{0},0,ST_TIE}; motion_set_event(t, 0, P_LEVEL, (int16_t)(k*8+b));
    }
    chain_config.count = 2; chain_config.row[0] = (chain_row_t){0,3}; chain_config.row[1] = (chain_row_t){7,2};
    for (uint32_t k = 0; k < NTRK; k++) { chain_patterns[0][k] = (uint8_t)k; chain_patterns[1][k] = (uint8_t)(7-k); }
    bad += check("complete project saves all 32 banks", project_save(2) == 0);
    pattern_init(); memset(&motion,0,sizeof motion); project_load(2);
    bad += check("project restores arrangement and active bank selection", chain_config.count == 2 && chain_patterns[0][3] == 3 && chain_patterns[1][3] == 4 && trk[3].pattern == 7);
    for (uint32_t k = 0; k < NTRK; k++) for (uint32_t b = 0; b < NPAT; b++) {
        track_t *t = &trk[k]; pattern_request(t,b);
        ok &= t->step[0].n == 4 && t->step[0].note[0] == 40+k*8+b && t->step[0].note[3] == 67 &&
            t->step[1].time == ST_TIE && t->p[P_SLEN] == 32+(int16_t)b && t->p[P_SSWING] == (int16_t)b &&
            t->step[0].hit == (1u<<b) && t->step[0].acc == (1u<<b) && step_chance(&t->step[0]) == 20+b && motion_count(t)==1;
        motion_begin(); motion_step(t,0,&motion); ok &= t->p[P_LEVEL] == (int16_t)(k*8+b); motion_end();
    }
    bad += check("every bank round-trips chord, ties, timing, chance, hits and bank-specific automation", ok);
    uint8_t raw[BANK_STORE_SIZE]; project_capture(&proj_scratch); bank_pack(raw,&proj_scratch,1);
    raw[BANK_ACTIVE_OFF] = 8; bank_checksum(raw);
    bad += check("invalid bank IDs are rejected even with a valid checksum", !bank_valid(raw,sizeof raw));
    bank_pack(raw,&proj_scratch,1); raw[BANK_EXTRA_OFF+3] |= 0xF0; bank_checksum(raw);
    bad += check("malformed inactive-bank chord is rejected before publication", !bank_valid(raw,sizeof raw) && trk[0].step[0].n == 4);
    return bad;
}
static int panel_banks(void)
{
    int bad=0; ui_power_on(); go_home(); uint8_t original_page=ui.page;
    btn_down(B_SEQ); frame(); key_down(PAT_KEY[7]); frame();
    bad += check("SEQ bank keys stay silent and do not record notes", kb_note[PAT_KEY[7]] == KB_SILENT && !TSEL->nheld);
    key_up(PAT_KEY[7]); frame(); btn_up(B_SEQ); frame();
    bad += check("SEQ plus eighth white key selects bank 8 without a page tap", TSEL->pattern == 7 && ui.home && ui.page == original_page);
    TSEL->step[3] = (step_t){{60,64,67,71},4,ST_NOTE,0,100};
    btn_down(B_SEQ); frame(); key_down(PAT_KEY[7]); frame(); key_down(PAT_KEY[2]); frame();
    key_up(PAT_KEY[2]); key_up(PAT_KEY[7]); frame(); btn_up(B_SEQ); frame();
    bad += check("SEQ plus two pattern keys does not copy", pattern_empty(0, 2) && TSEL->step[3].note[3] == 71);
    fm1_ms += 400u;
    btn_down(B_SEQ); frame(); key_down(PAT_KEY[7]); frames(1100u);
    key_up(PAT_KEY[7]); frame(); tap_key(PAT_KEY[2]); btn_up(B_SEQ); frame();
    pattern_request(TSEL,2);
    bad += check("the compact held-pattern dialog copies its full chord", TSEL->step[3].n == 4 && TSEL->step[3].note[3] == 71);
    return bad;
}
static int arranged_chords(void)
{
    int bad = 0, ok = 1; ui_power_on();
    for (uint32_t k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        for (uint32_t b = 0; b < 2u; b++) {
            pattern_request(t, b); track_defaults_steps(t); t->p[P_SLEN] = 2;
            t->step[0] = (step_t){{(uint8_t)(48+k+b),60,64,67},4,ST_NOTE,0,100};
        }
        pattern_request(t, 0);
        chain_patterns[0][k] = (uint8_t)(1u - (k & 1u));
        chain_patterns[1][k] = (uint8_t)(k & 1u);
    }
    chain_config.count = 2; chain_config.row[0] = (chain_row_t){1,2}; chain_config.row[1] = (chain_row_t){0,1};
    ok &= !chain_prepare(); events_block(32);
    for (uint32_t k = 0; k < NTRK; k++) ok &= trk[k].pattern == chain_patterns[0][k] && trk[k].seq_n == 4 &&
        trk[k].seq_notes[0] == 48+k+chain_patterns[0][k];
    bad += check("SONG starts four independently assigned banks with four-note chords", ok);
    bad += check("SONG protects its bank selections and contents while running", pattern_request(TSEL, 7) && pattern_copy(TSEL, 0, 7));
    uint32_t period = div_samples(trk[0].p[P_SDIV]);
    events_block(period); events_block(period);
    bad += check("SONG repeats the first row without changing its bank assignments", chain.running && chain.row == 0 && chain.remaining == 1 && trk[0].pattern == 1);
    events_block(period); events_block(period); ok = chain.running && chain.row == 1;
    for (uint32_t k = 0; k < NTRK; k++) ok &= trk[k].pattern == chain_patterns[1][k] && trk[k].seq_n == 4 &&
        trk[k].seq_notes[0] == 48+k+chain_patterns[1][k];
    bad += check("SONG changes all four banks together at track one's loop boundary", ok);
    events_block(period); events_block(period); ok = !song.playing && !chain.running;
    for (uint32_t k = 0; k < NTRK; k++) ok &= !trk[k].pattern && !trk[k].seq_n;
    bad += check("SONG completion restores original banks and releases all sequence voices", ok);
    return bad;
}
static int replaced_motion(void)
{
    int bad = 0; ui_power_on();
    motion_set_event(TSEL, 0, P_LEVEL, 40); pattern_request(TSEL, 1);
    motion_store_t edit = {0}; edit.on = 1; edit.count = 1;
    edit.event[0] = (motion_event_t){0,P_LEVEL,80};
    bad += check("replacing motion in another bank preserves valid tagged duplicates", !motion_replace_track(TSEL, &edit) &&
        motion_valid(&motion) && motion.count == 2 && motion_pattern[0] == 0 && motion_pattern[1] == 1);
    bad += check("retired user sample sources remain silent in GRAIN", !SMP_USER_SLOTS && !eng_ok(8) && !ENGINES[8]->npresets);
    return bad;
}
static int migrated_bank_rename(void)
{
    return 0;
}
/* SEQ's pattern dialog, C# / D# (ui_input.c pattern_key_len): a bank halved (LEN only, the rest kept hidden) and
 * doubled (its LEN steps, recorded notes and motion copied after themselves, what lay hidden there replaced); the
 * bank playing: undoable, the sequence going on into the copy */
static void rec_put(track_t *t, uint32_t actual, uint32_t note, uint32_t bits, uint32_t view)
{
    recorded_note_t r = {0};
    uint32_t i = 0;
    while (recording[i].vel) i++;
    r.on = 1000; r.note = (uint8_t)note; r.vel = 100; r.owner = (uint8_t)recording_owner(t);
    r.step = (uint8_t)(actual | bits);
    note_duration(&r, RECORD_UNIT);
    recording_restore_note(t, i, r);
    t->step[view] = (step_t){{(uint8_t)note}, 1, ST_NOTE, SF_RECORDED, 100};
}
static int rec_has(const track_t *t, uint32_t note, uint32_t step)   /* (step: the byte as stored) */
{
    for (uint32_t i = 0; i < RECORD_MAX; i++)
        if (recording[i].vel && (recording[i].owner & 31u) == recording_owner(t) && recording[i].note == note &&
            recording[i].step == step) return 1;
    return 0;
}
static int motion_at(uint32_t k, uint32_t step, int16_t value)
{
    for (uint32_t i = 0; i < motion.count; i++)
        if (motion.event[i].place == (k << 6 | step) && motion.event[i].value == value) return 1;
    return 0;
}
static int doubled_banks(void)
{
    int bad = 0; ui_power_on(); track_t *t = TSEL;
    track_defaults_steps(t); t->p[P_SLEN] = 8;
    t->step[5] = (step_t){{65}, 1, ST_NOTE, 0, 100};
    t->step[12] = (step_t){{99}, 1, ST_NOTE, 0, 100};               /* (hidden, past LEN) */
    rec_put(t, 3, 70, 0, 3);
    rec_put(t, 7, 72, 64u | 128u, 0);                               /* (played late in the last step: shown on the first) */
    rec_put(t, 9, 74, 0, 9);                                        /* (hidden) */
    motion_set_event(t, 2, P_LEVEL, 50); motion_set_event(t, 10, P_LEVEL, 70);
    pattern_key_len(0, 0, 1);
    bad += check("D#: LEN x2, the steps copied after themselves over the hidden ones", t->p[P_SLEN] == 16 &&
        t->step[13].note[0] == 65 && t->step[12].time == ST_REST && msg_is("PATTERN DOUBLED") && ui.pat_did);
    bad += check("  recorded notes copied (a wrapped one's copy wraps from the new last step), the hidden one gone",
        rec_has(t, 70, 3) && rec_has(t, 70, 11) && rec_has(t, 72, 7u | 192u) && rec_has(t, 72, 15u | 192u) &&
        !rec_has(t, 74, 9));
    bad += check("  motion copied, the hidden event gone", motion_count(t) == 2 && motion_at(0, 2, 50) &&
        motion_at(0, 10, 50));
    undo_swap();
    bad += check("  EDIT held: all of it back (LEN, hidden steps, recorded notes, motion)", t->p[P_SLEN] == 8 &&
        t->step[12].note[0] == 99 && rec_has(t, 74, 9) && !rec_has(t, 70, 11) && motion_at(0, 10, 70) &&
        motion_count(t) == 2);
    undo_step(1);
    bad += check("  EDIT + OCT+: doubled again", t->p[P_SLEN] == 16 && rec_has(t, 70, 11) && motion_at(0, 10, 50));

    ui_power_on(); t = TSEL; my_steps(t);                           /* (32 steps, notes on 0, 3 .. 30) */
    motion_set_event(t, 1, P_LEVEL, 60);
    pattern_key_len(0, 0, 1);
    bad += check("no recorded notes: 64 steps, the motion copied too", t->p[P_SLEN] == 64 && t->step[35].note[0] == 43 &&
        t->step[62].note[0] == 70 && motion_count(t) == 2 && motion_at(0, 1, 60) && motion_at(0, 33, 60));
    pattern_key_len(0, 0, 1);
    bad += check("  again: 64 STEPS MAX, nothing changed", t->p[P_SLEN] == 64 && msg_is("64 STEPS MAX"));
    undo_swap();
    bad += check("  EDIT held: 32 steps again, as a load", t->p[P_SLEN] == 32 && t->step[35].time == ST_REST &&
        motion_count(t) == 1 && motion_at(0, 1, 60));
    pattern_key_len(0, 0, 0);
    bad += check("C#: LEN /2, what is past it and the motion kept", t->p[P_SLEN] == 16 && t->step[18].note[0] == 58 &&
        msg_is("PATTERN HALVED") && motion_count(t) == 1);
    undo_swap();
    bad += check("  EDIT held: 32 again", t->p[P_SLEN] == 32 && motion_count(t) == 1);
    t->p[P_SLEN] = 15; pattern_key_len(0, 0, 0);
    bad += check("  15 steps: 8 (the middle one kept)", t->p[P_SLEN] == 8);
    t->p[P_SLEN] = 1; pattern_key_len(0, 0, 0);
    bad += check("  one step: 1 STEP MIN", t->p[P_SLEN] == 1 && msg_is("1 STEP MIN"));
    pattern_t other; pattern_read(0, 3, &other);
    other.timing[0] = 4; other.step[1] = (step_t){{61}, 1, ST_NOTE, 0, 100};
    pattern_write(0, 3, &other);
    pattern_key_len(0, 3, 1);
    bad += check("another bank: its own LEN and steps, the one playing as it was", pattern_at(0, 3)->timing[0] == 8 &&
        pattern_at(0, 3)->step[5].note[0] == 61 && t->p[P_SLEN] == 1);
    pattern_key_len(0, 3, 0);
    bad += check("  .. and halved", pattern_at(0, 3)->timing[0] == 4 && pattern_at(0, 3)->step[5].note[0] == 61);

    ui_power_on(); t = TSEL; track_defaults_steps(t); t->p[P_SLEN] = 4;
    motion_set_event(t, 0, P_LEVEL, 50);
    for (uint32_t i = 0; motion.count < MOTION_MAX; i++) motion_set_event(&trk[1], i, P_LEVEL, 10);
    pattern_key_len(0, 0, 1);
    bad += check("no room for the motion's copy: PATTERN DATA FULL, nothing changed", t->p[P_SLEN] == 4 &&
        msg_is("PATTERN DATA FULL") && motion_count(t) == 1);

    ui_power_on(); t = TSEL; track_defaults_steps(t); t->p[P_SLEN] = 4;
    seq_start(); events_block(32);
    uint32_t period = div_samples(t->p[P_SDIV]);
    events_block(period); events_block(period); events_block(period);
    uint32_t before = t->seq_idx;
    pattern_key_len(0, 0, 1);
    events_block(period);
    bad += check("playing, on its last step: on into the copy (not back to step 1)", before == 3u && t->seq_idx == 4u &&
        t->p[P_SLEN] == 8);
    seq_stop();
    return bad;
}
int main(void)
{
    int bad = banks_and_chords()+independent_switches()+all_banks_persist()+panel_banks()+arranged_chords()+replaced_motion()+migrated_bank_rename()+doubled_banks();
    printf("%s\n",bad ? "PATTERN TEST FAILED" : "pattern bank tests passed"); return !!bad;
}
