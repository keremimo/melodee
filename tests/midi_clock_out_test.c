/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * CLK INT as MIDI master: clock, Start and Stop on the real output queues,
 * against the real sequencer. */
#undef MELODEE_CLOCK_OUT
#define MELODEE_CLOCK_OUT 1
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static uint32_t pulses, starts, stops, continues, position;
static uint8_t order[8], order_n;

static void out_drain(void)
{
    while (mo_r != mo_w) {
        uint32_t pkt = midi_out_q[mo_r++ % MQ], st = (pkt >> 8) & 0xFFu;
        if (st == 0xF2u) {
            if ((pkt & 0xFu) != 3u) { printf("bad Song Position packet %08x\n", pkt); exit(1); }
            position = ((pkt >> 16) & 127u) | ((pkt >> 24) & 127u) << 7;
            if (order_n < sizeof order) order[order_n++] = (uint8_t)st;
            continue;
        }
        if (st < 0xF8u) continue;                   /* a track's own notes */
        if ((pkt & 0xFu) != 0xFu || pkt >> 16) { printf("bad real-time packet %08x\n", pkt); exit(1); }
        if (st == 0xF8u) pulses++;
        else if (st == 0xFAu) starts++;
        else if (st == 0xFCu) stops++;
        else if (st == 0xFBu) continues++;
        if (st != 0xF8u && order_n < sizeof order) order[order_n++] = (uint8_t)st;
    }
}
static void out_count_reset(void)
{
    out_drain();
    pulses = starts = stops = continues = order_n = 0;
    position = 0xFFFFu;
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

static int gated(const track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < NVOICE; i++) if (t->v[i].active && t->v[i].gate && t->v[i].note == note) return 1;
    return 0;
}
/* Everything that keeps time counts the same units. Exact equality, no tolerance: four divisions, an ARP, a recorded
 * off-grid note and the clock out against each other and against the true tempo, for a long run through tempo changes. */
static int no_drift_test(void)
{
    static const uint8_t DIVS[4] = {2, 1, 3, 5};         /* 1/16, 1/8, 1/32, the 1/16 triplet */
    static const int16_t BPM[] = {97, 120, 137, 173, 40, 240, 101};
    uint32_t period[4], last[4], arp_steps = 0, arp_last = 0, fires = 0, was_on = 0;
    uint32_t bad_tracks = 0, bad_pulses = 0, bad_arp = 0, bad_tempo = 0, bad_late = 0, per = (uint32_t)FS * 60u, bad_lfo = 0;
    uint64_t steps[4] = {0}, ticks;
    int64_t arp_gap = 0; int arp_gap_set = 0;
    out_setup(120);
    song.g[G_SWING] = 0;
    for (uint32_t k = 0; k < 4u; k++) {
        trk[k].p[P_SLEN] = 16; trk[k].p[P_SDIV] = DIVS[k]; trk[k].p[P_SSWING] = 0;
        period[k] = seq_div_samples(DIVS[k]);
    }
    track_t *a = &trk[3];
    a->p[P_AMODE] = 1; a->p[P_ARATE] = 2; a->nheld = 2; a->held[0] = 60; a->held[1] = 64; a->arp_phys = 1;
    recorded_note_t note = {.step = 3, .on = 97, .note7 = 61, .vel = 90, .duration = RECORD_UNIT / 8};
    recorded_set_owner(&note, recording_owner(&trk[0]));
    recording[0] = note;
    trk[0].step[3].time = ST_NOTE; trk[0].step[3].flags |= SF_RECORDED;
    recording_reindex();
    trk[1].p[P_LTRIG] = trk[2].p[P_LTRIG] = 0;
    trk[1].p[P_LSYNC] = 10; trk[2].p[P_LSYNC] = 1;       /* 1/32 (three pulses: never a whole 2^32 share), 4 bars */
    uint32_t loop = 16u * period[0], on = 3u * period[0] + 97u * period[0] / RECORD_UNIT;
    out_blocks(2);
    out_transport(1);
    out_count_reset();
    for (uint32_t k = 0; k < 4u; k++) last[k] = trk[k].seq_idx;
    ticks = int_clock_rem;
    uint32_t lfo_ph0[2] = {trk[1].lfo_ph, trk[2].lfo_ph}, lfo_rem0[2] = {lfo_sync_rem[1], lfo_sync_rem[2]};
    static const uint32_t LFO_X[2] = {3u, 384u};
    for (uint32_t b = 0; b < NELEM(BPM); b++) {
        song.g[G_BPM] = BPM[b];
        uint32_t blocks = 300u * (per / (uint32_t)BPM[b]) / CTL, block_units = CTL * MIDI_BEAT_UNITS * (uint32_t)BPM[b] / per + 1u;
        for (uint32_t i = 0; i < blocks; i++) {
            out_blocks(1);
            ticks += CTL * MIDI_BEAT_UNITS * (uint32_t)BPM[b];
            uint64_t at[4];
            for (uint32_t k = 0; k < 4u; k++) {
                if (trk[k].seq_idx != last[k]) { steps[k]++; last[k] = trk[k].seq_idx; }
                at[k] = steps[k] * period[k] + trk[k].seq_pos;
                if (at[k] != at[0]) bad_tracks++;
            }
            if (at[0] != ticks / per) bad_tempo++;
            if (pulses != at[0] / (MIDI_BEAT_UNITS / 24u) + 1u) bad_pulses++;
            for (uint32_t k = 0; k < 2u; k++) {
                track_lfo_tick(&trk[k + 1u]);
                if (trk[k + 1u].lfo_ph - lfo_ph0[k] != (uint32_t)(((at[0] << 22) + lfo_rem0[k]) / LFO_X[k])) bad_lfo++;
            }
            if (a->arp_note && a->arp_note != arp_last) { arp_steps++; arp_last = a->arp_note; }
            if (arp_steps) {
                int64_t gap = (int64_t)at[0] - (int64_t)((uint64_t)(arp_steps - 1u) * period[0] + a->arp_pos);
                if (!arp_gap_set) { arp_gap = gap; arp_gap_set = 1; }
                if (gap != arp_gap) bad_arp++;
            }
            uint32_t now_on = (uint32_t)gated(&trk[0], 61);
            if (now_on && !was_on) {
                fires++;
                uint32_t pos = (uint32_t)(at[0] % loop);
                if (pos < on || pos - on >= block_units) bad_late++;
            }
            was_on = now_on;
        }
    }
    uint64_t total = steps[0] * period[0] + trk[0].seq_pos;
    int bad = 0;
    bad += check("every division stays on the same unit", !bad_tracks && total > 51000000u);
    bad += check("the song keeps exactly the true tempo, through tempo changes", !bad_tempo);
    bad += check("the clock out stays on the same unit", !bad_pulses && pulses > 50000u);
    bad += check("the ARP never slides", !bad_arp && arp_steps > 8000u);
    bad += check("synced LFOs never slide", !bad_lfo);
    bad += check("a recorded off-grid note fires in its own block on every loop",
                 !bad_late && fires + 1u >= (uint32_t)(total / loop) && fires <= (uint32_t)(total / loop) + 1u);
    return bad;
}

