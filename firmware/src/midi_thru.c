/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The menu's MIDI THRU (ui.c MIDI_THRU_ON), the classic build's (fm1-base: platform/melodee_app.c midi_thru): what comes
 * in also goes on out, as it came (its channel, beside what Melodee plays with it), where it did not come from: USB in
 * to Bluetooth, the TRS jack's to USB and Bluetooth, Bluetooth's to USB (two Bluetooth links, BLE_LINKS 2: and to the
 * other link, a keyboard's on to the iPad); on the SMK-37 the jack's OUT too, unless it came in on the jack. Channel messages; clock, Start, Continue, Stop and Song Position (F8 FA FB FC F2, told by status) only
 * from the input CLK follows, so a follower downstream hears one clock (on INT, Melodee's own), through the transports'
 * clock queues as Melodee's own clock goes; nothing else (SysEx, MTC, active sensing, reset). Continuous streams (A0 B0
 * D0 E0, but the pedal and the all-off controllers) leave a busy ring's last 16 slots to the notes and the keys.
 * usb.c midi_enqueue calls it for every message, TIMER5; the rings are the audio ISR's too, so the IRQs are off meanwhile,
 * as usb.c midi_track_emit has them. source: midi_enqueue's (1 USB and the USB host's device, 2 TRS, 3 Bluetooth) */
static void midi_thru(uint32_t pkt, uint32_t source)
{
    uint32_t st = (pkt >> 8) & 255u, cin = pkt & 15u, cc = (pkt >> 16) & 127u, keep, pulse = st == 0xF8u, ie;
    int sync = 0;
    if (!(GLO_SELECT & MIDI_THRU_ON))
        return;
    if (st >= 0xF0u) {
        if (!(0x1D04u >> (st & 15u) & 1u) || source != (uint32_t)song.g[G_CLOCK])
            return;
        pkt = (pkt & ~15u) | (st == 0xF2u ? 3u : 15u);
        sync = 1;
    } else if (st >> 4 != cin || cin < 8u) {
        return;
    }
    keep = (0x6C00u >> cin & 1u) && !(cin == 11u && (cc == 64u || cc == 120u || cc == 123u)) ? 16u : 1u;
    ie = fm1_icfg(); fm1_irq_off();
    if (source != 1u && usb_midi_out_ready()) {
        if (sync) {
            if (rt_w - rt_r < (pulse ? RTQ - 4u : RTQ)) {
                midi_rt_q[rt_w % RTQ] = pkt;
                RING_PUBLISH(); rt_w++;
            }
        } else if (MQ - (mo_w - mo_r) >= keep) {
            midi_out_q[mo_w % MQ] = pkt;
            RING_PUBLISH(); mo_w++;
        }
    }
#if MELODEE_BLE
    uint32_t links = source == 3u ? mel_ble_other_links() : ~0u;   /* (one link: none for Bluetooth's own) */
    if (links) {
        if (sync)
            mel_ble_sync_to(pkt, pulse, links);
        else if (mel_ble_room_for(keep, links))
            (void)mel_ble_emit_to(pkt, links);
    }
#endif
#if BOARD_SMK37 && defined(SMO_RING)
    if (source != 2u)
        smo_put(pkt);
#endif
    fm1_icfg_set(ie);
}
