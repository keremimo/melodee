/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Jacek Mariański
 * Portions (ble_in_time, the held notes): Optimist's io/midi/midi_ble.c, GPL-3.0-only, github.com/w0ts/optimist */
/* Bluetooth MIDI (MELODEE_BLE): the menu's BLUETOOTH, on Optimist's open BLE stack (src/ble_unit.c, its own unit
 * ble.o: see there for where its RAM lives).
 *   OFF       the radio is never started (nothing of it runs, its RAM stays the audio arena's)
 *   COMPUTER  advertising as "Melodee XXXX" (XXXX from the address): a Mac (Audio MIDI Setup), an iPhone or iPad app
 *             connects; no pairing
 *   KEYBOARD  connecting out to the first BLE-MIDI keyboard or dongle heard (a NUX NEK-100's, a CME, a WIDI), the
 *             one connected before first while it is around, again whenever the link goes
 * Saved with the settings (settings_persist.c). The first time it is ON (at boot when saved, else from the menu) the
 * stack's RAM is claimed at the audio arena's top and the radio started: the clock as M-VAVE's (360 MHz; under
 * m-bootloader it is not, and the radio's calibration then fails), then the radio's start-up with its trims (the
 * factory VM's if the unit still has them, else measured: hal/ble/fm1_rf_cal.h), about a second; the audio is
 * silent meanwhile (audio.c ble_quiet). From then on OFF only stops advertising / scanning and ends the link: the
 * radio stays started until power-off. A start-up that hangs: the watchdog resets, the boot guard counts it, and the
 * next boot leaves the radio off (the setting kept, the menu can switch it OFF).
 *   in   the stack decodes BLE-MIDI into USB-MIDI packets in the BLE interrupts (ble_app_midi_in) into ble_in_q;
 *        TIMER5 moves them into the router (usb.c midi_enqueue, source 3), timed by the sender's timestamps
 *   out  the stream USB gets (ble_midi.h ble_midi_out from usb.c midi_track_emit), packed into notifications at the
 *        next connection event, with BLE-MIDI timestamps */
static void clock_boot(void);                   /* (main.c) */

/* ---- ble.o (src/ble_unit.c) */
void ble_unit_attach(void);
void ble_unit_irqs_hold(int hold);
int ble_unit_start(const uint8_t addr[6], int (*rd)(uint32_t off, uint8_t *dst, uint32_t n));
void ble_unit_mode(int mode);
void ble_unit_poll(uint32_t now);
int ble_unit_state(void);
const char *ble_unit_peer(void);
int ble_midi_ready(void);
int ble_connected(void);
extern uint8_t _ble_ram_start[], _ble_ram_end[];
enum { BU_OFF, BU_WAITING, BU_LINKED, BU_READY };
enum { BLE_MODE_OFF, BLE_MODE_COMPUTER, BLE_MODE_KEYBOARD, BLE_MODES };
static const char *const BLE_MODE_NAME[BLE_MODES] = {"OFF", "COMPUTER", "KEYBOARD"};

static uint8_t ble_up;                          /* the stack's RAM claimed and the radio started (once per boot) */
static uint8_t ble_mode_run;                    /* the mode the stack runs */
static uint8_t ble_failed;                      /* 1 no room in the arena (the next boot), 2 the boot guard held it */
static uint8_t ble_state;                       /* ble_unit_state, the main loop's copy (the header's BT tag) */

/* ---- in: the BLE interrupts -> ble_in_q -> TIMER5 -> the router */
#define BLE_IQ 32u
static uint32_t ble_in_q[BLE_IQ], ble_in_t[BLE_IQ];
static volatile uint32_t ble_iw, ble_ir;
static uint8_t ble_held[16][16];                /* notes a link has on (bit per note, per channel), TIMER5's */
static volatile uint8_t ble_release;            /* the link is gone: end those notes (ble_midi_drain) */
static uint8_t ble_was_conn;

void ble_app_state(void)                        /* the BLE interrupts (or the main loop with them held) */
{
    uint8_t c = (uint8_t)ble_connected();
    ble_out_on = (uint8_t)ble_midi_ready();
    if (!ble_out_on)
        ble_or = ble_ow;                        /* nothing waits for a link that does not listen */
    if (ble_was_conn && !c)
        ble_release = 1;
    ble_was_conn = c;
}

int ble_app_midi_peek(uint32_t *pkt, uint32_t *t_ms)
{
    if (ble_or == ble_ow)
        return 0;
    RING_PUBLISH();
    *pkt = ble_out_q[ble_or % BLE_OQ];
    *t_ms = ble_out_ms[ble_or % BLE_OQ];
    return 1;
}

void ble_app_midi_pop(void)
{
    RING_PUBLISH();
    ble_or++;
}

/* a message's time on our clock (TIMER4): the sender's millisecond (ts, 13 bits) unwrapped, plus the offset of the
 * least delayed message lately, drifting up 1/4096 of the time since; reset after 4 s quiet, a beat of late ones, or
 * one ahead of now (Optimist's midi_ble.c, where the reasons are told) */
