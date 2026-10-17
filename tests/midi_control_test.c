/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * End-to-end channel input and external-clock tests using the real queues,
 * UART parser, ownership model, sequencer, voice renderer and UI source. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void midi_test_reset(void)
{
    ui_power_on(); set_engine_of(&trk[0], HOST_SYNTH);   /* (the generic bend: the Prophet has its own range) */
    memset(midi_ch, 0, sizeof midi_ch); memset(midi_sel_on, 0, sizeof midi_sel_on);
    memset(midi_owners, 0, sizeof midi_owners); memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target); memset(&midi_clock, 0, sizeof midi_clock);midi_sync_active=0;
    memset(&um, 0, sizeof um); memset(midi_in_source, 0, sizeof midi_in_source);
    mi_r = mi_w = 0; midi_in_overflow = 0; midi_beat_samples = 0;
    fm1_in.notes = kb_prev = 0; fm1_ms = 0; song.sel = 0;
    events_block(CTL);
}
static void queued(uint32_t st, uint32_t d1, uint32_t d2, uint32_t source)
{
    midi_enqueue((st >= 0xF8u ? 0xFu : st >> 4) | st << 8 | d1 << 16 | d2 << 24, source);
    events_block(CTL);
}
static int gate_note(const track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < NVOICE; i++) if (t->v[i].active && t->v[i].gate && t->v[i].note == note) return 1;
    return 0;
}
static int controls_test(void)
{
    int bad = 0; midi_test_reset(); track_t *t = &trk[0];
    queued(0xE0, 127, 127, 1);
    bad += check("USB maximum bend is exactly the default +2 semitones", midi_bend_target[0] == 512);
    queued(0xE0, 0, 0, 1);
    bad += check("USB minimum bend is exactly the default -2 semitones", midi_bend_target[0] == -512);
    queued(0xE0, 0, 64, 1);
    bad += check("bend centre is zero", midi_bend_target[0] == 0);
    queued(0xB0, 101, 0, 1); queued(0xB0, 100, 0, 1); queued(0xB0, 6, 12, 1); queued(0xB0, 38, 50, 1);
    queued(0xE0, 127, 127, 1);
    bad += check("RPN0 supports semitone and cent bend sensitivity", midi_ch[0].semis == 12 && midi_ch[0].cents == 50 && midi_bend_target[0] == 3200);
    queued(0xB0, 99, 0, 1); queued(0xB0, 6, 24, 1);
    bad += check("selecting NRPN cancels RPN data entry", midi_ch[0].semis == 12);
    queued(0x90, 60, 100, 1); int32_t out[CTL]; int32_t before = midi_bend_q8[0];
    track_render(t, out, CTL);
    bad += check("bend is smoothed toward the target in the real voice renderer", midi_bend_q8[0] > before && midi_bend_q8[0] < midi_bend_target[0]);
    for (uint32_t i = 0; i < 64; i++) track_render(t, out, CTL);
    bad += check("bend smoothing reaches the exact target without changing saved pitch", midi_bend_q8[0] == 3200 && t->p[P_TRANS] == 0 && t->v[0].note == 60);
    queued(0xB0, 121, 0, 1);
    bad += check("Reset Controllers centres bend and preserves RPN sensitivity", !midi_bend_target[0] && midi_ch[0].semis == 12 && gate_note(t, 60));
    queued(0xB0, 1, 87, 1); queued(0xB0, 11, 23, 1); queued(0xD0, 90, 0, 1);
    bad += check("existing modwheel/expression/aftertouch stay routed to the track", t->mw == 87 && t->ex_off == 104 && t->at == 90);
    queued(0xE3, 127, 127, 1);
    bad += check("drum track ignores pitch bend", !midi_bend_target[3]);
    return bad;
}
static int sustain_test(void)
{
    int bad = 0; midi_test_reset(); track_t *t = &trk[0];
    queued(0x90, 60, 100, 1); queued(0xB0, 64, 127, 1); queued(0x80, 60, 0, 1);
    bad += check("pedal holds a released synth note and its owner", gate_note(t, 60) && mn_get(0, 60) == (1u | MIDI_PEDAL_NOTE));
    queued(0xB0, 64, 0, 1);
    bad += check("pedal release releases its held note", !gate_note(t, 60) && !mn_get(0, 60) && !midi_owners[0]);
    queued(0x90, 62, 100, 1); queued(0xB0, 64, 127, 1); queued(0xB0, 123, 0, 1);
    bad += check("All Notes Off honours sustain rather than hard-killing", gate_note(t, 62) && (mn_get(0, 62) & MIDI_PEDAL_NOTE));
    queued(0xB0, 121, 0, 1);
    bad += check("Reset Controllers releases pedal-held notes", !gate_note(t, 62) && !mn_get(0, 62));
    queued(0x93, 36, 100, 1); queued(0xB3, 64, 127, 1); queued(0x83, 36, 0, 1);
    bad += check("drum note-offs do not accumulate pedal-held owners", !mn_get(3, 36) && !midi_owners[3]);
    queued(0x90, 67, 100, 1); queued(0xB0, 64, 127, 1); queued(0x80, 67, 0, 1); queued(0xB0, 120, 0, 1);
    bad += check("All Sound Off ignores pedal and discards all track ownership", !mn_get(0, 67) && !midi_owners[0] && !t->nheld && !gate_note(t, 67));
    return bad;
}
static int ownership_test(void)
{
    int bad = 0; midi_test_reset(); song.g[G_ROUTE] = 1; track_t *t = &trk[0];
    queued(0x94, 60, 100, 1); queued(0x95, 60, 100, 2);
    bad += check("USB/TRS channels can share one sounding note with independent owners", midi_owners[0] == 2u && gate_note(t, 60));
    queued(0x84, 60, 0, 1);
    bad += check("one channel's note-off preserves another channel's hold", midi_owners[0] == 1u && gate_note(t, 60));
    queued(0x85, 60, 0, 2);
    bad += check("last channel owner releases the shared note", !midi_owners[0] && !gate_note(t, 60));
    fm1_in.notes = 1u; events_block(CTL); uint32_t local = kb_note[0];
    queued(0x94, local, 100, 1); queued(0x84, local, 0, 1);
    bad += check("MIDI release cannot cut a still-held local keyboard note", gate_note(t, local));
    fm1_in.notes = 0; events_block(CTL);
    bad += check("local release after MIDI release ends the shared note", !gate_note(t, local));
    queued(0x94, 65, 100, 1); song.sel = 1; queued(0x84, 65, 0, 1);
    bad += check("note-off follows its note-on across selected-track changes", !gate_note(&trk[0], 65) && !mn_get(4, 65));
    song.sel = 0; queued(0x94, 67, 100, 1); queued(0xB4, 64, 127, 1); queued(0x84, 67, 0, 1);
    panic_req |= 1u; events_block(CTL);
    queued(0x90, 67, 100, 1); queued(0xB4, 64, 0, 1);
    bad += check("preset panic forgets old pedal owners so later pedal-up preserves new notes", !mn_get(4, 67) && gate_note(&trk[0], 67));
    for (uint32_t i = 0; i <= MQ; i++) midi_enqueue(0x09u | 0x90u << 8 | 70u << 16 | 100u << 24, 1);
    events_block(CTL);
    bad += check("queue overflow recovers ownership and releases stuck notes", !midi_in_overflow && mi_r == mi_w && !midi_owners[0] && !gate_note(&trk[0], 67));
    return bad;
}
static void clock_setup(uint32_t mode)
{
    midi_test_reset(); song.g[G_CLOCK] = (int16_t)mode;
    trk[0].p[P_SLEN] = 16; trk[0].p[P_SDIV] = 2;
    for (uint32_t i = 0; i < 16; i++) trk[0].step[i] = (step_t){{(uint8_t)(60 + i)}, 1, ST_NOTE, 0, 100};
    events_block(CTL);
}
static void clock_packet(uint32_t source, uint32_t status, uint32_t ms)
{
    fm1_ms = ms;
    if (source == 2u) { um_byte(status); events_block(CTL); }
    else queued(status, 0, 0, source);
}
static void clock_to(uint32_t source, uint32_t start, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) clock_packet(source, 0xF8, start + (i * 125u) / 6u);
}
static int clock_test(uint32_t source)
{
    int bad = 0; clock_setup(source);
    clock_packet(source == 1u ? 2u : 1u, 0xFA, 1);
    bad += check(source == 1 ? "USB mode ignores TRS Start" : "TRS mode ignores USB Start", !song.playing);
    clock_packet(source, 0xFA, 2);
    bad += check(source == 1 ? "USB Start begins at step zero" : "TRS Start begins at step zero", song.playing && trk[0].seq_idx == 0u);
    clock_to(source, 2, 7);
    bad += check(source == 1 ? "USB six pulses advance exactly one 16th step" : "TRS six pulses advance exactly one 16th step", trk[0].seq_idx == 1u && song.g[G_BPM] == 120);
    fm1_ms = 132; events_block(CTL); uint16_t idx = trk[0].seq_idx; uint32_t pos = trk[0].seq_pos;
    clock_packet(source, 0xFC, 133);
    bad += check("external Stop advances to its timestamp and releases sequence notes", !song.playing && trk[0].seq_idx == idx && trk[0].seq_pos > pos && !trk[0].seq_n);
    pos = trk[0].seq_pos;
    clock_packet(source, 0xFB, 150);
    bad += check("external Continue resumes the existing step position", song.playing && trk[0].seq_idx == idx && trk[0].seq_pos == pos);
    clock_packet(source, 0xF8, 151); fm1_ms = 672; events_block(CTL);
    bad += check("lost clock times out and releases transport notes", !song.playing && !trk[0].seq_n);
    clock_packet(source, 0xFA, 700); clock_to(source, 700, 7);
    bad += check("Start after timeout restarts cleanly at the master's phase", song.playing && trk[0].seq_idx == 1u);
    /* Original base values remain after MIDI Stop even while motion sounds. */
    trk[0].p[P_LEVEL] = 23; motion_clear(&trk[0]); motion_set_event(&trk[0], 0, P_LEVEL, 100);
    clock_packet(source, 0xFA, 1000); clock_packet(source, 0xF8, 1000);
    bad += check("external-clock playback applies recorded motion", trk[0].p[P_LEVEL] == 100);
    clock_packet(source, 0xFC, 1010);
    bad += check("external Stop restores the musical parameter base", trk[0].p[P_LEVEL] == 23);
    return bad;
}
static int clock_arp_and_boundaries(void)
{
    int bad = 0; clock_setup(1); trk[0].p[P_AMODE] = 1; trk[0].p[P_ARATE] = 2;
    queued(0x90, 60, 100, 1); queued(0x90, 64, 100, 1);
    clock_packet(1, 0xFA, 10); clock_to(1, 10, 1); uint32_t before = trk[0].arp_idx;
    clock_to(2, 20, 7);
    bad += check("wrong-source clocks cannot advance the ARP", trk[0].arp_idx == before);
    clock_to(1, 10, 7);
    bad += check("selected-source MIDI clock advances the ARP", trk[0].arp_idx > before);
    clock_setup(2); fm1_ms = 100; um_byte(0x90); um_byte(60); um_byte(0xFA); um_byte(100); events_block(CTL);
    bad += check("TRS realtime interleaving preserves the incomplete note message", gate_note(&trk[0], 60) && song.playing);
    /* Millisecond counter wraps naturally during external-clock operation. */
    clock_setup(1); clock_packet(1, 0xFA, 0xFFFFFFF0u); clock_packet(1, 0xF8, 0xFFFFFFF0u);
    clock_packet(1, 0xF8, 5u); fm1_ms = 10; events_block(CTL);
    bad += check("external clock timestamps survive uint32 millisecond wrap", song.playing && midi_clock.last_ms == 5u);
    clock_setup(1);transport_req=1;events_block(CTL);uint32_t local=trk[0].seq_pos;
    clock_packet(1,0xF8,10);clock_packet(2,0xFA,20);
    bad+=check("clock pulses and wrong-source transport cannot take over local PLAY",song.playing&&!midi_sync_active&&trk[0].seq_pos>local&&!midi_beat_samples);
    fm1_ms=501;events_block(CTL);
    bad += check("local PLAY runs internally before MIDI transport is received",song.playing&&!midi_sync_active);
    clock_packet(1,0xFC,502);
    bad+=check("selected-source Stop arms sync and stops locally started playback",!song.playing&&midi_sync_active==1u);
    transport_req=1;events_block(CTL);fm1_ms=1003;events_block(CTL);
    bad+=check("after external transport claims playback, missing clocks still time out",!song.playing);
    return bad;
}
/* ARP HOLD latched on an external clock: a Stop, a lost clock or a tap while stopped must not leave the arp note
 * sounding; Continue plays the latched chord again; the internal clock's free-running arp is unchanged by STOP */
