/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* MIDI IN through the scale layouts (seq.c midi_map, midi_control.c midi_play): WHITE and ALL map incoming notes as the
 * keys, MPC banks E..H play tracks 1..4 as scale degrees from DEG (DRUM uses finger-drumming pads), other kits
 * and slices keep their own map; a mapped
 * note's note-off ends exactly what it started; ROOT, SCL, QNT and DEG are shared by every part (ui.c scale_share). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include "fixtures/mpc_sample_pad_notes.h"

static void scale_reset(void)
{
    ui_power_on();
    memset(midi_ch, 0, sizeof midi_ch); memset(midi_sel_on, 0, sizeof midi_sel_on);
    memset(midi_owners, 0, sizeof midi_owners); memset(mchord, 0, sizeof mchord);
    mi_r = mi_w = 0; midi_in_overflow = 0;
    fm1_in.notes = kb_prev = 0; song.sel = 0; song.octave = 0;
    events_block(CTL);
}
static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_enqueue(st >> 4 | st << 8 | d1 << 16 | d2 << 24, 1u);
    events_block(CTL);
}
static int sounding(const track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < NVOICE; i++) if (t->v[i].active && t->v[i].gate && t->v[i].note == note) return 1;
    return 0;
}
static uint32_t nsounding(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++) n += t->v[i].active && t->v[i].gate;
    return n;
}

/* the note `degree` scale degrees from C4 (0 = ROOT above C4), by walking semitones: an oracle independent of
 * scale_degree_map */
static int32_t walk(uint32_t scale, int32_t root, int32_t degree)
{
    int32_t offset = 0;
    while (degree) {
        int32_t dir = degree > 0 ? 1 : -1;
        offset += dir;
        if (SCALE_MASK[scale] & (1u << ((offset % 12 + 12) % 12))) degree -= dir;
    }
    return 60 + root + offset;
}

static int mapping_test(void)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    track_t *t = &trk[0];
    uint32_t s, note, ok = 1, ok_all = 1;
    int32_t root, trans;
    int bad = 0;
    scale_reset();
    song.octave = 2;                                  /* MIDI notes ignore the octave buttons */
    for (s = 0; s < SCALE_LEGACY; s++)
        for (root = 0; root < 12; root++)
            for (trans = -24; trans <= 24; trans += 24) {
                t->p[P_SCALE] = (int16_t)s; t->p[P_ROOT] = (int16_t)root; t->p[P_TRANS] = (int16_t)trans;
                t->p[P_QUANT] = Q_WHITE;
                for (note = 0; note < 128u; note++) {
                    int32_t d = DEGREE[note % 12u], want;
                    uint32_t got = midi_map(t, note);
                    if (d < 0) { ok &= got == KB_SILENT; continue; }
                    want = clamp(walk(s, root, d + ((int32_t)note / 12 - 5) * 7) + trans, 0, 127);
                    ok &= got == (uint32_t)want;
                }
                t->p[P_QUANT] = Q_ALL;
                for (note = 0; note < 128u; note++) {
                    int32_t want = walk(s, root, (int32_t)note - 60) + trans;
                    uint32_t got = midi_map(t, note);
                    ok_all &= want < 0 || want > 127 ? got == KB_SILENT : got == (uint32_t)want;
                }
            }
    bad += check("WHITE: 16 scales x 12 roots x TRN, every MIDI note, black keys silent", ok);
    bad += check("ALL: every MIDI note the next degree from C4 = ROOT, out of range silent", ok_all);
    t->p[P_SCALE] = 1; t->p[P_ROOT] = 0; t->p[P_TRANS] = 0;
    ok = 1;
    t->p[P_QUANT] = Q_OFF;
    for (note = 0; note < 128u; note++) ok &= midi_map(t, note) == note;
    t->p[P_QUANT] = Q_SNAP;                           /* SNAP is the keys' mode: MIDI passes through */
    for (note = 0; note < 128u; note++) ok &= midi_map(t, note) == note;
    bad += check("OFF and SNAP: MIDI notes pass through", ok);
    song.octave = 0;
    return bad;
}

