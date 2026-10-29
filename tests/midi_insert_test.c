/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Exercise note inserts through real engines, UI and persistence. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>

static int sounding(uint32_t tr, uint32_t note)
{
    for (uint32_t v = 0; v < NVOICE; v++) if (trk[tr].v[v].gate && trk[tr].v[v].note == note) return 1;
    return 0;
}
static uint32_t velocity(uint32_t tr, uint32_t note)
{
    for (uint32_t v = 0; v < NVOICE; v++) if (trk[tr].v[v].gate && trk[tr].v[v].note == note) return trk[tr].v[v].vel;
    return 0;
}
static void advance(uint32_t n)
{
    while (n) { uint32_t take = n > CTL ? CTL : n; midi_insert_tick(take, FS / 2u); n -= take; }
}
static void reset(void)
{
    ui_power_on();
    for (uint32_t tr = 0; tr < NPART; tr++) {
        set_engine_of(&trk[tr], ENGI_FM6); engine_block(&trk[tr]);
        trk[tr].p[P_VOICE] = V_POLY; trk[tr].p[P_CHRD] = 0;
    }
    mo_r = mo_w = 0;
}
static void delay(track_t *t, uint32_t slot, uint32_t time, uint32_t decay)
{
    int16_t *p = t->p + P_FXTYPE + slot * FX_PARAMS;
    p[0] = FX_MIDIDELAY; p[1] = 127; p[2] = (int16_t)time; p[3] = (int16_t)decay;
}
static void timing(void)
{
    reset(); delay(TSEL, 0, 1, 32); /* eighth note; lose roughly 25% each hit */
    int16_t before[P_COUNT]; memcpy(before, TSEL->p, sizeof before);
    trk_note_on(TSEL, 60, 100); trk_note_off(TSEL, 60);
    trk_note_on(&trk[1], 64, 100); trk_note_off(&trk[1], 64);
    advance(FS / 4u - 1u); assert(!gates()); advance(1);
    assert(sounding(0, 60) && velocity(0, 60) == 74 && !sounding(1, 64));
    advance(FS / 8u); assert(!gates()); advance(FS / 4u - FS / 8u);
    assert(sounding(0, 60) && velocity(0, 60) == 55);
    assert(!memcmp(before, TSEL->p, sizeof before) && !mo_w);
    seq_stop(); advance(FS); assert(!gates() && !midi_insert.pending);
    /* The 11025-sample eighth does not divide a 32-sample control block.
     * Servicing it late must not accumulate lateness in later deadlines. */
    reset(); delay(TSEL, 0, 1, 32); trk_note_on(TSEL, 60, 100); trk_note_off(TSEL, 60);
    advance(FS / 4u + CTL - 1u);
    uint32_t next_due = 0;
    for (uint32_t k = 0; k < MD_QUEUE; k++) if (midi_insert.q[k].track == 0 && midi_insert.q[k].vel) next_due = midi_insert.q[k].due;
    assert(next_due == 2u * (FS / 4u)); seq_stop();
    /* Every division uses sample time and continues with external clock stopped. */
    for (uint32_t d = 0; d < NELEM(N_DIV); d++) {
        reset(); delay(TSEL, 2, d, 64); song.g[G_CLOCK] = 1;
        trk_note_on(TSEL, 67, 100); trk_note_off(TSEL, 67);
        uint32_t gap = division_samples(FS / 2u, d);
        advance(gap - 1u); assert(!gates()); advance(1); assert(sounding(0, 67));
        trk_all_off(TSEL); assert(!midi_insert.pending);
    }
    puts("MIDI inserts: timed repeats, velocity decay, track isolation, all divisions and stopped clock passed");
}
static void ownership(void)
{
    reset(); delay(TSEL, 0, 1, 32);
    trk_note_on(TSEL, 60, 100); advance(FS / 4u); assert(sounding(0, 60));
    advance(FS / 8u); assert(sounding(0, 60)); /* echo's release cannot cut a held source */
    trk_note_off(TSEL, 60); assert(!sounding(0, 60));
    advance(FS / 4u - FS / 8u); assert(sounding(0, 60));
    trk_note_on(TSEL, 60, 90); advance(FS / 8u); assert(sounding(0, 60));
    trk_note_off(TSEL, 60); seq_stop(); advance(FS); assert(!gates());
    /* Editing/removing an insert cancels scheduled repeats and sounding echoes. */
    trk_note_on(TSEL, 65, 100); trk_note_off(TSEL, 65); advance(FS / 4u);
    TSEL->p[P_FXTYPE] = FX_OFF; advance(1); assert(!gates() && !midi_insert.pending);
    delay(TSEL, 0, 1, 0);
    for (uint32_t n = 0; n < 128u; n++) { trk_note_on(TSEL, n, 127); trk_note_off(TSEL, n); }
    assert(midi_insert.pending <= MD_QUEUE); advance(FS * 20u);
    assert(!gates() && !midi_insert.pending);
    /* Sound changes and panic must cancel pending onsets. */
    trk_note_on(TSEL, 60, 100); trk_note_off(TSEL, 60); trk_all_off(TSEL);
    advance(FS); assert(!gates() && !midi_insert.pending);
    puts("MIDI inserts: collision ownership, retriggers, removal, finite repeats, overflow and panic passed");
}
static uint64_t verb_tail(uint32_t engine, uint32_t release)
{
    reset(); set_engine_of(TSEL, engine); engine_block(TSEL);
    TSEL->p[P_VOICE] = V_POLY; TSEL->p[P_SUS] = 127;
    TSEL->p[P_FXTYPE] = FX_MIDIVERB; TSEL->p[P_FXMIX] = (int16_t)release;
    midi_insert_tick(0, FS / 2u);
    trk_note_on(TSEL, 60, 110);
    int32_t b[CTL];
    for (uint32_t k = 0; k < FS / CTL / 2u; k++) { memset(b, 0, sizeof b); track_render(TSEL, b, CTL); }
    trk_note_off(TSEL, 60);
    uint64_t energy = 0;
    for (uint32_t k = 0; k < FS / CTL; k++) {
        memset(b, 0, sizeof b); track_render(TSEL, b, CTL);
        if (k > FS / CTL / 5u) for (uint32_t n = 0; n < CTL; n++) energy += (uint64_t)abs(b[n]);
    }
    return energy;
}
static void verb(void)
{
    const uint32_t engines[] = {ENGI_PROPHET, ENGI_FM6, ENGI_CZ, 3u};
    for (uint32_t k = 0; k < NELEM(engines); k++) {
        uint64_t short_tail = verb_tail(engines[k], 10), long_tail = verb_tail(engines[k], 100);
        printf("MIDIVerb %s tail: %llu -> %llu\n", ENGINES[engines[k]]->name, (unsigned long long)short_tail, (unsigned long long)long_tail);
        assert(long_tail > short_tail && long_tail > 1000u);
    }
    reset(); TSEL->p[P_FXTYPE] = FX_MIDIVERB; TSEL->p[P_FXMIX] = 100;
    uint8_t native[FM6_PACKED]; fm6_pack(fm6_ed(TSEL), native);
    midi_insert_tick(0, FS / 2u); assert(midi_verb[0] == 100 && !midi_verb[1]);
    uint8_t after[FM6_PACKED]; fm6_pack(fm6_ed(TSEL), after); assert(!memcmp(native, after, sizeof native));
    TSEL->p[P_FXMIX] = 0; midi_insert_tick(0, FS / 2u); assert(!midi_verb[0]);
    puts("MIDIVerb: audible native-engine release, track isolation, bypass and unchanged instrument patch passed");
}
/* Original per-tick release calculation is the oracle. Changing the amount,
 * bypassing it, editing R4 and re-keying must invalidate the cached rate. */