static void arp_latch(uint32_t mode)
{
    midi_test_reset(); song.g[G_CLOCK] = (int16_t)mode; events_block(CTL);
    trk[0].p[P_AMODE] = 1; trk[0].p[P_AHOLD] = 1; trk[0].p[P_ARATE] = 2; trk[0].p[P_AGATE] = 120; trk[0].p[P_AOCT] = 2;
    events_block(CTL);
}
static uint32_t arp_ext_run(uint32_t ms)       /* Start, tap 60 (latched), clocks until the arp plays 72 */
{
    uint32_t i;
    clock_packet(1, 0xFA, ms); queued(0x90, 60, 100, 1); queued(0x80, 60, 0, 1);
    for (i = 0; i < 400 && trk[0].arp_note != 72; i++) clock_packet(1, 0xF8, ms + i * 21u);
    return ms + i * 21u;
}
static void idle_ms(uint32_t ms)               /* about ms of audio blocks, no MIDI */
{
    uint32_t i, ms0 = fm1_ms;
    for (i = 0; i < ms * 23u / 16u; i++) { fm1_ms = ms0 + i * 16u / 23u; events_block(CTL); }
}
static int arp_ext_stop_test(void)
{
    int bad = 0; uint32_t ms, i, off;
    arp_latch(1); ms = arp_ext_run(10);
    bad += check("ARP HOLD on the external clock plays the upper octave", trk[0].arp_note == 72 && gate_note(&trk[0], 72));
    clock_packet(1, 0xFC, ms + 5); idle_ms(2000);
    bad += check("external Stop ends the sounding ARP note", !trk[0].arp_note && !gate_note(&trk[0], 72) && !gate_note(&trk[0], 60));
    bad += check("external Stop keeps the latched HOLD chord", trk[0].nheld == 1u && trk[0].held[0] == 60u);
    ms = fm1_ms; clock_packet(1, 0xFB, ms);
    for (i = 1; i < 40 && !trk[0].arp_note; i++) clock_packet(1, 0xF8, ms + i * 21u);
    bad += check("Continue plays the latched chord again", trk[0].arp_note != 0);
    arp_latch(1); arp_ext_run(10); idle_ms(2000);
    bad += check("a lost external clock stops the transport and ends the ARP note", !song.playing && !trk[0].arp_note && !gate_note(&trk[0], 72));
    arp_latch(1); queued(0x90, 64, 100, 1); queued(0x80, 64, 0, 1);
    bad += check("a tap while the external transport is stopped sounds", trk[0].arp_note == 64);
    idle_ms(2000);
    bad += check("its ARP gate still ends in real time", !trk[0].arp_note && !gate_note(&trk[0], 64));
    arp_latch(0); transport_req = 1; events_block(CTL); queued(0x90, 60, 100, 1); queued(0x80, 60, 0, 1);
    for (i = 0; i < 4000 && trk[0].arp_note != 72; i++) events_block(CTL);
    off = trk[0].arp_off;
    transport_req = 2; events_block(CTL);
    bad += check("internal clock: STOP leaves the free-running ARP note to its gate", trk[0].arp_note == 72 && gate_note(&trk[0], 72) && trk[0].arp_off == off - CTL);
    for (i = 0; i < 4000 && (!trk[0].arp_note || trk[0].arp_note == 72); i++) events_block(CTL);
    bad += check("internal clock: the ARP keeps stepping after STOP", trk[0].arp_note && trk[0].arp_note != 72 && trk[0].nheld == 1u);
    return bad;
}
/* USB-MIDI back-pressure: full 64-byte bulk packets (16 events) faster than the audio ISR drains the ring are
 * left in the endpoint (NAK) instead of overflowing it, which would panic every sounding note */
