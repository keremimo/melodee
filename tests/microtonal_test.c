/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#include <assert.h>
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static uint32_t scale_id(const char *name)
{
    for (uint32_t i = 0; i < SCALE_TOTAL; i++) if (!strcmp(N_SCALE[i], name)) return i;
    assert(0); return 0;
}
static track_t *setup(const char *name)
{
    ui_power_on();
    memset(live_held, 0, sizeof live_held);
    memset(midi_ch, 0, sizeof midi_ch); memset(midi_sel_on, 0, sizeof midi_sel_on);
    memset(midi_owners, 0, sizeof midi_owners); memset(mchord, 0, sizeof mchord);
    mi_r = mi_w = 0; fm1_in.notes = kb_prev = 0;
    TSEL->p[P_SCALE] = scale_id(name); TSEL->p[P_QUANT] = Q_ALL;
    TSEL->p[P_CHRD] = 0; TSEL->p[P_VOICE] = V_POLY; TSEL->p[P_AMODE] = 0;
    return TSEL;
}
static void midi_in(uint32_t st, uint32_t note)
{
    midi_enqueue(st >> 4 | st << 8 | note << 16 | 100u << 24, 1u);
    events_block(CTL);
}
static void mapping(void)
{
    track_t *t = setup("24EDO");
    for (uint32_t s = SCALE_LEGACY; s < SCALE_MICRO_END; s++) {
        t->p[P_SCALE] = s;
        const micro_scale_t *sc = micro_scale(t);
        assert(scale_count(t) == sc->count);
        for (int32_t d = -40; d < 40; d++) {
            uint32_t note = scale_degree_map(t, d, 0, 1);
            if (note == KB_SILENT) continue;
            assert(note == (uint32_t)(60 + d));
            voice_t v = {0}; voice_scale_pitch(t, &v, note);
            double cents = v.pitch16 * 6.25 + v.scale_fine / 2.367;
            assert(fabs(cents - micro_pitch(t, note) / 100.0) < 0.5);
            assert(micro_pitch(t, (int32_t)note + sc->count) - micro_pitch(t, note) == sc->pitch[sc->count]);
        }
    }
    t->p[P_SCALE] = scale_id("24EDO");
    assert(micro_pitch(t, 61) == 605000 && micro_pitch(t, 84) == 720000);
    assert(micro_pitch(t, 59) == 595000);
    t->p[P_ROOT] = 3; t->p[P_TRANS] = -5;
    assert(micro_pitch(t, 60) == 580000);
    t->p[P_ROOT] = t->p[P_TRANS] = 0;
    t->p[P_QUANT] = Q_WHITE;
    assert(kb_map(t, 7) == 60 && kb_map(t, 8) == KB_SILENT && kb_map(t, 11) == 62);
    t->p[P_QUANT] = Q_SNAP;
    assert(kb_map(t, 11) == 68 && midi_map(t, 64) == 68);
    t->p[P_SCALE] = scale_id("19EDO");                    /* SNAP takes the nearest degree: */
    assert(kb_map(t, 12) == 68 && midi_map(t, 65) == 68); /* F, the 505-cent fourth (not 442) */
    assert(kb_map(t, 10) == 65 && kb_map(t, 17) == 76);   /* Eb 316, Bb 1011 (not 253, 947) */
    assert(kb_map(t, 13) == 69);                          /* F#: the 600-cent tie takes the lower */
    t->p[P_SCALE] = scale_id("5EDO");                     /* past the last degree: the next period's first */
    assert(midi_map(t, 71) == 65 && midi_map(t, 59) == 60);
    t->p[P_SCALE] = scale_id("JI-12");
    assert(midi_map(t, 62) == 62);                        /* 9/8 is 3.9 cents above D: no longer C#'s 16/15 */
    t->p[P_SCALE] = scale_id("24EDO");
    t->p[P_QUANT] = Q_MPC; t->p[P_MPCDEG] = 24;
    assert(midi_map(t, 101) == 83 && midi_map(t, 99) == KB_SILENT);
    t->p[P_QUANT] = Q_OFF;
    voice_t v = {0}; voice_scale_pitch(t, &v, 69);
    assert(v.pitch16 == 69 * 16 && !v.scale_fine);
    t->p[P_SCALE] = scale_id("BP-ET"); t->p[P_QUANT] = Q_SNAP; song.octave = 1;
    assert(kb_map(t, 7) == 73 && micro_pitch(t, 73) == 600000 + micro_scale(t)->pitch[13]);
    song.octave = 0;
    t->eng_req = 10; t->p[P_QUANT] = Q_ALL;
    assert(!micro_active(t) && midi_map(t, 38) == 38);
    puts("microtonal: every catalogue degree, negative periods, ROOT/TRN, layouts, bypass and drums ok");
}
static void lifecycle(void)
{
    track_t *t = setup("53EDO");
    midi_in(0x90, 60); midi_in(0x90, 61);
    uint32_t gated = 0;
    for (uint32_t i = 0; i < NVOICE; i++) gated += t->v[i].active && t->v[i].gate;
    assert(gated == 2); /* pitches < 23 cents apart retain separate note identities */
    t->p[P_SCALE] = 1; t->p[P_QUANT] = Q_WHITE; t->p[P_ROOT] = 5;
    midi_in(0x80, 60); midi_in(0x80, 61);
    for (uint32_t i = 0; i < NVOICE; i++) assert(!t->v[i].gate);
    t = setup("24EDO"); t->p[P_CHRD] = CH_DIA7;
    uint8_t out[4]; int32_t root; uint16_t mask;
    assert(chord_make(t, 60, out, &root, &mask) == 4 && out[0] == 60 && out[1] == 62 && out[3] == 66);
    t->p[P_VOIC] = VC_OPEN;
    assert(chord_make(t, 60, out, &root, &mask) == 4 && out[3] == 86);
    t->p[P_VOIC] = VC_CLOSE; t->p[P_CHRD] = CH_MAJ;
    assert(chord_make(t, 60, out, &root, &mask) == 3 && out[1] == 68 && out[2] == 74);
    t->p[P_CHRD] = 0; t->p[P_AMODE] = 1; t->p[P_AOCT] = 2;
    arp_add(t, 60); arp_tick(t, 1); assert(t->arp_note == 60);
    t->arp_pos = 0xFFFFFFF; arp_tick(t, 1); assert(t->arp_note == 84);
    t = setup("JI-MAJ");
    song.rec = song.playing = 1;
    input_on(t, 62, 100); input_off(t, 62);
    assert(t->step[0].n == 1 && t->step[0].note[0] == 62);
    static project_t before, after; static project_store_t wire;
    project_capture(&before); assert(proj_pack(&wire, &before));
    assert(proj_unpack(&after, wire.raw));
    assert(after.t[0].p[P_SCALE] == (int16_t)scale_id("JI-MAJ") && after.t[0].step[0].note[0] == 62);
    assert(micro_pitch(t, 62) == 600000 + (int32_t)round(120000 * log2(1.25)));
    puts("microtonal: close pitches, MIDI releases after changes, chords, ARP periods and project recordings ok");
}
static double audio_frequency(track_t *t)
{
    int32_t out[CTL], previous = 0;
    double first = 0, last = 0; uint32_t crossings = 0;
    for (uint32_t at = 0; at < FS / 2; at += CTL) {
        track_render(t, out, CTL);
        for (uint32_t k = 0; k < CTL; k++) {
            if (at + k > 4000 && previous < 0 && out[k] >= 0) {
                double x = at + k - (double)out[k] / (out[k] - previous);
                if (!crossings++) first = x;
                last = x;
            }
            previous = out[k];
        }
    }
    return crossings > 1 ? (crossings - 1) * FS / (last - first) : 0;
}
static void frequency(void)
{
    const char *names[] = {"24EDO", "31EDO", "JI-MAJ", "MT-1/4", "RAST-D", "BP-ET"};
    for (uint32_t s = 0; s < NELEM(names); s++) {     /* (FM6's INIT VOICE: one sine) */
        track_t *t = setup(names[s]);
        t->eng_req = t->engine = ENGI_FM6;
        eng_state_reset(); fm6_fn_reset();
        uint8_t patch[FP_SIZE + 1]; fm6_unpack(FM6_INIT, patch); fm6_put_patch(0, patch, 1);
        for (uint32_t j = P_E0; j < P_COUNT; j++) t->p[j] = 0;
        t->p[P_GLIDE] = t->p[P_LD_PIT] = t->p[P_ED_PIT] = 0;
        t->p[P_ATK] = 0; t->p[P_SUS] = 127;
        uint32_t note = s == 2 || s == 4 ? 62 : 61;
        double expected = 440 * pow(2, (micro_pitch(t, note) / 10000.0 - 69) / 12);
        trk_note_on(t, note, 100);
        double measured = audio_frequency(t);
        double error = fabs(1200 * log2(measured / expected));
        printf("microtonal: FM6 %s %.3f Hz, error %.3f cents\n", names[s], measured, error);
        assert(error < 0.6);
    }
}

