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

static int midi_knob_receive(uint32_t ch, uint32_t cc, uint32_t value)
{
    static const uint8_t map[2][8] = {{16,17,18,19,80,81,82,83}, {74,71,76,77,93,18,19,16}};
    uint32_t profile = settings_midi_knobs, k, w = midi_knob_w;
    if (!profile || profile >= MK_PROFILES) return 0;
    for (k = 0; k < 8u && map[profile - 1u][k] != cc; k++) ;
    if (k == 8u) return 0;
    if (w - midi_knob_r >= MIDI_KNOB_Q) {
        midi_knob_overflow = 1;                 /* discard this gesture; rebase absolute navigation */
        return 1;
    }
    uint32_t src = midi_event_source == 2u ? 1u : 0u, epoch = midi_knob_usb_epoch;
#if MELODEE_BLE
    if (midi_event_source == 3u) { src = 2u; epoch = midi_knob_ble_epoch; }
#endif
    midi_knob_q[w % MIDI_KNOB_Q] = value | k << 7 | ch << 10 |
        src << 14 | midi_knob_config() << 16 | (src == 1u ? 0u : epoch << 20);
    RING_PUBLISH();
    midi_knob_w = w + 1u;
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

/* MIDI LEARN (#105): a CC onto one knob of one track's page, whatever channel it comes on. The audio ISR keeps the
 * latest value of each learned CC (mlearn_val, a dirty bit each) and the CC a learn waits for; ui_midi_knobs.c turns
 * the knob (mlearn_poll). Up to MLEARN_N CCs; a learned CC no longer does what the standard map gave it. Kept until
 * power-off */
#define MLEARN_N 16u
typedef struct { uint8_t cc, track, page, slot, eng, id; } mlearn_t;   /* cc 0: free (never learned); page 0xFF:
                                                                      * Stage's knobs */
static mlearn_t mlearn[MLEARN_N], mlearn_want;          /* mlearn_want.cc 1: a knob turned while learning waits */
static volatile uint8_t mlearn_on, mlearn_val[MLEARN_N];   /* mlearn_on: learning (the menu's MIDI LEARN) */
static volatile uint16_t mlearn_dirty;
static volatile int16_t mlearn_seen = -1;                /* learning: the last CC sent */
static int mlearn_ok(uint32_t cc)                          /* not the CCs with a meaning of their own */
{
    return cc != 0u && cc != 1u && cc != 6u && cc != 11u && cc != 32u && cc != 38u && cc != 64u &&
           (cc < 96u || cc > 101u) && cc < 120u;
}
static int mlearn_receive(uint32_t cc, uint32_t value)
{
    if (!mlearn_ok(cc)) return 0;
    if (mlearn_on) { mlearn_seen = (int16_t)cc; return 1; }
    for (uint32_t k = 0; k < MLEARN_N; k++)
        if (mlearn[k].cc == cc) { mlearn_val[k] = (uint8_t)value; mlearn_dirty |= (uint16_t)(1u << k); return 1; }
    return 0;
}