static void fm6_release_rates(void)
{
    reset(); trk_note_on(TSEL, 60, 100);
    voice_t *v = 0;
    for (uint32_t k = 0; k < NVOICE; k++) if (TSEL->v[k].gate) v = &TSEL->v[k];
    assert(v);
    fm6_voice_t *s = fm6_state(TSEL, v);
    static const vmod_t m = {.shape = 64 << 8};
    for (uint32_t tick = 0; tick < 3000; tick++) {
        midi_verb[0] = (uint8_t)((tick / 37) % 4 == 0 ? 0 : (tick / 37) % 4 == 1 ? 86 : (tick / 37) % 4 == 2 ? 127 : 10);
        if (tick % 101 == 0) {
            fm6_patch_of(0)[FP_R1+3] = (uint8_t)(tick % 100);
            fm6_patch_of(0)[FP_RS] = (uint8_t)(tick % 8);
            fm6_pgen[0]++; fm6_sync(TSEL, 0);
        }
        if (tick % 200 == 0) { v->gate = 1; fm6_note_on(TSEL, v); }
        if (tick % 200 == 20) v->gate = 0;
        const uint8_t *ed = fm6_ed(TSEL);
        fm6_eg_t expected[6]; memcpy(expected, s->eg, sizeof expected);
        int down = s->down;
        if (!v->gate && down) {
            down = 0;
            for (uint32_t k = 0; k < 6; k++)
                fm6_eg_go(&expected[k], ed+k*FP_OP, s->ol[k], s->rs[k], 3);
        }
        if (!down && (midi_verb[0] || s->midi_release))
            for (uint32_t k = 0; k < 6; k++) if (expected[k].ix == 3) {
                int32_t rate = midi_verb[0] ? 99-midi_verb[0]*99/127 : ed[k*FP_OP+FP_R1+3];
                int32_t q = clamp(((rate*41)>>6)+s->rs[k], 0, 63);
                expected[k].inc = (4+(q&3)) << (2+FM6_LG_N+(q>>2));
            }
        for (uint32_t k = 0; k < 6; k++)
            (fm6_eg_step)(&expected[k], ed+k*FP_OP, s->ol[k], s->rs[k], down);
        fm6_control(TSEL, v, s, &m);
        assert(!memcmp(expected, s->eg, sizeof expected));
    }
    midi_verb[0] = 0;
    puts("FM6 MIDI release: 3000 changing, bypassed, edited and retriggered blocks match uncached envelopes");
}
static void original_recording(void)
{
    reset(); delay(TSEL, 0, 1, 32);
    TSEL->p[P_FX2TYPE] = FX_MIDIVERB; TSEL->p[P_FX2MIX] = 90;
    song.rec = song.rec_latch = 1; seq_start(); seq_advance(0);
    input_on(TSEL, 60, 73); seq_advance(1200); input_off(TSEL, 60);
    advance(FS / 4u); assert(sounding(0, 60));
    seq_stop();
    assert(recorded_note(&recording[0]) == 60 && recording[0].vel == 73 && recording[0].duration && !recording[1].vel);
    puts("MIDI inserts: recording retains source pitch, velocity and duration without capturing echoes");
}
static void ui_storage(void)
{
    reset(); go_title("FX"); fx_rack_row[0] = 0;
    edit_param(0, 1); assert(TSEL->p[P_FXTYPE] == FX_MIDIVERB && TSEL->p[P_FXMIX] == 64);
    int16_t *vp;
    const page_t *slot = &PAGES[page_titled("FX 1")];
    assert(str_eq(page_desc(slot, 0, &vp)->label, "REL"));
    assert(!page_desc(slot, 1, &vp) && !page_desc(slot, 2, &vp) && !page_desc(slot, 3, &vp));
    assert(!page_visible(page_titled("FX 1")));
    press(B_OCTUP); assert(cur_page()->graph == GR_FXRACK);  /* NOTE TAIL edits REL in its row */
    edit_param(3, 1); assert(TSEL->p[P_FXMIX] > 64);
    edit_param(0, 1); assert(TSEL->p[P_FXTYPE] == FX_MIDIDELAY && TSEL->p[P_FXRATE] == 1);
    assert(page_visible(page_titled("FX 1")));
    assert(str_eq(page_desc(slot, 0, &vp)->label, "LEVEL"));
    assert(page_desc(slot, 1, &vp)->fmt == F_ENUM && page_desc(slot, 1, &vp)->max == 9);
    assert(str_eq(page_desc(slot, 2, &vp)->label, "DECAY") && !page_desc(slot, 3, &vp));
    assert(GP[G_MFXTYPE].max == FX_PEDAL_CHORUS && GP[G_MFX2TYPE].max == FX_PEDAL_CHORUS && GP[G_MFX3TYPE].max == FX_PEDAL_CHORUS);
    delay(TSEL, 1, 4, 31); TSEL->p[P_FX3TYPE] = FX_MIDIVERB; TSEL->p[P_FX3MIX] = 90;
    project_t q; project_store_t p; project_capture(&q); assert(proj_pack(&p, &q) && proj_import(&proj_scratch, &p, sizeof p));
    for (uint32_t k = P_FXTYPE; k <= P_FX3CHAR; k++) assert(proj_scratch.t[0].p[k] == TSEL->p[k]);
    template_save(); memset(TSEL->p + P_FXTYPE, 0, FX_SLOTS * FX_PARAMS * sizeof(int16_t)); template_load();
    assert(TSEL->p[P_FXTYPE] == FX_MIDIDELAY && TSEL->p[P_FX2TYPE] == FX_MIDIDELAY && TSEL->p[P_FX3TYPE] == FX_MIDIVERB);
    assert(FX_STATE_COUNT==FX_SLOTS); /* note processors have no audio insert state */
    for (uint32_t k = 0; k < FX_SLOTS; k++) assert(!resource[RES_FX0+k].size);
    puts("MIDI inserts: rack controls, shared exclusion, project/template round trips and no DSP allocation passed");
}
int main(void) { timing(); ownership(); verb(); fm6_release_rates(); original_recording(); ui_storage(); printf("MIDI insert state: %zu bytes\n", sizeof midi_insert + sizeof midi_verb); return 0; }
