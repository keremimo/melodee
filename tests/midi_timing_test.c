/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Timestamped TRS/USB recording through the real queue/parser and sequencer.
 * Use the firmware's 24 MHz clock, including its natural uint32 wrap. */
#include <stdint.h>
static uint32_t timing_now;
#define MIDI_TIME_NOW() timing_now
#define MIDI_TICKS_PER_MS 24000u
#define MIDI_CONTROL_NO_MAIN 1
#include "midi_control_test.c"

static void timing_reset(uint32_t source, uint32_t start)
{
    midi_render_timed = 0;
    timing_now = 0;
    midi_test_reset();
    song.g[G_CLOCK] = (int16_t)source;
    events_block(CTL);
    timing_now = start;
    song.rec = 1;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_SLEN] = 16;
    trk[0].p[P_SDIV] = 2;
    trk[0].p[P_SSWING] = song.g[G_SWING] = 0;
    memset(trk[0].step, 0, sizeof trk[0].step);
    for (uint32_t i = 0; i < NSTEP; i++) trk[0].step[i].time = ST_REST;
}
static void timing_packet(uint32_t time, uint32_t st, uint32_t note, uint32_t vel, uint32_t source)
{
    timing_now = time;
    fm1_ms = time / MIDI_TICKS_PER_MS;
    if (source == 2) {
        um_byte(st);
        if (st < 0xF8) { um_byte(note); um_byte(vel); }
    } else
        midi_enqueue((st >= 0xF8 ? 0xF : st >> 4) | st << 8 | note << 16 | vel << 24, source);
}
static int recorded_grid(uint32_t bpm, uint32_t source, uint32_t early, uint32_t burst)
{
    uint32_t start = 24000000u, bad = 0;
    timing_reset(source, start);
    timing_packet(start, 0xFA, 0, 0, source);
    for (uint32_t pulse = 0; pulse < 96; pulse++) {
        uint32_t at = start + (uint32_t)((uint64_t)pulse * 60000u * MIDI_TICKS_PER_MS / (bpm * 24u));
        /* Real MIDI serialization allows notes on either side of their clock.
         * The initial FM-1 tempo is 120 regardless of the master's BPM. */
        if (early && pulse && pulse % 6 == 0)
            timing_packet(at - MIDI_TICKS_PER_MS / 4u, 0x90, 60 + pulse / 6u, 100, source);
        timing_packet(at, 0xF8, 0, 0, source);
        if (pulse % 6 == 0) {
            if (!early || !pulse)
                timing_packet(at + MIDI_TICKS_PER_MS, 0x90, 60 + pulse / 6u, 100, source);
            timing_packet(at + 2u * MIDI_TICKS_PER_MS, 0x80, 60 + pulse / 6u, 0, source);
        }
        if (!burst || pulse % 3 == 2) events_block(CTL);
    }
    events_block(CTL);
    for (uint32_t i = 0; i < 16; i++)
        if (trk[0].step[i].time != ST_NOTE || trk[0].step[i].n != 1 || trk[0].step[i].note[0] != 60 + i)
            bad++;
    return bad;
}
static int grid_test(void)
{
    uint32_t bpm[] = {40, 73, 97, 120, 137, 173, 240};
    int bad = 0;
    for (uint32_t source = 1; source <= 2; source++)
        for (uint32_t early = 0; early < 2; early++)
            for (uint32_t burst = 0; burst < 2; burst++) {
                int errors = 0;
                for (uint32_t i = 0; i < NELEM(bpm); i++) errors += recorded_grid(bpm[i], source, early, burst);
                char label[100];
                snprintf(label, sizeof label, "%s quantized 16ths, %s clock, %s drain, 40..240 BPM", source == 2 ? "TRS" : "USB",
                         early ? "note before" : "note after", burst ? "batched" : "immediate");
                bad += check(label, !errors);
            }
    return bad;
}
static int timeline_test(void)
{
    int bad = 0;
    uint32_t start = 24000000u, interval = 500000u;
    timing_reset(2, start);
    timing_packet(start, 0xFA, 0, 0, 2);
    timing_packet(start, 0xF8, 0, 0, 2);
    midi_render_timed = 1;
    midi_render_time = start;
    events_block(CTL);
    uint32_t cursor = mi_r;
    timing_packet(start + 12000u, 0x90, 60, 100, 2);
    events_block(CTL);
    bad += check("a future MIDI event waits for its audio block", mi_r == cursor && !midi_owners[0]);
    midi_render_time = start + 24000u;
    events_block(CTL);
    bad += check("the event sounds on the first block reaching its timestamp", mi_r == mi_w && gate_note(&trk[0], 60));
    uint32_t previous = trk[0].seq_pos;
    int smooth = 1;
    /* CPU/wall time stays fixed while four audio blocks cover 128 samples. */
    for (uint32_t b = 1; b <= 4; b++) {
        midi_render_time = start + 24000u + b * CTL * 544u;
        events_block(CTL);
        uint32_t step = trk[0].seq_pos - previous;
        if (step < 34 || step > 37) smooth = 0;
        previous = trk[0].seq_pos;
    }
    bad += check("external phase advances evenly across all four audio blocks", smooth);
    midi_render_timed = 0;
    timing_reset(2, 0xFFF00000u);
    start = timing_now;
    timing_packet(start, 0xFA, 0, 0, 2); timing_packet(start, 0xF8, 0, 0, 2); events_block(CTL);
    for (uint32_t p = 1; p <= 6; p++) {
        timing_packet(start + p * interval, 0xF8, 0, 0, 2);
        /* fm1_ms has its own, independent wrap period. */
        fm1_ms = 1000u + p * 21u;
        midi_in_ms[(mi_w - 1u) % MQ] = fm1_ms;
        events_block(CTL);
    }
    bad += check("24 MHz timer wrap preserves the exact six-clock step boundary", trk[0].seq_idx == 1 && trk[0].seq_pos == 0);
    timing_reset(2, 24000000u);
    timing_packet(24000000u, 0xFA, 0, 0, 2); events_block(0);
    trk[0].seq_pos = 3072;
    trk[0].arp_pos = 2048;
    song.g[G_CLOCK] = 0;
    events_block(0);
    bad += check("switching to INT preserves fractional sequence and ARP position",
                 trk[0].seq_pos == 3072u && trk[0].arp_pos == 2048u);
    return bad;
}
static int phase_test(void)
{
    int bad = 0;
    uint32_t start = 24000000u, time = start;
    timing_reset(2, start);
    timing_packet(start, 0xFA, 0, 0, 2); timing_packet(start, 0xF8, 0, 0, 2); events_block(CTL);
    int exact = 1;
    for (uint32_t pulse = 1; pulse <= 24u * 128u; pulse++) {
        /* Tempo switches and +/- 0.2 ms polling jitter cannot move musical phase. */
        uint32_t bpm = pulse < 192 ? 97 : pulse < 384 ? 173 : 137;
        time += 60000u * MIDI_TICKS_PER_MS / (bpm * 24u);
        uint32_t jitter = pulse % 2 ? 4800 : 0;
        timing_packet(time + jitter, 0xF8, 0, 0, 2); events_block(CTL);
        if (pulse % 6 == 0 && (trk[0].seq_idx != (pulse / 6) % 16 || trk[0].seq_pos != 0)) exact = 0;
    }
    bad += check("128 beats stay on exact steps through tempo changes and polling jitter", exact);
    return bad;
}
static int long_session_test(void)
{
    int bad = 0;
    for (uint32_t source = 1; source <= 2; source++) {
        uint32_t start = 0xFFF00000u, bpm = source == 1u ? 137u : 120u;
        timing_reset(source, start);
        song.rec = 0;
        timing_packet(start, 0xFA, 0, 0, source);
        events_block(0);
        /* Keep wall milliseconds independent while the raw 24 MHz timer and
         * the 16-bit received-pulse counter wrap during this two-hour run. */
        int exact = 1;
        uint32_t clocks = bpm * 24u * 120u;
        for (uint32_t pulse = 0; pulse <= clocks; pulse++) {
            uint64_t elapsed = (uint64_t)pulse * 60000u * MIDI_TICKS_PER_MS / (bpm * 24u);
            timing_packet(start + (uint32_t)elapsed, 0xF8, 0, 0, source);
            fm1_ms = 1000u + (uint32_t)(elapsed / MIDI_TICKS_PER_MS);
            midi_in_ms[(mi_w - 1u) % MQ] = fm1_ms;
            events_block(0);
            if (!song.playing || trk[0].seq_idx != pulse / 6u % 16u || trk[0].seq_pos != pulse % 6u * 1024u ||
                beat_n != pulse / 24u % 4u || beat_pos != pulse % 24u * 1024u) exact = 0;
            /* Exercise interpolation between arrivals, as audio blocks do. */
            timing_now += (uint32_t)(30000u * MIDI_TICKS_PER_MS / (bpm * 24u));
            events_block(0);
        }
        bad += check(source == 1u ? "USB two-hour clock keeps exact sequencer and beat phase across counter wraps" :
                                   "TRS two-hour clock keeps exact sequencer and beat phase across counter wraps", exact);
    }
    return bad;
}
static int overflow_phase_test(void)
{
    int bad = 0;
    for (uint32_t source = 1; source <= 2; source++) {
        uint32_t start = 24000000u;
        timing_reset(source, start);
        song.rec = 0;
        midi_rx_pulses[source - 1u] = 65534u; /* overflow recovery crosses the counter wrap too */
        midi_rx_pulses[2u - source] = 1010u;
        timing_packet(start, 0xFA, 0, 0, source);
        events_block(0);
        int exact = 1, released = 1;
        /* Repeated overflows: some clocks were already queued when recovery
         * discards them; others arrive while the full queue rejects packets.
         * Include the very first clock after Start and several lost at once. */
        for (uint32_t pulse = 0; pulse <= 96u; pulse++) {
            uint32_t at = start + pulse * 500000u;
            if (pulse % 24u == 0u) {
                timing_packet(at, 0x90, 60, 100, source);
                events_block(0);
                for (uint32_t i = 0; i < MQ - 1u; i++) timing_packet(at, 0xB0, 1, i, source);
                timing_packet(at, 0xF8, 0, 0, source); /* accepted, then discarded with notes */
                timing_packet(at, 0xB0, 1, 0, source); /* overflow */
            } else
                timing_packet(at, 0xF8, 0, 0, source);
            /* Unselected clocks must never enter the selected source's count. */
            timing_packet(at, 0xF8, 0, 0, 3u - source);
            if (pulse % 24u < 3u) continue;
            events_block(0);
            if (midi_owners[0] || gate_note(&trk[0], 60)) released = 0;
            if (pulse % 24u == 3u) continue; /* overflow clears here; the next clock carries recovery */
            if (!song.playing || trk[0].seq_idx != pulse / 6u % 16u || trk[0].seq_pos != pulse % 6u * 1024u) exact = 0;
        }
        /* Drain the final overflow and prove the next pulse restores phase. */
        events_block(0);
        timing_packet(start + 97u * 500000u, 0xF8, 0, 0, source); events_block(0);
        exact &= trk[0].seq_idx == 0u && trk[0].seq_pos == 1024u;
        bad += check(source == 1u ? "USB queue overflows recover every received clock without accumulating delay" :
                                   "TRS queue overflows recover every received clock without accumulating delay", exact);
        bad += check("clock recovery retains overflow's stuck-note release", released);
        for (uint32_t pulse = 98u; pulse <= 120u; pulse++) {
            timing_packet(start + pulse * 500000u, 0xF8, 0, 0, source); events_block(0);
        }
        bad += check("tempo reacquires 120 BPM after discarded clock entries", song.g[G_BPM] == 120 && midi_beat_samples == FS / 2u);
    }
    return bad;
}
static int arp_catchup_test(void)
{
    int bad = 0;
    for (uint32_t swing = 0; swing <= 100u; swing += 100u) {
        timing_reset(2, 24000000u);
        song.rec = 0;
        timing_packet(timing_now, 0xFA, 0, 0, 2); events_block(0);
        track_t *t = &trk[0];
        t->p[P_AMODE] = 1; t->p[P_ARATE] = 3; t->p[P_ASWING] = swing;
        t->p[P_AOCT] = 1; t->p[P_APROB] = 127; t->p[P_AGATE] = 64;
        t->nheld = 3; t->held[0] = 60; t->held[1] = 64; t->held[2] = 67;
        t->arp_idx = UINT32_MAX; t->arp_pos = 0xFFFFFFFu;
        arp_tick(t, 0);
        /* Compare a burst with the same elapsed time in small audio advances.
         * Four steps also leave a different note in the three-note chord. */
        uint32_t elapsed = 4u * 3072u + 321u;
        for (uint32_t n = elapsed; n;) {
            uint32_t chunk = n < 32u ? n : 32u;
            arp_tick(t, chunk); n -= chunk;
        }
        uint32_t idx = t->arp_idx, pos = t->arp_pos, off = t->arp_off, note = t->arp_note;
        if (t->arp_note) arp_off(t);
        t->arp_idx = UINT32_MAX; t->arp_pos = 0xFFFFFFFu;
        arp_tick(t, 0); arp_tick(t, elapsed);
        bad += check(swing ? "swung ARP burst matches small advances: note, index, residual and gate" :
                            "straight ARP burst matches small advances: note, index, residual and gate",
                     t->arp_idx == idx && t->arp_pos == pos && t->arp_off == off && t->arp_note == note);
    }
    return bad;
}
static void tempo_packet(uint32_t start, uint32_t time, uint32_t status, uint32_t source)
{
    timing_packet(time, status, 0, 0, source);
    /* TIMER4 wraps every ~179 s; the independent millisecond timer does not. */
    fm1_ms = 1000u + (time - start) / MIDI_TICKS_PER_MS;
    midi_in_ms[(mi_w - 1u) % MQ] = fm1_ms;
    events_block(0);
}
static int tempo_flutter_test(void)
{
    int bad = 0;
    for (uint32_t source = 1; source <= 2; source++) {
        /* Alternate endpoint delays on the six-clock measurement windows,
         * plus per-pulse polling jitter. Exercise the raw timer's wrap too. */
        uint32_t start = 0xFFF00000u, time = start;
        timing_reset(source, start);
        song.rec = 0;
        tempo_packet(start, start, 0xFA, source);
        uint32_t min_beat = UINT32_MAX, max_beat = 0;
        uint32_t min_advance = UINT32_MAX, max_advance = 0;
        int stable = 1, exact = 1;
        for (uint32_t pulse = 0; pulse <= 24u * 16u; pulse++) {
            uint32_t delay = ((pulse / 6u) % 2u) * MIDI_TICKS_PER_MS +
                             (pulse % 2u) * MIDI_TICKS_PER_MS / 4u;
            tempo_packet(start, time + delay, 0xF8, source);
            if (pulse >= 24u * 4u) {
                if (song.g[G_BPM] != 120) stable = 0;
                if (beat_samples() < min_beat) min_beat = beat_samples();
                if (beat_samples() > max_beat) max_beat = beat_samples();
            }
            if (pulse % 6u == 0 && (trk[0].seq_idx != pulse / 6u % 16u || trk[0].seq_pos)) exact = 0;
            /* Render half an ideal pulse after arrival: a jittery previous
             * interval must not speed up / slow down this block's playhead. */
            uint32_t before = trk[0].seq_pos;
            timing_now = time + delay + 250000u;
            fm1_ms = 1000u + (timing_now - start) / MIDI_TICKS_PER_MS;
            events_block(0);
            if (pulse >= 24u * 4u) {
                uint32_t advance = trk[0].seq_pos - before;
                if (advance < min_advance) min_advance = advance;
                if (advance > max_advance) max_advance = advance;
            }
            time += 500000u;
        }
        bad += check(source == 1 ? "USB jitter keeps the BPM display at 120" : "TRS jitter keeps the BPM display at 120", stable);
        bad += check("jitter moves the synced modulation period by less than 0.25 percent", max_beat - min_beat < FS / 800u);
        bad += check("between-pulse playhead advances stay within two musical units", max_advance - min_advance <= 2u);
        bad += check("filtered tempo keeps every six-clock step exactly on its pulse", exact);
        /* A real large tempo change must lock in one six-clock window. */
        uint32_t last = time - 500000u;
        for (uint32_t pulse = 1; pulse <= 6u; pulse++) {
            tempo_packet(start, last + pulse * 400000u, 0xF8, source);
        }
        bad += check("a real 120 to 150 BPM change locks within six clocks", song.g[G_BPM] == 150 && beat_samples() == FS * 2u / 5u);
        /* Small edits still get through the filter and display hysteresis. */
        last += 6u * 400000u;
        for (uint32_t pulse = 1; pulse <= 24u * 4u; pulse++) {
            tempo_packet(start, last + (uint32_t)((uint64_t)pulse * 60000u * MIDI_TICKS_PER_MS / (151u * 24u)), 0xF8, source);
        }
        bad += check("a one-BPM master edit reaches the display within four beats", song.g[G_BPM] == 151);
        /* Fractional tempos near a rounding boundary must not alternate
         * digits, including the shortest supported clock interval. */
        static const uint32_t bpm10[] = {400, 730, 1205, 1370, 2395, 2400};
        for (uint32_t k = 0; k < NELEM(bpm10); k++) {
            timing_reset(source, start);
            song.rec = 0;
            tempo_packet(start, start, 0xFA, source);
            int16_t shown = 0;
            int settled = 1, precise = 1;
            uint32_t expected = FS * 600u / bpm10[k];
            for (uint32_t pulse = 0; pulse <= 24u * 16u; pulse++) {
                uint32_t at = start + (uint32_t)((uint64_t)pulse * 600000u * MIDI_TICKS_PER_MS / (bpm10[k] * 24u));
                uint32_t delay = ((pulse / 6u) % 2u) * MIDI_TICKS_PER_MS;
                tempo_packet(start, at + delay, 0xF8, source);
                if (pulse == 24u * 4u) shown = song.g[G_BPM];
                if (pulse >= 24u * 4u) {
                    if (song.g[G_BPM] != shown) settled = 0;
                    if (beat_samples() + expected / 400u < expected || beat_samples() > expected + expected / 400u) precise = 0;
                }
            }
            char label[100];
            snprintf(label, sizeof label, "%s %u.%u BPM stays steady and modulation stays within 0.25 percent",
                     source == 1 ? "USB" : "TRS", bpm10[k] / 10u, bpm10[k] % 10u);
            bad += check(label, settled && precise && shown * 10 >= (int32_t)bpm10[k] - 10 && shown * 10 <= (int32_t)bpm10[k] + 10);
        }
    }
    return bad;
}
static int divisions_test(void)
{
    int bad = 0, exact = 1;
    static const uint32_t periods[] = {24576, 12288, 6144, 3072, 8192, 4096, 49152, 98304, 196608, 393216};
    for (uint32_t div = 0; div < NELEM(periods); div++) {
        uint32_t start = 24000000u;
        timing_reset(2, start);
        trk[0].p[P_SDIV] = (int16_t)div;
        trk[0].p[P_SLEN] = 64;
        timing_packet(start, 0xFA, 0, 0, 2);
        for (uint32_t pulse = 0; pulse <= 24u * 64u; pulse++) {
            uint32_t at = start + (uint32_t)((uint64_t)pulse * 60000u * MIDI_TICKS_PER_MS / (137u * 24u));
            timing_packet(at, 0xF8, 0, 0, 2); events_block(CTL);
            uint32_t units = pulse * 1024u;
            if (trk[0].seq_idx != (units / periods[div]) % 64u || trk[0].seq_pos != units % periods[div]) exact = 0;
        }
    }
    bad += check("all ten divisions, including triplets and 16-beat steps, keep exact phase", exact);
    return bad;
}
static int lengths_and_swing_test(void)
{
    int bad = 0, swing = 1;
    uint32_t start = 24000000u;
    timing_reset(2, start);
    timing_packet(start, 0xFA, 0, 0, 2);
    for (uint32_t pulse = 0; pulse <= 24; pulse++) {
        uint32_t at = start + pulse * 500000u;
        /* A serialized chord straddles the start of step 1. */
        if (pulse == 6) timing_packet(at - 12000u, 0x90, 60, 100, 2);
        timing_packet(at, 0xF8, 0, 0, 2);
        if (pulse == 6) {
            timing_packet(at + 12000u, 0x90, 64, 100, 2);
            timing_packet(at + 24000u, 0x90, 67, 100, 2);
        }
        /* Quantized length: two steps, ending at the start of step 3. */
        if (pulse == 18) {
            timing_packet(at + 12000u, 0x80, 60, 0, 2);
            timing_packet(at + 24000u, 0x80, 64, 0, 2);
            timing_packet(at + 36000u, 0x80, 67, 0, 2);
        }
        events_block(CTL);
    }
    bad += check("a chord straddling a boundary shares one onset and retains its full held length",
                 trk[0].step[1].n == 3 && trk[0].step[1].note[0] == 60 && trk[0].step[1].note[1] == 64 &&
                 trk[0].step[1].note[2] == 67 && trk[0].step[2].time == ST_TIE && trk[0].step[3].time == ST_TIE && step_gate(&trk[0].step[3]) < 8);
    timing_reset(2, start);
    trk[0].p[P_SSWING] = 100;
    timing_packet(start, 0xFA, 0, 0, 2);
    for (uint32_t pulse = 0; pulse <= 192; pulse++) {
        timing_packet(start + pulse * 500000u, 0xF8, 0, 0, 2); events_block(CTL);
        /* This fixture measures continuous capture beyond the first pass. */
        song.rec_pass = 0;
        uint32_t units = pulse * 1024u, pair = units / 12288u, within = units % 12288u;
        /* SWING 100: 6144 + floor(6144*100/250) = 8601, then 3687. */
        uint32_t odd = within >= 8601u;
        if (trk[0].seq_idx != (2u * pair + odd) % 16u || trk[0].seq_pos != within - odd * 8601u) swing = 0;
    }
    bad += check("maximum swing preserves every alternating boundary over eight beats", swing);
    /* A note near the delayed odd onset rounds onto it, not the straight grid. */
    timing_packet(start + 193u * 500000u, 0x90, 72, 100, 2); events_block(CTL);
    bad += check("synced MIDI quantization follows the swung onset", trk[0].step[0].note[0] == 72 && !trk[0].step[1].n);
    return bad;
}
static int loop_replay_test(void)
{
    int bad = 0;
    for (uint32_t source = 0; source < 3; source++) {
        timing_reset(2, 24000000u);
        track_t *t = &trk[0];
        timing_packet(timing_now, 0xFA, 0, 0, 2);
        timing_packet(timing_now, 0xF8, 0, 0, 2);
        events_block(CTL);
        /* Exercise the event store's cross-loop releases independently of the
         * UI's one-pattern punch-out (covered in recording_test.c). */
        song.rec_pass = 0;
        seq_advance(15u * 6144u + 5000u);
        t->p[P_RECQ] = 3; /* commit a 1/16 grid when this take stops */
        /* Three paths retain the same late last-step original onset. */
        if (source == 0) input_on(t, 60, 111);
        else { timing_packet(timing_now, 0x90, 60, 111, source); events_block(CTL); }
        seq_advance(2000);
        if (source == 0) input_off(t, 60);
        else { timing_packet(timing_now, 0x80, 60, 0, source); events_block(CTL); }
        bad += check("late loop-start input records only step zero, never both loop ends",
                     t->step[0].n == 1 && t->step[0].note[0] == 60 && t->step[15].time == ST_REST && !t->step[15].n);
        uint32_t captured = (recording[0].duration << (recording[0].owner >> 5)) * 6144u / RECORD_UNIT;
        bad += check("quantizing the onset preserves the 2000-unit note duration",
                     captured <= 2000u && captured + 6u > 2000u);
        bad += check("recorded velocity 111 stays 111 rather than becoming an accent",
                     t->step[0].vel == 111 && !(t->step[0].flags & SF_ACCENT));
        seq_stop(); song.rec = 0; t->p[P_SGATE] = 16;
        seq_start(); seq_advance(0);
        seq_advance(captured - 1u);
        bad += check("replay keeps the recorded note held despite a different track GATE", gate_note(t, 60));
        seq_advance(1);
        bad += check("replay releases the note at its captured duration", !gate_note(t, 60));
        seq_advance(16u * 6144u - captured - 1u);
        bad += check("replay has no extra onset on the final step", !gate_note(t, 60) && t->seq_idx == 15);
        seq_advance(1);
        bad += check("replay starts the note once when the loop returns to zero", gate_note(t, 60) && t->seq_idx == 0);
    }
    return bad;
}
static int recorded_gate_storage_test(void)
{
    timing_reset(2, 24000000u);
    trk[0].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 111, 0, 83};
    pattern_commit(&trk[0]);
    pattern_request(&trk[0], 1);
    trk[0].step[2] = (step_t){{64}, 1, ST_NOTE, 0, 70, 0, 211};
    project_capture(&proj_scratch);
    int ok = bank_pack(proj_wire_u.raw, &proj_scratch, 1) && bank_valid(proj_wire_u.raw, BANK_STORE_SIZE);
    if (ok) {
        project_restore_runtime(&proj_scratch); bank_restore(proj_wire_u.raw);
        ok = trk[0].step[2].acc == 211 && pattern_at(0, 0)->step[0].acc == 83;
    }
    return check("recorded gates survive project save/load in active and inactive banks", ok);
}
static int old_formats_and_overdub_test(void)
{
    int bad = 0;
    timing_reset(2, 24000000u);
    timing_reset(2, 24000000u);
    timing_packet(24000000u, 0xFA, 0, 0, 2); events_block(0);
    track_t *t = &trk[0]; seq_start(); seq_advance(0);
    input_on(t, 60, 111); seq_advance(14000); input_off(t, 60);
    seq_stop(); seq_start(); seq_advance(0);
    input_on(t, 60, 70); seq_advance(2000); input_off(t, 60);
    bad += check("a shorter repeated take removes its old tied tail and keeps softer velocity",
                 t->step[0].vel == 70 && step_gate(&t->step[0]) < 90 && t->step[1].time == ST_REST && t->step[2].time == ST_REST);
    t->seq_notes[0] = 60; t->seq_n = 1;
    input_on(t, 60, 100); seq_release(t);
    bad += check("a sequencer release cannot cut a note still held by the player", gate_note(t, 60));
    input_off(t, 60);
    return bad;
}
#ifndef MIDI_TIMING_NO_MAIN
int main(void)
{
    int bad = grid_test() + timeline_test() + phase_test() + long_session_test() + overflow_phase_test() + arp_catchup_test() + tempo_flutter_test() + divisions_test() + lengths_and_swing_test() +
              loop_replay_test() + recorded_gate_storage_test() + old_formats_and_overdub_test();
    printf("%s\n", bad ? "MIDI TIMING TEST FAILED" : "MIDI timing tests passed");
    return bad != 0;
}

#endif
