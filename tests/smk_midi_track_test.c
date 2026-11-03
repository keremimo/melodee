/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SMK-37: a MIDI Track at the TRS jack, whatever USB and BLE are doing. The real track paths, the jack's bytes taken
 * from its ring as the tick would send them: with no computer a note ends; a USB or BLE that is gone or full neither
 * keeps the jack waiting nor gets it duplicates, and still gets its own release later; a full ring never costs a
 * release; TRS MIDI OUT OFF sends nothing. Built with and without BLE (-DTRS_BLE). */
#ifdef TRS_BLE
#define MELODEE_BLE_TEST_NO_MAIN 1
#include "melodee_ble_test.c"
#else
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#endif

static int fails;
static void expect(int ok, const char *what)
{
    printf("smk_midi_track: %-78s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static uint8_t jack[4096];
static uint32_t jack_n;
static void jack_take(void)
{
    while (smo.r != smo.w && jack_n < sizeof jack) jack[jack_n++] = smo.ring[smo.r++ % SMO_RING];
}
static int jack_is(const uint8_t *want, uint32_t n)
{
    jack_take();
    return jack_n == n && !memcmp(jack, want, n);
}
#define JACK(...) ({ static const uint8_t want_[] = {__VA_ARGS__}; jack_is(want_, sizeof want_); })
static uint32_t jack_count(uint32_t status, uint32_t note)
{
    uint32_t found = 0;
    jack_take();
    for (uint32_t i = 0; i < jack_n; i += jack[i] >= 0xF8u ? 1u : 3u)
        found += jack[i] == status && jack[i + 1u] == note;
    return found;
}
static void flushes(void)
{
    for (uint32_t i = 0; i < 50u; i++) { events_block(0); jack_take(); }
}
static uint32_t usb_held(void) { return mo_w - mo_r; }
static int usb_next(uint32_t status, uint32_t note)
{
    if (mo_r == mo_w) return 0;
    uint32_t pkt = midi_out_q[mo_r++ % MQ];
    return ((pkt >> 8) & 255u) == status && ((pkt >> 16) & 127u) == note;
}
#ifdef TRS_BLE
static int ble_next(uint32_t status, uint32_t note)
{
    uint32_t pkt, ms;
    if (!ble_app_midi_peek(&pkt, &ms)) return 0;
    ble_app_midi_pop();
    return ((pkt >> 8) & 255u) == status && ((pkt >> 16) & 127u) == note;
}
static int ble_quiet(void) { uint32_t pkt, ms; return !ble_app_midi_peek(&pkt, &ms); }
#endif

/* every track's notes ended everywhere, no computer, an empty jack */
static track_t *fresh(void)
{
#ifdef TRS_BLE
    mb.tr = mb.tw; mb.ready = 0;
#endif
    usb.config = 1; mo_r = mo_w = 0;
    for (uint32_t i = 0; i < NTRK; i++) trk_all_off(&trk[i]);
    events_block(0);
    ui_power_on();
    track_solo = 0;
    mi_r = mi_w = 0; midi_in_overflow = 0;
    set_engine_of(&trk[0], ENGI_MIDI);
    events_block(0);
    usb.config = 0;
    mo_r = mo_w = 0;
    trs_out = 1; smo.dead = 0; smo.r = smo.w; smo.drops = 0;
    jack_n = 0;
    return &trk[0];
}

static void no_computer(void)
{
    track_t *t = fresh();
    trk_note_on(t, 60, 100);
    expect(JACK(0x90, 60, 100), "no computer: the note reaches the jack");
    trk_note_off(t, 60);
    expect(JACK(0x90, 60, 100, 0x80, 60, 0), "no computer: its release too");
    flushes();
    expect(JACK(0x90, 60, 100, 0x80, 60, 0), "no computer: and only once");
    expect(!midi_track_pending(0), "no computer: nothing is left waiting");

    t = fresh();
    trk_note_on(t, 60, 100); trk_note_on(t, 64, 90);
    trk_all_off(t);
    expect(JACK(0x90, 60, 100, 0x90, 64, 90, 0x80, 60, 0, 0x80, 64, 0), "no computer: all off ends what sounds");

    t = fresh();
    trk_note_on(t, 60, 100); trk_note_on(t, 60, 90);
    trk_note_off(t, 60);
    expect(JACK(0x90, 60, 100, 0x80, 60, 0, 0x90, 60, 90, 0x80, 60, 0), "no computer: a retrigger finishes the first strike");

    t = fresh();
    trk_note_on(t, 60, 100);
    t->p[P_E0] = 7; events_block(0);
    trk_note_on(t, 61, 100); trk_note_off(t, 61);
    expect(JACK(0x90, 60, 100, 0x80, 60, 0, 0x96, 61, 100, 0x86, 61, 0), "no computer: a channel change ends the note on its old channel");
    t->p[P_E0] = 1; events_block(0);

    t = fresh();
    trk_note_on(t, 60, 100);
    t->p[P_MUTE] = 1; events_block(0); flushes();
    t->p[P_MUTE] = 0;
    expect(JACK(0x90, 60, 100, 0x80, 60, 0), "no computer: mute ends the note");

    t = fresh();
    midi_event(0xB0, 0, 64, 127); midi_event(0x90, 0, 60, 97); midi_event(0x80, 0, 60, 0);
    expect(JACK(0xB0, 64, 127, 0x90, 60, 97), "no computer: a pedal holds the note");
    seq_stop(); flushes();
    expect(JACK(0xB0, 64, 127, 0x90, 60, 97, 0xB0, 64, 0, 0x80, 60, 0), "no computer: stop lifts the pedal and ends the note");
}

static void usb_gone(void)
{
    track_t *t = fresh();
    usb.config = 1;
    trk_note_on(t, 60, 100);
    expect(usb_next(0x90, 60) && JACK(0x90, 60, 100), "USB unplugged: the note went both ways");
    usb.config = 0;
    trk_note_off(t, 60);
    flushes();
    expect(JACK(0x90, 60, 100, 0x80, 60, 0), "USB unplugged: the jack gets the release once, however many retries");
    expect(midi_track_pending(0), "USB unplugged: USB's release stays owed");
    trk_note_on(t, 62, 100); trk_note_off(t, 62);
    expect(jack_count(0x90, 62) == 1u && jack_count(0x80, 62) == 1u, "USB unplugged: the jack keeps playing");
    usb.config = 1; flushes();
    expect(usb_next(0x80, 60) && !usb_held(), "USB back: its release, once, and nothing it never heard");
    expect(jack_count(0x80, 60) == 1u, "USB back: nothing more for the jack");
    expect(!midi_track_pending(0), "USB back: nothing is left waiting");

    /* the channel moves on while USB still owes the old one */
    t = fresh();
    usb.config = 1;
    trk_note_on(t, 60, 100); mo_r = mo_w;
    usb.config = 0;
    t->p[P_E0] = 7; events_block(0);
    trk_note_on(t, 61, 100); flushes();
    expect(jack_count(0x80, 60) == 1u && jack_count(0x96, 61) == 1u && !jack_count(0x86, 61),
          "USB unplugged, channel changed: the jack's new note keeps sounding");
    usb.config = 1; flushes();
    expect(usb_next(0x80, 60), "USB back: its release on the channel it was struck on");
    expect(!jack_count(0x86, 61), "USB back: the jack's note still sounds");
    trk_note_off(t, 61);
    expect(jack_count(0x86, 61) == 1u, "USB back: the jack's note ends on its own channel");
    t->p[P_E0] = 1; events_block(0);
}

static void usb_full(void)
{
    track_t *t = fresh();
    usb.config = 1; mo_w = mo_r + MQ;
    trk_note_on(t, 60, 100);
    expect(JACK(0x90, 60, 100), "USB full: the note still reaches the jack");
    trk_note_off(t, 60); flushes();
    expect(JACK(0x90, 60, 100, 0x80, 60, 0), "USB full: its release reaches the jack, once");
    mo_r = mo_w; flushes();
    expect(!usb_held() && !midi_track_pending(0), "USB free: no release of a note it never heard");

    t = fresh();
    usb.config = 1;
    trk_note_on(t, 60, 100); mo_r = mo_w;
    mo_w = mo_r + MQ;
    trk_note_off(t, 60); flushes();
    expect(JACK(0x90, 60, 100, 0x80, 60, 0), "USB stalls after the note: the jack gets the release, once");
    expect(midi_track_pending(0), "USB stalls after the note: USB's release stays owed");
    mo_r = mo_w; flushes();
    expect(usb_next(0x80, 60) && !usb_held() && !midi_track_pending(0), "USB free: its release, once");
    expect(jack_count(0x80, 60) == 1u, "USB free: nothing more for the jack");
}

static void ring_full(void)
{
    /* nothing drains: as many notes as the ring admits, then their releases */
    track_t *t = fresh();
    for (uint32_t n = 0; n < 128u; n++) trk_note_on(t, n, 100);
    uint32_t held = (smo.w - smo.r) / 3u;
    expect(held >= 40u, "ring: at least 40 notes admitted at once");
    trk_all_off(t);
    jack_take();
    expect(jack_n == held * 6u, "ring: no computer, every admitted note gets its release");

    t = fresh();
    usb.config = 1;
    for (uint32_t n = 0; n < MQ; n++) trk_note_on(t, n, 100);
    held = (smo.w - smo.r) / 3u;
    trk_all_off(t);
    jack_take();
    expect(jack_n == held * 6u, "ring: USB too, every admitted note gets its release");

    t = fresh();
    for (uint32_t n = 0; n < 10u; n++) trk_note_on(t, n, 100);
    for (uint32_t i = 0; i < 2u * SMO_RING; i++) { midi_sync_emit(0xF8u); rt_r = rt_w; }
    trk_all_off(t);
    uint32_t offs = 0;
    for (uint32_t n = 0; n < 10u; n++) offs += jack_count(0x80, n);
    expect(offs == 10u, "ring: a clock cannot eat the room of a release");

    t = fresh();
    for (uint32_t i = 0; i < 85u; i++) smo_put(0x09u | 0x90u << 8 | 1u << 16 | 1u << 24);
    expect(smo.w - smo.r == 255u, "ring: everything released, its whole room is back");
}

static void jack_off_or_dead(void)
{
    track_t *t = fresh();
    usb.config = 1; trs_out = 0;
    trk_note_on(t, 60, 100); trk_note_off(t, 60); flushes();
    expect(usb_next(0x90, 60) && usb_next(0x80, 60) && !jack_n && smo.r == smo.w, "TRS MIDI OUT OFF: USB as ever, nothing at the jack");

    t = fresh();
    trs_out = 0; trk_note_on(t, 60, 100);
    trs_out = 1; trk_note_off(t, 60); flushes();
    expect(!jack_n && !smo.drops, "struck while OFF: no release at the jack");

    t = fresh();
    trk_note_on(t, 60, 100); jack_take();
    trs_out = 0; trk_note_off(t, 60); flushes();
    trs_out = 1;
    for (uint32_t i = 0; i < 85u; i++) smo_put(0x09u | 0x90u << 8 | 1u << 16 | 1u << 24);
    expect(smo.w - smo.r == 255u, "released while OFF: its room is back");

    t = fresh();
    trk_note_on(t, 60, 100); jack_take();
    smo.dead = 1;
    trk_note_off(t, 60); flushes();
    smo.dead = 0;
    for (uint32_t i = 0; i < 85u; i++) smo_put(0x09u | 0x90u << 8 | 1u << 16 | 1u << 24);
    expect(smo.w - smo.r == 255u, "released with no UART: its room is back");
}

static void other_streams(void)
{
    fresh();
    midi_out_event(0x09u | 0x90u << 8 | 60u << 16 | 100u << 24);
    midi_out_event(0x08u | 0x80u << 8 | 60u << 16);
    midi_sync_emit(0xF8u); rt_r = rt_w;
    expect(JACK(0x90, 60, 100, 0x80, 60, 0, 0xF8), "instrument notes and the clock still reach the jack with no computer");
    usb.config = 1;
    midi_out_event(0x09u | 0x90u << 8 | 61u << 16 | 100u << 24);
    expect(usb_next(0x90, 61) && jack_count(0x90, 61) == 1u, "instrument notes go to USB and the jack, once each");
}

#ifdef TRS_BLE
static void ble(void)
{
    track_t *t = fresh();
    mb.ready = 1;
    trk_note_on(t, 60, 100);
    expect(ble_next(0x90, 60) && JACK(0x90, 60, 100), "BLE: the note goes both ways");
    trk_note_off(t, 60);
    expect(ble_next(0x80, 60) && JACK(0x90, 60, 100, 0x80, 60, 0) && ble_quiet(), "BLE: the release too, once each");

    t = fresh();
    mb.ready = 1; mb.tw = mb.tr + MBQ;
    trk_note_on(t, 60, 100);
    expect(JACK(0x90, 60, 100), "BLE full: the note still reaches the jack");
    trk_note_off(t, 60); flushes();
    expect(JACK(0x90, 60, 100, 0x80, 60, 0), "BLE full: its release reaches the jack, once");
    mb.tr = mb.tw; flushes();
    expect(ble_quiet() && !midi_track_pending(0), "BLE free: no release of a note it never heard");

    t = fresh();
    mb.ready = 1;
    trk_note_on(t, 60, 100);
    expect(ble_next(0x90, 60), "BLE stalls after the note: the note went out");
    mb.tw = mb.tr + MBQ;
    trk_note_off(t, 60); flushes();
    expect(JACK(0x90, 60, 100, 0x80, 60, 0) && midi_track_pending(0), "BLE stalls after the note: the jack once, BLE's release owed");
    mb.tr = mb.tw; flushes();
    expect(ble_next(0x80, 60) && ble_quiet() && !midi_track_pending(0), "BLE free: its release, once");
    expect(jack_count(0x80, 60) == 1u, "BLE free: nothing more for the jack");

    t = fresh();
    mb.ready = 1;
    trk_note_on(t, 60, 100); expect(ble_next(0x90, 60), "BLE lost after the note: the note went out");
    mb.ready = 0;
    trk_note_off(t, 60); flushes();
    trk_note_on(t, 62, 100); trk_note_off(t, 62);
    expect(jack_count(0x80, 60) == 1u && jack_count(0x90, 62) == 1u && jack_count(0x80, 62) == 1u, "BLE lost: the jack keeps playing");
    mb.ready = 1; flushes();
    expect(ble_next(0x80, 60) && ble_quiet() && !midi_track_pending(0), "BLE back: its release, once");
}
#endif

/* a microtonal track: the jack gets what USB and BLE get, the semitone nearest each note, and keeps its own books */
static void microtonal(void)
{
    uint32_t edo24 = 0, edo53 = 0;
    for (uint32_t i = 0; i < SCALE_TOTAL; i++) {
        if (!strcmp(N_SCALE[i], "24EDO")) edo24 = i;
        if (!strcmp(N_SCALE[i], "53EDO")) edo53 = i;
    }
    track_t *t = fresh();
    t->p[P_SCALE] = (int16_t)edo24; t->p[P_QUANT] = Q_ALL;
    trk_note_on(t, 84, 100);                            /* 24 degrees up: an octave, not two */
    expect(JACK(0x90, 72, 100), "microtonal, no computer: the nearest semitone, not the address");
    t->p[P_ROOT] = 5;
    trk_note_off(t, 84);
    t->p[P_ROOT] = 0;
    expect(JACK(0x90, 72, 100, 0x80, 72, 0), "microtonal, no computer: the release goes where the press went, ROOT moved");
    flushes();
    expect(JACK(0x90, 72, 100, 0x80, 72, 0) && !midi_sent_n, "microtonal, no computer: only once, nothing kept");

    t = fresh();
    t->p[P_SCALE] = (int16_t)edo53; t->p[P_QUANT] = Q_ALL;
    trk_note_on(t, 60, 100); trk_note_on(t, 61, 90);    /* 23 cents apart, one semitone */
    expect(JACK(0x90, 60, 100, 0x80, 60, 0, 0x90, 60, 90), "microtonal: a shared semitone is struck again");
    trk_note_off(t, 60);
    expect(JACK(0x90, 60, 100, 0x80, 60, 0, 0x90, 60, 90), "microtonal: it sounds while the other degree is held");
    trk_note_off(t, 61);
    expect(JACK(0x90, 60, 100, 0x80, 60, 0, 0x90, 60, 90, 0x80, 60, 0) && !midi_sent_n, "microtonal: the last release ends it");

    t = fresh();
    t->p[P_SCALE] = (int16_t)edo24; t->p[P_QUANT] = Q_ALL;
    trk_note_on(t, 84, 100); trk_note_on(t, 62, 100);
    trk_all_off(t);
    expect(JACK(0x90, 72, 100, 0x90, 61, 100, 0x80, 61, 0, 0x80, 72, 0) && !midi_sent_n, "microtonal: all off ends the semitones, nothing kept");

    t = fresh();                                         /* a computer too: both get the semitone, once each */
    usb.config = 1;
    t->p[P_SCALE] = (int16_t)edo24; t->p[P_QUANT] = Q_ALL;
    trk_note_on(t, 84, 100);
    expect(usb_next(0x90, 72) && usb_held() == 0 && midi_sent_n == 2u, "microtonal, USB up: the same semitone, an entry each");
    trk_note_off(t, 84);
    expect(usb_next(0x80, 72) && usb_held() == 0 && !midi_sent_n, "microtonal, USB up: the release there too");
    expect(JACK(0x90, 72, 100, 0x80, 72, 0), "microtonal, USB up: the jack's once");
    usb.config = 0;

    t = fresh();                                         /* USB refused the press: the jack's release still comes, USB owes none */
    t->p[P_SCALE] = (int16_t)edo24; t->p[P_QUANT] = Q_ALL;
    trk_note_on(t, 84, 100);
    usb.config = 1;
    trk_note_off(t, 84);
    flushes();
    expect(JACK(0x90, 72, 100, 0x80, 72, 0) && usb_held() == 0 && !midi_sent_n, "microtonal: a press USB never took owes it no release");
    usb.config = 0;
    t->p[P_SCALE] = 1; t->p[P_QUANT] = Q_OFF;
}

int main(void)
{
    ui_power_on();
#ifdef TRS_BLE
    reset_ble();
#endif
    no_computer(); usb_gone(); usb_full(); ring_full(); jack_off_or_dead(); other_streams(); microtonal();
#ifdef TRS_BLE
    ble();
#endif
    printf("smk_midi_track: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