static int mpc_test(void)
{
    track_t *t = &trk[0];
    uint32_t note, ok = 1;
    int bad = 0;
    scale_reset();
    t->p[P_SCALE] = 1; t->p[P_ROOT] = 2; t->p[P_QUANT] = Q_MPC; t->p[P_MPCDEG] = 1;   /* D major */
    for (uint32_t k = 0; k < 4u; k++) {
        set_engine_of(&trk[k], ENGI_SID);
        trk[k].p[P_QUANT] = Q_MPC; trk[k].p[P_SCALE] = 1; trk[k].p[P_MPCDEG] = 1;
        for (note = 0; note < 128u; note++) {
            int pad = -1;
            for (uint32_t p = 0; p < 16u; p++) if (MPC_PAD_NOTES[k][p] == note) pad = (int)p;
            uint32_t want = pad < 0 ? KB_SILENT : (uint32_t)walk(1, trk[k].p[P_ROOT], pad - 1);
            ok &= midi_map(&trk[k], note) == want;
        }
    }
    bad += check("MPC: actual project map, all 128 notes on all four tracks (F wraps at 127)", ok);
    bad += check("MPC: pad 02 on DEG 1 is ROOT, pad 01 below, pad 03 above",
                 midi_map(t, 101) == 62u && midi_map(t, 100) == 61u && midi_map(t, 102) == 64u && midi_map(t, 115) == 62u + 24u);
    t->p[P_MPCDEG] = 3;
    bad += check("MPC: DEG 3 moves E02 to the third", midi_map(t, 101) == 66u);
    t->p[P_MPCDEG] = 9;                               /* (past the scale's 7: the 7th) */
    bad += check("MPC: DEG is clamped to the scale's notes", midi_map(t, 101) == 73u && track_desc(t, P_MPCDEG)->max == 7);
    t->p[P_MPCDEG] = 1;
    t->mpc_octave = 1;
    bad += check("MPC: the octave buttons shift the pads", midi_map(t, 101) == 74u);
    t->mpc_octave = 0;
    set_engine_of(&trk[3], ENGI_808);
    trk[3].p[P_QUANT] = Q_MPC;
    bad += check("MPC: a DRUM track maps Bank H to drum sounds, outside Bank H silent",
                 midi_map(&trk[3], 21) == 38u && midi_map(&trk[3], 36) == KB_SILENT);
    trk[3].p[P_QUANT] = Q_WHITE;
    bad += check("WHITE: a DRUM track keeps its GM notes", midi_map(&trk[3], 37) == 37u && midi_map(&trk[3], 36) == 36u);
    return bad;
}

static void mpc_setup(void)
{
    scale_reset();
    for (uint32_t k = 0; k < 4u; k++) {
        track_t *t = &trk[k];
        set_engine_of(t, ENGI_SID);
        t->p[P_QUANT] = Q_MPC; t->p[P_SCALE] = 1; t->p[P_MPCDEG] = 1;
        t->p[P_ROOT] = (int16_t)(k * 2u); t->p[P_TRANS] = 0;
        t->p[P_CHRD] = 0; t->p[P_AMODE] = 0; t->p[P_VOICE] = V_POLY;
        memset(t->step, 0, sizeof t->step);
    }
}

static void pad_input(uint32_t source, uint32_t st, uint32_t note, uint32_t vel)
{
    if (source == 1u) { midi_in_event(st >> 4 | st << 8 | note << 16 | vel << 24); events_block(CTL); }
    else { um_byte((uint8_t)st); um_byte((uint8_t)note); um_byte((uint8_t)vel); events_block(CTL); }
}

