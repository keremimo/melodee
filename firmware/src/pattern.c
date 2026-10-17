/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Four editable/playing patterns live in track.step. Inactive banks use a
 * bounded, lossless pool of 16-byte chunks, addressed in 16-step blocks.
 * Empty blocks allocate nothing; identical steps share one snapshot; other
 * blocks store only non-default steps. The dense fallback is nine bytes per
 * step (including unused note bytes and n on rests/ties).
 *
 * One staging pattern per track prepares the next bank. At a prepared switch
 * the ISR swaps bounded step/timing arrays and retires the incoming cold copy.
 * The staging buffer now owns the outgoing bank until foreground service
 * compresses it. If service has not run, a bounded synchronous fallback keeps
 * switching correct; it never waits for the UI, allocates a general heap, or
 * touches flash. At most NPAT-1 cold banks per track occupy the pool, so the
 * dense bound below guarantees space even if every step is different.
 * Callers serialize mutations with the sequencer (as the old bank arrays did). */
#include "step_snapshot.h"
#define PAT_BLOCK_STEPS 16u
#define PAT_BLOCKS ((NSTEP + PAT_BLOCK_STEPS - 1u) / PAT_BLOCK_STEPS)
#define PAT_CHUNK_BYTES 16u
#define PAT_BLOCK_CHUNKS ((PAT_BLOCK_STEPS * sizeof(step_snapshot_t) + PAT_CHUNK_BYTES - 1u) / PAT_CHUNK_BYTES)
#define PAT_CHUNKS (NTRK * (NPAT - 1u) * PAT_BLOCKS * PAT_BLOCK_CHUNKS)
#define PAT_RETAINED_CHUNKS (PAT_CHUNKS < 672u ? PAT_CHUNKS : 672u)
#define PAT_NONE 0x7FFFu
#define PAT_REPEAT 0x8000u
_Static_assert(NSTEP % PAT_BLOCK_STEPS == 0u && PAT_CHUNKS < PAT_NONE, "bounded pattern block pool");
typedef struct { step_t step[NSTEP]; int16_t timing[4]; } pattern_t;
typedef struct { uint16_t head, mask; } pattern_block_t;
static uint8_t pattern_chunks_retained[PAT_RETAINED_CHUNKS][PAT_CHUNK_BYTES] __attribute__((section(".noinit")));
static uint8_t pattern_chunks_ram[PAT_CHUNKS - PAT_RETAINED_CHUNKS][PAT_CHUNK_BYTES];
static uint16_t pattern_link[PAT_CHUNKS], pattern_free, pattern_pool_used, pattern_pool_peak;
static pattern_block_t pattern_blocks[NTRK][NPAT][PAT_BLOCKS];
static int16_t pattern_timings[NTRK][NPAT][4];
static struct { pattern_t data; uint8_t bank, pending; } pattern_stage[NTRK];
static uint32_t pattern_fallbacks;
static uint8_t chain_patterns[CHAIN_ROWS][NTRK];
static uint8_t motion_pattern[MOTION_MAX];
static uint16_t pattern_epoch; /* invalidates destructive edits waiting for a loop end */
static void seq_release(track_t *t);
static void recording_reset(void);
static void recording_stop(track_t *t);
static int recording_owns(const track_t *t, uint32_t note);
static void motion_restore(track_t *t);
static uint32_t pattern_guard(void)
{
#if defined(FM1_IRQ_TARGET)
    uint32_t f = fm1_icfg(); fm1_irq_off(); return f;
#else
    return 0;
#endif
}
static void pattern_unguard(uint32_t f)
{
#if defined(FM1_IRQ_TARGET)
    fm1_icfg_set(f);
#else
    (void)f;
#endif
}
static uint8_t *pattern_chunk(uint32_t i)
{
    return i < PAT_RETAINED_CHUNKS ? pattern_chunks_retained[i] : pattern_chunks_ram[i - PAT_RETAINED_CHUNKS];
}
static void pattern_free_block(pattern_block_t *b)
{
    uint32_t h = b->head & ~PAT_REPEAT;
    while (h != PAT_NONE) {
        uint32_t next = pattern_link[h];
        pattern_link[h] = pattern_free; pattern_free = (uint16_t)h; pattern_pool_used--; h = next;
    }
    b->head = PAT_NONE; b->mask = 0;
}
static void pattern_free_bank(uint32_t k, uint32_t bank)
{
    for (uint32_t b = 0; b < PAT_BLOCKS; b++) pattern_free_block(&pattern_blocks[k][bank][b]);
}
/* The chain is walked once per block, rather than once per byte or step. */
typedef struct { uint16_t head; uint8_t pos; } pattern_stream_t;
static uint8_t pattern_byte_read(pattern_stream_t *s)
{
    uint8_t v = pattern_chunk(s->head)[s->pos++];
    if (s->pos == PAT_CHUNK_BYTES) { s->head = pattern_link[s->head]; s->pos = 0; }
    return v;
}
static void pattern_byte_write(pattern_stream_t *s, uint8_t v)
{
    if (s->pos == PAT_CHUNK_BYTES) {
        uint16_t h = pattern_free; pattern_free = pattern_link[h]; pattern_link[h] = PAT_NONE;
        pattern_pool_used++; if (pattern_pool_used > pattern_pool_peak) pattern_pool_peak = pattern_pool_used;
        if (s->head != PAT_NONE) pattern_link[s->head] = h;
        s->head = h; s->pos = 0;
    }
    pattern_chunk(s->head)[s->pos++] = v;
}
static step_t pattern_stream_step(pattern_stream_t *s)
{
    step_snapshot_t v; uint8_t *p = (uint8_t *)&v;
    for (uint32_t i = 0; i < sizeof v; i++) p[i] = pattern_byte_read(s);
    return step_snapshot_get(&v);
}
static void pattern_encode_block(pattern_block_t *b, const step_t *steps)
{
    step_t empty = {{0}, 0, ST_REST};
    uint32_t mask = 0, same = 1;
    for (uint32_t i = 0; i < PAT_BLOCK_STEPS; i++) {
        if (memcmp(&steps[i], &empty, sizeof empty)) mask |= 1u << i;
        if (memcmp(&steps[i], &steps[0], sizeof empty)) same = 0;
    }
    pattern_free_block(b);
    b->mask = (uint16_t)mask;
    if (!mask) return;
    pattern_stream_t s = {PAT_NONE, PAT_CHUNK_BYTES};
    for (uint32_t i = 0; i < (same ? 1u : PAT_BLOCK_STEPS); i++) if (same || (mask & (1u << i))) {
        step_snapshot_t v; step_snapshot_put(&v, &steps[i]); const uint8_t *p = (const uint8_t *)&v;
        for (uint32_t j = 0; j < sizeof v; j++) {
            pattern_byte_write(&s, p[j]);
            if (b->head == PAT_NONE) b->head = s.head;
        }
    }
    if (same) b->head |= PAT_REPEAT;
}
static void pattern_decode_block(const pattern_block_t *b, step_t *steps)
{
    pattern_stream_t s = {(uint16_t)(b->head & ~PAT_REPEAT), 0};
    step_t repeat = {{0}, 0, ST_REST};
    if (b->head & PAT_REPEAT) repeat = pattern_stream_step(&s);
    for (uint32_t i = 0; i < PAT_BLOCK_STEPS; i++) {
        steps[i] = repeat;
        if (!(b->head & PAT_REPEAT) && (b->mask & (1u << i))) steps[i] = pattern_stream_step(&s);
    }
}
/* Only inactive banks are encoded. An outgoing bank may temporarily belong
 * to staging, which leaves at least one cold bank's worth of chunks free. */