static int usb_burst_test(void)
{
    int bad = 0; uint8_t pkt[64]; uint32_t e, k, taken = 0;
    midi_test_reset();
    queued(0x90, 48, 100, 1);
    for (e = 0; e < 16; e++) {                       /* CC7 = 100 on 16 channels */
        pkt[4 * e] = 0x0B; pkt[4 * e + 1] = (uint8_t)(0xB0 | e); pkt[4 * e + 2] = 7; pkt[4 * e + 3] = 100;
    }
    for (k = 0; k < 5; k++)                          /* 5 polls at 2 kHz before the next half drains */
        taken += (uint32_t)ep1_take(pkt, sizeof pkt);
    bad += check("a USB burst fills the MIDI ring without overflowing it", taken == 3u && !midi_in_overflow && mi_w - mi_r == 48u);
    bad += check("the packet that does not fit is refused (left for the next poll)", !ep1_take(pkt, sizeof pkt) && mi_w - mi_r == 48u);
    for (e = 0; e < 8; e++)                          /* TRS MIDI meanwhile: its 8 slots are free */
        midi_in_event(0x0Bu | 0xB1u << 8 | 1u << 16 | e << 24);
    bad += check("TRS MIDI into a ring full of USB still fits (no overflow)", !midi_in_overflow && mi_w - mi_r == 56u);
    events_block(CTL);
    bad += check("the ring drains with no panic: the held note keeps sounding", gate_note(&trk[0], 48) && mi_r == mi_w);
    bad += check("the refused packet is taken once the ring has room", ep1_take(pkt, sizeof pkt) && mi_w - mi_r == 16u);
    events_block(CTL);
    bad += check("the held note survives all 88 events", gate_note(&trk[0], 48) && !midi_in_overflow);
    for (k = 0; k < 3; k++) ep1_take(pkt, sizeof pkt);
    midi_in_event(0x09u | 0x90u << 8 | 50u << 16 | 100u << 24);   /* one more event: 49 used, 15 free */
    bad += check("free space below one packet refuses a whole packet", !ep1_take(pkt, sizeof pkt) && !midi_in_overflow);
    events_block(CTL);
    return bad;
}
/* Exercise the physical-byte parser and USB receiver before the shared event queue. */
static void route_input(uint32_t source, uint32_t st, uint32_t d1, uint32_t d2)
{
    if (source == 2u) {
        um_byte(st); um_byte(d1);
        if ((st & 0xE0u) != 0xC0u) um_byte(d2);
    } else
        midi_in_event((st >> 4) | st << 8 | d1 << 16 | d2 << 24);
    events_block(CTL);
}

