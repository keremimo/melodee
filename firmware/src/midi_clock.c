/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Adapted from MIDI clock contributions by ChanceTheMaker and keremimo (2026).
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Audio ISR state. Musical phase is exactly 1024 units per MIDI pulse; changing
 * the measured tempo cannot move steps, gates, ties or the arpeggiator. */
static struct {
    uint32_t pos, rendered, last_ms, start_ms, last_time, interval_time, tempo_time, tempo_dt;
    uint16_t pulse_count;
    uint16_t spot;                                  /* Song Position: 1/16 steps from the top */
    uint8_t mode, selected, have_pulse, tempo_valid, tempo_n, tempo_count, count_valid, spot_valid, seek_wait;
} midi_clock;

/* An external clock's LFO units: the measured tempo, so pulse jitter cannot modulate it. Past a quarter of a pulse from
 * the song's units it is pulled back, so it cannot slide either. */
static struct {
    uint32_t rem;
    int32_t err;
} lfo_ext;
static uint32_t lfo_ext_units(uint32_t n, uint32_t song_units)
{
    uint32_t beat = beat_samples(), ticks = n * MIDI_BEAT_UNITS + lfo_ext.rem, units = ticks / beat;
    lfo_ext.rem = ticks % beat;
    lfo_ext.err += (int32_t)song_units - (int32_t)units;
    int32_t pull = lfo_ext.err / 256, out = (int32_t)units + pull;
    if (out < 0) {
        pull -= out;
        out = 0;
    }
    lfo_ext.err -= pull;
    return (uint32_t)out;
}

/* (Sequencer and ARP phase carry over: every clock counts the same units.) */
static void midi_clock_mode(uint32_t mode)
{
    midi_sync_active = (uint8_t)mode;
    midi_clock.mode = (uint8_t)mode;
}

/* Local PLAY / a REC note: follow the master only while its clock is running. A stopped master
 * (Stop received, pulses gone) hands timing back to the internal clock until its next Start,
 * Continue or Stop, so PLAY never dies on the lost-clock timeout. 1 = sync kept. */
static uint32_t midi_clock_local_start(uint32_t ms)
{
    if (!midi_clock.mode)
        return 0;
    if (midi_clock.tempo_valid && ms - midi_clock.last_ms <= 500u)
        return 1;
    midi_clock_mode(0);
    return 0;
}

/* Song Position, stopped only: the next Continue starts there, on its first clock (a master holds that clock until its
 * song reaches the spot). Every pattern is where it would be after playing from the top; a song's sections stay local. */
static void midi_clock_position(uint32_t spot)
{
    if (song.playing) return;
    midi_clock.spot = (uint16_t)spot;
    midi_clock.spot_valid = 1;
}
static void midi_clock_seek(void)
{
    uint32_t units = midi_clock.spot * (MIDI_BEAT_UNITS / 4u);
    midi_clock.seek_wait = 0;
    midi_clock.pos = midi_clock.rendered = 0;
    midi_clock.have_pulse = 0;
    seq_seek_units = units;
    seq_start();
    beat_pos = units % MIDI_BEAT_UNITS;
    beat_n = (units / MIDI_BEAT_UNITS) & 3u;
}

static __attribute__((noinline)) void midi_clock_transport(uint32_t status, uint32_t ms)
{
    if (status == 0xFAu) {
        midi_clock.spot_valid = midi_clock.seek_wait = 0;
        midi_clock.pos = midi_clock.rendered = 0;
        lfo_ext.err = 0;
        midi_clock.have_pulse = midi_clock.tempo_valid = 0;
        midi_clock.count_valid = 0;
        midi_clock.interval_time = 0;
        midi_clock.start_ms = ms;
        seq_start();
    } else if (status == 0xFBu) {
        cin_left = 0;
        midi_clock.pos = midi_clock.rendered;
        lfo_ext.err = 0;
        midi_clock.have_pulse = midi_clock.tempo_valid = 0;
        midi_clock.count_valid = 0;
        midi_clock.interval_time = 0;
        midi_clock.start_ms = ms;
        if (midi_clock.spot_valid && !song.playing) {
            midi_clock.spot_valid = 0;
            midi_clock.seek_wait = 1;
            return;
        }
        if (!song.playing) {
            motion_begin();
            song.rec_wait = song.rec;
            song.rec_pass = 0;
        }
        song.playing = 1;
    } else if (status == 0xFCu) {
        midi_clock.seek_wait = 0;
        seq_stop();
    }
}