/* A song cursor tells a follower where to start: Song Position and Continue, no Start. */
static int cursor_row(uint32_t len, uint32_t div)
{
    out_setup(120);
    chain_defaults(&chain_config); chain_config.count = 2;
    memset(chain_patterns, 0, sizeof chain_patterns);
    for (uint32_t k = 0; k < NPART; k++) {
        trk[k].p[P_SLEN] = (int16_t)len; trk[k].p[P_SDIV] = (int16_t)div; trk[k].p[P_SSWING] = 0;
        pattern_commit(&trk[k]);
    }
    song.g[G_SWING] = 0;
    out_count_reset();
    return chain_prepare_from(1) == 0;
}
static int cursor_test(void)
{
    int bad = 0;
    bad += check("song cursor prepares", cursor_row(16, 2));
    out_blocks(1);
    bad += check("cursor on a 1/16: Song Position, then Continue, no Start",
                 order_n == 2u && order[0] == 0xF2u && order[1] == 0xFBu && position == 16u && continues == 1u && !starts);
    bad += check("its pulse waits a block", !pulses);
    out_blocks(1);
    bad += check("the next block's pulse is the cursor's 1/16", pulses == 1u && chain.running && trk[0].seq_idx == 0u);
    out_transport(2);
    bad += check("STOP ends it with one Stop", stops == 1u);

    /* Fifteen 1/32 steps end between two 1/16 steps: the follower goes to the next, the pulse waits for the song to get there. */
    bad += check("song cursor prepares between 1/16 steps", cursor_row(15, 3));
    out_blocks(1);
    bad += check("between 1/16 steps: the next one", position == 8u && continues == 1u && !pulses);
    uint32_t early = 0, bad_pulses = 0;
    for (uint32_t i = 0; i < 4000u && !pulses; i++) {
        if (trk[0].seq_idx != 0u) early++;
        out_blocks(1);
    }
    bad += check("the first pulse lands where the song reaches it, on the unit",
                 !early && pulses == 1u && trk[0].seq_idx == 1u && trk[0].seq_pos < 64u);
    for (uint32_t i = 0; i < 600u; i++) {
        out_blocks(1);
        if (pulses != (trk[0].seq_idx * 3072u + trk[0].seq_pos - 3072u) / 1024u + 1u) bad_pulses++;
    }
    bad += check("pulses run on from there, on the same unit", !bad_pulses && pulses > 15u);
    out_transport(2);

    out_setup(120);
    chain_defaults(&chain_config); chain_config.count = 2;
    memset(chain_patterns, 0, sizeof chain_patterns);
    out_count_reset();
    chain_prepare_from(0);
    out_blocks(1);
    bad += check("a song from its top sends plain Start", starts == 1u && !continues && position == 0xFFFFu);
    out_transport(2);
    return bad;
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
    bad += no_drift_test();
    bad += cursor_test();
    bad += source_test();
    bad += queue_test();
    printf("%s\n", bad ? "MIDI CLOCK OUT TEST FAILED" : "MIDI clock out tests passed");
    return bad != 0;
}
