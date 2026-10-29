/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Real panel, USB/TRS parser, sequencer, song, recorder, mixer and project paths. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>

static void reset_midi(uint32_t track)
{
    ui_power_on();
    track_solo = 0;
    mi_r = mi_w = mo_r = mo_w = 0; midi_in_overflow = 0;
    memset(midi_sel_on, 0, sizeof midi_sel_on);
    memset(midi_owners, 0, sizeof midi_owners);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(&um, 0, sizeof um);
    usb.config = 1;
    if (track >= NPART) song.ntrk = NTRK;              /* (tracks 5..8: in use) */
    set_engine_of(&trk[track], ENGI_MIDI);
    events_block(0); /* consume the sound-change panic before playing */
    assert(trk[track].engine == ENGI_MIDI);
    mo_r = mo_w = 0;
}
static uint32_t pop_midi(uint32_t status, uint32_t note, uint32_t value)
{
    if (mo_r == mo_w) fprintf(stderr, "Missing MIDI %02x %u %u (input owners %u, route %d, input %d)\n", status, note, value, midi_owners[1], song.g[G_ROUTE], song.g[G_MIDI]);
    assert(mo_r != mo_w);
    uint32_t pkt = midi_out_q[mo_r++ % MQ];
    assert(((pkt >> 8) & 255u) == status);
    assert(((pkt >> 16) & 127u) == note);
    assert(((pkt >> 24) & 127u) == value);
    assert((pkt & 15u) == status >> 4);
    return pkt;
}
static void empty_midi(void) { assert(mo_r == mo_w); }
static void trs(uint32_t status, uint32_t note, uint32_t value)
{
    um_byte(status); um_byte(note);
    if (um_len(status) == 2u) um_byte(value);
    events_block(0);
}
static void notes_and_panel(void)
{
    for (uint32_t tr = 0; tr < NTRK; tr++) {
        reset_midi(tr); track_t *t = &trk[tr];
        assert(eng_ok(ENGI_MIDI) && ENGINES[ENGI_MIDI]->npresets == 1 && !eng_state_size(ENGI_MIDI));
        assert(t->p[P_E0] == (int32_t)tr + 1);
        trk_note_on(t, 0, 73); trk_note_on(t, 127, 119);
        pop_midi(0x90u | tr, 0, 73); pop_midi(0x90u | tr, 127, 119);
        assert(!gates());
        trk_note_off(t, 0); trk_note_on(t, 127, 0);
        pop_midi(0x80u | tr, 0, 0); pop_midi(0x80u | tr, 127, 0); empty_midi();
    }
    reset_midi(0); track_t *t = TSEL;
    t->p[P_E0] = 16;
    go_home(); kb_note[0] = 60; kb_trk[0] = 0;
    key_on(0, t); pop_midi(0x9F, 60, 100); empty_midi();
    key_off(0, t); pop_midi(0x8F, 60, 0); empty_midi();
    t->p[P_AMODE] = 1;
    key_on(0, t); empty_midi(); /* keys feed the arp; no duplicate raw key */
    arp_step(t, seq_beat_samples(), seq_beat_samples());
    assert(mo_w > mo_r && !gates());
    seq_stop(); key_off(0, t); mo_r = mo_w;
    puts("MIDI Track: default channels, full note range, velocities, keys and arp passed");
}
static void routing(void)
{
    reset_midi(1); track_t *t = &trk[1];
    t->p[P_E0] = 12;
    trs(0x91, 60, 77); empty_midi(); /* USB selected, extra TRS off */
    t->p[P_E1] = 1;
    trs(0x91, 60, 77); pop_midi(0x9B, 60, 77);
    trs(0x81, 60, 0); pop_midi(0x8B, 60, 0); empty_midi();
    midi_in_event(0x09u | 0x91u << 8 | 62u << 16 | 81u << 24); events_block(0);
    pop_midi(0x9B, 62, 81);
    t->p[P_E1] = 0;
    trs(0x81, 62, 0); empty_midi(); /* a disabled input cannot release USB's note */
    midi_in_event(0x08u | 0x81u << 8 | 62u << 16); events_block(0);
    pop_midi(0x8B, 62, 0);
    t->p[P_E1] = 0; song.g[G_MIDI] = 1;
    trs(0x91, 64, 90); pop_midi(0x9B, 64, 90);
    song.g[G_MIDI] = 0; /* a release from its original input survives a selection change */
    trs(0x81, 64, 0); pop_midi(0x8B, 64, 0);
    song.g[G_MIDI] = 1;
    /* Existing range and selected-track routing precede the output remap. */
    song.g[G_ROUTE] = 2;
    trs(0x91, 65, 90); empty_midi();
    trs(0x95, 65, 90); pop_midi(0x9B, 65, 90);
    trs(0x85, 65, 0); pop_midi(0x8B, 65, 0);
    song.g[G_ROUTE] = 1; song.sel = 1;
    t->p[P_CHRD] = CH_MAJ;
    trs(0x98, 60, 91);
    pop_midi(0x9B, 60, 91); pop_midi(0x9B, 64, 91); pop_midi(0x9B, 67, 91);
    trs(0x88, 60, 0);
    pop_midi(0x8B, 60, 0); pop_midi(0x8B, 64, 0); pop_midi(0x8B, 67, 0);
    trs(0xB8, 1, 100); pop_midi(0xBB, 1, 100);
    int16_t level = t->p[P_LEVEL];
    trs(0xB8, 7, 22); pop_midi(0xBB, 7, 22); assert(t->p[P_LEVEL] == level);
    trs(0xE8, 5, 70); pop_midi(0xEB, 5, 70);
    trs(0xD8, 80, 0); pop_midi(0xDB, 80, 0);
    trs(0xC8, 23, 0); pop_midi(0xCB, 23, 0);
    empty_midi();
    puts("MIDI Track: TRS toggle, USB, ranges, SEL, chords and channel controls passed");
}
static void cleanup(void)
{
    reset_midi(0); track_t *t = TSEL;
    trk_note_on(t, 60, 100); pop_midi(0x90, 60, 100);
    t->p[P_E0] = 7; events_block(0); pop_midi(0x80, 60, 0);
    trk_note_on(t, 64, 88); pop_midi(0x96, 64, 88);
    t->p[P_MUTE] = 1; events_block(0); pop_midi(0x86, 64, 0);
    trk_note_on(t, 65, 100); empty_midi();
    t->p[P_MUTE] = 0;
    trk_note_on(t, 60, 100); pop_midi(0x96, 60, 100);
    track_solo = 2; events_block(0); pop_midi(0x86, 60, 0);
    track_solo = 0;
    trk_note_on(t, 60, 100); pop_midi(0x96, 60, 100);
    set_engine_of(t, ENGI_FM6); events_block(0); pop_midi(0x86, 60, 0); empty_midi();
    reset_midi(0); t = TSEL;
    midi_event(0xB0, 0, 64, 127); pop_midi(0xB0, 64, 127);
    midi_event(0x90, 0, 60, 97); pop_midi(0x90, 60, 97);
    midi_event(0x80, 0, 60, 0); empty_midi();
    seq_stop(); pop_midi(0xB0, 64, 0); pop_midi(0x80, 60, 0); empty_midi();
    /* Fill the real USB ring: releases are retried, new notes fail silent. */
    reset_midi(0); t = TSEL;
    for (uint32_t n = 0; n < MQ; n++) trk_note_on(t, n, 100);
    assert(mo_w - mo_r == MQ);
    trk_note_on(t, 100, 100); assert(!(midi_output[0].held[3] & (1u << 4)));
    trk_all_off(t); assert(midi_track_pending(0));
    mo_r = mo_w; events_block(0);
    for (uint32_t n = 0; n < MQ; n++) pop_midi(0x80, n, 0);
    empty_midi(); assert(!midi_track_pending(0));
    puts("MIDI Track: channel changes, mute, solo, sound changes, sustain, stop and full USB queue passed");
}
static void silent_and_saved(void)
{
    reset_midi(0); track_t *t = TSEL;
    t->p[P_E0] = 14; t->p[P_E1] = 1;
    int16_t level = t->p[P_LEVEL], pan = t->p[P_PAN];
    tracks_edit(0, -20); tracks_edit(1, 10);
    assert(t->p[P_LEVEL] == level && t->p[P_PAN] == pan);
    go_title("FX"); edit_param(0, 10); edit_param(3, 100); press(B_OCTUP);
    assert(t->p[P_FXTYPE] == FX_OFF && pop.on != POP_LIST && cur_page()->graph == GR_FXRACK);
    /* Stale/restored FX and audio modulation cannot allocate or render. */
    t->p[P_FXTYPE] = FX_REVERB; t->p[P_FXMIX] = 127;
    t->p[P_FX2TYPE] = FX_MIDIDELAY; t->p[P_FX2MIX] = 127;
    t->p[P_FXSEND] = t->p[P_FXSEND2] = 127;
    t->p[P_M1SRC] = 1; t->p[P_M1DST] = MD_PITCH; t->p[P_M1AMT] = 63;
    trk_note_on(t, 60, 99); trk_note_off(t, 60); mo_r = mo_w;
    int32_t out[CTL * 2]; uint32_t lfo = t->lfo_ph;
    for (uint32_t k = 0; k < 10; k++) mix_block(out, CTL);
    for (uint32_t k = 0; k < NELEM(out); k++) assert(!out[k]);
    assert(!gates() && !midi_insert.pending && !midi_verb[0] && t->lfo_ph == lfo);
    assert(!resource[RES_ENGINE0].size);
    for (uint32_t s = 0; s < FX_SLOTS; s++) assert(!resource[RES_FX0 + s].size);
    project_capture(&proj_scratch);
    static project_t saved; saved = proj_scratch;
    t->p[P_E0] = 1; t->p[P_E1] = 0;
    assert(!project_restore_runtime(&saved)); events_block(0);
    assert(t->eng_req == ENGI_MIDI && t->p[P_E0] == 14 && t->p[P_E1] == 1);
    static project_store_t packed;
    project_capture(&saved); assert(proj_pack(&packed, &saved));
    assert(proj_import(&proj_scratch, &packed, sizeof packed));
    assert(proj_scratch.t[0].engine == ENGI_MIDI && proj_scratch.t[0].p[P_E0] == 14 && proj_scratch.t[0].p[P_E1] == 1);
    puts("MIDI Track: disabled mixer/FX, zero voices/FX resources, silence and project round-trip passed");
}
static void sequencing(void)
{
    reset_midi(0); track_t *t = TSEL;
    t->p[P_SLEN] = 2; t->p[P_SDIV] = 0; t->p[P_SGATE] = 64;
    step_clear(&t->step[0]); t->step[0].n = 1; t->step[0].note[0] = 60; t->step[0].time = ST_NOTE; t->step[0].vel = 71;
    seq_start(); seq_advance(0); pop_midi(0x90, 60, 71);
    seq_stop(); pop_midi(0x80, 60, 0); empty_midi();
    /* A take preserves its original onset, velocity and held duration. */
    reset_midi(0); t = TSEL;
    song.rec = song.rec_latch = 1; seq_start(); seq_advance(0);
    seq_advance(600); input_on(t, 64, 83); pop_midi(0x90, 64, 83);
    seq_advance(300); input_off(t, 64); pop_midi(0x80, 64, 0);
    seq_stop(); song.rec = song.rec_latch = 0;
    assert(recorded_note(&recording[0]) == 64 && recording[0].vel == 83 && recording[0].duration);
    mo_r = mo_w; seq_start(); seq_advance(0); empty_midi();
    seq_advance(600); pop_midi(0x90, 64, 83);
    seq_advance(300); pop_midi(0x80, 64, 0); seq_stop(); empty_midi();
    /* SONG changes banks through the same track without changing its destination. */
    reset_midi(0); t = TSEL; t->p[P_E0] = 9;
    for (uint32_t bank = 0; bank < 2; bank++) {
        pattern_t p; pattern_read(0, bank, &p);
        for (uint32_t n = 0; n < NSTEP; n++) step_clear(&p.step[n]);
        p.step[0].n = 1; p.step[0].note[0] = (uint8_t)(60 + bank * 7); p.step[0].time = ST_NOTE; p.step[0].vel = 90;
        p.timing[0] = 1; p.timing[1] = 0; p.timing[2] = 0; p.timing[3] = 64;
        pattern_write(0, bank, &p);
    }
    chain_defaults(&chain_config); chain_config.count = 2;
    chain_patterns[0][0] = 0; chain_patterns[1][0] = 1;
    assert(!chain_prepare()); events_block(1); pop_midi(0x98, 60, 90);
    pattern_poll(); seq_advance(seq_beat_samples());
    pop_midi(0x88, 60, 0); pop_midi(0x98, 67, 90);
    pattern_poll(); seq_advance(seq_beat_samples()); pop_midi(0x88, 67, 0);
    assert(!song.playing && !chain.running && t->p[P_E0] == 9); empty_midi();
    puts("MIDI Track: step sequencing, recorded-take timing/velocity and multi-row song playback passed");
}