static __attribute__((noinline)) void midi_clock_pulse(uint32_t ms, uint32_t time, uint16_t count)
{
    if (midi_clock.seek_wait) midi_clock_seek();
    uint32_t pulses = midi_clock.count_valid ? (uint16_t)(count - midi_clock.pulse_count) : 1u;
    if (!pulses || (midi_clock.have_pulse && ms - midi_clock.last_ms > 500u)) pulses = 1u;
    midi_clock.pulse_count = count;
    midi_clock.count_valid = 1;
    /* Missing queue entries have no usable timing endpoints. Keep the last
     * interpolated rate and acquire tempo again from consecutive pulses. */
    if (pulses > 1u) midi_clock.tempo_valid = 0;
    if (!midi_clock.tempo_valid || ms - midi_clock.last_ms > 500u) {
        midi_clock.tempo_valid = 1;
        midi_clock.tempo_time = time;
        midi_clock.tempo_n = 0;
        midi_clock.tempo_dt = 0;
        midi_clock.tempo_count = 0;
    } else if (++midi_clock.tempo_n == 6u) {
        uint32_t dt = time - midi_clock.tempo_time;
        midi_clock.tempo_time = time;
        midi_clock.tempo_n = 0;
        /* Allow endpoint jitter around the supported 40..240 BPM range. */
        if (dt >= 60u * MIDI_TICKS_PER_MS && dt <= 377u * MIDI_TICKS_PER_MS) {
            /* Six clocks acquire tempo quickly, but their endpoint jitter must
             * not modulate the LFO or the rate between pulses. Smooth duration
             * over roughly two beats; a change above 6.25% locks immediately.
             * Keep eighth-tick precision even with millisecond host clocks.
             * Musical phase still advances by exactly 1024 units per clock. */
            uint32_t first = !midi_clock.tempo_dt;
            int32_t delta = (int32_t)(dt * 8u) - (int32_t)midi_clock.tempo_dt;
            uint32_t change = (uint32_t)(delta < 0 ? -delta : delta);
            if (first || change > midi_clock.tempo_dt / 16u) {
                midi_clock.tempo_dt = dt * 8u;
                midi_clock.tempo_count = 1;
            } else {
                /* Average acquisition windows before using the steady filter,
                 * so a delayed first pulse cannot bias the initial lock. */
                if (midi_clock.tempo_count < 8u) midi_clock.tempo_count++;
                midi_clock.tempo_dt = (uint32_t)((int32_t)midi_clock.tempo_dt + delta / midi_clock.tempo_count);
            }
            dt = midi_clock.tempo_dt / 8u;
            midi_clock.interval_time = dt / 6u;
            midi_beat_samples = (uint32_t)(((uint64_t)FS * dt) / (250u * MIDI_TICKS_PER_MS));
            /* +/- 0.875 BPM hysteresis keeps fractional master tempos from
             * repeatedly rolling the header digits across a rounding edge. */
            int32_t bpm = (int32_t)(((uint64_t)15000u * MIDI_TICKS_PER_MS * 256u + dt / 2u) / dt);
            int32_t shown = (int32_t)song.g[G_BPM] * 256;
            if (first || bpm > shown + 224 || bpm < shown - 224)
                song.g[G_BPM] = (int16_t)clamp((bpm + 128) / 256, 40, 240);
        }
    }
    if (song.playing) {
        if (midi_clock.have_pulse) {
            uint32_t interval = time - midi_clock.last_time;
            if (pulses == 1u && !midi_clock.tempo_dt && interval >= 8u * MIDI_TICKS_PER_MS && interval <= 80u * MIDI_TICKS_PER_MS)
                midi_clock.interval_time = interval;
            midi_clock.pos += pulses * (MIDI_BEAT_UNITS / 24u);
        } else if (pulses > 1u) {
            /* Start's first clock is phase zero, even when it was discarded. */
            midi_clock.pos += (pulses - 1u) * (MIDI_BEAT_UNITS / 24u);
        }
        midi_clock.have_pulse = 1;
    }
    if (!midi_clock.interval_time)
        midi_clock.interval_time = 60000u * MIDI_TICKS_PER_MS / ((uint32_t)song.g[G_BPM] * 24u);
    midi_clock.last_ms = ms;
    midi_clock.last_time = time;
}