static int mpc_route_test(void)
{
    int bad = 0;
    for (uint32_t source = 1; source <= 2u; source++) {
        uint32_t ok = 1;
        for (uint32_t route = 0; route < 5u; route++) {
            mpc_setup(); song.g[G_ROUTE] = (int16_t)route; song.g[G_MIDI] = (int16_t)(source - 1u);
            for (uint32_t k = 0; k < 4u; k++)
                for (uint32_t p = 0; p < 16u; p++) {
                    uint32_t note = MPC_PAD_NOTES[k][p], ch = p;
                    uint32_t want = (uint32_t)walk(1, (int32_t)k * 2, (int32_t)p - 1);
                    song.sel = (uint8_t)((k + 1u) % 4u);
                    pad_input(source, 0x90u | ch, note, 100);
                    ok &= mn_get(ch, note) == k + 1u && sounding(&trk[k], want);
                    for (uint32_t other = 0; other < 4u; other++) ok &= nsounding(&trk[other]) == (other == k);
                    /* Both explicit note-off and velocity-zero note-on must release the saved owner. */
                    pad_input(source, (p & 1u ? 0x90u : 0x80u) | ch, note, 0);
                    ok &= !mn_get(ch, note) && !midi_owners[k] && !nsounding(&trk[k]);
                }
            for (uint32_t note = 36; note < 100u; note++) {
                pad_input(source, 0x90, note, 100);
                ok &= !mn_get(0, note);
            }
        }
        bad += check(source == 1 ? "MPC USB: all 64 pads, channels, ROUT settings, A-D silent and releases" :
                                                  "MPC TRS: all 64 pads, channels, ROUT settings, A-D silent and releases", ok);
    }
    mpc_setup();
    for (uint32_t k = 0; k < 4u; k++) midi(0x90, MPC_PAD_NOTES[k][1], 100);
    uint32_t ok = 1;
    for (uint32_t k = 0; k < 4u; k++) ok &= sounding(&trk[k], 60u + k * 2u) && midi_owners[k] == 1u;
    bad += check("MPC: all four banks can hold notes together on one MIDI channel", ok);
    song.sel = 2; song.g[G_ROUTE] = 4; song.octave = 2;
    trk[0].p[P_QUANT] = Q_OFF; /* mode, scale and root change before release */
    for (uint32_t k = 0; k < 4u; k++) {
        trk[k].p[P_SCALE] = 2; trk[k].p[P_ROOT] = 11;
        midi(0x80, MPC_PAD_NOTES[k][1], 0);
        ok &= !nsounding(&trk[k]) && !midi_owners[k];
    }
    bad += check("MPC: releases survive mode, scale, octave, selection and ROUT changes", ok);
    mpc_setup();
    midi(0xB0, 64, 127);
    for (uint32_t k = 0; k < 4u; k++) {
        midi(0x90, MPC_PAD_NOTES[k][1], 100); midi(0x80, MPC_PAD_NOTES[k][1], 0);
    }
    midi(0xB0, 64, 0);
    ok = 1;
    for (uint32_t k = 0; k < 4u; k++) ok &= !nsounding(&trk[k]) && !midi_owners[k];
    bad += check("MPC: one channel's pedal releases its bank notes on every track", ok);
    mpc_setup();
    for (uint32_t k = 0; k < 4u; k++) midi(0x90, MPC_PAD_NOTES[k][1], 100);
    midi(0xB0, 123, 0);
    ok = 1;
    for (uint32_t k = 0; k < 4u; k++) ok &= !nsounding(&trk[k]) && !midi_owners[k];
    bad += check("MPC: All Notes Off releases every bank owned by the channel", ok);
    mpc_setup();
    set_engine_of(&trk[3], ENGI_808); settings_drumch = 10;
    midi(0x99, 101, 100);
    bad += check("MPC: bank E overrides the DRUM channel and still reaches track 1", mn_get(9, 101) == 1u && sounding(&trk[0], 60));
    midi(0x89, 101, 0);
    mpc_setup();
    trk[2].p[P_CHRD] = CH_MAJ;
    midi(0x90, 5, 100);
    bad += check("MPC: bank G uses track 3's chord settings", nsounding(&trk[2]) == 3u &&
                 sounding(&trk[2], 64) && sounding(&trk[2], 68) && sounding(&trk[2], 71));
    midi(0x90, 5, 100); /* retrigger replaces its old owner and chord */
    midi(0x80, 5, 0);
    bad += check("MPC: chord retrigger and release leave no held notes", !nsounding(&trk[2]) && !midi_owners[2]);
    mpc_setup();
    trk[1].p[P_AMODE] = 1;
    midi(0x90, 117, 100);
    bad += check("MPC: bank F feeds track 2's arpeggiator", trk[1].nheld == 1u && trk[1].held[0] == 62);
    midi(0x80, 117, 0);
    bad += check("MPC: bank F release clears its arpeggiator", !trk[1].nheld && !midi_owners[1]);
    mpc_setup();
    song.ntrk = 2; song.rec = 1u;
    midi(0x90, MPC_PAD_NOTES[2][1], 100); midi(0x90, MPC_PAD_NOTES[3][1], 100);
    bad += check("MPC: absent tracks stay silent and do not start recording", !song.playing && !mn_get(0, 5) && !mn_get(0, 21));
    mpc_setup();
    song.rec = 15u;
    for (uint32_t k = 0; k < 4u; k++) midi(0x90, MPC_PAD_NOTES[k][1], 100);
    for (uint32_t k = 0; k < 4u; k++) midi(0x80, MPC_PAD_NOTES[k][1], 0);
    ok = song.playing;
    for (uint32_t k = 0; k < 4u; k++) ok &= trk[k].step[0].n == 1u && trk[k].step[0].note[0] == 60u + k * 2u;
    bad += check("MPC: armed banks start and record mapped pitches into their own tracks", ok);
    return bad;
}

