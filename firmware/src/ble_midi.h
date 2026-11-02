/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Jacek Mariański */
/* Bluetooth MIDI (MELODEE_BLE): what the files before ble_midi.c use. The audio interrupt's side (MIDI out, the
 * quiet while the radio calibrates) is here, inline, so it is placed with the audio code. */
#pragma once

static uint8_t settings_ble;                    /* the menu's BLUETOOTH: 0 OFF, 1 COMPUTER, 2 KEYBOARD (settings_persist.c) */
static volatile uint8_t ble_quiet;              /* the radio calibrates: audio.c renders silence meanwhile */

/* MIDI out: the stream USB gets (usb.c midi_track_emit, the audio interrupt), while a link takes MIDI; the stack
 * takes it at its next connection event (ble_app_midi_peek, the BLE interrupts) */
#define BLE_OQ 32u
static uint32_t ble_out_q[BLE_OQ], ble_out_ms[BLE_OQ];
static volatile uint32_t ble_ow, ble_or;
static volatile uint8_t ble_out_on;             /* a link with MIDI both ways (ble_midi.c ble_app_state) */

static inline int ble_midi_out(uint32_t pkt)
{
    if (!ble_out_on || ble_ow - ble_or >= BLE_OQ)
        return 0;
    ble_out_q[ble_ow % BLE_OQ] = pkt;
    ble_out_ms[ble_ow % BLE_OQ] = fm1_ms;
    RING_PUBLISH();
    ble_ow++;
    return 1;
}

/* the menu's BLUETOOTH row (ui_menu.c; ble_midi.c) */
static void ble_midi_set(uint8_t mode);
static const char *ble_midi_value(void);
static uint8_t ble_midi_tag(void);
static const char *ble_midi_status(void);       /* GLO > SYSTEM's MIDI column: OFF, WAIT, ON */
static volatile uint32_t ble_rx_pkts;           /* packets into the router (its RX) */              /* the header's BT tag: 0 none, 1 waiting, 2 connected */