#define BIN_MS (1000u * FM1_TICKS_PER_US)
static struct {
    uint32_t s, o, at;
    uint16_t ts;
    uint8_t ok, late;
} bin;
static uint32_t ble_in_time(uint16_t ts, uint16_t last_ts)
{
    uint32_t now = fm1_ticks(), arr, t, rise;
    uint32_t back = (uint32_t)(uint16_t)(last_ts - ts) & 0x1FFFu;
    arr = now - back * BIN_MS;
    if (bin.ok && now - bin.at < 4000u * BIN_MS)
        bin.s += (uint32_t)(uint16_t)(ts - bin.ts) & 0x1FFFu;
    else
        bin.s = 0, bin.o = arr, bin.ok = 1;
    rise = (now - bin.at) >> 12;
    bin.ts = ts;
    bin.at = now;
    t = arr - bin.s * BIN_MS;
    if ((int32_t)(t - (bin.o + rise)) < 0)
        bin.o = t;
    else
        bin.o += rise;
    t = bin.o + bin.s * BIN_MS;
    bin.late = (int32_t)(now - t) > (int32_t)(100u * BIN_MS) ? (uint8_t)(bin.late + 1u) : 0u;
    if (bin.late >= 24u || (int32_t)(t - now) > 0) {
        bin.s = 0;
        bin.o = arr;
        bin.late = 0;
        t = arr;
    }
    return t;
}

void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    int clock = (cin == 0xFu && (st == 0xF8u || (st >= 0xFAu && st <= 0xFCu))) || (cin == 3u && st == 0xF2u);
    if (!settings_ble || ((cin < 8u || cin > 0xEu) && !clock) || ble_iw - ble_ir >= BLE_IQ)
        return;                                 /* (channel messages, clock and transport: as TRS) */
    ble_in_q[ble_iw % BLE_IQ] = pkt;
    ble_in_t[ble_iw % BLE_IQ] = ble_in_time(ts, last_ts);
    RING_PUBLISH();
    ble_iw++;
}

static void ble_note_seen(uint32_t pkt)
{
    uint32_t st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 15u, n = (pkt >> 16) & 0x7Fu;
    if (st == 0x90u && ((pkt >> 24) & 0x7Fu))
        ble_held[ch][n >> 3] |= (uint8_t)(1u << (n & 7u));
    else if (st == 0x80u || st == 0x90u)
        ble_held[ch][n >> 3] &= (uint8_t)~(1u << (n & 7u));
}

static int ble_enqueue(uint32_t pkt, uint32_t t)  /* usb.c midi_enqueue, with the message's own time */
{
    uint32_t at = mi_w % MQ;
    if (midi_in_overflow || mi_w - mi_r >= MQ)
        return 0;
    midi_in_q[at] = pkt;
    midi_in_ms[at] = fm1_ms;
    midi_in_time[at] = t;
    midi_in_source[at] = 3u;                    /* (seq.c: not USB's, so a USB host session ending keeps it) */
    midi_in_pulse[at] = midi_rx_pulses[2];
    RING_PUBLISH();
    mi_w++;
    return 1;
}

static void ble_midi_drain(void)                /* TIMER5, 2 kHz: BLE in -> the router */
{
    while (ble_ir != ble_iw) {
        uint32_t pkt;
        RING_PUBLISH();
        pkt = ble_in_q[ble_ir % BLE_IQ];
        if (((pkt >> 8) & 0xFFu) == 0xF8u)
            midi_rx_pulses[2]++;                /* (GLO > CLK BT's count) */
        if (!ble_enqueue(pkt, ble_in_t[ble_ir % BLE_IQ])) {
            if (((pkt >> 8) & 0xFFu) == 0xF8u)
                midi_rx_pulses[2]--;            /* (counted again the next tick) */
            return;                             /* the router's ring is full: the next tick */
        }
        ble_note_seen(pkt);
        ble_rx_pkts++;
        RING_PUBLISH();
        ble_ir++;
    }
    if (ble_release) {                          /* a note-off for each note the link left on */
        uint32_t ch, n, left = 0;
        for (ch = 0; ch < 16u; ch++)
            for (n = 0; n < 128u; n++) {
                if (!(ble_held[ch][n >> 3] & (1u << (n & 7u))))
                    continue;
                if (ble_enqueue(8u | (0x80u | ch) << 8 | n << 16, fm1_ticks()))
                    ble_held[ch][n >> 3] &= (uint8_t)~(1u << (n & 7u));
                else
                    left = 1;
            }
        if (!left)
            ble_release = 0;
    }
}

/* ---- the start */
static int ble_vm_read(uint32_t off, uint8_t *dst, uint32_t n)   /* the factory VM (SPI reads, no XIP decryption) */
{
    return flash_ok ? st_read(off, dst, n) : -1;
}

/* a random static address that is the same at every start: from the flash part's unique ID (FNV-1a), so a computer
 * finds the same device again without anything stored */
