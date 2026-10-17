/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* MELODEE_CAPTURE_FAST: live capture allocation against the original full
 * scan. Forked children replay the same seeded scenarios with each version and
 * hash the complete recording, track, pattern, song and resource state after
 * every operation. Host instruction counts are not target cycles. */
#ifndef MELODEE_CAPTURE_FAST
#define MELODEE_CAPTURE_FAST 1
#endif
#define AUDIO_CPU_FIXTURE_NO_MAIN 1
#include "audio_cpu_fixture.c"
#include <sys/wait.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static uint64_t instructions(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 r;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&r)) return r.ri_instructions;
#endif
    return 0;
}

/* The original allocation, retained verbatim from parent 18fbd7e. */
static __attribute__((noinline)) int capture_reference(track_t *t, uint32_t note, uint32_t vel, uint32_t step)
{
    /* Retire overwritten overview cells before a new take can own them again. */
    recording_prune(t);
    uint32_t owner = recording_owner(t), period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    uint32_t actual = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_idx;
    uint32_t span = step_samples(t, period, actual), pos = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_pos;
    uint32_t on = (uint32_t)(((uint64_t)pos * RECORD_UNIT + span / 2u) / span), free = RECORD_MAX;
    if (on > 65535u) on = 65535u;
    for (uint32_t i = 0; i < RECORD_MAX; i++) {
        recorded_note_t *r = &recording[i];
        if ((r->owner & 31u) == owner && (recording_flags[i] & 1u) && r->note == note) recording_finish(i);
        /* Repeated passes replace the same pitch at the same captured time.
         * Distinct hits in one step remain separate events. */
        uint32_t same_time = r->on == on;
        if (r->vel && (r->owner & 31u) == owner && r->note == note && (r->step & 63u) == actual && same_time) free = i;
        else if (free == RECORD_MAX && (!r->vel || (!recording_present(r) && !recording_sounding(i) && !(recording_flags[i] & 1u)))) free = i;
    }
    if (free == RECORD_MAX) { recording_full = 1; return 0; }
    recording_undo_capture(t);
    if (recording_sounding(free)) recording_end(&trk[(recording[free].owner & 31u) / NPAT], free);
    recording_unlink(free);
    recorded_note_t *r = &recording[free];
    *r = (recorded_note_t){(uint16_t)on, 1, (uint8_t)note, (uint8_t)vel, (uint8_t)owner, (uint8_t)(actual | (step != actual ? 64u : 0u) | (step == 0u && actual ? 128u : 0u))};
    recording_insert(t, free);
    memset(&recording_run[free], 0, sizeof recording_run[free]);
    recording_flags[free] = 13;
    recording_live_add(free);
    t->step[step].flags |= SF_RECORDED;
    recording_tracks |= (uint8_t)(1u << trk_index(t));
    recording_generation++;
    return 1;
}

static uint64_t capture_hash;
static void mix(const void *p, size_t n)
{
    const uint8_t *b = p;
    size_t i = 0;
    for (; i + 8u <= n; i += 8u) {
        uint64_t w;
        memcpy(&w, b + i, sizeof w);
        capture_hash = (capture_hash ^ w) * 1099511628211ull;
        capture_hash ^= capture_hash >> 29;
    }
    for (; i < n; i++) capture_hash = (capture_hash ^ b[i]) * 1099511628211ull;
}
#define MIX(x) mix(&(x), sizeof(x))
static uint64_t state_hash(int rc)
{
    capture_hash = 14695981039346656037ull;
    MIX(rc); MIX(recording); MIX(recording_run); MIX(recording_flags); MIX(recording_fraction); MIX(recording_head);
    MIX(recording_next); MIX(recording_due); MIX(recording_refs); MIX(recording_schedule);
#if MELODEE_RECORD_CURSOR
    MIX(recording_live_next); MIX(recording_live_head);
#endif
#if MELODEE_RECORD_QUEUE
    MIX(recording_heap_store); MIX(recording_heap_chunk); MIX(recording_heap_free); MIX(recording_heap_taken); MIX(recording_heap_pos); MIX(recording_message); MIX(recording_watch); MIX(recording_watch_bits);
    MIX(recording_clock); MIX(recording_heap_count); MIX(recording_heap_owner); MIX(recording_heap_len);
#endif
#if MELODEE_RECORD_INDEX
    MIX(recording_pitch); MIX(recording_pitch_dirty); MIX(recording_pitch_key); MIX(recording_pitch_count);
#endif
    uint32_t generation = recording_generation; uint8_t full = recording_full;
    MIX(generation); MIX(full); MIX(recording_tracks); MIX(recording_undo_active); MIX(recording_undo_skip);
    MIX(recording_undo_lost); MIX(trk); MIX(song); MIX(resource);
    for (uint32_t k=0;k<NTRK;k++) for (uint32_t b=0;b<NPAT;b++) {
        pattern_t p; pattern_read(k,b,&p); MIX(p);
    }
    for (uint32_t b = 0; b < RESOURCE_BANKS; b++) mix(resource_banks[b].start, (size_t)(resource_banks[b].end - resource_banks[b].start));
    return capture_hash;
}