static void usb_session_cleanup(void)
{
    reset_midi(1);
    set_engine_of(&trk[0], HOST_SYNTH); events_block(0);
    midi_enqueue(0x643c9009u, 1); midi_enqueue(0x643e9009u, 2); events_block(0);
    assert((mn_get(0, 60) & 0x7fu) == 1 && (mn_get(0, 62) & 0x7fu) == 1);
    kb_note[0] = 65; kb_trk[0] = 0; key_on(0, &trk[0]);
    uint32_t keys = kb_chn[0]; assert(keys);
    midi_enqueue(0x64409009u, 1); midi_enqueue(0x64439009u, 2);
    midi_usb_lost = 1; events_block(0);
    assert(!midi_usb_lost && !mn_get(0,60) && !mn_get(0,64));
    assert((mn_get(0,62) & 0x7fu) == 1 && (mn_get(0,67) & 0x7fu) == 1 && kb_chn[0] == keys);
    key_off(0, &trk[0]);
    midi_enqueue(0x64409009u, 1);
    midi_render_timed = 1; midi_render_time = fm1_ms - 1u;
    midi_usb_lost = 1; events_block(0);
    assert(mi_r == mi_w && !mn_get(0,64));             /* discard stale USB even ahead of the block timeline */
    midi_enqueue(0x64459009u, 2); midi_enqueue(0x64479009u, 1);
    midi_usb_lost = 1; events_block(0);               /* a future TRS event blocks the old USB event behind it */
    assert(mi_r != mi_w);
    midi_render_time = fm1_ms; events_block(0);
    assert(mi_r == mi_w && !mn_get(0,71) && (mn_get(0,69) & 0x7fu) == 1);
    midi_render_timed = 0;
    puts("MIDI USB session: disconnected USB owners/stale input released, TRS and local keys retained");
#if MELODEE_USB_HOST
    reset_midi(0);
    usb.config = 0; uh.on = uh.run = 1; uh.out_mps = 64;
    track_t *t = TSEL;
    t->p[P_E0] = 12;
    trk_note_on(t, 72, 85); pop_midi(0x9b,72,85);
    midi_track_controller(t,0xb0,64,127); pop_midi(0xbb,64,127);
    uh.run = 0; uh.out_mps = 0; midi_usb_lost = 1; events_block(0);
    empty_midi(); assert(midi_track_pending(0));
    trk_note_on(t, 74, 85); empty_midi();               /* offline: no new note before old releases */
    uh.run = 1; uh.out_mps = 64; events_block(0);
    pop_midi(0xbb,64,0); pop_midi(0x8b,72,0); empty_midi();
    trk_note_on(t, 74, 85); pop_midi(0x9b,74,85);
    trk_note_off(t, 74); pop_midi(0x8b,74,0); empty_midi();
    uh.on = uh.run = uh.out_mps = 0;
    puts("MIDI Track host output: remapped channel, sustain/note cleanup before new notes after reconnect passed");
#endif
}
/* a bank's steps: one note n at each of its first `count` steps (n 0: empty) */
static int bank_put(uint32_t k, uint32_t bank, uint32_t n, uint32_t count, int32_t len)
{
    pattern_t p; pattern_read(k, bank, &p);
    for (uint32_t i = 0; i < NSTEP; i++) {
        step_clear(&p.step[i]);
        if (n && i < count) { p.step[i].n = 1; p.step[i].note[0] = (uint8_t)(n + i % 12u); p.step[i].time = ST_NOTE; p.step[i].vel = 100; }
    }
    p.timing[0] = (int16_t)len;
    return pattern_write(k, bank, &p);
}
static void bank_fill(uint32_t k, uint32_t bank, uint32_t n, uint32_t count, int32_t len) { assert(bank_put(k, bank, n, count, len)); }
static int bank_is(uint32_t k, uint32_t bank, uint32_t n, uint32_t count, int32_t len)
{
    pattern_t p; pattern_read(k, bank, &p);
    if (p.timing[0] != len) return 0;
    for (uint32_t i = 0; i < NSTEP; i++) {
        int on = n && i < count;
        if (on != (p.step[i].time == ST_NOTE && p.step[i].n == 1u)) return 0;
        if (on && (p.step[i].note[0] != n + i % 12u || p.step[i].vel != 100u)) return 0;
    }
    return 1;
}
static uint32_t slot_magic(uint32_t slot) { uint32_t m; memcpy(&m, proj_bank_slot[slot], 4); return m; }
static void midi_track_navigation(void)
{
    reset_midi(0); project_new_blank();
    for(uint32_t k=NPART;k<NTRK;k++)track_add();
    go_home(); track_select(1); btn_down(B_SEQ);
    for (uint32_t k = NTRK; k-- > 0;) {
        pattern_keys(1u << key_at(1, k));
        assert(song.sel == k && trk_group() == k / NPART * NPART);
    }
    pattern_keys(1u << key_at(1, 7)); assert(song.sel == 7);
    btn_up(B_SEQ); stop_transport();
    go_page(GR_PATGRID); patgrid_edit(0,1);
    assert(trk[4].pattern==1&&trk[0].pattern==0&&song.sel==7);
    layer_opened(LAYER_GLO);
    int16_t before=trk[0].p[P_MUTE];
    layer_key(LAYER_GLO,TRK_KEY[1]);
    assert(trk[5].p[P_MUTE]&&!trk[4].p[P_MUTE]&&trk[0].p[P_MUTE]==before);
    ui.layer=LAYER_GLO; kb_layer=1u; layer_keys(1u);
    assert(track_solo==(1u<<4));
    kb_layer=0; layer_keys(0); assert(!track_solo); ui.layer=0;
    /* Away from Patterns, SEQ still selects only existing tracks. */
    go_home();
    song.ntrk=5; track_select(3); btn_down(B_SEQ);
    pattern_keys(1u << key_at(1, 4)); assert(song.sel == 4);
    for (uint32_t k = 5; k < 11; k++) {
        pattern_keys(1u << key_at(1, k)); assert(song.sel == 4);
    }
    pattern_keys(1u << key_at(1, 1)); assert(song.sel == 1);
    btn_up(B_SEQ);
    go_page(GR_PATGRID);
    pattern_keys(1u << key_at(1, 7));
    assert(song.sel == 7 && trk_n() == NTRK && midi_track_only(TSEL) && cur_page()->graph == GR_PATGRID);
    puts("MIDI-only navigation: direct SEQ track keys, partial groups, PATTERNS knobs and GLO mute/solo passed");
}
static void midi_only_step_entry(void)
{
    for(uint32_t k=NPART;k<NTRK;k++){
        reset_midi(k); song.sel=(uint8_t)k; song.g[G_ROUTE]=1;
        go_page(GR_ROLL); song.seq_mode=1; ui.cursor=0;
        trk[k].p[P_QUANT]=0; trk[k].p[P_CHRD]=0; seq_midi_reset();
        midi_event(0x90,15,127,100); seq_midi_events(1);
        assert(step_midi_held==1&&trk[k].step[0].n==1&&trk[k].step[0].note[0]==127);
        midi_event(0x80,15,127,0); seq_midi_events(1); seq_entry_finish();
        assert(!step_midi_held&&ui.cursor==1);
        midi_event(0x90,15,60,100); seq_midi_events(1); assert(step_midi_held==1);
        midi_forget_track(k); seq_midi_events(1); assert(!step_midi_held);
    }
    puts("MIDI-only step entry: tracks 5..8, channel 16, note 127, releases and all-off passed");
}
/* Tracks 5..8: added on demand, MIDI Tracks only, every bank saved (FBKL) and moved down by a removal */
static void midi_only_tracks(void)
{
    reset_midi(0);
    project_new_blank();
    assert(trk_n() == NPART);
    for (uint32_t k = NPART; k < NTRK; k++) {
        track_add();
        assert(trk_n() == k + 1u && song.sel == k && trk[k].eng_req == ENGI_MIDI && trk[k].p[P_E0] == (int32_t)k + 1);
        assert(cur_page()->scope == SC_ENGINE && cur_page()->id[0] == P_E0);   /* (its channel, not the browser) */
    }
    track_add(); assert(trk_n() == NTRK);
    events_block(0); mo_r = mo_w;
    set_engine_of(&trk[5], ENGI_FM6); events_block(0);
    assert(trk[5].eng_req == ENGI_MIDI && trk[5].engine == ENGI_MIDI);
    mo_r = mo_w;
    trk_note_on(&trk[5], 60, 90); pop_midi(0x95, 60, 90);
    trk_note_off(&trk[5], 60); pop_midi(0x85, 60, 0); empty_midi();
    for (uint32_t v = 0; v < NVOICE; v++) assert(!midi_voice[v].active);
    int32_t out[CTL * 2]; mix_block(out, CTL);
    /* the music: track 5 (index 4) two banks, track 7 (index 6) channel 11 on its third bank, a song column, a take */
    bank_fill(4, 0, 48, 16, 16); bank_fill(4, 3, 50, 64, 64);
    bank_fill(6, 2, 70, 5, 8); trk[6].p[P_E0] = 11;
    pattern_switch(&trk[6], 2); assert(trk[6].pattern == 2);
    chain_defaults(&chain_config); chain_config.count = 2; chain_patterns[1][4] = 3; chain_patterns[0][6] = 2;
    recorded_note_t r = {0}; recorded_set_note(&r,61); r.vel = 77; r.duration = 100; recorded_set_owner(&r, 6u * NPAT + 2u);
    recorded_set_step(&r, 1, 0, 0); recording[RECORD_MAX - 1u] = r; recording_reindex();
    bank_fill(1, 1, 30, 7, 32);                         /* (a part's other bank: sparse in FBKL too) */
    song.sel = 6;
    assert(!project_save_as(0, "EIGHT"));
    assert(slot_magic(0) == BANKP_MAGIC);
    project_new_blank(); assert(trk_n() == NPART && bank_is(4, 0, 0, 0, TP[P_SLEN].def));
    project_load(0);
    assert(trk_n() == NTRK && song.sel == 6);
    assert(bank_is(4, 0, 48, 16, 16) && bank_is(4, 3, 50, 64, 64) && bank_is(6, 2, 70, 5, 8) && bank_is(1, 1, 30, 7, 32));
    assert(trk[6].pattern == 2 && trk[6].p[P_E0] == 11 && trk[4].p[P_E0] == 5 && trk[6].eng_req == ENGI_MIDI);
    assert(chain_config.count == 2 && chain_patterns[1][4] == 3 && chain_patterns[0][6] == 2);
    {
        uint32_t found = 0;
        for (uint32_t i = 0; i < RECORD_MAX; i++)
            found += recording[i].vel == 77u && recorded_owner(&recording[i]) == 6u * NPAT + 2u && recorded_note(&recording[i]) == 61u;
        assert(found == 1u);
    }
    /* removing track 2: track 5 (MIDI) to place 4, its banks and channel with it; the last place empty */
    assert(!track_remove(1));
    assert(trk_n() == NTRK - 1u && trk[3].eng_req == ENGI_MIDI && trk[3].p[P_E0] == 5 && trk[5].p[P_E0] == 11);
    assert(bank_is(3, 0, 48, 16, 16) && bank_is(3, 3, 50, 64, 64) && bank_is(5, 2, 70, 5, 8) && trk[5].pattern == 2);
    assert(chain_patterns[1][3] == 3 && chain_patterns[0][5] == 2);
    /* four tracks in use: FBKP too (FBKK only when FBKP cannot hold a dense four-part project) */
    project_new_blank(); assert(!project_save_as(1, "FOUR")); assert(slot_magic(1) == BANKP_MAGIC);
    /* every bank of every track full: more than a project holds. The edit past its room is refused (and leaves the
     * bank as it was), so the project still saves */
    for (uint32_t k = trk_n(); k < NTRK; k++) track_add();
    uint32_t refused = 0;
    pattern_full = 0;
    for (uint32_t k = 0; k < NTRK; k++)
        for (uint32_t b = 0; b < trk_npat(k); b++) {
            pattern_t before; pattern_read(k, b, &before);
            if (bank_put(k, b, 40, 64, 64)) { assert(bank_is(k, b, 40, 64, 64)); continue; }
            pattern_t after; pattern_read(k, b, &after); assert(!memcmp(&before, &after, sizeof after)); refused++;
        }
    assert(refused && pattern_full && pattern_file_cost() <= pattern_room());
    assert(!project_save_as(2, "FULL") && !bank_too_full && slot_magic(2) == BANKP_MAGIC);
    puts("MIDI-only tracks 5..8: add, MIDI only, FBKP save/load, removal, edits past a project's room refused passed");
}
int main(void)
{
    notes_and_panel(); routing(); cleanup(); silent_and_saved(); sequencing();
    usb_session_cleanup(); midi_only_tracks(); midi_track_navigation(); midi_only_step_entry();
    puts("MIDI TRACK TESTS PASSED");
    return 0;
}
