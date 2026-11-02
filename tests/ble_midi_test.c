/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Jacek Mariański */
/* Bluetooth MIDI's glue (firmware/src/ble_midi.c, MELODEE_BLE) against the real router, sequencer, MIDI Track,
 * settings and resource arena; the stack (ble.o) is a stub here: Optimist's own tests cover it. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>

/* ---- what melodee.c gives ble_midi.c on the device */
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static int host_clock_sets;
static int fm1_clock_stock(void) { host_clock_sets++; return 1; }
#define BLE_RAM_AT (_resource_end - 0x1000)      /* (app.ld: .ble_bss at the arena's top) */
#define BLE_RAM_BYTES 0x1000u
#include "../firmware/src/ble_midi.c"
static int host_clock_boots;
static void clock_boot(void) { host_clock_boots++; }

/* ---- the stack (ble.o) */
static int stub_starts, stub_mode = -1, stub_state = BU_OFF, stub_ready, stub_conn, stub_held;
static uint8_t stub_addr[6];
static const char *stub_peer = "";
void ble_unit_attach(void) {}
void ble_unit_irqs_hold(int hold) { stub_held = hold; }
int ble_unit_start(const uint8_t addr[6], int (*rd)(uint32_t off, uint8_t *dst, uint32_t n))
{
    uint8_t b[4];
    assert(ble_quiet || !stub_starts);          /* (at run time: the audio silent meanwhile) */
    assert(rd(0xE7000u, b, sizeof b) != 0);     /* (no flash on the host: the trims are measured) */
    memcpy(stub_addr, addr, 6);
    return ++stub_starts, 0;
}
void ble_unit_mode(int mode) { stub_mode = mode; }
void ble_unit_poll(uint32_t now) { (void)now; }
int ble_unit_state(void) { return stub_state; }
const char *ble_unit_peer(void) { return stub_peer; }
int ble_midi_ready(void) { return stub_ready; }
int ble_connected(void) { return stub_conn; }

static void ble_link(int on)                         /* a link with MIDI both ways comes / goes (the BLE interrupts) */
{
    stub_conn = stub_ready = on;
    stub_state = on ? BU_READY : BU_WAITING;
    ble_app_state();
}
static void reset(void)
{
    ui_power_on();
    mi_r = mi_w = mo_r = mo_w = 0; midi_in_overflow = 0;
    ble_iw = ble_ir = ble_ow = ble_or = 0; ble_release = 0; ble_was_conn = 0;
    memset(ble_held, 0, sizeof ble_held); memset(midi_rx_pulses, 0, sizeof midi_rx_pulses);
    memset(midi_owners, 0, sizeof midi_owners); memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_sel_on, 0, sizeof midi_sel_on); memset(&bin, 0, sizeof bin);
    stub_conn = stub_ready = 0; ble_out_on = 0;
    usb.config = 1;                                       /* (USB MIDI out up: a MIDI Track's output shows there) */
    settings_ble = BLE_MODE_KEYBOARD;
    song.g[G_MIDI] = 0; song.g[G_ROUTE] = 0; song.sel = 0;
    events_block(0);
    mo_r = mo_w;
}
static uint32_t pkt(uint32_t st, uint32_t d1, uint32_t d2) { return (st >= 0xF0u ? 0xFu : st >> 4) | st << 8 | d1 << 16 | d2 << 24; }
static void ble_in(uint32_t st, uint32_t d1, uint32_t d2) { host_ticks += 1000u; ble_app_midi_in(pkt(st, d1, d2), 100, 100); }
static int gate_note(const track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < NVOICE; i++) if (t->v[i].active && t->v[i].gate && t->v[i].note == note) return 1;
    return 0;
}
static int out_has(uint32_t status, uint32_t note)   /* the USB MIDI out queue holds this message */
{
    for (uint32_t i = mo_r; i != mo_w; i++)
        if (((midi_out_q[i % MQ] >> 8) & 0xFFu) == status && ((midi_out_q[i % MQ] >> 16) & 0x7Fu) == note) return 1;
    return 0;
}