/* MIDI OUT: the semitone nearest the pitch heard, a release going where its press went (midi_track.c midi_sent) */
static void out_clear(void)
{
    mo_r = mo_w = 0; usb.config = 1; midi_sent_n = 0;
    memset(midi_output, 0, sizeof midi_output);
#if BOARD_SMK37
    fm1_in.key_vel[SMK_KEY_F3] = fm1_in.key_vel[SMK_KEY_F3 + 1u] = 100;
#endif
}
static void out(uint32_t status, uint32_t note, uint32_t value)
{
    assert(mo_r != mo_w);
    uint32_t pkt = midi_out_q[mo_r++ % MQ];
    if (((pkt >> 8) & 255u) != status || ((pkt >> 16) & 127u) != note || pkt >> 24 != value)
        fprintf(stderr, "MIDI OUT %02x %u %u, expected %02x %u %u\n", (pkt >> 8) & 255u, (pkt >> 16) & 127u, pkt >> 24, status, note, value);
    assert(((pkt >> 8) & 255u) == status && ((pkt >> 16) & 127u) == note && pkt >> 24 == value && (pkt & 15u) == status >> 4);
}
static void no_out(void) { assert(mo_r == mo_w); }
static void key_press(track_t *t, uint32_t k, uint32_t note) { kb_note[k] = (uint8_t)note; kb_trk[k] = (uint8_t)trk_index(t); key_on(k, t); }
static track_t *midi_track_on(const char *name)
{
    track_t *t = setup(name);
    track_solo = 0;
    set_engine_of(t, ENGI_MIDI);
    events_block(0);                                  /* (the sound change's panic) */
    t->p[P_SCALE] = scale_id(name); t->p[P_QUANT] = Q_ALL;
    out_clear();
    return t;
}
static int gate_on(const track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < NVOICE; i++) if (t->v[i].active && t->v[i].gate && t->v[i].note == note) return 1;
    return 0;
}
static void out_mapping(void)
{
    static const uint8_t ADDRESS[] = {59, 60, 61, 62, 63, 64, 72, 84}, NEAREST[] = {60, 60, 61, 61, 62, 62, 66, 72};
    static const uint8_t EDO5[] = {60, 62, 65, 67, 70, 72};
    track_t *t = setup("24EDO");
    for (uint32_t i = 0; i < NELEM(ADDRESS); i++) assert(midi_sent_pitch(t, ADDRESS[i]) == NEAREST[i]);
    t->p[P_ROOT] = 2; assert(midi_sent_pitch(t, 60) == 62);
    t->p[P_TRANS] = -5; assert(midi_sent_pitch(t, 60) == 57);
    t->p[P_ROOT] = t->p[P_TRANS] = 0;
    t->p[P_SCALE] = scale_id("5EDO");
    for (uint32_t i = 0; i < NELEM(EDO5); i++) assert(midi_sent_pitch(t, 60 + i) == EDO5[i]);
    for (uint32_t s = SCALE_LEGACY; s < SCALE_MICRO_END; s++) {  /* (the retired entries too) */
        t->p[P_SCALE] = s;
        for (uint32_t note = 0; note < 128u; note++) {
            uint32_t sent = midi_sent_pitch(t, note);
            int32_t off = scale_pitch(t, (int32_t)note) - (int32_t)sent * 10000;
            assert(sent < 128u && off >= -5000 && off < 5000);   /* the nearest; a tie goes up, as on the iPad */
        }
    }
    puts("microtonal MIDI OUT: nearest semitone for every scale and address, ROOT and TRN, ties, the range ok");
}
static void out_release(void)
{
    track_t *t = midi_track_on("24EDO");
    trk_note_on(t, 84, 90); out(0x90, 72, 90); no_out();
    t->p[P_ROOT] = 5; trk_note_off(t, 84); out(0x80, 72, 0); no_out();
    t->p[P_ROOT] = 0;
    trk_note_on(t, 64, 90); out(0x90, 62, 90);
    t->p[P_SCALE] = scale_id("5EDO"); trk_note_off(t, 64); out(0x80, 62, 0);
    t->p[P_SCALE] = scale_id("24EDO");
    trk_note_on(t, 64, 90); out(0x90, 62, 90);
    t->p[P_QUANT] = Q_OFF; trk_note_off(t, 64); out(0x80, 62, 0);
    t->p[P_QUANT] = Q_ALL;
    trk_note_on(t, 64, 90); out(0x90, 62, 90);
    t->p[P_SCALE] = 1; trk_note_off(t, 64); out(0x80, 62, 0);
    assert(!midi_sent_n);
    trk_note_on(t, 64, 90); out(0x90, 64, 90); assert(!midi_sent_n);    /* twelve-tone: itself, nothing kept */
    t->p[P_SCALE] = scale_id("24EDO");
    trk_note_off(t, 64); out(0x80, 64, 0); no_out();
    trk_note_on(t, 72, 90); trk_note_on(t, 84, 80);   /* an address that is another press's semitone */
    out(0x90, 66, 90); out(0x90, 72, 80);
    trk_note_off(t, 72); out(0x80, 66, 0); no_out();
    trk_note_off(t, 72); no_out();                    /* (a stray release: 84 owns semitone 72) */
    trk_note_off(t, 84); out(0x80, 72, 0); no_out();
    trk_note_on(t, 64, 90); trk_note_on(t, 64, 80);   /* the same degree again: finishes the first strike */
    out(0x90, 62, 90); out(0x80, 62, 0); out(0x90, 62, 80);
    trk_note_off(t, 64); out(0x80, 62, 0); no_out(); assert(!midi_sent_n);
    t->p[P_QUANT] = Q_OFF;                            /* bypassed, tuned and twelve-tone notes: themselves */
    trk_note_on(t, 64, 90); out(0x90, 64, 90); trk_note_off(t, 64); out(0x80, 64, 0);
    t->p[P_QUANT] = Q_ALL; t->p[P_SCALE] = 1; t->p[P_TUNING] = 1; assert(tune_active(t));
    trk_note_on(t, 64, 90); out(0x90, 64, 90); trk_note_off(t, 64); out(0x80, 64, 0);
    t->p[P_TUNING] = 0; assert(!midi_sent_n);

    t = setup("24EDO"); out_clear();                  /* the keys of an instrument track */
    uint32_t mc = trk_index(t);
    key_press(t, 0, 84); out(0x90 | mc, 72, 100); no_out();
    assert(gate_on(t, 84));                           /* the voice keeps its degree and its pitch */
    for (uint32_t i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate) assert(t->v[i].pitch16 == micro_pitch(t, 84) * 16 / 10000);
    t->p[P_ROOT] = 5; t->p[P_QUANT] = Q_OFF;
    trk_note_off(t, 84); no_out(); assert(midi_sent_n == 1);   /* (the sequencer's release: not the key's) */
    key_off(0, t); out(0x80 | mc, 72, 0); no_out(); assert(!midi_sent_n);
    t->p[P_ROOT] = 0; t->p[P_QUANT] = Q_ALL;

    t = midi_track_on("24EDO");                       /* what is stored stays the degree */
    song.rec = song.playing = 1;
    input_on(t, 62, 100); out(0x90, 61, 100);
    input_off(t, 62); out(0x80, 61, 0); no_out();
    assert(t->step[0].n == 1 && t->step[0].note[0] == 62);
    song.rec = song.playing = 0;
    puts("microtonal MIDI OUT: releases after ROOT, SCL and QNT changes, keys and MIDI Track, bypasses, stored degrees ok");
}
static void out_shared(void)
{
    track_t *t = midi_track_on("24EDO");
    trk_note_on(t, 61, 90); out(0x90, 61, 90);        /* 60.5 and 61.0: one semitone */
    trk_note_on(t, 62, 70); out(0x80, 61, 0); out(0x90, 61, 70);   /* struck again */
    trk_note_off(t, 61); no_out();                    /* 62 still holds it */
    trk_note_off(t, 62); out(0x80, 61, 0); no_out();
    trk_note_on(t, 61, 90); trk_note_on(t, 62, 90); mo_r = mo_w;
    trk_note_off(t, 62); no_out();                    /* either order, whatever changed */
    t->p[P_ROOT] = 3; trk_note_off(t, 61); out(0x80, 61, 0); no_out();
    t->p[P_ROOT] = 0;
    trk_note_on(t, 62, 90); trk_note_on(t, 63, 90);   /* a neighbour is no sharer */
    out(0x90, 61, 90); out(0x90, 62, 90);
    trk_note_off(t, 62); out(0x80, 61, 0); no_out();
    trk_note_off(t, 63); out(0x80, 62, 0); no_out();
    t->p[P_SCALE] = scale_id("53EDO");
    assert(midi_sent_pitch(t, 60) == 60 && midi_sent_pitch(t, 61) == 60 && midi_sent_pitch(t, 62) == 60);
    trk_note_on(t, 60, 90); out(0x90, 60, 90);
    trk_note_on(t, 61, 80); out(0x80, 60, 0); out(0x90, 60, 80);
    trk_note_on(t, 62, 70); out(0x80, 60, 0); out(0x90, 60, 70);
    trk_note_off(t, 61); no_out(); trk_note_off(t, 60); no_out();
    trk_note_off(t, 62); out(0x80, 60, 0); no_out(); assert(!midi_sent_n);

    t = setup("24EDO"); out_clear();                  /* the keys */
    uint32_t mc = trk_index(t);
    key_press(t, 0, 61); out(0x90 | mc, 61, 100);
    key_press(t, 1, 62); out(0x80 | mc, 61, 0); out(0x90 | mc, 61, 100);
    assert(gate_on(t, 61) && gate_on(t, 62));         /* inside: two voices, as ever */
    key_off(0, t); no_out(); assert(!gate_on(t, 61) && gate_on(t, 62));
    t->p[P_SCALE] = 1; key_off(1, t); out(0x80 | mc, 61, 0); no_out();
    t->p[P_SCALE] = scale_id("24EDO");
    key_midi_on(t, 64, 90); key_midi_on(t, 64, 80);   /* one degree from two keys: two releases */
    out(0x90 | mc, 62, 90); out(0x80 | mc, 62, 0); out(0x90 | mc, 62, 80);
    key_midi_off(t, 64); no_out(); key_midi_off(t, 64); out(0x80 | mc, 62, 0); no_out();
    assert(!midi_sent_n);
    puts("microtonal MIDI OUT: a shared semitone struck again, ending with its last degree, on both paths ok");
}
static void out_cleanup(void)
{
    track_t *t = midi_track_on("24EDO");
    trk_note_on(t, 84, 90); trk_note_on(t, 64, 90); mo_r = mo_w;
    t->p[P_MUTE] = 1; midi_track_service(t);          /* a mute ends the semitones the presses sent */
    out(0x80, 62, 0); out(0x80, 72, 0); no_out(); assert(!midi_sent_n);
    trk_note_off(t, 84); no_out();                    /* (their releases come later: nothing left to do) */
    t->p[P_MUTE] = 0;
    trk_note_on(t, 84, 90); out(0x90, 72, 90);
    t->p[P_E0] = 5; midi_track_service(t); out(0x80, 72, 0); no_out();   /* a channel change: on the old channel */
    trk_note_on(t, 84, 90); out(0x94, 72, 90); trk_note_off(t, 84); out(0x84, 72, 0);
    t->p[P_E0] = 1;
    usb.config = 0; trk_note_on(t, 84, 90); no_out(); assert(!midi_sent_n);   /* a refused press keeps nothing */
    usb.config = 1; trk_note_on(t, 84, 90); out(0x90, 72, 90);
    usb.config = 0; t->p[P_ROOT] = 3; trk_note_off(t, 84); no_out();
    usb.config = 1; midi_track_service(t); out(0x80, 72, 0); no_out();   /* a waiting release: the same semitone */
    t->p[P_ROOT] = 0;
    for (uint32_t i = 0; i < MIDI_SENT_N; i++) { trk_note_on(t, 30 + i, 90); mo_r = mo_w; }
    assert(midi_sent_n == MIDI_SENT_N);
    trk_note_on(t, 100, 90); no_out(); assert(midi_sent_n == MIDI_SENT_N);   /* full: silent, nothing to end */
    trk_note_off(t, 100); no_out();
    for (uint32_t i = 0; i < MIDI_SENT_N; i++) { trk_note_off(t, 30 + i); mo_r = mo_w; }
    assert(!midi_sent_n);
    for (uint32_t i = 0; i < 4u; i++) assert(!midi_output[0].held[i] && !midi_output[0].off[i]);
    trk_note_on(t, 100, 90); out(0x90, 80, 90); trk_note_off(t, 100); out(0x80, 80, 0); no_out();
    puts("microtonal MIDI OUT: mute, channel change, stalled USB, a full table ok");
}
#if BOARD_SMK37
static void ext_key(uint32_t k, uint32_t v)
{
    host_extq[host_extq_w++ % 8u] = (uint16_t)(k << 8 | v);
    ext_keys_block();
}
static void out_outer_keys(void)
{
    track_t *t = setup("24EDO"); out_clear();         /* key 0, 12 degrees below C4: address 48, pitch 54 */
    uint32_t mc = trk_index(t);
    ext_key(0, 90); out(0x90 | mc, 54, 90); no_out();
    assert(gate_on(t, 48));
    t->p[P_ROOT] = 7; ext_key(0, 0); out(0x80 | mc, 54, 0); no_out();
    t->p[P_ROOT] = 0; assert(!midi_sent_n);
    t = midi_track_on("24EDO");                       /* a MIDI Track's go out with input_on, once */
    ext_key(0, 90); out(0x90, 54, 90); no_out();
    ext_key(0, 0); out(0x80, 54, 0); no_out();
    t->p[P_SCALE] = 1; t->p[P_QUANT] = Q_OFF;
    ext_key(0, 90); out(0x90, 48, 90); no_out();
    ext_key(0, 0); out(0x80, 48, 0); no_out();
    puts("microtonal MIDI OUT: the SMK-37's outer keys, one note each on a MIDI Track ok");
}
#endif
int main(void)
{
#if !BOARD_SMK37
    mapping(); lifecycle(); frequency();
#endif
    out_mapping(); out_release(); out_shared(); out_cleanup();
#if BOARD_SMK37
    out_outer_keys();
#endif
    puts("microtonal tests passed");
    return 0;
}
