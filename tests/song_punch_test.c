/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void setup(uint32_t rows, uint32_t steps)
{
    ui_power_on();
    trk[0].p[P_SLEN] = (int16_t)steps; pattern_commit(&trk[0]);
    chain_config.count = (uint8_t)rows;
    go_page(GR_SONG); frame();
}
static void dsp(void)
{
    int32_t l[CTL], r[CTL];
    for (uint32_t b = 0; b < 6u; b++) {
        for (uint32_t i = 0; i < CTL; i++) { l[i] = 10000; r[i] = -2000; }
        punch_process(l, r, CTL);
    }
}
static int playback(void)
{
    int bad = 0; setup(2, 8);
    chain_config.row[0].repeat = 2;
    chain_config.fx[0][0] = (chain_fx_t){PX_MONO + 1, 1, 3};
    chain_config.fx[0][1] = (chain_fx_t){PX_LPF + 1, 2, 1};
    chain_config.fx[0][2] = (chain_fx_t){PX_REV + 1, 5, 1};
    chain_config.row[1].slot = CHAIN_REST; memset(chain_patterns[1], CHAIN_REST, NTRK);
    chain_config.fx[1][0] = (chain_fx_t){PX_GATE + 1, 1, 2};
    bad += check("timed punches prepare with the existing song transport", !chain_prepare());
    events_block(1); dsp();
    bad += check("a future buffered punch warms history from the song start", punch_song_req == -1 && punch_ring && punch_song_buffered);
    uint32_t period = seq_div_samples(trk[0].p[P_SDIV]);
    events_block(period); dsp();
    bad += check("the punch starts on its conductor step and reaches master DSP", punch_song_req == PX_MONO && punch.cur == PX_MONO);
    events_block(period); dsp();
    bad += check("an overlapping later punch temporarily takes priority", punch_song_req == PX_LPF && punch.cur == PX_LPF);
    events_block(period); dsp();
    bad += check("the earlier punch resumes until its own duration ends", punch_song_req == PX_MONO && punch.cur == PX_MONO);
    events_block(period); dsp();
    bad += check("duration ends on the first excluded step with a dry fade", punch_song_req == -1 && punch.cur == -1 && !punch.g);
    events_block(period); dsp();
    bad += check("buffered song effects play without the FX layer or a held key", punch_song_req == PX_REV && punch.cur == PX_REV && punch.req == -1 && !punch.keybit);
    for (uint32_t i = 0; i < 4; i++) events_block(period);
    bad += check("section repeats replay punch timing from step zero", !chain.row && chain.remaining == 1 && punch_song_req == PX_MONO);
    for (uint32_t i = 0; i < 7; i++) events_block(period);
    bad += check("entering a rest clears the preceding section punch", chain.row == 1 && punch_song_req == -1);
    events_block(seq_div_samples(2u)); dsp();
    bad += check("resting sections retain their own straight-step punch clock", punch_song_req == PX_GATE && punch.cur == PX_GATE);
    seq_stop(); dsp();
    bad += check("STOP clears song requests, fades dry and releases history", punch_song_req == -1 && !punch_song_buffered && punch.cur == -1 && !punch_ring);
    return bad;
}
static int overrides_and_restarts(void)
{
    int bad = 0; setup(1, 1);
    chain_config.row[0].repeat = 2;
    chain_config.fx[0][0] = (chain_fx_t){PX_LPF + 1, 0, 1};
    chain_prepare(); events_block(1); dsp();
    bad += check("filter-only songs do not allocate punch history", punch.cur == PX_LPF && !punch_ring);
    uint32_t gate = punch_song_gate;
    punch.cut = 40 << 8;
    events_block(seq_div_samples(trk[0].p[P_SDIV])); dsp();
    bad += check("a full-section punch restarts its sweep on the next repeat", punch_song_gate != gate && punch.cur == PX_LPF && punch.cut > (120 << 8));
    punch_panel_lock = 1; key_down(punch_key_of_fx(PX_MONO)); keyboard_block(); dsp();
    bad += check("a live FX key overrides the song without changing the saved lane", punch.cur == PX_MONO && punch_song_req == PX_LPF && chain_config.fx[0][0].fx == PX_LPF + 1);
    key_up(punch_key_of_fx(PX_MONO)); keyboard_block(); dsp();
    bad += check("releasing the live key resumes the active song punch", punch.cur == PX_LPF && punch.req == -1);
    punch_panel_lock = 0; seq_stop(); dsp();
    setup(2, 8); chain_patterns[1][0] = CHAIN_HOLD; chain_config.row[1].slot = CHAIN_HOLD;
    chain_config.fx[1][0] = (chain_fx_t){PX_MONO + 1, 0, 1};
    chain_prepare_from(1); events_block(1); dsp();
    bad += check("starting inside a continued block uses section-local FX timing", chain.row == 1 && punch_song_req == PX_MONO && punch.cur == PX_MONO);
    seq_stop(); dsp(); return bad;
}
static int editing(void)
{
    int bad = 0; setup(3, 16);
    press(B_OCTUP); sheet_do("Punch FX");
    bad += check("the section menu opens the named punch timeline", song_punch_on() && song_punch.row == 0 && song.grid == 2);
    key_down(punch_key_of_fx(PX_MONO)); keyboard_block(); frame();
    bad += check("choosing an FX key writes the punch without playing a note or live effect", chain_config.fx[0][0].fx == PX_MONO + 1 && kb_note[punch_key_of_fx(PX_MONO)] == KB_SILENT && punch.req == -1);
    key_up(punch_key_of_fx(PX_MONO)); keyboard_block(); frame();
    turn(EN_K3, 3);
    turn(EN_K4, 4);
    chain_fx_t expected = {PX_MONO + 1, 3, 5};
    bad += check("direct start and end knobs edit a saved timed punch", !memcmp(&expected, &chain_config.fx[0][0], sizeof expected));
    bad += check("a new unsaved song with only punch edits is considered dirty", project_dirty());
    host_sent = 0; turn(EN_K4, 100);
    bad += check("punch duration cannot run beyond the section pattern", chain_config.fx[0][0].length == 13);
    bad += check("changing timing redraws only the changed lane and placement cards", host_sent < 22000u);
    press(B_OCTDN); hold(B_EDIT);
    bad += check("song undo restores a punch edit", chain_config.fx[0][0].length == 5);
    song_insert(1);
    bad += check("duplicating a section copies its timed master punches", !memcmp(chain_config.fx[0], chain_config.fx[1], sizeof chain_config.fx[0]));
    song_sections_move(); ui.song_row = chain_config.count; song_apply_destination();
    bad += check("moving a section moves its punches and closes the source gap", chain_config.fx[0][0].fx && !chain_config.fx[1][0].fx && chain_config.fx[3][0].fx);
    song_edit_history(0);
    bad += check("undo restores the section order and its punches together", chain_config.fx[0][0].fx && chain_config.fx[1][0].fx && !chain_config.fx[3][0].fx);
    song_punch.row = 0; song_punch_clear();
    bad += check("clear section FX preserves the section and other sections' punches", !chain_fx_count(0) && chain_fx_count(1) == 1 && chain_config.count == 4);
    song_edit_history(0);
    bad += check("clearing section FX is undoable", chain_fx_count(0) == 1);
    chain_prepare(); events_block(1); expected = chain_config.fx[0][0]; song_punch_edit(0, -1);
    bad += check("punch edits are refused during song playback", !memcmp(&expected, &chain_config.fx[0][0], sizeof expected) && msg_is("STOP TO EDIT"));
    seq_stop(); dsp(); return bad;
}
static int visual_controls(void)
{
    int bad = 0; setup(2, 16);
    press(B_FX);
    bad += check("FX tapped on SONG opens its visual punch editor directly", song_punch_on() && song_punch.row == 0);
    host_sent = 0; frame();
    bad += check("an idle song FX selector sends no screen redraw", !host_sent);
    ui_message("FIRST"); str_cpy(ui.msg2, "SECOND", sizeof ui.msg2); ui.msg_t = 1; frame();
    bad += check("feedback advances its queued message while the FX editor stays open", msg_is("SECOND") && !ui.msg2[0]);
    ui.msg_t = 0; frame();
    turn(EN_K1, 1);
    bad += check("knob 1 uses the existing keyboard's effect order", chain_config.fx[0][0].fx == punch_fx_of_key(0) + 1);
    turn(EN_K1, 1);
    bad += check("the next effect follows the next physical key", chain_config.fx[0][0].fx == punch_fx_of_key(1) + 1);
    press(B_OCTUP);
    bad += check("Enter selects the next free punch without adding phantom data", song_punch.event == 1 && chain_fx_count(0) == 1);
    turn(EN_K1, 1); turn(EN_K4, 4); turn(EN_K3, 100);
    bad += check("moving a punch to the section edge preserves its duration", chain_config.fx[0][1].start == 11 && chain_config.fx[0][1].length == 5);
    turn(EN_K3, -100);
    bad += check("moving the punch back retains its original duration", !chain_config.fx[0][1].start && chain_config.fx[0][1].length == 5);
    press(B_EDIT);
    bad += check("EDIT removes only the selected punch", !chain_config.fx[0][1].fx && chain_fx_count(0) == 1 && song_punch_on());
    hold(B_EDIT);
    bad += check("EDIT held restores the deleted punch through song undo", chain_config.fx[0][1].fx && song_punch_on());
    turn(EN_K3, 6); turn(EN_K4, -100);
    bad += check("END stops at START and retains one visible step", chain_config.fx[0][1].start == 6 && chain_config.fx[0][1].length == 1);
    turn(EN_K4, 3);
    bad += check("END extends the visible range to the inclusive last step", chain_config.fx[0][1].start + chain_config.fx[0][1].length == 10);
    turn(EN_K1, -100);
    bad += check("the FX knob has an explicit OFF before its first effect", !chain_config.fx[0][1].fx && !chain_config.fx[0][1].start && !chain_config.fx[0][1].length && chain_fx_count(0) == 1);
    hold(B_EDIT);
    bad += check("setting OFF is undoable and preserves the other punches", chain_config.fx[0][1].fx && chain_config.fx[0][1].start == 6 && chain_config.fx[0][1].length == 4 && chain_fx_count(0) == 2);
    turn(EN_K1, -100); turn(EN_K1, 1);
    bad += check("turning right from OFF adds the first effect with fresh timing", chain_config.fx[0][1].fx == punch_fx_of_key(0) + 1 && !chain_config.fx[0][1].start && chain_config.fx[0][1].length == 1);
    bad += check("the visual grid follows the conductor pattern length", song_punch_steps_at(0) == 16);
    chain_patterns[1][0] = CHAIN_HOLD;
    bad += check("continued sections show their own sixteen-step punch grid", song_punch_steps_at(1) == 16);
    turn(EN_SELECT, 1);
    bad += check("SELECT browses sections without leaving the selector", song_punch_on() && song_punch.row == 1 && ui.song_row == 1);
    press(B_PLAY); events_block(1); frame();
    bad += check("PLAY auditions the song from the selected section", chain.running && chain.row == 1 && song_punch_on());
    turn(EN_K1, 1);
    bad += check("auditioning never changes saved punch data", !chain_fx_count(1) && msg_is("STOP TO EDIT"));
    press(B_PLAY); events_block(1); frame();
    bad += check("PLAY stops audition without leaving the editor", !chain.running && song_punch_on());
    press(B_OCTDN);
    bad += check("Back returns to SONG and restores ordinary key ownership", !pop.on && cur_page()->graph == GR_SONG && song.grid == keys_mode());
    press(B_FX); press(B_HOME);
    bad += check("HOME closes the selector and returns to Stage", !pop.on && ui.home);
    setup(1, 16); btn_down(B_FX); frames(800);
    bad += check("holding FX on SONG retains the live layer gesture", ui.layer == LAYER_FX && !song_punch_on() && !pop.on);
    btn_up(B_FX); frame();
    setup(1, 16); key_down(2); keyboard_block(); press(B_FX);
    bad += check("entering the editor keeps an already held musical note's ownership", gates() && kb_note[2] != KB_SILENT);
    key_up(2); keyboard_block(); frame();
    bad += check("that musical key releases normally inside the FX selector", !gates());
    setup(1, 16); song_punch_open(); pattern_epoch++; frame();
    bad += check("a project/pattern storage reset closes stale section selection", !song_punch_on());
    return bad;
}
static int timeline_pixels(void)
{
    int bad = 0; setup(1, 16); palette_set(0);
    const uint8_t effect[] = {PX_LPF, PX_MONO, PX_REV, PX_LOOP16};
    for (uint32_t j = 0; j < CHAIN_FX_EVENTS; j++)
        chain_config.fx[0][j] = (chain_fx_t){effect[j] + 1u, (uint8_t)(j * 2u), 4};
    song_punch_open(); frame();
    uint32_t wrong = 0;
    for (uint32_t j = 0; j < CHAIN_FX_EVENTS; j++) for (uint32_t i = 0; i < 16; i++) {
        uint16_t c = i >= j * 2u && i < j * 2u + 4u ? punch_color(effect[j]) : T_SURF;
        wrong += host_screen[(122u + j * 25u) * 240u + 10u + i * 14u] != swap16(c);
    }
    bad += check("all four named lanes colour exactly their saved steps, including overlaps", !wrong);
    turn(EN_K3, 2); turn(EN_K4, -1);
    static uint16_t saved[240 * 240];
    memcpy(saved, host_screen, sizeof saved); ui.force = 1; frame();
    bad += check("moving the endpoints leaves the same pixels as a complete redraw", !memcmp(saved, host_screen, sizeof saved));
    press(B_PLAY); events_block(1); frame();
    bad += check("audition draws the current step across the four punch lanes", host_screen[117u * 240u + 8u] == swap16(T_TEXT) && host_screen[192u * 240u + 8u] == swap16(T_TEXT));
    events_block(seq_div_samples(trk[0].p[P_SDIV])); frame();
    bad += check("the audition playhead advances to the next grid cell", host_screen[117u * 240u + 22u] == swap16(T_TEXT) && host_screen[117u * 240u + 8u] != swap16(T_TEXT));
    seq_stop(); dsp(); press(B_OCTDN);
    memset(chain_config.fx[0], 0, sizeof chain_config.fx[0]);
    chain_config.row[0].repeat = 2;
    chain_config.fx[0][0] = (chain_fx_t){PX_LPF + 1, 2, 4}; ui.force = 1; frame();
    for (uint32_t k = 0; k < trk_n() && k < NPART; k++) {
        trk[k].step[0] = (step_t){.time = ST_NOTE, .n = 1, .note = {60}, .vel = 96};
        pattern_commit(&trk[k]);
    }
    ui.force = 1; frame();
    uint16_t bar = ux_mix(punch_color(PX_LPF), T_TEXT, 30);
    wrong = 0;
    for (uint32_t k = 0; k < trk_n() && k < NPART; k++) {
        uint32_t y = SG_ROW_Y + k * SG_ROW_H + 6u;
        wrong += host_screen[y * 240u + 15u] != swap16(bar);
        wrong += host_screen[y * 240u + 41u] != swap16(bar);
        wrong += host_screen[y * 240u + 25u] != swap16(T_TRK(k));
    }
    bad += check("vertical punch ranges repeat inside each filled pattern block", !wrong);
    bad += check("the song progress track stays visible when punches are present", host_screen[(SG_FOOT_Y + 34u) * 240u + 20u] == swap16(T_SURF));
    /* A whole-section punch must leave the rounded corners, selection rim,
     * empty grid and complete footer exactly as the original screen drew. */
    memset(chain_config.fx[0], 0, sizeof chain_config.fx[0]); frame();
    memcpy(saved, host_screen, sizeof saved);
    chain_config.fx[0][0] = (chain_fx_t){PX_LPF + 1, 0, 16}; frame();
    wrong = 0;
    for (uint32_t k = 0; k < trk_n() && k < NPART; k++) for (uint32_t y = 0; y < SG_ROW_H; y++) for (uint32_t x = 0; x < 240u; x++) {
        int inside = x >= 9u && x < 63u && y >= 2u && y < 30u;
        if (inside && (y < 7u || y >= 25u)) {
            int32_t edge = x - 9u < 62u - x ? (int32_t)(x - 9u) : (int32_t)(62u - x);
            if (edge < 5) inside = rr_cov(5, edge, y < 7u ? y - 2u : 29u - y) != 0;
        }
        uint32_t i = (SG_ROW_Y + k * SG_ROW_H + y) * 240u + x;
        if (!inside) wrong += saved[i] != host_screen[i];
    }
    bad += check("punch colour cannot escape a rounded block or alter its outline", !wrong);
    bad += check("punches do not replace any footer or progress pixels", !memcmp(saved + SG_FOOT_Y * 240u, host_screen + SG_FOOT_Y * 240u, (240u - SG_FOOT_Y) * 240u * sizeof *saved));
    memset(trk[0].step, 0, sizeof trk[0].step); pattern_commit(&trk[0]); frame();
    memcpy(saved, host_screen, sizeof saved);
    memset(chain_config.fx[0], 0, sizeof chain_config.fx[0]); frame();
    bad += check("empty dashed blocks receive no punch overlay", !memcmp(saved + SG_ROW_Y * 240u, host_screen + SG_ROW_Y * 240u, SG_ROW_H * 240u * sizeof *saved));
    chain_config.fx[0][0] = (chain_fx_t){PX_LPF + 1, 2, 4}; frame();
    chain_config.fx[0][0].start = 8; frame();
    memcpy(saved, host_screen, sizeof saved); ui.force = 1; frame();
    bad += check("section tint follows timing changes even when the FX count is unchanged", !memcmp(saved, host_screen, sizeof saved));
    trk[0].step[0] = (step_t){.time = ST_NOTE, .n = 1, .note = {60}, .vel = 96};
    trk[0].p[P_SLEN] = 64; pattern_commit(&trk[0]); chain_config.row[0].repeat = 1;
    chain_config.fx[0][0] = (chain_fx_t){PX_LPF + 1, 63, 1}; frame();
    bad += check("a one-step punch at the far edge remains visible inside the rounded block", host_screen[(SG_ROW_Y + 10u) * 240u + 62u] == swap16(bar));
    chain_config.fx[0][0].start = 0; frame();
    bad += check("a one-step punch at the first edge remains visible inside the rounded block", host_screen[(SG_ROW_Y + 10u) * 240u + 9u] == swap16(bar));
    return bad;
}
static int persistence(void)
{
    int bad = 0; setup(2, 64);
    chain_config.fx[0][0] = (chain_fx_t){PX_FREEZE + 1, 63, 1};
    chain_config.fx[1][3] = (chain_fx_t){PX_MONO + 1, 0, 64};
    chain_config_t expected = chain_config;
    bad += check("songs containing timed punches save in the 16-pattern format, punches included", !project_save(0) && *(uint32_t *)proj_bank_slot[0] == BANKP_MAGIC);
    chain_defaults(&chain_config); project_load(0);
    bad += check("all timed punches survive a complete project reload", !memcmp(&expected, &chain_config, sizeof expected));
    uint8_t *raw = proj_bank_slot[0];
    uint32_t offset = BANKP_STEPS_OFF + BANKM_SONG_BYTES;
    static const uint8_t malformed[][3] = {{PUNCH_NFX + 1, 0, 1}, {1, 64, 1}, {1, 63, 2}, {1, 0, 0}, {0, 1, 0}};
    int valid = 0;
    for (uint32_t j = 0; j < NELEM(malformed); j++) {
        memcpy(raw + offset, malformed[j], 3); bank_checksum(raw);
        valid += bank_valid(raw, BANK_STORE_SIZE);
    }
    bad += check("invalid effects, steps and durations fail before project publication", !valid);
    setup(1, 16); project_save(1); chain_config.fx[0][0] = expected.fx[0][0]; project_load(1);
    bad += check("loading an older project clears all previous song FX", !chain_fx_count(0));
    return bad;
}
int main(void)
{
    int bad = playback() + overrides_and_restarts() + editing() + visual_controls() + timeline_pixels() + persistence();
    printf("Song punch: %s\n", bad ? "FAILED" : "passed"); return !!bad;
}