static void settings_case(void)
{
    persist_t q = {0};
    uint8_t *f = q.favorites.factory[FX_PREF_ROW] + FX_PREF_BYTE;
    q.magic = PERSIST_MAGIC; q.panel = PANEL_DEFAULT;
    for (uint8_t m = 0; m < BLE_MODES; m++) {
        settings_ble = m; settings_export(&q);
        assert(m ? f[0] == 'B' && f[1] == 'T' && f[2] == 1u && f[3] == m : !f[0] && !f[1] && !f[2] && !f[3]);
        settings_ble = 99; assert(settings_import(&q, sizeof q) && settings_ble == m);
    }
    f[0] = 'F'; f[1] = 'Q'; f[2] = 1; f[3] = 2;           /* a record from before (the retired FX quality) */
    assert(settings_import(&q, sizeof q) && settings_ble == BLE_MODE_OFF);
    f[0] = 'B'; f[1] = 'T'; f[2] = 1; f[3] = 3;           /* a mode this firmware does not know */
    assert(settings_import(&q, sizeof q) && settings_ble == BLE_MODE_OFF);
    f[2] = 2; f[3] = 1;                                   /* another version */
    assert(settings_import(&q, sizeof q) && settings_ble == BLE_MODE_OFF);
    puts("BLE settings: modes saved and loaded, OFF writes the old zeros, older and unknown records are OFF");
}

static void input(void)
{
    reset(); track_t *t = &trk[0];
    ble_in(0x90, 60, 100);
    assert(ble_iw == 1u && mi_w == 0u);                   /* (the BLE interrupt only fills its own ring) */
    ble_midi_drain();
    assert(mi_w == 1u && midi_in_source[0] == 3u && midi_in_q[0] == pkt(0x90, 60, 100));
    assert((int32_t)(midi_in_time[0] - host_ticks) <= 0); /* (the sender's time, never ahead of now) */
    events_block(CTL);
    assert(gate_note(t, 60));
    ble_in(0x80, 60, 0); ble_midi_drain(); events_block(CTL);
    assert(!gate_note(t, 60));
    /* what TRS takes: channel messages, clock and transport; SysEx and the rest are not routed */
    uint32_t w = ble_iw;
    ble_app_midi_in(0x04u | 0xF0u << 8 | 0x7Du << 16 | 0x01u << 24, 100, 100);
    ble_app_midi_in(pkt(0xFE, 0, 0), 100, 100);
    assert(ble_iw == w);
    settings_ble = BLE_MODE_OFF;                          /* (switched OFF: what is still in flight is dropped) */
    ble_in(0x90, 61, 100); assert(ble_iw == w);
    settings_ble = BLE_MODE_KEYBOARD;
    /* clock: counted for GLO > CLK BT, apart from USB's */
    ble_in(0xF8, 0, 0); ble_in(0xF8, 0, 0); ble_midi_drain();
    assert(midi_rx_pulses[2] == 2u && midi_rx_pulses[0] == 0u && midi_rx_pulses[1] == 0u);
    assert(midi_in_pulse[(mi_w - 1u) % MQ] == 2u);
    midi_enqueue(pkt(0xF8, 0, 0), 1u);
    assert(midi_rx_pulses[0] == 1u && midi_rx_pulses[2] == 2u);
    events_block(CTL);
    puts("BLE in: into the router as source 3 with its time, plays, filtered as TRS, clock counted apart");
}

static void full_router(void)
{
    reset();
    mi_w = mi_r + MQ;                                     /* the router's ring full */
    ble_in(0xF8, 0, 0); ble_in(0x90, 62, 90); ble_in(0x80, 62, 0);
    ble_midi_drain(); ble_midi_drain();
    assert(ble_ir == 0u && ble_iw == 3u && !midi_in_overflow);   /* nothing lost, nothing marked lost */
    assert(midi_rx_pulses[2] == 0u);                      /* (a clock waiting is not counted twice) */
    mi_r = mi_w;                                          /* room again: all three, in order */
    ble_midi_drain();
    assert(ble_ir == 3u && mi_w - mi_r == 3u && midi_rx_pulses[2] == 1u);
    assert(midi_in_q[mi_r % MQ] == pkt(0xF8, 0, 0) && midi_in_q[(mi_r + 1u) % MQ] == pkt(0x90, 62, 90) &&
           midi_in_q[(mi_r + 2u) % MQ] == pkt(0x80, 62, 0));
    assert(midi_in_pulse[mi_r % MQ] == 1u);
    events_block(CTL);
    puts("BLE in: a full router waits, keeps the order and counts each clock once");
}