static void ble_addr(uint8_t a[6])
{
    uint8_t uid[16];
    uint32_t h0 = 2166136261u, h1 = 0x6D2B79F5u, i, f;
    memset(uid, 0, sizeof uid);
    f = irq_save();
    FL_FAR(fl_uid_ram)(uid);
    irq_restore(f);
    for (i = 0; i < 16u; i++) {
        h0 = (h0 ^ uid[i]) * 16777619u;
        h1 = (h1 ^ uid[15u - i]) * 16777619u;
    }
    a[0] = (uint8_t)h0, a[1] = (uint8_t)(h0 >> 8), a[2] = (uint8_t)(h0 >> 16), a[3] = (uint8_t)(h0 >> 24);
    a[4] = (uint8_t)h1;
    a[5] = (uint8_t)((h1 >> 8) | 0xC0u);        /* random static: the top two bits 11 */
}

/* the stack's RAM and the radio, the first time ON. boot: IRQs still off (fm1_main, before they are on) -> 1 up */
static int ble_start(int boot)
{
    uint8_t a[6];
    uint32_t f;
    if (ble_up)
        return 1;
    f = pattern_guard();
    if (!resource_claim(RES_BLE, _ble_ram_start, (uint32_t)(_ble_ram_end - _ble_ram_start))) {
        pattern_unguard(f);
        ble_failed = 1;                         /* sounding engines hold that memory: at the next boot, first */
        return 0;
    }
    pattern_unguard(f);
    ble_addr(a);
    if (!boot) {
        ble_quiet = 1;                          /* (the next half: silence, nothing on CPU1) */
        fm1_delay_ms(10);
    }
    fm1_wdt_feed();
    f = irq_save();
    (void)fm1_clock_stock();                    /* (RAM code, IRQs off: the flash's clock moves under it) */
    irq_restore(f);                             /* (at boot icfg's global enable is still off: fm1_main sets it) */
    clock_boot();                               /* the voice budget at 360 MHz */
    ble_unit_irqs_hold(1);
    (void)ble_unit_start(a, ble_vm_read);       /* (about a second: the radio's calibration) */
    fm1_wdt_feed();
    ble_quiet = 0;
    ble_up = 1;
    return 1;
}

static void ble_apply(int boot)                 /* settings_ble into the stack */
{
    if (settings_ble == ble_mode_run)
        return;
    if (settings_ble && !ble_up && !ble_start(boot))
        return;
    if (!ble_up)
        return;
    ble_unit_mode(settings_ble);
    ble_mode_run = settings_ble;
    if (!settings_ble)
        ble_release = 1;
}

/* fm1_main: after the settings, before anything takes the audio arena (the stack's RAM first, when ON was saved) */
static void ble_midi_claim(void)
{
    if (settings_ble && !bootguard.failed)
        (void)resource_claim(RES_BLE, _ble_ram_start, (uint32_t)(_ble_ram_end - _ble_ram_start));
}

/* fm1_main: after the audio and USB are set up, IRQs still off */
static void ble_midi_init(void)
{
    ble_unit_attach();                          /* the two vectors, masked */
    if (!settings_ble)
        return;
    if (bootguard.failed) {                     /* (a crash or hang in a boot's first 30 s: maybe the radio's) */
        ble_failed = 2;
        return;
    }
    ble_apply(1);
}

/* the menu's BLUETOOTH: KNOB 1 steps, applied once it rests (ble_midi_poll: a start-up per step would be a second
 * of silence each) */
static uint8_t ble_set_pending;
static uint32_t ble_set_t;
static void ble_midi_set(uint8_t mode)
{
    settings_ble = mode < BLE_MODES ? mode : 0u;
    ble_set_pending = 1;
    ble_set_t = fm1_ms;
}

static void ble_midi_poll(void)                 /* main loop */
{
    uint8_t s;
    if (ble_set_pending && fm1_ms - ble_set_t > 700u) {
        ble_set_pending = 0;
        settings_save();
        ble_failed = 0;                         /* (chosen again: HELD tries once more) */
        ble_apply(0);
        if (ble_failed == 1)
            ui_say("BLUETOOTH ", "AT NEXT START");   /* (saved: the stack's RAM is claimed before the sounds then) */
    }
    if (!ble_up)
        return;
    ble_unit_poll(fm1_ms);
    s = (uint8_t)ble_unit_state();
    if (s != ble_state) {
        if (s == BU_READY)
            ui_say("BLUETOOTH ", ble_unit_peer()[0] ? ble_unit_peer() : "CONNECTED");
        else if (ble_state == BU_READY)
            ui_say("BLUETOOTH ", "DISCONNECTED");
        ble_state = s;
    }
}

/* the menu row's value */
static const char *ble_midi_value(void)
{
    if (ble_failed == 2)
        return "HELD";                          /* (the boot guard: switch it OFF, or restart) */
    return BLE_MODE_NAME[settings_ble < BLE_MODES ? settings_ble : 0u];
}

static const char *ble_midi_status(void)        /* GLO > SYSTEM's MIDI column with BT chosen */
{
    if (!ble_up || !settings_ble)
        return "OFF";
    return ble_state == BU_READY ? "ON" : "WAIT";
}

static uint8_t ble_midi_tag(void)
{
    if (!ble_up || !settings_ble)
        return 0;
    return ble_state == BU_READY ? 2u : 1u;
}
