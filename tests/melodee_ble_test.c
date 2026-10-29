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
    for (uint32_t font=0; font<2; font++) {
        if (font) PREF_BITS |= PREF_PIXEL; else PREF_BITS &= ~PREF_PIXEL;
        for (uint32_t n=0; n<=BLE_SCAN_N; n++) {
            mb.count=(uint8_t)n;
            for(uint32_t i=0;i<n;i++) {mb.idx[i]=(uint8_t)i;str_cpy(mb.found.e[i].name,"Keyboard",BLE_NAME_MAX+1);}
            for(uint32_t row=0;row<n+4;row++) {ble_row=(uint8_t)row;ui.menu=3;ui.force=1;draw_ble_page();}
        }
    }
    puts("Melodee BLE integration: all passed");
    return 0;
}

#endif