static void pattern_encode(uint32_t k, uint32_t bank, const pattern_t *p)
{
    for (uint32_t b = 0; b < PAT_BLOCKS; b++) pattern_encode_block(&pattern_blocks[k][bank][b], p->step + b * PAT_BLOCK_STEPS);
    memcpy(pattern_timings[k][bank], p->timing, sizeof p->timing);
}
static void pattern_flush(uint32_t k)
{
    if (pattern_stage[k].pending) {
        pattern_encode(k, pattern_stage[k].bank, &pattern_stage[k].data);
        pattern_stage[k].pending = 0; pattern_stage[k].bank = 0xff;
    }
}
static int16_t pattern_timing(uint32_t k, uint32_t bank, uint32_t i)
{
    if (bank == trk[k].pattern) return trk[k].p[P_SLEN + i];
    if (pattern_stage[k].pending && pattern_stage[k].bank == bank) return pattern_stage[k].data.timing[i];
    return pattern_timings[k][bank][i];
}
static void pattern_read(uint32_t k, uint32_t bank, pattern_t *p)
{
    uint32_t f = pattern_guard();
    if (bank == trk[k].pattern) {
        memcpy(p->step, trk[k].step, sizeof p->step); memcpy(p->timing, &trk[k].p[P_SLEN], sizeof p->timing);
    } else if (pattern_stage[k].pending && pattern_stage[k].bank == bank) *p = pattern_stage[k].data;
    else {
        for (uint32_t b = 0; b < PAT_BLOCKS; b++) pattern_decode_block(&pattern_blocks[k][bank][b], p->step + b * PAT_BLOCK_STEPS);
        memcpy(p->timing, pattern_timings[k][bank], sizeof p->timing);
    }
    pattern_unguard(f);
}
/* A point query for precise-note ownership avoids expanding a whole bank. */
static step_t pattern_step(uint32_t k, uint32_t bank, uint32_t i)
{
    if (bank == trk[k].pattern) return trk[k].step[i];
    uint32_t f = pattern_guard();
    if (pattern_stage[k].pending && pattern_stage[k].bank == bank) {
        step_t v = pattern_stage[k].data.step[i]; pattern_unguard(f); return v;
    }
    const pattern_block_t *b = &pattern_blocks[k][bank][i / PAT_BLOCK_STEPS];
    uint32_t bit = i % PAT_BLOCK_STEPS;
    step_t empty = {{0}, 0, ST_REST};
    if (!(b->mask & (1u << bit))) { pattern_unguard(f); return empty; }
    pattern_stream_t s = {(uint16_t)(b->head & ~PAT_REPEAT), 0};
    if (!(b->head & PAT_REPEAT)) {
        uint32_t preceding = b->mask & ((1u << bit) - 1u), bytes = 0;
        while (preceding) { preceding &= preceding - 1u; bytes += sizeof(step_snapshot_t); }
        while (bytes >= PAT_CHUNK_BYTES) { s.head = pattern_link[s.head]; bytes -= PAT_CHUNK_BYTES; }
        s.pos = (uint8_t)bytes;
    }
    step_t v = pattern_stream_step(&s); pattern_unguard(f); return v;
}
static void pattern_write(uint32_t k, uint32_t bank, const pattern_t *p)
{
    pattern_flush(k);
    if (bank == trk[k].pattern) {
        memcpy(trk[k].step, p->step, sizeof p->step); memcpy(&trk[k].p[P_SLEN], p->timing, sizeof p->timing);
    } else pattern_encode(k, bank, p);
    if (pattern_stage[k].bank == bank) pattern_stage[k].bank = 0xff;
}
static void pattern_commit(track_t *t)
{
    memcpy(pattern_timings[t - trk][t->pattern], &t->p[P_SLEN], 4u * sizeof(int16_t));
}
/* Foreground service owns compression in the ordinary prepared-switch path.
 * Mutations are bounded and guarded; there is no arena compaction or moving
 * of unrelated banks. A request changed by the ISR is checked under the guard. */
