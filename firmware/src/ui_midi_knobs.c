/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Foreground only. Absolute parameter values cover their full range. Absolute
 * navigation is a jog: the first value establishes position without jumping.
 * Histories are separate for each USB/TRS channel, including after overflow. */
#ifndef MIDI_KNOB_SOURCES
#if MELODEE_BLE
#define MIDI_KNOB_SOURCES 3u
#else
#define MIDI_KNOB_SOURCES 2u
#endif
#endif
static uint8_t midi_knob_last[MIDI_KNOB_SOURCES][16][8] __attribute__((section(".pool")));
static uint8_t midi_knob_seen[MIDI_KNOB_SOURCES][16], midi_knob_ui_usb_epoch;
static uint32_t midi_knob_ui_config;
#if MELODEE_BLE
static uint8_t midi_knob_ui_ble_epoch;
#endif
static int32_t midi_knob_relative(uint32_t value, uint32_t mode)
{
    if (mode == MK_REL1) return (int32_t)value - 64;
    if (mode == MK_REL2) return value < 64u ? (int32_t)value : (int32_t)value - 128;
    return (int32_t)value - 16;                 /* Arturia Relative 3: 15 down, 17 up */
}
static void midi_knobs_poll(void)
{
    static const uint8_t roles[7] = {EN_SELECT,EN_PRESET,EN_ALGO,EN_K1,EN_K2,EN_K3,EN_K4};
    uint32_t config = midi_knob_config();
    memset(midi_panel_delta, 0, sizeof midi_panel_delta);
    midi_panel_mask = 0;
    for (uint32_t k = 0; k < NE; k++) midi_panel_absolute[k] = -1;
    if (midi_knob_ui_usb_epoch != midi_knob_usb_epoch) {
        memset(midi_knob_seen[0], 0, sizeof midi_knob_seen[0]);
        midi_knob_ui_usb_epoch = midi_knob_usb_epoch;
    }
#if MELODEE_BLE
    if (midi_knob_ui_ble_epoch != midi_knob_ble_epoch) {
        memset(midi_knob_seen[2], 0, sizeof midi_knob_seen[2]);
        midi_knob_ui_ble_epoch = midi_knob_ble_epoch;
    }
#endif
    if (config != midi_knob_ui_config || midi_knob_overflow) {
        memset(midi_knob_seen, 0, sizeof midi_knob_seen);
        midi_knob_ui_config = config;
        if (midi_knob_overflow) {
            fm1_irq_off();
            midi_knob_r = midi_knob_w;
            midi_knob_overflow = 0;
            fm1_irq_on();
        }
        if (!settings_midi_knobs) { midi_master_volume = 127; midi_master_apply(); }
    }
    /* Bound each pass even if a controller keeps sending while we drain. */
    uint32_t end = midi_knob_w;
    while (midi_knob_r != end) {
        uint32_t pkt = midi_knob_q[midi_knob_r % MIDI_KNOB_Q];
        RING_PUBLISH();
        midi_knob_r++;
        if (((pkt >> 16) & 15u) != config) continue;
        uint32_t v = pkt & 127u, k = (pkt >> 7) & 7u, ch = (pkt >> 10) & 15u, src = (pkt >> 14) & 3u;
        if (src >= MIDI_KNOB_SOURCES) continue;
        if (!src && ((pkt >> 20) & 255u) != midi_knob_ui_usb_epoch) continue;
#if MELODEE_BLE
        if (src == 2u && ((pkt >> 20) & 255u) != midi_knob_ui_ble_epoch) continue;
#endif
        uint32_t mode = settings_midi_knob_mode, seen = midi_knob_seen[src][ch] & (1u << k);
        int32_t delta = mode == MK_ABSOLUTE ? seen ? (int32_t)v - midi_knob_last[src][ch][k] : 0 :
                        midi_knob_relative(v, mode);
        midi_knob_last[src][ch][k] = (uint8_t)v;
        midi_knob_seen[src][ch] |= (uint8_t)(1u << k);
        panel_moved = 1;
        if (!k) {
            midi_master_volume = (uint8_t)(mode == MK_ABSOLUTE ? v : clamp((int32_t)midi_master_volume + delta, 0, 127));
            midi_master_apply();
        } else {
            uint32_t role = roles[k - 1u];
            midi_panel_delta[role] = clamp(midi_panel_delta[role] + delta, -1024, 1024);
            if (delta) midi_panel_mask |= (uint8_t)(1u << role);
            if (mode == MK_ABSOLUTE && delta && k >= 4u) midi_panel_absolute[role] = (int16_t)v;
        }
    }
}
