/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>

static void setup(uint32_t count)
{
    ui_power_on(); chain_config.count = (uint8_t)count;
    for (uint32_t r = 0; r < count; r++) {
        chain_config.row[r] = (chain_row_t){0, 1};
        memset(chain_patterns[r], 0, NTRK);
    }
    for (uint32_t k = 0; k < NPART; k++) {
        trk[k].p[P_SLEN] = 1;
        trk[k].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 90};
        pattern_commit(&trk[k]);
    }
    go_page(GR_SONG); ui.song_row = 0; frame();
}
static uint32_t choose(uint32_t row, uint32_t engine, uint32_t preset)
{
    ui.song_row = (uint8_t)row;
    press(B_OCTUP); sheet_do("Set preset");
    assert(song_picker.active && cur_page()->graph == GR_BROWSE);
    engine_load_of(TSEL, engine, preset);
    press(B_OCTUP);
    assert(!song_picker.active && cur_page()->graph == GR_SONG);
    uint32_t id = chain_config.sound[row][song.sel]; assert(id);
    return id;
}
static void browser_and_history(void)
{
    setup(3);
    motion_set_event(TSEL, 0, P_ED_FLT, 12);
    motion_store_t before_motion = motion;
    uint32_t before = track_sig(TSEL), before_undo = undo_top;
    song_preset_open(); engine_load_of(TSEL, ENGI_PROPHET, 21); press(B_OCTDN);
    assert(!song_picker.active && !chain_config.sound[0][0]);
    assert(track_sig(TSEL) == before && !memcmp(&before_motion, &motion, sizeof motion) && undo_top == before_undo);
    uint32_t id = choose(1, ENGI_PROPHET, 21);
    assert(track_sig(TSEL) == before && !memcmp(&before_motion, &motion, sizeof motion) && undo_top == before_undo);
    assert(!chain_config.sound[0][0] && !chain_config.sound[2][0]);
    assert(song_sound_get(id)->engine == ENGI_PROPHET && song_sound_get(id)->preset == 21);
    song_preset_remove(); assert(!chain_config.sound[1][0]);
    song_edit_history(0); assert(chain_config.sound[1][0] == id);
    song_edit_history(0); assert(!chain_config.sound[1][0]);
    song_edit_history(1); assert(chain_config.sound[1][0] == id);
    song_preset_open(); engine_load_of(TSEL, ENGI_FM6, 4); go_home();
    assert(!song_picker.active && track_sig(TSEL) == before);
    go_page(GR_SONG); song_preset_open(); engine_load_of(TSEL, ENGI_FM6, 7); track_select(1);
    assert(!song_picker.active && track_sig(&trk[0]) == before);
    puts("Song presets: browser cancel, commit, navigation, motion preservation and undo passed");
}
static void playback(void)
{
    setup(3); uint32_t id = choose(1, ENGI_PROPHET, 60);
    int16_t base[P_COUNT]; memcpy(base, TSEL->p, sizeof base); uint8_t engine = TSEL->eng_req;
    uint32_t tick = seq_div_samples(TSEL->p[P_SDIV]);
    assert(!chain_prepare_from(1)); events_block(1);
    assert(chain.row == 1 && TSEL->eng_req == ENGI_PROPHET && !memcmp(p5_patch_of(TSEL), &song_sound_get(id)->patch.p5, sizeof(p5_patch_t)));
    events_block(tick);
    assert(chain.row == 2 && TSEL->eng_req == engine);
    for (uint32_t i = 0; i < P_COUNT; i++) assert(TSEL->p[i] == base[i]);
    events_block(tick); assert(!chain.running);
    assert(!chain_prepare_from(1)); transport_req = 2; events_block(1);
    assert(!chain.armed && !chain.running && TSEL->eng_req == engine);
    chain_patterns[1][0] = CHAIN_HOLD; chain_config.row[1].slot = CHAIN_HOLD;
    assert(!chain_prepare_from(0)); events_block(1); events_block(tick);
    assert(chain.row == 1 && TSEL->eng_req == ENGI_PROPHET && TSEL->seq_idx == 0);
    seq_stop(); assert(TSEL->eng_req == engine && !memcmp(TSEL->p, base, sizeof base));
    chain_config.rsv[CHAIN_LOOP] = 1;
    assert(!chain_prepare_from(1)); events_block(1); events_block(16u * tick); events_block(tick);
    assert(chain.running && !chain.row && TSEL->eng_req == engine);
    seq_stop();
    puts("Song presets: direct start, section restore, continuations, cancellation, loop and STOP passed");
}
static void native_and_persistence(void)
{
    const uint32_t engines[] = {ENGI_PROPHET, ENGI_FM6, ENGI_CZ, ENGI_DRUM, 3};
    for (uint32_t j = 0; j < NELEM(engines); j++) {
        setup(3); uint32_t id = choose(1, engines[j], 1);
        song_sound_t expected = *song_sound_get(id);
        assert(!project_save(0)); assert(*(uint32_t *)proj_bank_slot[0] == BANKP_MAGIC);   /* (FBKP keeps the song's sound copies) */
        uint8_t raw[BANK_STORE_SIZE]; memcpy(raw, proj_bank_slot[0], sizeof raw);
        /* Library and live sound edits cannot change the saved song copy. */
        engine_load_of(TSEL, ENGI_FM6, 8); project_load(0);
        id = chain_config.sound[1][0]; assert(id && !memcmp(song_sound_get(id), &expected, sizeof expected));
        assert(!chain_prepare_from(1)); events_block(1); assert(TSEL->eng_req == engines[j]); seq_stop();
        uint32_t off = BANK_ACTIVE_OFF + bank_sound_off();
        raw[off + 4u + NTRK] = CHAIN_SOUNDS + 1u; bank_checksum(raw);
        assert(!bank_valid(raw, sizeof raw));
        memcpy(raw, proj_bank_slot[0], sizeof raw); raw[off + BANKO_HEAD_BYTES + P_COUNT] = 255; bank_checksum(raw);
        assert(!bank_valid(raw, sizeof raw));
    }
    setup(2); engine_load_of(TSEL, ENGI_FM6, 3); assert(up_store_to(0,0,"LOCAL FM") == 3);
    song_preset_open(); assert(!up_load_to(0,0)); song_preset_finish(1);
    uint32_t id = chain_config.sound[0][0]; assert(id);
    song_sound_t expected = *song_sound_get(id);
    engine_load_of(TSEL, ENGI_FM6, 12); assert(up_store_to(0,0,"REPLACED") == 3);
    assert(!project_save(0)); project_load(0);
    assert(!memcmp(song_sound_get(chain_config.sound[0][0]), &expected, sizeof expected));
    puts("Song presets: every native engine, user overwrite independence, versioned save/load and corruption rejection passed");
}
static void arrangement_edits(void)
{
    setup(4); uint32_t a = choose(0, ENGI_CZ, 2), b = choose(1, ENGI_FM6, 2);
    ui.song_row = 0; song_insert(1); assert(chain_config.sound[1][0] == a && chain_config.sound[2][0] == b);
    song_edit_history(0); assert(chain_config.count == 4 && chain_config.sound[1][0] == b);
    ui.song_row = 0; song_cell_copy(); ui.song_row = 3; song.sel = 1; song_apply_destination();
    /* Occupied notes must be cleared before copying a block. */
    chain_patterns[3][1] = CHAIN_REST; song_edit_sync(); ui.song_row = 0; song.sel = 0;
    song_cell_copy(); ui.song_row = 3; song.sel = 1; song_apply_destination();
    assert(chain_config.sound[3][1] == a && chain_config.sound[0][0] == a);
    song.sel = 0; ui.song_row = 0; song_rows_remove(0,1);
    assert(chain_config.sound[0][0] == b && chain_config.sound[2][1] == a && !chain_config.sound[3][1]);
    puts("Song presets: section duplication, undo, cell copy and deletion passed");
}
static void automation_and_failures(void)
{
    setup(3); uint32_t id = choose(0, ENGI_FM6, 2);
    chain_config.sound[1][0] = (uint8_t)id;
    motion_set_event(TSEL, 0, P_ED_FLT, 31);
    int16_t base = TSEL->p[P_ED_FLT];
    uint32_t tick = seq_div_samples(TSEL->p[P_SDIV]);
    assert(!chain_prepare_from(0)); events_block(1); assert(TSEL->p[P_ED_FLT] == 31);
    events_block(tick); assert(chain.row == 1 && TSEL->p[P_ED_FLT] == 31);
    seq_stop(); assert(TSEL->p[P_ED_FLT] == base);
    chain_config.sound[2][1] = CHAIN_SOUNDS; /* missing pool entry: fail before arming */
    assert(chain_prepare_from(0) == 67 && !chain.armed && !song_sounds.mask);
    seq_stop(); assert(TSEL->p[P_ED_FLT] == base);
    chain_config.sound[2][1] = 0;
    assert(!resource[RES_SONG_BASE].size);
    song_preset_open(); assert(song_picker.active);
    uint32_t before = chain_config.sound[ui.song_row][0];
    engine_load_of(TSEL, ENGI_CZ, 9);
    /* Maximal immutable history cannot overwrite any referenced sound. */
    song_preset_finish(0); assert(chain_config.sound[ui.song_row][0] == before);
    puts("Song presets: automation bases, missing snapshot rejection and safe cancellation passed");
}
static void whole_pattern_sound(void)
{
    setup(6); track_t *t = &trk[1];
    trk[0].p[P_SLEN] = 16; pattern_commit(&trk[0]);
    t->p[P_SLEN] = 64;
    for (uint32_t j = 1; j < 64; j++) t->step[j] = (step_t){{0}, 0, ST_TIE};
    pattern_commit(t);
    for (uint32_t r = 1; r < 4; r++) chain_patterns[r][1] = CHAIN_HOLD;
    chain_patterns[5][1] = CHAIN_REST;
    song.sel = 1; uint8_t base = t->eng_req;
    uint32_t id = choose(2, ENGI_PROPHET, 13); /* select in the middle of the 64-step occurrence */
    for (uint32_t r = 0; r < 4; r++) assert(chain_config.sound[r][1] == id);
    assert(!chain_config.sound[4][1]); /* another occurrence of the same note pattern stays independent */
    uint32_t step = seq_div_samples(t->p[P_SDIV]);
    assert(!chain_prepare_from(0)); events_block(1);
    for (uint32_t r = 0; r < 4; r++) {
        assert(chain.row == r && t->eng_req == ENGI_PROPHET && t->seq_idx == r * 16u && t->seq_n == 1);
        for (uint32_t j = 0; j < 16; j++) events_block(step);
    }
    assert(chain.row == 4 && t->eng_req == base); seq_stop();
    assert(!chain_prepare_from(2)); events_block(1);
    assert(t->eng_req == ENGI_PROPHET && t->seq_idx == 32 && t->seq_n == 1); seq_stop();
    ui.song_row = 3; song_preset_remove();
    for (uint32_t r = 0; r < 4; r++) assert(!chain_config.sound[r][1]);
    song_edit_history(0);
    for (uint32_t r = 0; r < 4; r++) assert(chain_config.sound[r][1] == id);
    chain_patterns[4][1] = CHAIN_REST; song_edit_sync(); ui.song_row = 1;
    song_block_resize(1); assert(chain_config.sound[4][1] == id);
    song_block_resize(-3);
    for (uint32_t r = 2; r < 5; r++) assert(!chain_config.sound[r][1]);
    song_block_resize(2);
    for (uint32_t r = 0; r < 4; r++) assert(chain_config.sound[r][1] == id);
    /* Older saved assignments only named the first section: inherit through HOLD. */
    for (uint32_t r = 1; r < 4; r++) chain_config.sound[r][1] = 0;
    assert(!project_save(0)); project_load(0); t = &trk[1];
    assert(!chain_prepare_from(0)); events_block(1);
    for (uint32_t r = 0; r < 4; r++) { assert(t->eng_req == ENGI_PROPHET); for (uint32_t j = 0; j < 16; j++) events_block(step); }
    assert(t->eng_req == base); seq_stop();
    puts("Song presets: all 64 steps, continued notes, middle selection/start, independent occurrences, resize, undo and legacy inheritance passed");
}