static int routing_range_test(void)
{
    int bad = 0;
    static const uint8_t routes[] = {0, 2, 3, 4};
    for (uint32_t source = 1; source <= 2; source++) {
        uint32_t ok = 1;
        for (uint32_t r = 0; r < 4; r++) {
            for (uint32_t ch = 0; ch < 16; ch++) {
                midi_test_reset(); settings_drumch = 0;
                for (uint32_t t = 0; t < NPART; t++) set_engine_of(&trk[t], HOST_SYNTH);
                song.sel = 2; song.g[G_ROUTE] = routes[r];
                route_input(source, 0x90u | ch, 60, 100);
                int target = ch >= r * 4u && ch < r * 4u + 4u ? (int)(ch - r * 4u) : -1;
                ok &= mn_get(ch, 60) == (uint32_t)(target + 1);
                for (uint32_t t = 0; t < NPART; t++) ok &= gate_note(&trk[t], 60) == ((int)t == target);
                if (target < 0) {
                    int32_t level = trk[2].p[P_LEVEL];
                    uint32_t mw = trk[2].mw, at = trk[2].at;
                    route_input(source, 0xB0u | ch, 7, 0);
                    route_input(source, 0xB0u | ch, 1, 100);
                    route_input(source, 0xD0u | ch, 100, 0);
                    route_input(source, 0xE0u | ch, 127, 127);
                    route_input(source, 0xB0u | ch, 120, 0);
                    ok &= trk[2].p[P_LEVEL] == level && trk[2].mw == mw && trk[2].at == at && !midi_bend_target[2];
                }
                /* Velocity-zero note-ons are releases too. */
                route_input(source, 0x90u | ch, 60, 0);
                ok &= !mn_get(ch, 60);
                for (uint32_t t = 0; t < NPART; t++) ok &= !gate_note(&trk[t], 60);
            }
        }
        bad += check(source == 1 ? "USB: every channel in all four ranges maps or is filtered, including controllers" :
                                  "TRS: every channel in all four ranges maps or is filtered, including controllers", ok);
        midi_test_reset(); settings_drumch = 0; song.g[G_ROUTE] = 1;
        ok = 1;
        for (uint32_t ch = 0; ch < 16; ch++) {
            route_input(source, 0x90u | ch, 60, 100);
            ok &= mn_get(ch, 60) == 1u && gate_note(&trk[0], 60);
            route_input(source, 0x80u | ch, 60, 0);
        }
        bad += check("SEL still accepts every channel", ok && !gate_note(&trk[0], 60));

        midi_test_reset(); settings_drumch = 0;
        route_input(source, 0x90, 62, 100);
        song.g[G_ROUTE] = 2; song.sel = 1;
        route_input(source, 0x80, 62, 0);
        bad += check("note-off follows its owner after switching to a range excluding its channel",
                     !mn_get(0, 62) && !gate_note(&trk[0], 62));
        song.g[G_ROUTE] = 0;
        route_input(source, 0x90, 64, 100); route_input(source, 0xB0, 64, 127); route_input(source, 0x80, 64, 0);
        song.g[G_ROUTE] = 4;
        route_input(source, 0x90, 65, 100); route_input(source, 0xB0, 64, 0);
        bad += check("excluded channel cannot start new notes while pedal-up still releases its original note",
                     !mn_get(0, 64) && !mn_get(0, 65) && !gate_note(&trk[0], 64));
        song.g[G_ROUTE] = 0;
        route_input(source, 0x90, 67, 100); song.g[G_ROUTE] = 2;
        route_input(source, 0xB0, 120, 0);
        bad += check("excluded channel panic clears its original owner", !midi_owners[0] && !gate_note(&trk[0], 67));
        song.g[G_ROUTE] = 0;
        route_input(source, 0xB0, 64, 127);
        song.g[G_ROUTE] = 2; route_input(source, 0xB0, 64, 0);
        song.g[G_ROUTE] = 0;
        route_input(source, 0x90, 69, 100); route_input(source, 0x80, 69, 0);
        bad += check("pedal-up while excluded and without owners prevents stale sustain on re-entry",
                     !mn_get(0, 69) && !gate_note(&trk[0], 69));
        song.g[G_ROUTE] = 0; song.rec = 1u << 1;
        route_input(source, 0x9D, 70, 100); route_input(source, 0x8D, 70, 0);
        bad += check("channel 14 cannot start an armed take in CH1-4", !song.playing && !mn_get(13, 70) && !trk[1].step[0].n);
    }
    return bad;
}