static uint32_t midi_clock_advance(uint32_t now)
{
    uint32_t target, elapsed, offset, n;
    if (!midi_clock.have_pulse)
        return 0;
    elapsed = now - midi_clock.last_time;
    /* The render timeline can precede a freshly received pulse. */
    if ((int32_t)elapsed < 0)
        elapsed = 0;
    if (elapsed > midi_clock.interval_time)
        elapsed = midi_clock.interval_time;
    offset = elapsed * (MIDI_BEAT_UNITS / 24u) / midi_clock.interval_time;
    if (offset >= MIDI_BEAT_UNITS / 24u)
        offset = MIDI_BEAT_UNITS / 24u - 1u;
    target = midi_clock.pos + offset;
    n = (int32_t)(target - midi_clock.rendered) > 0 ? target - midi_clock.rendered : 0u;
    midi_clock.rendered += n;
    return n;
}

/* CLK INT: this unit is the master, on USB and BLE. Pulses run while stopped too, so a follower has the tempo before
 * Start. They count the sequencer's own units, so no division, recording or ARP can slide against a follower.
 * The first pulse after Start is step 0, a block later (so is its sound). */
static struct {
    int32_t pos;
    uint8_t running;
} midi_sync_out;

/* A song cursor: Song Position and Continue instead of Start. Song Position counts 1/16 steps, so a cursor between two
 * goes to the next: returns the units until the song reaches it, where the first pulse belongs. */
static uint32_t midi_sync_out_position(uint64_t units)
{
    uint32_t rem, spot = chain_div_samples(units + 6143u, 6144u, &rem);
    if (spot > 16383u) spot = 16383u;
    midi_sync_send(0x3u | 0xF2u << 8 | (spot & 127u) << 16 | (spot >> 7) << 24);
    midi_sync_emit(0xFBu);
    return 6143u - rem;
}

static void midi_sync_out_block(uint32_t units)
{
    uint32_t start = seq_started;
    seq_started = 0;
    if (midi_sync_out.running && (start || !song.playing || song.g[G_CLOCK])) {
        midi_sync_emit(0xFCu);
        midi_sync_out.running = 0;
    }
    if (song.g[G_CLOCK])
        return;
    if (start && song.playing) {
        midi_sync_out.running = 1;
        if (chain.running && chain.start_units) {
            midi_sync_out.pos = (int32_t)(MIDI_BEAT_UNITS / 24u) - (int32_t)midi_sync_out_position(chain.start_units);
            return;
        }
        midi_sync_emit(0xFAu);
        /* the units after step 0 in this block (the end of a count-in) */
        midi_sync_out.pos = MIDI_BEAT_UNITS / 24u + (!chain.running && trk[0].seq_pos < MIDI_BEAT_UNITS / 24u ? trk[0].seq_pos : 0u);
        return;
    }
    midi_sync_out.pos += (int32_t)units;
    if (midi_sync_out.pos >= (int32_t)(MIDI_BEAT_UNITS / 24u)) {
        midi_sync_out.pos -= (int32_t)(MIDI_BEAT_UNITS / 24u);
        midi_sync_emit(0xF8u);
    }
}