static void release(void)
{
    reset(); track_t *t = &trk[0], *t2 = &trk[1];
    song.ntrk = 2;
    ble_link(1);
    ble_in(0x90, 60, 100); ble_in(0x91, 67, 100); ble_in(0x90, 64, 100); ble_in(0x80, 64, 0);
    ble_midi_drain(); events_block(CTL);
    assert(gate_note(t, 60) && gate_note(t2, 67) && !gate_note(t, 64));
    mi_w = mi_r + MQ - 1u;                                /* room for one note-off only */
    ble_link(0);
    assert(ble_release && !ble_out_on);
    ble_midi_drain();
    assert(ble_release);                                  /* (one sent, one left for the next tick) */
    mi_r = mi_w - 1u;
    ble_midi_drain();
    assert(!ble_release && mi_w - mi_r == 2u);
    events_block(CTL);
    assert(!gate_note(t, 60) && !gate_note(t2, 67));
    ble_midi_drain(); assert(mi_r == mi_w);               /* (nothing more: each note released once) */
    puts("BLE in: a link that drops releases its notes on each channel, also through a full router");
}

static void midi_track_case(void)
{
    reset(); track_t *t = &trk[1];
    set_engine_of(t, ENGI_MIDI); events_block(0); mo_r = mo_w;
    song.g[G_ROUTE] = 1; song.sel = 1;                   /* (ROUT SEL: track 2, the MIDI Track) */
    t->p[P_E0] = 12;
    song.g[G_MIDI] = 0;                                   /* USB chosen: Bluetooth's notes do not reach it */
    ble_in(0x90, 60, 77); ble_midi_drain(); events_block(0);
    assert(!out_has(0x9B, 60));
    ble_in(0x80, 60, 0); ble_midi_drain(); events_block(0); mo_r = mo_w;
    song.g[G_MIDI] = 2;                                   /* BT chosen */
    ble_in(0x90, 61, 77); ble_midi_drain(); events_block(0);
    assert(out_has(0x9B, 61)); mo_r = mo_w;
    midi_enqueue(pkt(0x90, 62, 77), 1u); events_block(0); /* USB's: not while BT is chosen */
    assert(!out_has(0x9B, 62));
    midi_enqueue(pkt(0x80, 62, 0), 1u); events_block(0);
    ble_in(0x80, 61, 0); ble_midi_drain(); events_block(0);
    assert(out_has(0x8B, 61)); mo_r = mo_w;
    song.g[G_MIDI] = 1;                                   /* TRS chosen: as before, neither USB nor BT */
    ble_in(0x90, 63, 77); ble_midi_drain(); events_block(0);
    midi_enqueue(pkt(0x90, 64, 77), 1u); events_block(0);
    assert(!out_has(0x9B, 63) && !out_has(0x9B, 64));
    song.g[G_MIDI] = 0;
    midi_enqueue(pkt(0x90, 65, 77), 1u); events_block(0); /* USB chosen: as before */
    assert(out_has(0x9B, 65));
    seq_stop(); events_block(0); mo_r = mo_w;
    puts("BLE MIDI Track: GLO > SYSTEM MIDI BT takes Bluetooth's input; USB and TRS as before");
}

static void output(void)
{
    uint32_t p, ms;
    reset();
    assert(!ble_midi_out(pkt(0x90, 60, 100)) && ble_ow == 0u);   /* no link: nothing queued */
    usb.config = 1;
    assert(midi_track_emit(pkt(0x90, 60, 100)) && ble_ow == 0u);  /* (USB still gets it) */
    ble_link(1);
    fm1_ms = 1234u;
    assert(midi_track_emit(pkt(0x90, 61, 100)) && ble_ow == 1u);
    usb.config = 0;
    assert(midi_track_emit(pkt(0x80, 61, 0)) && ble_ow == 2u);   /* Bluetooth alone: admitted */
    assert(ble_app_midi_peek(&p, &ms) && p == pkt(0x90, 61, 100) && ms == 1234u);
    ble_app_midi_pop();
    assert(ble_app_midi_peek(&p, &ms) && p == pkt(0x80, 61, 0));
    for (uint32_t i = 0; i < BLE_OQ + 4u; i++) ble_midi_out(pkt(0x90, 70, 1));
    assert(ble_ow - ble_or == BLE_OQ);                   /* bounded */
    ble_link(0);
    assert(ble_or == ble_ow && !ble_app_midi_peek(&p, &ms));   /* dropped with the link */
    assert(!ble_midi_out(pkt(0x90, 60, 100)));
    puts("BLE out: the stream USB gets, only while a link takes MIDI, bounded, dropped with the link");
}