/* DRUM overrides destinations only inside the selected range. Without a kit, normal routing applies. */
static int drum_channel_test(void)
{
    int bad = 0;
    midi_test_reset();
    settings_drumch = 10;
    set_engine_of(&trk[3], HOST_SYNTH);
    set_engine_of(&trk[2], ENGI_DRUM);
    song.g[G_ROUTE] = 0;
    queued(0x99, 36, 100, 1);
    bad += check("DRUM 10 cannot bypass CH1-4", !midi_owners[2] && !midi_track(9));
    song.g[G_ROUTE] = 3;
    queued(0x99, 36, 100, 1);
    bad += check("DRUM 10 within CH9-12 plays the first DRUM track", trk[2].v[0].active && mn_get(9, 36) == 3u);
    song.sel = 1; song.g[G_ROUTE] = 0;
    queued(0x89, 36, 0, 1);
    bad += check("DRUM note-off follows its owner after changing range", !midi_owners[2]);
    song.g[G_ROUTE] = 1;
    bad += check("ROUT SEL: channel 10 still the DRUM track", midi_track(9) == &trk[2] && midi_track(4) == &trk[1]);
    set_engine_of(&trk[2], HOST_SYNTH);
    bad += check("SEL without a DRUM track uses selection", midi_track(9) == &trk[1]);
    song.g[G_ROUTE] = 3;
    bad += check("range without a DRUM track uses its normal channel offset", midi_track(9) == &trk[1]);
    set_engine_of(&trk[3], ENGI_DRUM);
    settings_drumch = 2; song.g[G_ROUTE] = 0;
    bad += check("DRUM 2 overrides track 2 inside CH1-4, channel 10 stays filtered",
                 midi_track(1) == &trk[3] && !midi_track(9) && midi_track(0) == &trk[0]);
    settings_drumch = 0;
    bad += check("DRUM OFF: channel 2 track 2 again", midi_track(1) == &trk[1]);
    return bad;
}

