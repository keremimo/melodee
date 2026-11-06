/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* CPU0 owns outgoing notes, separately from audio voices. Failed note-ons are
 * silent; failed releases remain pending until USB can accept them. A channel
 * change finishes the old channel before admitting notes on the new one.
 * SMK-37: the TRS jack keeps its own books. It waits for neither USB nor BLE,
 * and their retries never reach it: a note it admitted ends there exactly once,
 * while a release still owed to USB or BLE stays pending here. */
static int midi_track_emit(uint32_t packet); /* usb.c: bounded, reports admission */
static struct {
    uint32_t held[4], off[4];
    uint8_t channel, pedal, pedal_off;
} midi_output[NTRK];

/* A microtonal note's number is a degree address (microtonal.h): only Melodee knows its pitch, so what leaves is the
 * semitone nearest that pitch, fixed at the press. It stays here until the release, so ROOT, SCL or QNT changing
 * meanwhile cannot strand it. Each press has an entry, so degrees sharing a semitone sound until the last lets go, and
 * the same degree pressed twice takes two releases. Owner: the track for its MIDI Track notes; the track + NTRK for an
 * instrument track's keys, because trk_note_off comes through midi_track_note_off on every track; the track + 2 NTRK
 * for what the SMK-37's TRS jack admitted, as it keeps its own books. Twelve-tone notes leave as themselves: no entry,
 * and an empty table is all a release pays. Full: the note-on stays silent. */
#define MIDI_SENT_N 32u
static struct { uint8_t owner, note, sent; } midi_sent[MIDI_SENT_N];
static uint8_t midi_sent_n;

static uint32_t midi_sent_pitch(const track_t *t, uint32_t note)
{ return ((uint32_t)scale_pitch(t, (int32_t)note) + 5000u) / 10000u; }

static void midi_sent_add(uint32_t owner, uint32_t note, uint32_t sent)
{
    midi_sent[midi_sent_n].owner = (uint8_t)owner;
    midi_sent[midi_sent_n].note = (uint8_t)note;
    midi_sent[midi_sent_n++].sent = (uint8_t)sent;
}

static int midi_sent_shared(uint32_t owner, uint32_t sent)
{
    for (uint32_t i = 0; i < midi_sent_n; i++)
        if (midi_sent[i].owner == owner && midi_sent[i].sent == sent) return 1;
    return 0;
}

/* A release: the note to end. -1: other presses still hold it. */
static int32_t midi_sent_release(uint32_t owner, uint32_t note)
{
    int32_t sent = (int32_t)note;
    if (!midi_sent_n) return sent;
    for (uint32_t i = 0; i < midi_sent_n; i++)
        if (midi_sent[i].owner == owner && midi_sent[i].note == note) {
            sent = midi_sent[i].sent;
            midi_sent[i] = midi_sent[--midi_sent_n];
            break;
        }
    return midi_sent_shared(owner, (uint32_t)sent) ? -1 : sent;
}

static void midi_sent_forget(uint32_t owner)
{
    for (uint32_t i = midi_sent_n; i--; )
        if (midi_sent[i].owner == owner) midi_sent[i] = midi_sent[--midi_sent_n];
}

static uint32_t midi_track_channel(const track_t *t)
{ return (uint32_t)clamp(t->p[P_E0], 1, 16) - 1u; }

static int midi_track_blocked(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk);
    return tr >= trk_n() || t->p[P_MUTE] || (track_solo && !(track_solo & (1u << tr)));
}

#if BOARD_SMK37
static struct {
    uint32_t held[4];
    uint8_t channel, pedal;
} midi_trs[NTRK] SMO_SECTION;
#define MIDI_TRS_OWNER(tr) ((tr) + 2u * NTRK)

static void midi_trs_all_off(uint32_t tr)
{
    uint32_t ch = midi_trs[tr].channel;
    midi_sent_forget(MIDI_TRS_OWNER(tr));
    if (midi_trs[tr].pedal) {
        midi_trs[tr].pedal = 0;
        smo_end(0x0Bu | (0xB0u | ch) << 8 | 64u << 16);
    }
    for (uint32_t word = 0; word < 4u; word++) {
        uint32_t bits = midi_trs[tr].held[word];
        midi_trs[tr].held[word] = 0;
        for (; bits; bits &= bits - 1u)
            smo_end(0x08u | (0x80u | ch) << 8 | (word * 32u + (uint32_t)__builtin_ctz(bits)) << 16);
    }
}