static void pattern_prepare(uint32_t k, uint32_t bank)
{
    uint32_t f = pattern_guard();
    pattern_flush(k);
    if (bank < NPAT && bank != trk[k].pattern && pattern_stage[k].bank != bank) {
        pattern_read(k, bank, &pattern_stage[k].data); pattern_stage[k].bank = (uint8_t)bank;
    }
    pattern_unguard(f);
}
static void pattern_apply(track_t *t, uint32_t bank)
{
    uint32_t k = (uint32_t)(t - trk), old = t->pattern;
    seq_release(t); recording_stop(t); motion_restore(t);
    if (bank != old) {
        if (pattern_stage[k].pending || pattern_stage[k].bank != bank) {
            pattern_fallbacks++; pattern_flush(k);
            pattern_read(k, bank, &pattern_stage[k].data);
        }
        pattern_free_bank(k, bank);
        for (uint32_t i = 0; i < NSTEP; i += PAT_BLOCK_STEPS) {
            step_t v[PAT_BLOCK_STEPS];
            memcpy(v, t->step + i, sizeof v);
            memcpy(t->step + i, pattern_stage[k].data.step + i, sizeof v);
            memcpy(pattern_stage[k].data.step + i, v, sizeof v);
        }
        for (uint32_t i = 0; i < 4u; i++) {
            int16_t v = t->p[P_SLEN+i]; t->p[P_SLEN+i] = pattern_stage[k].data.timing[i]; pattern_stage[k].data.timing[i] = v;
        }
        pattern_stage[k].bank = (uint8_t)old; pattern_stage[k].pending = 1;
    }
    t->pattern = (uint8_t)bank; t->pattern_next = 0xff;
    t->rh_n = t->rskip_n = 0; t->pattern_gen++;
}
static void pattern_init(void)
{
    recording_reset(); pattern_epoch++;
    pattern_pool_used = pattern_pool_peak = 0; pattern_fallbacks = 0; pattern_free = 0;
    for (uint32_t i = 0; i < PAT_CHUNKS; i++) pattern_link[i] = (uint16_t)(i + 1u < PAT_CHUNKS ? i + 1u : PAT_NONE);
    memset(chain_patterns, 0, sizeof chain_patterns); memset(motion_pattern, 0, sizeof motion_pattern);
    memset(pattern_stage, 0, sizeof pattern_stage);
    for (uint32_t k = 0; k < NTRK; k++) {
        pattern_stage[k].bank = 0xff;
        trk[k].pattern = 0; trk[k].pattern_next = 0xff; trk[k].pattern_gen++;
        for (uint32_t bank = 0; bank < NPAT; bank++) {
            for (uint32_t b = 0; b < PAT_BLOCKS; b++) pattern_blocks[k][bank][b] = (pattern_block_t){PAT_NONE, 0};
            for (uint32_t i = 0; i < 4u; i++) pattern_timings[k][bank][i] = TP[P_SLEN+i].def;
        }
    }
}
static void pattern_switch(track_t *t, uint32_t bank)
{
    pattern_commit(t); pattern_apply(t, bank); t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
}
