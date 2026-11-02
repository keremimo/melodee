/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * CLK INT as MIDI master: clock, Start and Stop on the real output queues,
 * against the real sequencer. */
#undef MELODEE_CLOCK_OUT
#define MELODEE_CLOCK_OUT 1
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static uint32_t pulses, starts, stops;
static uint8_t order[8], order_n;

static void out_drain(void)
{
    while (mo_r != mo_w) {
        uint32_t pkt = midi_out_q[mo_r++ % MQ], st = (pkt >> 8) & 0xFFu;
        if (st < 0xF8u) continue;                   /* a track's own notes */
        if ((pkt & 0xFu) != 0xFu || pkt >> 16) { printf("bad real-time packet %08x\n", pkt); exit(1); }
        if (st == 0xF8u) pulses++;
        else if (st == 0xFAu) starts++;
        else if (st == 0xFCu) stops++;
        if (st != 0xF8u && order_n < sizeof order) order[order_n++] = (uint8_t)st;
    }
}
static void out_count_reset(void)
{
    out_drain();
    pulses = starts = stops = order_n = 0;
}
static void out_setup(int16_t bpm)
{
    ui_power_on();
    usb.config = 1; mo_r = mo_w = 0;
    settings_countin = 0; song.rec = 0;
    memset(&midi_sync_out, 0, sizeof midi_sync_out);
    song.g[G_CLOCK] = 0; song.g[G_BPM] = bpm;
    trk[0].p[P_SLEN] = 16; trk[0].p[P_SDIV] = 2;
    events_block(CTL);
    out_count_reset();
}
static void out_blocks(uint32_t blocks)
{
    while (blocks--) { events_block(CTL); out_drain(); }
}
static void out_transport(uint32_t req)
{
    transport_req = req;
    events_block(CTL);
    out_drain();
}

static int stopped_test(void)
{
    int bad = 0;
    out_setup(120);
    out_blocks(100u * (FS / 2u) / CTL);             /* 100 beats */
    bad += check("stopped: 24 pulses a beat, for a follower's tempo", pulses >= 2399u && pulses <= 2401u);
    bad += check("stopped: pulses alone, no transport", !starts && !stops);
    out_setup(240);
    out_blocks((FS / 4u) * 40u / CTL);              /* 40 beats */
    bad += check("the top tempo has its 24 pulses a beat too", pulses >= 959u && pulses <= 961u);
    return bad;
}

static int transport_test(void)
{
    int bad = 0;
    out_setup(120);
    out_transport(1);
    bad += check("PLAY sends Start, its pulse waits a block", starts == 1u && !pulses && !stops);
    out_blocks(1);
    bad += check("the next block's pulse is step 0", pulses == 1u && trk[0].seq_idx == 0u);
    out_blocks(FS / 2u / CTL);
    bad += check("Start goes once", starts == 1u && !stops);
    out_transport(2);
    bad += check("STOP sends one Stop", stops == 1u && starts == 1u);
    out_transport(2); out_blocks(4);
    bad += check("a second STOP sends nothing", stops == 1u);
    uint32_t before = pulses;
    out_blocks(FS / 2u / CTL);
    bad += check("pulses go on after Stop", pulses > before);

    out_setup(120);
    out_transport(1); out_blocks(100); out_count_reset();
    out_transport(3);
    bad += check("GLO + PLAY: Stop, then Start, in that order",
                 order_n == 2u && order[0] == 0xFCu && order[1] == 0xFAu);

    out_setup(120);
    settings_countin = 1; song.rec = 1;
    out_transport(1);
    bad += check("a count-in sends no Start", cin_left && !starts && !song.playing);
    for (uint32_t i = 0; i < FS * 4u / CTL && !song.playing; i++) out_blocks(1);
    bad += check("Start comes with the song, not the count-in", song.playing && starts == 1u && pulses > 80u);
    settings_countin = 0; song.rec = 0;
    return bad;
}

/* The period of a step is rounded down from the beat (137 BPM: 1 sample a beat, 3 at worst): pulses must not slide. */
static int lock_test(int16_t bpm)
{
    uint32_t bad_pulses = 0, last = 0, worst = 0;
    out_setup(bpm);
    out_transport(1);
    for (uint32_t i = 0; i < 1000u * ((uint32_t)FS * 60u / (uint32_t)bpm) / CTL; i++) {
        out_blocks(1);
        if (pulses != last && (pulses - 1u) % 6u == 0u) {
            if (trk[0].seq_idx != (pulses - 1u) / 6u % 16u) bad_pulses++;
            if (trk[0].seq_pos > worst) worst = trk[0].seq_pos;   /* samples into the step, at its pulse */
        }
        last = pulses;
    }
    return check("six pulses stay on the 1/16 step, within two blocks, over 1000 beats",
                 !bad_pulses && worst <= 2u * CTL && pulses > 23000u);
}

static int source_test(void)
{
    int bad = 0;
    out_setup(120);
    out_transport(1); out_blocks(40); out_count_reset();
    song.g[G_CLOCK] = 1;
    out_blocks(1);
    bad += check("leaving INT tells the follower: one Stop", stops == 1u && !song.playing);
    out_count_reset();
    midi_enqueue(0xFu | 0xFAu << 8, 1u);
    midi_enqueue(0xFu | 0xF8u << 8, 1u);
    out_blocks(FS / CTL);
    bad += check("CLK USB sends nothing, even while it follows", !pulses && !starts && !stops && song.playing);
    song.g[G_CLOCK] = 0;
    out_blocks(FS / CTL);
    bad += check("back on INT the pulses return", pulses >= 47u && !starts);
    return bad;
}

static int queue_test(void)
{
    int bad = 0;
    out_setup(240);
    for (uint32_t i = 0; i < FS * 2u / CTL; i++) events_block(CTL);    /* a port nobody reads */
    bad += check("pulses keep half the USB queue free", mo_w - mo_r == MQ / 2u);
    bad += check("notes are still admitted", midi_track_emit(0x9u | 0x90u << 8 | 60u << 16 | 100u << 24) == 1);
    transport_req = 1; events_block(CTL);
    bad += check("Start is still admitted", ((midi_out_q[(mo_w - 1u) % MQ] >> 8) & 0xFFu) == 0xFAu);
    out_setup(120);
    usb.config = 0;
    uint32_t w = mo_w;
    out_blocks(FS / CTL);
    bad += check("no host: nothing queued", mo_w == w);
    return bad;
}

int main(void)
{
    int bad = stopped_test();
    bad += transport_test();
    bad += lock_test(137);
    bad += lock_test(97);
    bad += source_test();
    bad += queue_test();
    printf("%s\n", bad ? "MIDI CLOCK OUT TEST FAILED" : "MIDI clock out tests passed");
    return bad != 0;
}
