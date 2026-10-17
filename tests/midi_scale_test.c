/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* MIDI IN through the scale layouts (seq.c midi_map, midi_control.c midi_play): WHITE and ALL map incoming notes as the
 * keys, MPC plays MPC Bank H pads (MIDI 20..35) as scale degrees from DEG, kits and slices keep their own map; a mapped
 * note's note-off ends exactly what it started; SCL, QNT and DEG are shared by every part (ui.c scale_share). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

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
    for (note = 0; note < 128u; note++)
        ok &= (note < 20u || note > 35u) == (midi_map(t, note) == KB_SILENT);
    bad += check("MPC: only Bank H (MIDI 20..35) plays", ok);
    bad += check("MPC: H02 on DEG 1 is the ROOT, H01 the degree below, H03 the next",
                 midi_map(t, 21) == 62u && midi_map(t, 20) == 61u && midi_map(t, 22) == 64u && midi_map(t, 35) == 62u + 24u);
    t->p[P_MPCDEG] = 3;
    bad += check("MPC: DEG 3 moves H02 to the third", midi_map(t, 21) == 66u);
    t->p[P_MPCDEG] = 9;                               /* (past the scale's 7: the 7th) */
    bad += check("MPC: DEG is clamped to the scale's notes", midi_map(t, 21) == 73u && track_desc(t, P_MPCDEG)->max == 7);
    t->p[P_MPCDEG] = 1;
    song.octave = 1;
    bad += check("MPC: the octave buttons shift the pads", midi_map(t, 21) == 74u);
    song.octave = 0;
    set_engine_of(&trk[3], ENGI_DRUM);
    trk[3].p[P_QUANT] = Q_MPC;
    bad += check("MPC: a DRUM track keeps its notes, outside Bank H silent",
                 midi_map(&trk[3], 21) == 21u && midi_map(&trk[3], 36) == KB_SILENT);
    trk[3].p[P_QUANT] = Q_WHITE;
    bad += check("WHITE: a DRUM track keeps its GM notes", midi_map(&trk[3], 37) == 37u && midi_map(&trk[3], 36) == 36u);
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
    for (k = 0; k < NPAGES && PAGES[k].graph != GR_SCALE_PICKER; k++) {}
    ui.page = (uint8_t)k;                             /* SCALES: outside the SCL cycle, opened from its row */
    edit_param(1, 3);                                 /* SCL */
    edit_param(3, 1);                                 /* QNT */
    for (k = 0; k < NPART; k++) ok &= trk[k].p[P_SCALE] == TSEL->p[P_SCALE] && trk[k].p[P_QUANT] == TSEL->p[P_QUANT];
    bad += check("SCL and QNT turned on one track are every part's", ok && TSEL->p[P_SCALE] == 3 && TSEL->p[P_QUANT] == 1);
    edit_param(2, 5);                                 /* ROOT: the track's own */
    bad += check("ROOT stays the track's", TSEL->p[P_ROOT] == 5 && trk[1].p[P_ROOT] == 0);
    return bad;
}

int main(void)
{
    int bad = 0;
    bad += mapping_test();
    bad += mpc_test();
    bad += play_test();
    bad += share_test();
    printf("%s\n", bad ? "MIDI SCALE TEST FAILED" : "MIDI scale layouts test passed");
    return bad != 0;
}
