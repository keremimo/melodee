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
    for (uint32_t s = SCALE_LEGACY; s < SCALE_TOTAL; s++) {
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
    const char *names[] = {"24EDO", "31EDO", "JI-MAJ", "MT-1/4", "RAST", "BP-ET"};
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
int main(void)
{
    mapping(); lifecycle(); frequency();
    puts("microtonal tests passed");
    return 0;
}