static void midi_trs_service(const track_t *t, uint32_t tr)
{
    if (!midi_track_only(t) || midi_track_blocked(t) || midi_trs[tr].channel != midi_track_channel(t))
        midi_trs_all_off(tr);
}

static void midi_trs_note_off(uint32_t tr, uint32_t note)
{
    int32_t sent = midi_sent_release(MIDI_TRS_OWNER(tr), note);
    if (sent < 0) return;
    uint32_t word = (uint32_t)sent >> 5, mask = 1u << (sent & 31);
    if (!(midi_trs[tr].held[word] & mask)) return;
    midi_trs[tr].held[word] &= ~mask;
    smo_end(0x08u | (0x80u | midi_trs[tr].channel) << 8 | (uint32_t)sent << 16);
}

static void midi_trs_note_on(const track_t *t, uint32_t tr, uint32_t note, uint32_t vel)
{
    uint32_t ch = midi_track_channel(t), sent = note;
    int micro = micro_active(t);
    if (micro) {                                        /* the semitone USB and BLE get, whether or not they take it */
        if (midi_sent_n == MIDI_SENT_N) return;
        sent = midi_sent_pitch(t, note);
        if (midi_trs[tr].held[sent >> 5] & (1u << (sent & 31u))) {   /* another degree's semitone: struck again */
            midi_trs[tr].held[sent >> 5] &= ~(1u << (sent & 31u));
            smo_end(0x08u | (0x80u | midi_trs[tr].channel) << 8 | sent << 16);
        }
    }
    if (!smo_start(0x09u | (0x90u | ch) << 8 | sent << 16 | (vel & 127u) << 24)) return;
    midi_trs[tr].channel = (uint8_t)ch;
    midi_trs[tr].held[sent >> 5] |= 1u << (sent & 31u);
    if (micro) midi_sent_add(MIDI_TRS_OWNER(tr), note, sent);
}

static void midi_trs_controller(const track_t *t, uint32_t tr, uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t ch = midi_track_channel(t), pkt = (st >> 4) | (st | ch) << 8 | d1 << 16 | d2 << 24;
    uint32_t down = st == 0xB0u && d1 == 64u && d2 >= 64u;
    uint32_t lift = st == 0xB0u && ((d1 == 64u && d2 < 64u) || d1 == 121u);
    midi_trs[tr].channel = (uint8_t)ch;
    if (lift && midi_trs[tr].pedal) {
        midi_trs[tr].pedal = 0;
        smo_end(pkt);
    } else if (down && !midi_trs[tr].pedal) {
        midi_trs[tr].pedal = (uint8_t)smo_start(pkt);
    } else {
        smo_put(pkt);
    }
}

/* TRS MIDI OUT set (main.c smo_sync). Going OFF, what the jack sounds ends first: its releases would find the port shut
 * and the note hang on the synth. A full ring waits for the next pass, still ON. USB and BLE go on as they were. */
static void midi_trs_set(uint32_t on)
{
    if (!on) {
        for (uint32_t tr = 0; tr < NTRK; tr++) midi_trs_all_off(tr);
        if (!smo_stop()) return;
    }
    smo_on(on);
}
#else
#define midi_trs_service(t, tr) ((void)0)
#define midi_trs_note_off(tr, note) ((void)0)
#define midi_trs_note_on(t, tr, note, vel) ((void)0)
#define midi_trs_controller(t, tr, st, d1, d2) ((void)0)
#endif

static int midi_track_pending(uint32_t tr)
{
    return midi_output[tr].pedal_off || midi_output[tr].off[0] || midi_output[tr].off[1] ||
           midi_output[tr].off[2] || midi_output[tr].off[3];
}

