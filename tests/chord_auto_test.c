/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* AUTO's musical examples and invariants, against the real input, sequencer,
 * recorder, MIDI ownership, project codec and chord display paths. */
#include <assert.h>
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static track_t *setup(void)
{
    ui_power_on();
    track_t *t = &trk[0];
    set_engine_of(t, HOST_SYNTH);
    t->p[P_VOICE] = V_POLY; t->p[P_SCALE] = 1; t->p[P_ROOT] = t->p[P_TRANS] = 0;
    t->p[P_CHRD] = CH_AUTO; t->p[P_QUANT] = Q_OFF; t->p[P_VOIC] = VC_CLOSE;
    events_block(CTL);
    return t;
}
static uint32_t pcs(const uint8_t *out, uint32_t n)
{
    uint32_t mask = 0;
    for (uint32_t i = 0; i < n; i++) mask |= 1u << (out[i] % 12u);
    return mask;
}
static void expect(track_t *t, uint32_t input, const uint8_t *want, uint32_t count, const char *name)
{
    uint8_t out[4]; int32_t root; uint16_t mask; char text[12];
    uint32_t n = chord_make(t, input, out, &root, &mask);
    if (n != count || memcmp(out, want, count)) {
        printf("AUTO %s:", name); for (uint32_t i = 0; i < n; i++) printf(" %u", out[i]); puts("");
        assert(0);
    }
    chord_auto_name(text, (uint32_t)root, mask);
    assert(!strcmp(text, name));
}
static void musical_examples(void)
{
    track_t *t = setup();
    static const uint8_t major[7][3] = {{60,64,67},{62,65,69},{64,67,71},{65,69,72},{67,71,74},{69,72,76},{71,74,77}};
    static const char *names[] = {"C","Dm","Em","F","G","Am","Bdim"};
    for (uint32_t i = 0; i < 7u; i++) expect(t, major[i][0], major[i], 3, names[i]);
    t->p[P_SCALE] = 2; t->p[P_ROOT] = 9;
    expect(t, 69, (const uint8_t[]){69,72,76}, 3, "Am");
    expect(t, 64, (const uint8_t[]){64,67,71}, 3, "Em");
    t->p[P_SCALE] = 7;
    expect(t, 64, (const uint8_t[]){64,68,71}, 3, "E");
    t->p[P_SCALE] = 3; t->p[P_ROOT] = 2;
    expect(t, 67, (const uint8_t[]){67,71,74}, 3, "G");
    t->p[P_ROOT] = 0; t->p[P_SCALE] = 5;
    expect(t, 60, (const uint8_t[]){60,64,67}, 3, "C");
    expect(t, 62, (const uint8_t[]){62,67,69}, 3, "Dsus4");
    expect(t, 69, (const uint8_t[]){69,72,76}, 3, "Am");
    t->p[P_SCALE] = 6;
    expect(t, 60, (const uint8_t[]){60,63,67}, 3, "Cm");
    t->p[P_SCALE] = 13;
    expect(t, 60, (const uint8_t[]){60,64,68}, 3, "Caug");
    t->p[P_SCALE] = 12;
    expect(t, 66, (const uint8_t[]){66,70,75}, 3, "F# set");
    t->p[P_SCALE] = 0;
    expect(t, 61, (const uint8_t[]){61,65,68}, 3, "C#");
    t->p[P_SCALE] = 1; t->p[P_TRANS] = 1; t->p[P_QUANT] = Q_ALL;
    expect(t, kb_map(t, 7), (const uint8_t[]){61,65,68}, 3, "C#");
    t->p[P_TRANS] = 2;
    expect(t, kb_map(t, 7), (const uint8_t[]){62,66,69}, 3, "D");
    puts("AUTO: major/minor/modes, pentatonic, whole tone, blues fallback, chromatic and transpose examples passed");
}
static void every_scale(void)
{
    track_t *t = setup();
    const int trans[] = {-24,-13,0,1,24};
    uint32_t cases = 0;
    for (uint32_t s = 0; s < SCALE_LEGACY; s++)
        for (int root = 0; root < 12; root++)
            for (uint32_t x = 0; x < NELEM(trans); x++)
                for (uint32_t v = 0; v <= VC_BASS; v++) {
                    t->p[P_SCALE] = s; t->p[P_ROOT] = root; t->p[P_TRANS] = trans[x]; t->p[P_VOIC] = v;
                    uint32_t allowed = 0;
                    for (int note = 0; note < 12; note++)
                        if (SCALE_MASK[s] & (1u << note)) allowed |= 1u << ((note + root + trans[x] + 120) % 12);
                    for (uint32_t note = 0; note < 128u; note++) {
                        uint8_t out[4], again[4]; int32_t r, r2; uint16_t mask, mask2;
                        uint32_t n = chord_make(t, note, out, &r, &mask);
                        uint32_t n2 = chord_make(t, note, again, &r2, &mask2);
                        assert(n >= 3 && n <= 4 && n == n2 && r == r2 && mask == mask2 && !memcmp(out, again, n));
                        assert(!(pcs(out, n) & ~allowed) && (pcs(out, n) & (1u << (r % 12))));
                        for (uint32_t j = 0; j < n; j++) {
                            assert(out[j] <= 127);
                            if (j) assert(out[j] > out[j-1]);
                        }
                        cases++;
                    }
                }
    printf("AUTO: %u cases cover all 16 Western scales, roots, transpose, voicings and MIDI endpoints; complete, ordered, deterministic, strictly in scale\n", cases);
}
static void context(void)
{
    track_t *t = setup(); uint8_t out[4], repeated[4]; int32_t r; uint16_t mask;
    uint32_t n = chord_build(t, 60, out);
    chord_auto_t saved = chord_auto[0];
    for (uint32_t i = 0; i < 20; i++) chord_make(t, 65, repeated, &r, &mask);
    assert(!memcmp(&saved, &chord_auto[0], sizeof saved)); /* UI cannot teach its own previews. */
    assert(chord_build(t, 60, repeated) == n && !memcmp(out, repeated, n));
    n = chord_build(t, 65, out);
    assert(n == 3 && !memcmp(out, (uint8_t[]){60,65,69}, 3)); /* C -> F keeps common C in place. */
    chord_auto_forget(0);
    chord_build(t, 62, out); n = chord_build(t, 67, out);
    assert(n == 4 && pcs(out,n) == ((1u<<7)|(1u<<11)|(1u<<2)|(1u<<5))); /* ii -> V7 */
    n = chord_build(t, 60, out);
    assert(n == 3 && pcs(out,n) == ((1u<<0)|(1u<<4)|(1u<<7))); /* -> I */
    chord_auto_tick(2u * beat_samples());
    expect(t, 65, (const uint8_t[]){65,69,72}, 3, "F"); /* Fresh phrase. */
    chord_build(t, 60, out);
    t->p[P_SCALE] = 2;
    expect(t, 60, (const uint8_t[]){60,63,67}, 3, "Cm");
    t->p[P_SCALE] = 1; t->p[P_ROOT] = 2;
    expect(t, 62, (const uint8_t[]){62,66,69}, 3, "D");
    t->p[P_ROOT] = 0; t->p[P_VOIC] = VC_INV1;
    expect(t, 60, (const uint8_t[]){64,67,72}, 3, "C");
    t = setup(); song.ntrk = 2;
    track_t *lead = &trk[1]; set_engine_of(lead, HOST_SYNTH);
    lead->p[P_CHRD] = CH_OFF; lead->p[P_MUTE] = 0;
    input_on(lead, 71, 100);
    expect(t, 60, (const uint8_t[]){60,64,67,71}, 4, "Cmaj7");
    input_off(lead,71);
    expect(t, 60, (const uint8_t[]){60,64,67,71}, 4, "Cmaj7"); /* Recently released melody. */
    chord_auto_tick(2u * beat_samples());
    expect(t, 60, (const uint8_t[]){60,64,67}, 3, "C");
    input_on(lead,65,100);
    expect(t, 60, (const uint8_t[]){60,65,67}, 3, "Csus4");
    track_solo = 1;
    expect(t, 60, (const uint8_t[]){60,64,67}, 3, "C");
    track_solo = 0;
    lead->p[P_MUTE] = 1;
    expect(t, 60, (const uint8_t[]){60,64,67}, 3, "C");
    lead->p[P_MUTE] = 0; lead->p[P_CHRD] = CH_MAJ;
    expect(t, 60, (const uint8_t[]){60,64,67}, 3, "C"); /* Generated chord cannot bias AUTO. */
    input_off(lead,65); chord_auto_forget(1); lead->p[P_CHRD] = CH_OFF;
    for (uint32_t note = 11; note < 128; note += 12) {
        live_held[1][note >> 5] |= 1u << (note & 31u);
        expect(t,60,(const uint8_t[]){60,64,67,71},4,"Cmaj7");
        live_held[1][note >> 5] = 0;
    }
    chord_auto_observe(lead,61);
    expect(t, 60, (const uint8_t[]){60,64,67}, 3, "C"); /* Out-of-scale melody cannot add pitches. */
    lead->p[P_CHRD] = CH_AUTO; lead->p[P_SCALE] = 1; lead->p[P_ROOT] = lead->p[P_TRANS] = 0;
    lead->p[P_VOICE] = V_POLY; lead->p[P_VOIC] = VC_CLOSE;
    chord_build(t,60,out);
    expect(lead,65,(const uint8_t[]){65,69,72},3,"F"); /* Independent harmony histories. */
    chord_auto_clock = 0xFFFFFFF0u; chord_build(t,60,out);
    chord_auto_tick(32); assert(chord_auto[0].n);
    chord_auto_tick(2u * beat_samples()); assert(!chord_auto[0].n);
    puts("AUTO: read-only previews, stable repeats, smooth inversions, ii-V7-I, phrase expiry, settings, melody, isolation and clock wrap passed");
}
static uint32_t auto_gates(const track_t *t)
{
    uint32_t n=0;
    for (uint32_t i=0; i<NVOICE; i++) n += t->v[i].active && t->v[i].gate;
    return n;
}
static void midi(uint32_t status, uint32_t note, uint32_t vel)
{
    midi_enqueue(status >> 4 | status << 8 | note << 16 | vel << 24, 1);
    events_block(CTL);
}
static void integration(void)
{
    track_t *t = setup();
    key_down(7); assert(auto_gates(t)==3);
    chord_auto_tick(8u * beat_samples()); assert(chord_auto[0].n == 3);
    uint8_t started[4]; memcpy(started,kb_chord[7],4);
    uint8_t out[4]; chord_build(t,65,out);
    t->p[P_SCALE]=2; t->p[P_TRANS]=1; t->p[P_CHRD]=CH_MIN7;
    assert(!memcmp(started,kb_chord[7],4)); key_up(7); assert(!auto_gates(t));
    t=setup(); t->p[P_STRUM]=2;
    key_down(7); assert(auto_gates(t)==1); key_up(7);
    seq_advance(beat_samples()); assert(!auto_gates(t) && !kb_strum_pending);
    t=setup(); midi(0x90,60,100); assert(auto_gates(t)==3);
    midi(0xB0,64,127); midi(0x80,60,0); assert(auto_gates(t)==3);
    t->p[P_ROOT]=3; t->p[P_CHRD]=CH_OFF;
    midi(0xB0,64,0); assert(!auto_gates(t));
    t=setup(); t->p[P_STRUM]=2;
    midi(0x90,60,100); assert(auto_gates(t)==1);
    midi(0x80,60,0); seq_advance(beat_samples()); assert(!auto_gates(t) && !midi_strum_pending);
    t=setup(); t->p[P_AMODE]=1;
    key_down(7); assert(t->nheld==3); key_up(7); assert(!t->nheld);
    t=setup(); song.rec=song.playing=1;
    key_down(7); assert(t->step[0].n==3 && !memcmp(t->step[0].note,(uint8_t[]){60,64,67},3));
    key_up(7); assert(!auto_gates(t));
    static project_t before, after; static project_store_t wire;
    project_capture(&before); assert(proj_pack(&wire,&before) && proj_unpack(&after,wire.raw));
    assert(after.t[0].p[P_CHRD]==CH_AUTO && after.t[0].step[0].n==3);
    seq_stop(); assert(!chord_auto[0].n);
    t=setup(); t->p[P_VOICE]=V_MONO;
    key_down(7); assert(auto_gates(t)==1); key_up(7); assert(!auto_gates(t));
    t=setup(); host_preset(t,ENGI_808,0); t->p[P_CHRD]=CH_AUTO;
    uint32_t n=chord_build(t,36,out); assert(n==1 && out[0]==36);
    t=setup(); t->p[P_SCALE]=31; t->p[P_QUANT]=Q_ALL;
    n=chord_build(t,60,out); assert(n==1 && out[0]==60);
    puts("AUTO: frozen keyboard/MIDI ownership, early strum release, sustain, ARP, recording, project roundtrip, STOP, mono, kits and microtonal bypass passed");
}
int main(void)
{
    _Static_assert(CH_POW == 9 && CH_AUTO == 10, "old chord IDs stay unchanged");
    musical_examples(); every_scale(); context(); integration();
    puts("AUTO chord tests passed");
    return 0;
}