static void start(void)
{
    reset();
    settings_ble = BLE_MODE_OFF; ble_up = 0; ble_mode_run = 0; ble_failed = 0; stub_starts = 0;
    /* a sounding engine over the arena's top: no room, nothing started, the next start then */
    resource_release(RES_BLE);
    uint8_t *top = resource_allocate(RES_ENGINE0, RESOURCE_MAIN_CAPACITY - 64u);
    assert(top && top + RESOURCE_MAIN_CAPACITY - 64u > BLE_RAM_AT);
    ble_midi_set(BLE_MODE_COMPUTER);
    fm1_ms += 100u; ble_midi_poll();
    assert(!stub_starts && !ble_up);                      /* (the knob still turning) */
    fm1_ms += 800u; ble_midi_poll();
    assert(!stub_starts && !ble_up && ble_failed == 1u && settings_ble == BLE_MODE_COMPUTER);
    assert(!strcmp(ui.msg, "BLUETOOTH AT NEXT START") || strstr(ui.msg, "NEXT START"));
    assert(!strcmp(ble_midi_value(), "COMPUTER") && ble_midi_tag() == 0u && !strcmp(ble_midi_status(), "OFF"));
    resource_release(RES_ENGINE0);
    /* room: claimed, the clock set, the radio started once, the mode */
    ble_midi_set(BLE_MODE_KEYBOARD); fm1_ms += 800u; ble_midi_poll();
    assert(stub_starts == 1 && ble_up && stub_mode == BLE_MODE_KEYBOARD && !ble_quiet);
    assert(resource[RES_BLE].ptr == BLE_RAM_AT && resource[RES_BLE].size == BLE_RAM_BYTES);
    assert(host_clock_sets == 1 && host_clock_boots == 1);
    assert((stub_addr[5] & 0xC0u) == 0xC0u);              /* a random static address */
    stub_state = BU_WAITING; ble_midi_poll();
    assert(ble_midi_tag() == 1u && !strcmp(ble_midi_status(), "WAIT"));
    stub_peer = "NUX NEK-100"; stub_state = BU_READY; ble_midi_poll();
    assert(ble_midi_tag() == 2u && !strcmp(ble_midi_status(), "ON") && strstr(ui.msg, "NUX"));
    /* another mode, OFF and back: the radio is not started again */
    ble_midi_set(BLE_MODE_COMPUTER); fm1_ms += 800u; ble_midi_poll();
    ble_midi_set(BLE_MODE_OFF); fm1_ms += 800u; ble_midi_poll();
    assert(stub_mode == BLE_MODE_OFF && ble_midi_tag() == 0u);
    ble_midi_set(BLE_MODE_KEYBOARD); fm1_ms += 800u; ble_midi_poll();
    assert(stub_starts == 1 && stub_mode == BLE_MODE_KEYBOARD && host_clock_sets == 1);
    /* the boot guard: a start that failed leaves the radio off and says so */
    ble_up = 0; ble_mode_run = 0; ble_failed = 0; stub_starts = 0;
    bootguard.failed = 1; ble_midi_init(); bootguard.failed = 0;
    assert(!stub_starts && !strcmp(ble_midi_value(), "HELD"));
    puts("BLE start: claimed only when ON, AT NEXT START without room, one start, modes, header tag, boot guard");
}

int main(void)
{
    settings_case();
    input();
    full_router();
    release();
    midi_track_case();
    output();
    start();
    puts("BLE MIDI TESTS PASSED");
    return 0;
}
