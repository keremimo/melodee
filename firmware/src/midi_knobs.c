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
