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

static void midi_trs_all_off(uint32_t tr)
{
    uint32_t ch = midi_trs[tr].channel;
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
    uint32_t word = note >> 5, mask = 1u << (note & 31u);
    if (!(midi_trs[tr].held[word] & mask)) return;
    midi_trs[tr].held[word] &= ~mask;
    smo_end(0x08u | (0x80u | midi_trs[tr].channel) << 8 | note << 16);
}

static void midi_trs_note_on(const track_t *t, uint32_t tr, uint32_t note, uint32_t vel)
{
    uint32_t ch = midi_track_channel(t);
    if (!smo_start(0x09u | (0x90u | ch) << 8 | note << 16 | (vel & 127u) << 24)) return;
    midi_trs[tr].channel = (uint8_t)ch;
    midi_trs[tr].held[note >> 5] |= 1u << (note & 31u);
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
    uint32_t tr = (uint32_t)(t - trk), word = note >> 5, mask = 1u << (note & 31u);
    midi_trs_note_off(tr, note);
    if (midi_output[tr].held[word] & mask) midi_output[tr].off[word] |= mask;
    midi_track_flush(tr);
}

static void midi_track_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    if (note > 127u || midi_track_blocked(t)) return;
    uint32_t tr = (uint32_t)(t - trk);
    midi_track_service(t);
    midi_track_note_off(t, note); /* retrigger: finish the previous strike */
    midi_trs_note_on(t, tr, note, vel);
    if (midi_track_pending(tr)) return;
    uint32_t ch = midi_track_channel(t);
    if (!midi_track_emit(0x09u | (0x90u | ch) << 8 | note << 16 | (vel & 127u) << 24)) return;
    midi_output[tr].channel = (uint8_t)ch;
    midi_output[tr].held[note >> 5] |= 1u << (note & 31u);
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