static void midi_track_flush(uint32_t tr)
{
    if (!midi_track_pending(tr)) return;
    uint32_t ch = midi_output[tr].channel;
    if (midi_output[tr].pedal_off) {
        if (!midi_track_emit(0x0Bu | (0xB0u | ch) << 8 | 64u << 16)) return;
        midi_output[tr].pedal = midi_output[tr].pedal_off = 0;
    }
    for (uint32_t word = 0; word < 4u; word++) {
        uint32_t bits = midi_output[tr].off[word];
        while (bits) {
            uint32_t bit = (uint32_t)__builtin_ctz(bits), mask = 1u << bit;
            uint32_t note = word * 32u + bit;
            if (!midi_track_emit(0x08u | (0x80u | ch) << 8 | note << 16)) return;
            midi_output[tr].off[word] &= ~mask;
            midi_output[tr].held[word] &= ~mask;
            bits &= ~mask;
        }
    }
}

static void midi_track_wire_off(track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk);
    for (uint32_t word = 0; word < 4u; word++) midi_output[tr].off[word] |= midi_output[tr].held[word];
    if (midi_output[tr].pedal) midi_output[tr].pedal_off = 1;
    midi_sent_forget(tr);
    midi_track_flush(tr);
}

#if BOARD_SMK37
static void midi_track_all_off(track_t *t)
{
    midi_trs_all_off((uint32_t)(t - trk));
    midi_track_wire_off(t);
}
#else
#define midi_track_all_off midi_track_wire_off
#endif

static void midi_track_service(track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk);
    midi_trs_service(t, tr);
    if (!midi_track_only(t) || midi_track_blocked(t) || midi_output[tr].channel != midi_track_channel(t))
        midi_track_wire_off(t);
    else midi_track_flush(tr);
}

static void midi_track_note_off(track_t *t, uint32_t note)
{
    if (note > 127u) return;
    uint32_t tr = (uint32_t)(t - trk);
    int32_t sent = midi_sent_release(tr, note);
    midi_trs_note_off(tr, note);
    if (sent >= 0) {
        uint32_t word = (uint32_t)sent >> 5, mask = 1u << (sent & 31);
        if (midi_output[tr].held[word] & mask) midi_output[tr].off[word] |= mask;
    }
    midi_track_flush(tr);
}

static void midi_track_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    if (note > 127u || midi_track_blocked(t)) return;
    uint32_t tr = (uint32_t)(t - trk), sent = note;
    int micro = micro_active(t);
    midi_track_service(t);
    midi_track_note_off(t, note); /* retrigger: finish the previous strike */
    midi_trs_note_on(t, tr, note, vel);
    if (midi_track_pending(tr)) return;
    if (micro) {
        if (midi_sent_n == MIDI_SENT_N) return;
        sent = midi_sent_pitch(t, note);
        if (midi_output[tr].held[sent >> 5] & (1u << (sent & 31u))) { /* another degree's semitone: struck again */
            midi_output[tr].off[sent >> 5] |= 1u << (sent & 31u);
            midi_track_flush(tr);
            if (midi_track_pending(tr)) return;
        }
    }
    uint32_t ch = midi_track_channel(t);
    if (!midi_track_emit(0x09u | (0x90u | ch) << 8 | sent << 16 | (vel & 127u) << 24)) return;
    midi_output[tr].channel = (uint8_t)ch;
    midi_output[tr].held[sent >> 5] |= 1u << (sent & 31u);
    if (micro) midi_sent_add(tr, note, sent);
}

static void midi_track_controller(track_t *t, uint32_t st, uint32_t d1, uint32_t d2)
{
    if (midi_track_blocked(t)) return;
    uint32_t tr = (uint32_t)(t - trk);
    midi_track_service(t);
    midi_trs_controller(t, tr, st, d1, d2);
    if (midi_track_pending(tr)) return;
    uint32_t ch = midi_track_channel(t);
    if (!midi_track_emit((st >> 4) | (st | ch) << 8 | d1 << 16 | d2 << 24)) {
        if (st == 0xB0u && ((d1 == 64u && d2 < 64u) || d1 == 121u) && midi_output[tr].pedal)
            midi_output[tr].pedal_off = 1;
        return;
    }
    midi_output[tr].channel = (uint8_t)ch;
    if (st == 0xB0u && d1 == 64u) midi_output[tr].pedal = d2 >= 64u;
    if (st == 0xB0u && d1 == 121u) midi_output[tr].pedal = 0;
}