static uint32_t choose_pattern(uint32_t bank, uint32_t engine, uint32_t preset)
{
    go_page(GR_PATGRID); assert(!pattern_request(TSEL, bank));
    uint32_t before = track_sig(TSEL);
    press(B_OCTUP); sheet_do("Set preset");
    assert(song_picker.active && song_picker.pattern && song_picker.row == bank);
    engine_load_of(TSEL, engine, preset); press(B_OCTUP);
    assert(!song_picker.active && cur_page()->graph == GR_PATGRID && track_sig(TSEL) == before);
    uint32_t id = pattern_sound[song.sel][bank]; assert(id); return id;
}
static void pattern_presets(void)
{
    setup(4); uint32_t def = TSEL->eng_req;
    motion_set_event(TSEL, 0, P_ED_FLT, 12); motion_store_t saved_motion = motion;
    uint32_t a = choose_pattern(0, ENGI_CZ, 2), b = choose_pattern(1, ENGI_FM6, 3);
    assert(!chain_config.sound[0][0]);
    assert(!memcmp(&saved_motion, &motion, sizeof motion));
    uint32_t tick = seq_div_samples(TSEL->p[P_SDIV]);
    assert(!pattern_request(TSEL, 0)); seq_start(); events_block(1);
    assert(TSEL->eng_req == ENGI_CZ);
    assert(!pattern_request(TSEL, 1)); events_block(tick);
    assert(TSEL->pattern == 1 && TSEL->eng_req == ENGI_FM6);
    assert(!pattern_request(TSEL, 2)); for (uint32_t j = 0; j < 64u; j++) events_block(tick);
    assert(TSEL->pattern == 2 && TSEL->eng_req == def);
    seq_stop(); assert(TSEL->eng_req == def);
    assert(!pattern_request(TSEL, 1)); seq_start(); events_block(1);
    assert(TSEL->eng_req == ENGI_FM6); seq_stop(); assert(TSEL->eng_req == def);
    go_page(GR_SONG); uint32_t c = choose(1, ENGI_PROPHET, 13);
    chain_patterns[0][0] = 0; chain_patterns[1][0] = 1; chain_patterns[2][0] = 1; chain_patterns[3][0] = 2;
    for (uint32_t r = 0; r < 4; r++) chain_config.row[r].slot = chain_patterns[r][0];
    assert(!chain_prepare_from(0)); events_block(1); assert(TSEL->eng_req == ENGI_CZ);
    events_block(tick); assert(chain.row == 1 && TSEL->eng_req == ENGI_PROPHET);
    for (uint32_t j = 0; j < (uint32_t)pattern_timing(0, 1, 0); j++) events_block(tick); assert(chain.row == 2 && TSEL->eng_req == ENGI_FM6);
    for (uint32_t j = 0; j < (uint32_t)pattern_timing(0, 1, 0); j++) events_block(tick); assert(chain.row == 3 && TSEL->eng_req == def);
    seq_stop(); assert(TSEL->eng_req == def);
    assert(!chain_prepare_from(2)); events_block(1); assert(TSEL->eng_req == ENGI_FM6); seq_stop();
    assert(!pattern_copy(TSEL, 0, 3) && pattern_sound[0][3] == a);
    assert(!pattern_empty(0, 3));
    song_sound_t pa = *song_sound_get(a), pb = *song_sound_get(b), sc = *song_sound_get(c);
    assert(!project_save(0)); assert(*(uint32_t *)proj_bank_slot[0] == BANKQ_MAGIC);
    uint8_t raw[BANK_STORE_SIZE]; memcpy(raw, proj_bank_slot[0], sizeof raw);
    project_new_blank(); project_load(0);
    assert(!memcmp(song_sound_get(pattern_sound[0][0]), &pa, sizeof pa));
    assert(!memcmp(song_sound_get(pattern_sound[0][1]), &pb, sizeof pb));
    assert(!memcmp(song_sound_get(chain_config.sound[1][0]), &sc, sizeof sc));
    assert(TSEL->eng_req == def);
    assert(resource[RES_SONG_BASE].size);
    assert(!pattern_request(TSEL, 0)); seq_start(); events_block(1); assert(TSEL->eng_req == ENGI_CZ); seq_stop();
    uint32_t off = BANK_ACTIVE_OFF + bank_pattern_off();
    raw[off] = CHAIN_SOUNDS + 1u; bank_checksum(raw); assert(!bank_valid(raw, sizeof raw));
    memcpy(raw, proj_bank_slot[0], sizeof raw); raw[off + NPART * NPAT] = raw[off]; bank_checksum(raw);
    assert(!bank_valid(raw, sizeof raw));
    assert(!bank_valid((uint8_t[12]){0}, 12));
    go_page(GR_PATGRID); assert(!pattern_request(TSEL, 0));
    pattern_preset_open(); engine_load_of(TSEL, ENGI_PROPHET, 8); press(B_OCTDN);
    assert(TSEL->eng_req == def && !memcmp(song_sound_get(pattern_sound[0][0]), &pa, sizeof pa));
    pattern_preset_remove(); assert(!pattern_sound[0][0] && pattern_sound[0][1]);
    undo_step(0); assert(pattern_sound[0][0]); undo_step(1); assert(!pattern_sound[0][0]);
    undo_step(0); assert(pattern_sound[0][0]);
    seq_clear_now(CF_DEL_PAT, 0); assert(!pattern_sound[0][0]);
    undo_step(0); assert(pattern_sound[0][0]); undo_step(1); assert(!pattern_sound[0][0]);
    project_new_blank(); assert(!pattern_sounds_any());
    assert(!project_save(1)); assert(*(uint32_t *)proj_bank_slot[1] == BANKP_MAGIC);
    project_load(1); assert(!pattern_sounds_any());
    puts("Pattern presets: browser, manual switches, Song > Pattern > Default, direct start, copy, save/load, legacy and invalid IDs passed");
}

