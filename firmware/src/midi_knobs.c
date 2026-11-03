/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Audio ISR -> foreground panel input. Never load a preset or edit UI state in
 * the audio ISR. General uses MIDI's eight general-purpose controllers; MiniLab
 * uses Arturia's factory Arturia/User CCs (not its DAW/MCU port).
 * https://support.arturia.com/hc/en-us/articles/6189475866396 */
#define MIDI_KNOB_Q 64u
static uint32_t midi_knob_q[MIDI_KNOB_Q] __attribute__((section(".pool")));
static volatile uint32_t midi_knob_w, midi_knob_r;
static volatile uint8_t midi_knob_overflow;
static volatile uint8_t midi_knob_usb_epoch;
#if MELODEE_BLE
#define MIDI_KNOB_SOURCES 3u
static volatile uint8_t midi_knob_ble_epoch;
#else
#define MIDI_KNOB_SOURCES 2u
#endif
static uint32_t midi_knob_config(void)
{ return settings_midi_knobs | (uint32_t)settings_midi_knob_mode << 2; }

static void midi_knob_push(uint32_t k, uint32_t ch, uint32_t value)
{
    uint32_t w = midi_knob_w;
    if (w - midi_knob_r >= MIDI_KNOB_Q) {
        midi_knob_overflow = 1;                 /* discard this gesture; rebase absolute navigation */
        return;
    }
    uint32_t src = midi_event_source == 2u ? 1u : 0u, epoch = midi_knob_usb_epoch;
#if MELODEE_BLE
    if (midi_event_source == 3u) { src = 2u; epoch = midi_knob_ble_epoch; }
#endif
    midi_knob_q[w % MIDI_KNOB_Q] = value | k << 7 | ch << 10 |
        src << 14 | midi_knob_config() << 16 | (src == 1u ? 0u : epoch << 20);
    RING_PUBLISH();
    midi_knob_w = w + 1u;
}
static int midi_knob_receive(uint32_t ch, uint32_t cc, uint32_t value)
{
    static const uint8_t map[2][8] = {{16,17,18,19,80,81,82,83}, {74,71,76,77,93,18,19,16}};
    uint32_t profile = settings_midi_knobs, k;
    if (!profile || profile >= MK_PROFILES) return 0;
    for (k = 0; k < 8u && map[profile - 1u][k] != cc; k++) ;
    if (k == 8u) return 0;
    midi_knob_push(k, ch, value);
    return 1;
}

/* MIDI master volume attenuates the physical pot, including USB playback.
 * Kept out of the sample loops; unity is exactly the existing master path. */
static uint32_t midi_master_physical_q12 = 4096u;
static uint8_t midi_master_volume = 127;
static void midi_master_apply(void)
{
    uint32_t v = midi_master_volume;
    song.master_q12 = midi_master_physical_q12 * v * v / (127u * 127u);
}
static void midi_master_physical(uint32_t q12)
{
    midi_master_physical_q12 = q12;
    midi_master_apply();
}

/* MIDI LEARN (#105): another controller's knob or button as one of the panel's own, on every page. A knob is a CC on
 * any channel and goes the way of the MIDI KNOBS map (its MIDI MODE too); a button is a CC on any channel (64 and up
 * down) or a note on its channel, and closes the panel's button under the scan (hal/fm1_input.h fm1__key), so holds and
 * combos work too. A learned message no longer does what it did. mlearn_row (midi_knobs.h) is saved with the settings:
 * 0 nothing, else the CC, or MLEARN_NOTE + the note. The Settings page: ui_midi_learn.c */
#define MLEARN_NOTE 0x80u
static volatile uint8_t mlearn_on;                       /* the page is open: nothing learned acts, messages wait */
static volatile int16_t mlearn_seen = -1;                /* the last message: mlearn_row's value | channel << 8 */
static int mlearn_valid(void)
{ return mlearn_row[28] == 'M' && mlearn_row[29] == 'L' && mlearn_row[30] == 1u; }
static uint32_t mlearn_note_ch(uint32_t b)                /* button b's: a nibble each */
{ return (uint32_t)(mlearn_row[MLEARN_N + b / 2u] >> (b & 1u ? 4u : 0u)) & 15u; }
static int mlearn_ok(uint32_t cc)                          /* not the CCs with a meaning of their own */
{
    return cc != 0u && cc != 1u && cc != 6u && cc != 11u && cc != 32u && cc != 38u && cc != 64u &&
           (cc < 96u || cc > 101u) && cc < 120u;
}
static int mlearn_receive(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    /* midi_knob_push's k of each knob role (EN_SELECT, EN_ALGO, EN_PRESET, EN_K1..4) */
    static const uint8_t knob_q[MLEARN_KNOBS] = {1, 3, 2, 4, 5, 6, 7};
    uint32_t src;
    int cc = st == 0xB0u, on = cc ? d2 >= 64u : st == 0x90u && d2;
    if (cc ? !mlearn_ok(d1) : st != 0x90u && st != 0x80u) return 0;
    src = cc ? d1 : MLEARN_NOTE | d1;
    if (mlearn_on) {                                    /* (a note held from before still ends) */
        if (cc || on) mlearn_seen = (int16_t)(src | ch << 8);
        return cc || on;
    }
    if (!mlearn_valid()) return 0;
    for (uint32_t k = 0; k < MLEARN_N; k++) {
        if (mlearn_row[k] != src) continue;
        if (k < MLEARN_KNOBS) {
            if (!cc) continue;
            midi_knob_push(knob_q[k], ch, d2);
            return 1;
        }
        uint32_t b = k - MLEARN_KNOBS;
        if (!cc && mlearn_note_ch(b) != ch) continue;
        /* the tap bit outlasts a release faster than the scan; the scan's clearing it can lose to this, harmlessly */
        if (on) { fm1_in.remote |= 1u << b; fm1_in.remote_tap |= 1u << b; }
        else fm1_in.remote &= ~(1u << b);
        return 1;
    }
    return 0;
}