static uint32_t capture_seed;
static uint32_t rnd(uint32_t n) { capture_seed = capture_seed * 1664525u + 1013904223u; return (capture_seed >> 8) % n; }
static void set_cell(uint32_t k, uint32_t bank, uint32_t i, step_t s)
{
    pattern_t p; pattern_read(k,bank,&p); p.step[i]=s; pattern_write(k,bank,&p);
}
static void stale(void)
{
    uint32_t k = rnd(NTRK), bank = rnd(NPAT), i = rnd(NSTEP);
    step_t s = pattern_step(k, bank, i);
    switch (rnd(3)) {
    case 0: s.time = ST_REST; break;
    case 1: s.flags &= (uint8_t)~SF_RECORDED; break;
    default: s.time = ST_TIE; break;
    }
    set_cell(k,bank,i,s);
}
/* Coverage only (not an oracle): which allocation path the next call takes. */
static uint32_t coverage[5]; /* replacement, other-owner retired slot, own retired slot, full, live-hold seal */
static void classify(const track_t *t, uint32_t note)
{
    uint32_t owner = recording_owner(t), period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    uint32_t actual = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_idx;
    uint32_t span = step_samples(t, period, actual), pos = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_pos;
    uint32_t on = (uint32_t)(((uint64_t)pos * RECORD_UNIT + span / 2u) / span), match = 0, seal = 0, first = RECORD_MAX;
    if (on > 65535u) on = 65535u;
    for (uint32_t i = 0; i < RECORD_MAX; i++) {
        const recorded_note_t *r = &recording[i];
        uint32_t mine = r->vel && (r->owner & 31u) == owner, present = r->vel && recording_present(r);
        if (mine && present && r->note == note) {
            seal |= recording_flags[i] & 1u;
            match |= (r->step & 63u) == actual && r->on == on;
        }
        if (first == RECORD_MAX && (!r->vel || (mine && !present) ||
            (!present && !recording_sounding(i) && !(recording_flags[i] & 1u)))) first = i;
    }
    if (match) coverage[0]++;
    else if (first == RECORD_MAX) coverage[3]++;
    else if (recording[first].vel) coverage[(recording[first].owner & 31u) == owner ? 2 : 1]++;
    coverage[4] += seal;
}
#define OPS 160u
static const uint8_t pool[] = {60, 62, 64, 67, 36, 38, 42, 46};
/* One seeded scenario; impl 0 = original, 1 = candidate. */
static void scenario(uint32_t seed, int impl, uint64_t *out)
{
    static const char *const assignments[] = {"0124", "0000", "4455", "2222", "3301"};
    static const uint32_t counts[] = {0, 64, 256, RECORD_MAX};
    capture_seed = (seed + 1u) * 2654435761u;
    capture_seed ^= capture_seed >> 15; capture_seed *= 2246822519u; capture_seed ^= capture_seed >> 13;
    /* Sequenced draws: C leaves argument evaluation order unspecified. */
    uint32_t count = counts[rnd(4)], quant = rnd(2), spread = rnd(2), tails = rnd(2);
    const char *a = assignments[rnd(5)];
    int ok = count ? make_fixture(count, quant, spread, tails, a) : fixture_tracks(quant, a);
    /* Half of the full stores keep every cell: capture must refuse. */
    uint32_t fresh = count == RECORD_MAX && rnd(2);
    assert(ok);
    /* Move some events to other banks of their track, present or retired. */
    for (uint32_t i = 0; i < RECORD_MAX; i++) if (recording[i].vel && rnd(4) == 0) {
        recorded_note_t *r = &recording[i];
        uint32_t k = (r->owner & 31u) / NPAT, bank = 1u + rnd(NPAT - 1u);
        r->owner = (uint8_t)((r->owner & 224u) | (k * NPAT + bank));
        if (fresh || rnd(3)) {
            uint32_t v=recording_view(&trk[k],r); step_t s=pattern_step(k,bank,v);
            s.time=ST_NOTE; s.flags|=SF_RECORDED; set_cell(k,bank,v,s);
        }
    }
    recording_reindex();
    for (uint32_t i = fresh ? 0u : rnd(48); i; i--) stale();
    seq_stop(); song.rec = song.rec_latch = 0; settings_countin = 0;
    for (uint32_t k = 0; k < NTRK; k++) if (rnd(3)) { rec_set(&trk[k], 1); if (rnd(2)) song.rec_latch |= (uint8_t)(1u << k); }
    seq_start(); seq_advance(0);
    for (uint32_t o = 0; o < OPS; o++) {
        track_t *t = &trk[rnd(NTRK)];
        uint32_t note = pool[rnd(sizeof pool)], op = rnd(10), rc = 0;
        if (op < 6) {
            uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
            uint32_t idx = t->seq_pos == 0x7FFFFFFFu ? 0u : t->seq_idx % len;
            if (rnd(4) == 0) idx = (idx + 1u) % len;   /* rounded to the next cell */
            if (!impl) classify(t, note);
            uint32_t vel = 1u + rnd(127);
            rc = (uint32_t)(impl ? recording_note(t, note, vel, idx) : capture_reference(t, note, vel, idx));
            if (rnd(3) == 0) {                          /* the same captured time again: replacement */
                if (!impl) classify(t, note);
                vel = 1u + rnd(127);
                rc |= (uint32_t)(impl ? recording_note(t, note, vel, idx) : capture_reference(t, note, vel, idx)) << 1;
            }
        } else if (op < 8) recording_release(t, note);
        else if (op < 9 || fresh) seq_advance(rnd(4000));
        else stale();
        out[o] = state_hash((int)rc);
    }
}