static void pattern_native_and_recorded_undo(void)
{
    const uint32_t engines[] = {ENGI_FM6, ENGI_CZ, ENGI_PROPHET, ENGI_DRUM, 3};
    for (uint32_t e = 0; e < NELEM(engines); e++) {
        setup(0); engine_load_of(TSEL, ENGI_FM6, 9);
        song_sound_t base, live;
        song_sound_capture(&base, TSEL, "");
        uint32_t id = choose_pattern(0, engines[e], 2);
        song_sound_t sound = *song_sound_get(id);
        seq_start(); events_block(1);
        song_sound_capture(&live, TSEL, "");
        assert(live.engine == sound.engine && !memcmp(&live.patch, &sound.patch, sizeof live.patch));
        seq_stop(); song_sound_capture(&live, TSEL, ""); assert(!memcmp(&base, &live, sizeof base));
        assert(!project_save(0)); project_new_blank(); project_load(0);
        assert(!memcmp(song_sound_get(pattern_sound[0][0]), &sound, sizeof sound));
        seq_start(); events_block(1); assert(TSEL->eng_req == engines[e]); seq_stop();
    }
    setup(0); uint32_t id = choose_pattern(0, ENGI_CZ, 4);
    recorded_note_t note = {.step=0,.on=5000,.note7=60,.vel=80,.duration=RECORD_UNIT/2};
    recorded_set_owner(&note, recording_owner(TSEL));
    recording[0] = note; TSEL->step[0].flags |= SF_RECORDED; recording_reindex();
    seq_clear_now(CF_DEL_PAT, 0); assert(!pattern_sound[0][0] && !recording[0].vel);
    pattern_poll(); assert(!resource[RES_SONG_BASE].size);
    undo_step(0); assert(pattern_sound[0][0] == id && !memcmp(&recording[0], &note, sizeof note));
    assert(resource[RES_SONG_BASE].size);
    undo_step(1); assert(!pattern_sound[0][0] && !recording[0].vel);
    undo_step(0); assert(pattern_sound[0][0] == id && recording[0].vel);
    seq_start(); events_block(1); assert(TSEL->eng_req == ENGI_CZ); seq_stop();
    setup(1); engine_load_of(TSEL, ENGI_FM6, 9);
    song_sound_t base, stopped; song_sound_capture(&base, TSEL, "");
    id = choose_pattern(0, ENGI_CZ, 4);
    pattern_preset_open(); engine_load_of(TSEL, ENGI_PROPHET, 8);
    press(B_PLAY); assert(!song.playing && song_picker.active);
    transport_req = 1; events_block(1); frame();
    assert(song.playing && !song_picker.active && TSEL->eng_req == ENGI_CZ);
    seq_stop(); song_sound_capture(&stopped, TSEL, ""); assert(!memcmp(&base, &stopped, sizeof base));
    pattern_preset_open(); engine_load_of(TSEL, ENGI_PROPHET, 8);
    assert(!chain_prepare_from(0)); events_block(1); frame();
    assert(chain.running && !song_picker.active && TSEL->eng_req == ENGI_CZ);
    seq_stop(); song_sound_capture(&stopped, TSEL, ""); assert(!memcmp(&base, &stopped, sizeof base));
    puts("Pattern presets: native snapshots, exact default restoration, recorded Delete undo and external start during audition passed");
}

int main(void)
{
    pattern_presets(); pattern_native_and_recorded_undo();
    browser_and_history(); playback(); native_and_persistence(); arrangement_edits(); automation_and_failures(); whole_pattern_sound();
    puts("Song presets: passed"); return 0;
}