static int drum_retrigger_accounting(void)
{
    midi_test_reset();track_t *t=&trk[0];int32_t out[CTL*2];int bad=0;
    int16_t previous_budget=settings_budget;settings_budget=32;
    set_engine_of(t,ENGI_DRUM);apply_preset_to(t,0);t->engine=t->eng_req;t->p[P_VOICE]=V_POLY;song.g[G_ROUTE]=0;settings_drumch=0;
    midi_note_event(0,36,110);midi_note_event(0,38,110);mix_block(out,CTL);
    capacity_steals=0;
    for(uint32_t repeat=0;repeat<100;repeat++){
        midi_note_event(0,36,110);midi_note_event(0,38,110);mix_block(out,CTL);
    }
    bad+=check("same-pitch MIDI drum retriggers reuse circuits without capacity steals",capacity_steals==0);
    uint32_t before=capacity_steals;midi_note_event(0,35,110);
    bad+=check("replacing another drum pitch on its lane still counts",capacity_steals==before+1u);
    midi_test_reset();set_engine_of(t,ENGI_CZ);apply_preset_to(t,0);t->engine=t->eng_req;t->p[P_VOICE]=V_POLY;settings_budget=8;
    capacity_steals=0;
    for(uint32_t note=48;note<60;note++){trk_note_on(t,note,100);mix_block(out,CTL);}
    bad+=check("actual shared-budget pressure still counts capacity steals",capacity_steals>0);
    settings_budget=previous_budget;return bad;
}

#ifndef MIDI_CONTROL_NO_MAIN
int main(void)
{
    int bad = controls_test() + sustain_test() + ownership_test() + clock_test(1) + clock_test(2) + clock_arp_and_boundaries() +
              arp_ext_stop_test() + usb_burst_test() + routing_range_test() + drum_channel_test() + drum_retrigger_accounting();
    printf("%s\n", bad ? "MIDI CONTROL/CLOCK TEST FAILED" : "MIDI control/clock integration tests passed"); return bad != 0;
}
#endif