static int parity(uint32_t seeds)
{
    static uint64_t a[OPS], b[OPS];
    uint32_t total[NELEM(coverage)] = {0};
    for (uint32_t seed = 0; seed < seeds; seed++) {
        uint64_t *dest[2] = {a, b};
        for (int impl = 0; impl < 2; impl++) {
            int fd[2];
            assert(!pipe(fd));
            pid_t pid = fork();
            assert(pid >= 0);
            if (!pid) {
                static uint64_t h[OPS];
                close(fd[0]); scenario(seed, impl, h);
                _exit(write(fd[1], h, sizeof h) == (ssize_t)sizeof h && write(fd[1], coverage, sizeof coverage) == (ssize_t)sizeof coverage ? 0 : 3);
            }
            close(fd[1]);
            uint32_t seen[NELEM(coverage)];
            size_t got = 0, want = sizeof a + sizeof seen;
            while (got < want) {
                ssize_t n = read(fd[0], got < sizeof a ? (uint8_t *)dest[impl] + got : (uint8_t *)seen + (got - sizeof a),
                                 got < sizeof a ? sizeof a - got : want - got);
                if (n <= 0) break;
                got += (size_t)n;
            }
            close(fd[0]);
            int status = 0; waitpid(pid, &status, 0);
            if (got != want || !WIFEXITED(status) || WEXITSTATUS(status)) { printf("seed %u impl %d failed\n", seed, impl); return 1; }
            if (!impl) for (uint32_t k = 0; k < NELEM(seen); k++) total[k] += seen[k];
        }
        for (uint32_t o = 0; o < OPS; o++) if (a[o] != b[o]) { printf("seed %u: state differs after operation %u\n", seed, o); return 1; }
    }
    printf("paths: %u replacements, %u other-owner and %u own retired slots reused, %u full-store refusals, %u live holds sealed\n",
           total[0], total[1], total[2], total[3], total[4]);
    if (seeds >= 100u) for (uint32_t k = 0; k < NELEM(total); k++) assert(total[k] >= 20u);
    return 0;
}

/* Live note cost with a stored pattern: one track or spread over four. */
static void costs(void)
{
    static const uint32_t counts[] = {0, 64, 256, 960};
    puts("stored,layout,original_instructions_per_note,candidate_instructions_per_note");
    for (uint32_t spread = 0; spread < 2; spread++) for (uint32_t c = 0; c < NELEM(counts); c++) {
        double cost[2] = {0, 0};
        for (int impl = 0; impl < 2; impl++) {
            uint32_t count = counts[c];
            int ok = count ? make_fixture(count == 960u ? RECORD_MAX : count, 0, spread, 0, "0000") : fixture_tracks(0, "0000");
            assert(ok);
            if (count == 960u) {                        /* 64 free slots at the end */
                uint32_t n = 0;
                for (uint32_t i = RECORD_MAX; i-- > 0 && n < 64u;) if (recording[i].vel) { memset(&recording[i], 0, sizeof recording[i]); n++; }
                recording_reindex();
            }
            seq_stop(); song.rec = song.rec_latch = 0; settings_countin = 0;
            track_t *t = &trk[0]; rec_set(t, 1); song.rec_latch = 1; seq_start(); seq_advance(0);
            uint64_t total = 0;
            for (uint32_t k = 0; k < 48u; k++) {
                seq_advance(1378);
                uint32_t note = 72u + k % 12u;
                uint64_t start = instructions();
                if (impl) recording_note(t, note, 100, t->seq_idx); else capture_reference(t, note, 100, t->seq_idx);
                total += instructions() - start;
                seq_advance(2000); rec_release(t, note); seq_advance(5512u - 1378u - 2000u);
            }
            cost[impl] = (double)total / 48.0;
        }
        printf("%u,%s,%.0f,%.0f\n", counts[c], spread ? "all" : "single", cost[0], cost[1]);
#ifdef __APPLE__
        /* Gate only where instruction counters exist. */
        assert(cost[1] <= cost[0] * 0.75);
#endif
    }
}

int main(int argc, char **argv)
{
    uint32_t seeds = argc > 1 ? (uint32_t)strtoul(argv[1], 0, 10) : 400u;
    if (parity(seeds)) return 1;
    if (argc <= 2) costs();
    printf("capture allocation: %u seeded scenarios x %u operations, full state parity with the original scan\n", seeds, OPS);
    return 0;
}