static int play_test(void)
{
    track_t *t = &trk[0];
    int bad = 0;
    scale_reset();
    t->p[P_SCALE] = 2; t->p[P_ROOT] = 9; t->p[P_QUANT] = Q_WHITE; t->p[P_CHRD] = 0;   /* A minor */
    t->p[P_VOICE] = V_POLY;                           /* (track 1's sound is a LEGATO bass) */
    midi(0x90, 62, 100);                              /* D4: the second white key -> B4 */
    bad += check("WHITE: a MIDI note plays its mapped note", sounding(t, 71) && !sounding(t, 62) && nsounding(t) == 1u);
    midi(0x90, 61, 100);                              /* C#4: black, silent, owns nothing */
    bad += check("WHITE: a black-key note plays nothing", nsounding(t) == 1u && !mn_get(0, 61));
    midi(0x80, 61, 0);
    midi(0x80, 62, 0);
    bad += check("WHITE: its note-off ends the mapped note, nothing hangs", !nsounding(t) && !mn_get(0, 62) &&
                 !midi_owners[0]);
    t->p[P_QUANT] = Q_ALL;
    midi(0x90, 61, 100);
    midi(0x90, 60, 100);
    bad += check("ALL: two MIDI notes play two degrees", sounding(t, 69) && sounding(t, 71) && nsounding(t) == 2u);
    t->p[P_QUANT] = Q_OFF;                            /* the layout changes while they are held */
    midi(0x80, 61, 0);
    midi(0x80, 60, 0);
    bad += check("a layout change while held: the note-offs still end what played", !nsounding(t) && !midi_owners[0]);
    return bad;
}

static int share_test(void)
{
    int bad = 0;
    uint32_t k, ok = 1;
    scale_reset();
    ui.home = 0;
    for (k = 0; k < NPAGES && PG(k).graph != GR_SCALE_PICKER; k++) {}
    ui.page = (uint8_t)k;                             /* SCALES: outside the SCL cycle, opened from its row */
    edit_param(1, 3);                                 /* SCL */
    edit_param(3, 1);                                 /* QNT */
    for (k = 0; k < NPART; k++) ok &= trk[k].p[P_SCALE] == TSEL->p[P_SCALE] && trk[k].p[P_QUANT] == TSEL->p[P_QUANT];
    bad += check("SCL and QNT turned on one track are every part's", ok && TSEL->p[P_SCALE] == 3 && TSEL->p[P_QUANT] == 1);
    song.sel = 2;                                    /* edits from another track update the project too */
    edit_param(2, 5);
    for (k = 0; k < NPART; k++) ok &= trk[k].p[P_ROOT] == 5;
    bad += check("ROOT turned on another track is every part's", ok);
    return bad;
}

int main(void)
{
    int bad = 0;
    bad += mapping_test();
    bad += mpc_test();
    bad += mpc_route_test();
    bad += play_test();
    bad += share_test();
    printf("%s\n", bad ? "MIDI SCALE TEST FAILED" : "MIDI scale layouts test passed");
    return bad != 0;
}
