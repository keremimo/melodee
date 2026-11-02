/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define MELODEE_BLE 1
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>

static uint32_t packet(uint32_t st, uint32_t note, uint32_t vel)
{ return (st >> 4) | st << 8 | note << 16 | vel << 24; }
static void reset_ble(void)
{
    ui_power_on(); memset(&mb, 0, sizeof mb); memset(&mb_saved, 0, sizeof mb_saved);
    ble_init((uint8_t[]){1,2,3,4,5,0xC6}, 1);
    mb.started = mb.on = 1;
    mi_r = mi_w = mo_r = mo_w = 0; midi_usb_lost = midi_ble_lost = 0;
    memset(midi_owner_ble, 0, sizeof midi_owner_ble);
    memset(midi_owner_trs, 0, sizeof midi_owner_trs);
    events_block(0);
}

#ifndef MELODEE_BLE_TEST_NO_MAIN
int main(void)
{
    reset_ble();
    /* Neither a bare LL connection nor GATT discovery is a usable MIDI link. */
    assert(!mel_ble_ready());
    usb.up = usb.config = 0; mb.ready = 1;
    uint32_t p = packet(0x90, 60, 100), out, ms;
    assert(midi_track_emit(p) && mo_r == mo_w);
    assert(ble_app_midi_peek(&out, &ms) && out == p); ble_app_midi_pop();
    assert(!ble_app_midi_peek(&out, &ms));
    usb.up = usb.config = 1;
    assert(midi_track_emit(p)); assert(mo_w == 1 && mb.tw - mb.tr == 1);
    mb.tw = mb.tr + MBQ;
    assert(!midi_track_emit(p) && mo_w == 1); /* no duplicate USB on a BLE retry */
    mb.tw = mb.tr; mo_r = mo_w;
    puts("BLE adapter: independent and atomic USB/BLE output admission passed");

    /* Clock out: each transport admits it alone, a pulse leaves half of each queue to notes. */
    uint32_t pulse = 0xFu | 0xF8u << 8, start = 0xFu | 0xFAu << 8;
    midi_sync_emit(0xF8u);
    assert(mo_w - mo_r == 1 && midi_out_q[mo_r % MQ] == pulse);
    assert(ble_app_midi_peek(&out, &ms) && out == pulse); ble_app_midi_pop();
    mo_r = mo_w;
    mb.tw = mb.tr + MBQ / 2u;
    midi_sync_emit(0xF8u); assert(mb.tw - mb.tr == MBQ / 2u && mo_w - mo_r == 1);
    midi_sync_emit(0xFAu); assert(mb.tw - mb.tr == MBQ / 2u + 1u && mo_w - mo_r == 2);
    mb.tw = mb.tr + MBQ;
    midi_sync_emit(0xF8u); assert(mo_w - mo_r == 3);          /* a full BLE ring costs USB no pulse */
    mb.tw = mb.tr; mo_r = mo_w; mo_w += MQ;                   /* an unread USB port costs BLE no Start */
    midi_sync_emit(0xFAu); assert(mo_w - mo_r == MQ && ble_app_midi_peek(&out, &ms) && out == start);
    ble_app_midi_pop(); mo_r = mo_w;
    struct ble_midi_enc air; uint8_t air_buf[8];
    ble_midi_enc_reset(&air); ble_midi_enc_begin(&air, air_buf, sizeof air_buf);
    assert(ble_midi_enc_add(&air, pulse, 5) > 0 && ble_midi_enc_len(&air) == 3 && air_buf[2] == 0xF8u);
    puts("BLE adapter: clock and transport admitted per transport and encoded passed");

    reset_ble();
    ble_app_midi_in(packet(0x90, 60, 100), 10, 10); mel_ble_drain(); events_block(0);
    assert(mn_get(0,60) && midi_note_source(0,60) == 3);
    /* USB's lost-session cleanup must preserve a BLE-owned note. */
    midi_usb_lost = 1; events_block(0); assert(mn_get(0,60));
    midi_enqueue(packet(0x90, 64, 100), 1); events_block(0); assert(mn_get(0,64));
    mb.lost = 1; mel_ble_drain(); events_block(0);
    assert(!mn_get(0,60) && mn_get(0,64));
    /* A wireless maintenance SysEx never enters the OTA/parser path. */
    uint32_t w = mb.rw; ble_app_midi_in(0x2235F004u, 0, 0); assert(mb.rw == w);
    /* An old BLE release cannot end a USB retrigger of the same channel/note. */
    midi_enqueue(packet(0x90, 64, 110), 3); events_block(0);
    midi_enqueue(packet(0x90, 64, 120), 1); events_block(0);
    midi_enqueue(packet(0x80, 64, 0), 3); events_block(0); assert(mn_get(0,64));
    puts("BLE adapter: routing, disconnect ownership and maintenance isolation passed");

    reset_ble();
    song.g[G_CLOCK] = 3;
    ble_app_midi_in(0xFA0Fu, 0, 0); mel_ble_drain(); events_block(0);
    assert(song.playing && midi_clock.mode == 3);
    ble_app_midi_in(0xF80Fu, 0, 0); mel_ble_drain(); events_block(0);
    assert(midi_rx_pulses[2] == 1);
    mb.overflow = 1; mel_ble_drain(); events_block(0); assert(!song.playing);
    puts("BLE adapter: external clock and overflow release passed");

    reset_ble();
    mb.attempt = 1; bsmp.init = 1; bsmp.st = I_CONFIRM; smp_passkey = 42;
    char text[24]; ble_head_text(text); assert(!strcmp(text,"PIN 000042"));
    smp_passkey = 0; ble_head_text(text); assert(!strcmp(text,"PIN 000000"));
    smp_passkey = 999999; ble_head_text(text); assert(!strcmp(text,"PIN 999999"));
    mel_ble_disconnect(); assert(mel_ble_code() == BLE_NO_PASSKEY);
    mb.cal = 0; assert(!mel_ble_set(1)); assert(!strcmp(mel_ble_status(),"NO CAL"));
    mb_saved.valid = 1; mb_saved.addr[0] = 12; mb_saved.rf[0] = 0xAC;
    memset(mb_saved.bond, 1, sizeof mb_saved.bond); mel_ble_forget();
    assert(mb_saved.valid && mb_saved.addr[0] == 12 && mb_saved.rf[0] == 0xAC && !mb_saved.bond[0]);
    mb.on=1;mb.closing=1;
    ble_app_bond((uint8_t[8]){1},12,(uint8_t[16]){2}); assert(!mb_saved.bond[0]);
    ble_app_central_keys(&(struct ble_keys){.has=BLE_KEYS_LTK}); assert(!mb.keys_new);
    mb.closing=0;
    puts("BLE adapter: passkey lifecycle, missing calibration and forget isolation passed");

    /* Mode: VISIBLE advertises and never scans; CONNECT OUT is quiet until the page scans. Both saved. */
    reset_ble(); mb.cal = 1; ble_enable(1); assert(bll.state == LL_ADV);   /* (the stack's default before a mode applies) */
    ble_ll_advertise(0); assert(bll.state == LL_OFF);
    mel_ble_scan(1); assert(mb.scanning && bll.state == LL_SCAN);
    mel_ble_visible(1); assert(mb_saved.flags == MB_VISIBLE && !mb.scanning && bll.state == LL_ADV);
    mel_ble_scan(1); assert(!mb.scanning && bll.state == LL_ADV);
    assert(!strcmp(mel_ble_status(), "VISIBLE 0201"));
    ble_head_text(text); assert(!strcmp(text, "VISIBLE 0201"));
    mel_ble_scan(0); mel_ble_visible(0); assert(!mb_saved.flags && bll.state == LL_OFF);
    assert(!strcmp(mel_ble_status(), "BLE AVAILABLE"));
    puts("BLE adapter: Mode VISIBLE advertises, CONNECT OUT stays quiet passed");

    /* Power-on: the radio as last set. Radio OFF before the quiet moment wins. */
    reset_ble(); mb.cal = 1; mb.on = 0;
    assert(mel_ble_set(1) && mb_saved.flags == MB_RADIO);
    assert(mel_ble_set(0) && !mb_saved.flags && !mb.on);
    mb_saved.flags = MB_RADIO; mb.boot = 1; ble_boot_poll(); assert(mb.on && !mb.boot);
    mb.boot = 1; assert(mel_ble_set(0) && !mb.boot); ble_boot_poll(); assert(!mb.on);
    puts("BLE adapter: radio state kept across power-on passed");

    /* Relink: rounds of a 10 s scan every 30 s until the last device is seen, then connect; a drop or a miss
     * keeps looking, Disconnect ends it and its scan. */
    reset_ble(); mb.cal = 1; ble_enable(1); ble_ll_advertise(0);
    const uint8_t widi[6] = {0x10,0x20,0x30,0x40,0x50,0x60};
    ble_store_set_last(&mb.store, widi, 0, "WIDI", BLE_KIND_MIDI);
    mel_ble_set(1); assert(mb.relink);
    mel_ble_poll(); assert(mb.relink_scan && bll.state == LL_SCAN && !strcmp(mel_ble_status(), "RECONNECTING"));
    fm1_ms += MB_RELINK_SCAN_MS; mel_ble_poll(); assert(!mb.relink_scan && bll.state == LL_OFF);
    fm1_ms += MB_RELINK_MS - MB_RELINK_SCAN_MS; mel_ble_poll(); assert(mb.relink_scan && bll.state == LL_SCAN);
    struct ble_found *e = &mb.found.e[0]; memset(e, 0, sizeof *e);
    e->used = e->midi = 1; e->kind = BLE_KIND_MIDI; memcpy(e->addr, widi, 6); e->seen_ms = fm1_ms;
    mel_ble_poll(); assert(mb.attempt && !mb.relink_scan && bll.state == LL_INIT && !bll.scan_want);
    assert(!strcmp(mel_ble_status(), "RECONNECTING"));
    fm1_ms += 10001u; mel_ble_poll(); assert(!mb.attempt && mb.relink && !mb.failure[0] && bll.state == LL_OFF);
    fm1_ms += MB_RELINK_MS; e->used = 1; e->seen_ms = fm1_ms; mel_ble_poll(); assert(mb.attempt);   /* (heard again) */
    mel_ble_disconnect(); assert(!mb.relink); mel_ble_poll(); assert(bll.state == LL_OFF && !bll.scan_want);
    fm1_ms += MB_RELINK_MS; mel_ble_poll(); assert(!mb.attempt && bll.state == LL_OFF);
    mel_ble_visible(1); mel_ble_set(1); assert(!mb.relink && bll.state == LL_ADV);
    puts("BLE adapter: power-on relink to the last device passed");

    reset_ble(); settings_midi_knobs = MK_GENERAL;
    midi_event_source=1; assert(midi_knob_receive(0,17,20));
    midi_event_source=3; assert(midi_knob_receive(0,17,100));
    midi_knobs_poll(); assert(!midi_panel_delta[EN_SELECT]);
    midi_event_source=1; assert(midi_knob_receive(0,17,21));
    midi_event_source=3; assert(midi_knob_receive(0,17,101));
    midi_knobs_poll(); assert(midi_panel_delta[EN_SELECT]==2);
    assert(midi_knob_receive(0,17,120)); midi_knob_ble_epoch++;
    midi_knobs_poll(); assert(!midi_panel_delta[EN_SELECT]); midi_event_source=0;
    puts("BLE controller knobs: separate USB/BLE histories and reconnect epochs passed");

    /* Real BLE page drawing: both fonts, 0..8 discoveries, all selections. */
    reset_ble();
    for (uint32_t font=0; font<4; font++) {
        if (font & 1u) PREF_BITS |= PREF_PIXEL; else PREF_BITS &= ~PREF_PIXEL;
        mb_saved.flags = font >= 2u ? MB_VISIBLE : 0;
        for (uint32_t n=0; n<=BLE_SCAN_N; n++) {
            mb.count=(uint8_t)n;
            for(uint32_t i=0;i<n;i++) {mb.idx[i]=(uint8_t)i;str_cpy(mb.found.e[i].name,"Keyboard",BLE_NAME_MAX+1);}
            for(uint32_t row=0;row<ble_found_n()+5;row++) {ble_row=(uint8_t)row;ui.menu=3;ui.force=1;draw_ble_page();}
        }
    }
    puts("Melodee BLE integration: all passed");
    return 0;
}

#endif
