/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#include "performance_options.h"
/* A bounded performance store shared by the project's banks (NTRK x NPAT). Step notes
 * are its editable overview; the original onsets, velocities and independent
 * releases live here. QNT commits a grid to the new take when it finishes. */
static recorded_note_t recording[RECORD_MAX] __attribute__((section(".pool")));
/* A live capture cannot also be playing from this slot: recording_fire skips
 * capture holds, and slot replacement cancels the old release before capture
 * starts. Once recording_finish seals duration, held is dead; the release
 * queue can reuse its word. Sounding ownership comes from recording_message,
 * never from the stale counter. The reference countdown uses left itself as
 * an active flag, so retain its separate fields for parity comparisons. */
#if MELODEE_RECORD_QUEUE
static union { uint32_t held, left; } recording_run[RECORD_MAX];
_Static_assert(sizeof recording_run == RECORD_MAX * 4u, "shared capture/deadline counters");
#else
static struct { uint32_t held, left; } recording_run[RECORD_MAX];
#endif
static uint8_t recording_flags[RECORD_MAX]; /* 1: live hold; 2: runtime list; 4: heard this loop; 8: captured in this take; 16: release overview bit 6 */
static uint32_t recording_fraction[NTRK];
/* Index by bank and cache onset arithmetic outside the steady audio tick. */
static uint16_t recording_head[NTRK * NPAT] = {[0 ... NTRK * NPAT - 1u] = RECORD_MAX};
static uint16_t recording_next[RECORD_MAX];
/* Keep the onset cache in ordinary RAM: the expanded event payload remains in
 * the pool, leaving the full audio reserve alongside CPU1's private stacks. */
static uint32_t recording_due[RECORD_MAX];
static uint16_t recording_refs[NTRK][128] __attribute__((section(".pool")));
static struct { uint32_t period, key, ordered_period, ordered_key; uint16_t cursor; } recording_schedule[NTRK];
#if MELODEE_RECORD_CURSOR
/* Only capture holds (or sounding events in countdown mode) are visited. Keep the extra index in
 * the pool, rather than taking the target's remaining data SRAM. */
#if MELODEE_RECORD_QUEUE
/* Only captures own live links; only sounding playback owns heap positions.
 * A sealed capture can linger in the list until the next tick. Detach it
 * before the release queue overwrites its link, preserving other captures. */
static union {uint16_t live[RECORD_MAX],heap[RECORD_MAX];} recording_runtime_index __attribute__((section(".pool")));
_Static_assert(sizeof recording_runtime_index==RECORD_MAX*2u,"shared capture/release indexes");
#define recording_live_next recording_runtime_index.live
#define recording_heap_pos recording_runtime_index.heap
#else
static uint16_t recording_live_next[RECORD_MAX] __attribute__((section(".pool")));
#endif
static uint16_t recording_live_head[NTRK] = {[0 ... NTRK - 1u] = RECORD_MAX};
#endif
static uint32_t recording_view(const track_t *t, const recorded_note_t *r);
#if MELODEE_RECORD_QUEUE
/* Each track can own the entire shared store. Indexed heaps permit
 * O(log N) insertion, retrigger and cancellation without allocating in audio.
 * The position's top six bits retain the release's overview cell; its bottom
 * ten bits address the heap. For longer patterns, a spare runtime flag keeps
 * the seventh overview bit, without enlarging the 1024-entry index. */
_Static_assert(RECORD_MAX == 1024u && NSTEP <= 128u, "packed queue index");
/* The NTRK heaps share one store of 32-entry chunks. Row tr holds track tr's
 * first 32 entries, so the tick's top and small queues index it directly; a
 * deeper chunk is taken from the free stack as a heap grows past a multiple
 * of 32 and returned as it shrinks back. A note sounds in at most one track's
 * heap and at most RECORD_MAX notes sound, so the chunks in use never exceed
 * RECORD_MAX / 32 + NTRK: the store cannot run out. */
#define RECORDING_ROWS (RECORD_MAX / 32u + NTRK)
#define RECORDING_SHARED (RECORDING_ROWS - NTRK)
_Static_assert(RECORDING_ROWS == RECORD_MAX / 32u + NTRK && RECORDING_ROWS <= 128u, "every sounding note has a chunk; row - j fits a byte");
static uint16_t recording_heap_store[RECORDING_ROWS * 32u] __attribute__((section(".pool")));
/* Entries 32j..32j+31 live in row chunk[tr][j] + j: keeping row - j lets
 * (chunk << 5) + at index the store directly. [0] is tr once the heap has
 * grown past 32. */
static int8_t recording_heap_chunk[NTRK][RECORD_MAX / 32u] __attribute__((section(".pool")));
/* Free stack entry k holds a shared row's offset XOR k: the zeroed pool lists them all. */
static uint8_t recording_heap_free[RECORDING_SHARED] __attribute__((section(".pool")));
static uint8_t recording_heap_taken __attribute__((section(".pool")));
/* Entry at of a heap: in its own row, or (deep) wherever the table says. */
static inline __attribute__((always_inline)) uint16_t *recording_heap_slot(uint16_t *row, const int8_t *chunk, uint32_t at, int deep)
{
    return deep ? &recording_heap_store[(uint32_t)(chunk[at >> 5] * 32) + at] : &row[at];
}
#define RQ(tr, at) (*recording_heap_slot(&recording_heap_store[(tr) * 32u], recording_heap_chunk[tr], at, (at) >= 32u))
/* bit 7 marks a pending note-off; only the low seven bits reach MIDI/synth. */
static uint8_t recording_message[RECORD_MAX] __attribute__((section(".pool")));
static uint16_t recording_watch[NTRK][NSTEP] __attribute__((section(".pool")));
static uint32_t recording_watch_bits[NTRK][(NSTEP + 31u) / 32u];
static uint32_t recording_clock[NTRK];
static uint16_t recording_heap_count[NTRK], recording_heap_owner[NTRK], recording_heap_len[NTRK];
#endif
static int recording_sounding(uint32_t i)
{
#if MELODEE_RECORD_QUEUE
    return (recording_message[i] & 128u) != 0;
#else
    return recording_run[i].left != 0;
#endif
}
/* Tests and UI queries use remaining time without updating every voice. */
static uint32_t recording_left(uint32_t i)
{
#if MELODEE_RECORD_QUEUE
    if (!recording_sounding(i)) return 0;
    return recording_run[i].left - recording_clock[recorded_owner(&recording[i]) / NPAT];
#else
    return recording_run[i].left;
#endif
}
#if MELODEE_RECORD_QUEUE
static uint32_t recording_queue_view(uint32_t i)
{
    uint32_t view = recording_heap_pos[i] >> 10;
#if NSTEP > 64u
    view |= (uint32_t)(recording_flags[i] & 16u) << 2;
#endif
    return view;
}
static void recording_queue_set_view(uint32_t i, uint32_t view)
{
    recording_heap_pos[i] = (uint16_t)((recording_heap_pos[i] & 1023u) | (view << 10));
#if NSTEP > 64u
    recording_flags[i] = (uint8_t)((recording_flags[i] & ~16u) | ((view >> 2) & 16u));
#endif
}
static int recording_before(uint32_t a, uint32_t b)
{
    /* The longest supported gate is below 2^31 clock units. Unsigned clock
     * rollover is therefore unambiguous, including a deadline of zero. */
    int32_t d = (int32_t)(recording_run[a].left - recording_run[b].left);
    return d < 0 || (!d && a < b);
}
static inline __attribute__((always_inline)) void recording_heap_put(uint16_t *slot, uint32_t at, uint32_t i)
{
    *slot = (uint16_t)i;
    recording_heap_pos[i] = (recording_heap_pos[i] & 0xFC00u) | (uint16_t)at;
}
static inline __attribute__((always_inline)) void recording_heap_sift(uint32_t tr, uint32_t at, uint32_t i, int deep)
{
    uint16_t *row = &recording_heap_store[tr * 32u];
    const int8_t *chunk = recording_heap_chunk[tr];
#define RS(x) recording_heap_slot(row, chunk, x, deep)
    while (at) {
        uint32_t parent = (at - 1u) / 2u, other = *RS(parent);
        if (!recording_before(i, other)) break;
        recording_heap_put(RS(at), at, other); at = parent;
    }
    for (;;) {
        uint32_t child = at * 2u + 1u;
        if (child >= recording_heap_count[tr]) break;
        if (child + 1u < recording_heap_count[tr] && recording_before(*RS(child + 1u), *RS(child))) child++;
        uint32_t other = *RS(child);
        if (!recording_before(other, i)) break;
        recording_heap_put(RS(at), at, other); at = child;
    }
    recording_heap_put(RS(at), at, i);
#undef RS
}
/* A heap that fits its own row (most queues) sifts as directly as the old
 * per-track arrays; only a deeper one pays the chunk lookup. Two functions
 * keep the compiler from folding them back into one with a runtime flag. */
static __attribute__((noinline)) void recording_heap_fix_row(uint32_t tr, uint32_t at, uint32_t i) { recording_heap_sift(tr, at, i, 0); }
static __attribute__((noinline)) void recording_heap_fix_deep(uint32_t tr, uint32_t at, uint32_t i) { recording_heap_sift(tr, at, i, 1); }
static inline __attribute__((always_inline)) void recording_heap_fix(uint32_t tr, uint32_t at, uint32_t i)
{
    if (recording_heap_count[tr] <= 32u) recording_heap_fix_row(tr, at, i);
    else recording_heap_fix_deep(tr, at, i);
}
/* Take the next chunk: 0 only if the free stack is empty, which the bound
 * above rules out (the caller then schedules no release). */
static __attribute__((noinline)) int recording_heap_grow(uint32_t tr)
{
    uint32_t at = recording_heap_count[tr], k = recording_heap_taken;
    if (k >= RECORDING_SHARED || at >= RECORD_MAX) return 0;
    recording_heap_chunk[tr][0] = (int8_t)tr;   /* (the deep sift reads every row through the table) */
    recording_heap_chunk[tr][at >> 5] = (int8_t)(NTRK + (recording_heap_free[k] ^ k) - (at >> 5));
    recording_heap_taken = (uint8_t)(k + 1u);
    return 1;
}
/* Before count++: a new chunk only as it crosses a multiple of 32. */
static inline __attribute__((always_inline)) int recording_heap_room(uint32_t tr)
{
    uint32_t at = recording_heap_count[tr];
    return at < 32u || (at & 31u) || recording_heap_grow(tr);
}
static void recording_queue_remove(uint32_t i)
{
    if (!recording_sounding(i)) return;
    uint32_t tr = recorded_owner(&recording[i]) / NPAT, pos = recording_heap_pos[i];
    uint32_t view = recording_queue_view(i), at = pos & 1023u;
    if (!--recording_watch[tr][view]) recording_watch_bits[tr][view >> 5] &= ~(1u << (view & 31u));
    recording_message[i] = 0;
    uint32_t last = --recording_heap_count[tr];
    if (last < 32u) {                           /* (most queues: their own row only) */
        if (at != last) recording_heap_fix_row(tr, at, recording_heap_store[tr * 32u + last]);
        return;
    }
    if (at != last) recording_heap_fix(tr, at, RQ(tr, last));
    if (!(last & 31u)) {                        /* that chunk is empty now */
        uint32_t k = --recording_heap_taken;
        recording_heap_free[k] = (uint8_t)((recording_heap_chunk[tr][last >> 5] + (last >> 5) - NTRK) ^ k);
    }
}
static void recording_live_detach(uint32_t i);
static int recording_queue_add(track_t *t, uint32_t i, uint32_t remaining)
{
    uint32_t tr = trk_index(t), was = recording_sounding(i);
    if (!recording_heap_count[tr]) {
        recording_heap_owner[tr] = (uint16_t)(tr * NPAT + t->pattern);
        recording_heap_len[tr] = (uint16_t)t->p[P_SLEN];
    }
    /* An edit can change a sounding event's overview. Remove/reinsert also
     * cancels its old deadline rather than leaving a stale release behind. */
    if (was) recording_queue_remove(i);
    if (recording_flags[i]&2u) recording_live_detach(i); /* before the shared index becomes a heap position */
    if (!recording_heap_room(tr)) return 0;
    uint32_t view = recording_view(t, &recording[i]);
    recording_run[i].left = recording_clock[tr] + remaining;
    recording_message[i] = recorded_note(&recording[i]) | 128u;
    recording_queue_set_view(i, view);
    recording_watch[tr][view]++;
    recording_watch_bits[tr][view >> 5] |= 1u << (view & 31u);
    uint32_t at = recording_heap_count[tr]++;
    recording_heap_fix(tr, at, i);
    return 1;
}
#endif
static volatile uint8_t recording_full;
static uint8_t recording_tracks;
static volatile uint32_t recording_generation; /* ISR or UI changed event storage */
static volatile uint8_t recording_quant_blocked;
static void recording_prune(track_t *t);

static void recording_live_add(uint32_t i)
{
#if MELODEE_RECORD_CURSOR
#if MELODEE_RECORD_QUEUE
    if (!(recording_flags[i] & 1u)) return; /* capture holds only */
#endif
    if (recording_flags[i] & 2u) return;
    uint32_t tr = recorded_owner(&recording[i]) / NPAT;
    recording_live_next[i] = recording_live_head[tr];
    recording_live_head[tr] = (uint16_t)i;
    recording_flags[i] |= 2u;
#endif
}
static void recording_live_detach(uint32_t i)
{
#if MELODEE_RECORD_CURSOR
    if (!(recording_flags[i] & 2u)) return;
    uint16_t *link = &recording_live_head[recorded_owner(&recording[i]) / NPAT];
    while (*link < RECORD_MAX && *link != i) link = &recording_live_next[*link];
    if (*link == i) *link = recording_live_next[i];
    recording_flags[i] &= (uint8_t)~2u;
#endif
}

static uint32_t recording_owner(const track_t *t) { return trk_index(t) * NPAT + t->pattern; }
static uint32_t recording_prefix(const track_t *t, uint32_t period, uint32_t idx)
{
    uint32_t a = step_samples(t, period, 0), b = step_samples(t, period, 1);
    return (idx / 2u) * (a + b) + (idx & 1u) * a;
}
static uint32_t recording_loop(const track_t *t, uint32_t period)
{
    return recording_prefix(t, period, (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP));
}
static int recording_valid(const recorded_note_t *r)
{
    return !r->vel || (r->pitch >= -24 && r->pitch <= 24 && recorded_owner(r) < NTRK * NPAT && recorded_step(r) < NSTEP && r->duration && recorded_note(r) <= 127u && ((r->step & 128u) || recorded_step(r) + ((r->step & 64u) != 0u) < NSTEP));
}
static uint32_t recording_view(const track_t *t, const recorded_note_t *r)
{
    (void)t;
    return r->step & 128u ? 0u : recorded_step(r) + ((r->step & 64u) != 0u);
}
static int recording_present(const recorded_note_t *r)
{
    uint32_t owner = recorded_owner(r), k = owner / NPAT, bank = owner % NPAT;
    uint32_t view = recording_view(&trk[k], r);
    if (view >= NSTEP) return 0;
    step_t step = pattern_step(k, bank, view);
    return step.time == ST_NOTE && (step.flags & SF_RECORDED);
}
static int recording_active(const track_t *t, uint32_t i)
{
    const recorded_note_t *r = &recording[i];
    if (!r->vel || recorded_owner(r) != recording_owner(t) || recorded_step(r) >= (uint32_t)t->p[P_SLEN]) return 0;
    uint32_t view = recording_view(t, r);
    return t->step[view].time == ST_NOTE && (t->step[view].flags & SF_RECORDED);
}
#if MELODEE_RECORD_INDEX
/* Active banks only: exact pitch bits per rounded overview cell (4096 B).
 * Dirty cells coalesce deletions/edits; rebuilding scans one bank once, never
 * once per removed duplicate. Overview flags stay live at query time. The key
 * includes original-onset LEN eligibility, which differs for wrapped cells. */
/* Cache the four note-position and eight fixed-lane ownership bits, plus
 * the exact overview note key. Timed event eligibility remains independent. */
static uint16_t recording_pitch[NTRK][NSTEP] __attribute__((section(".pool")));
static uint32_t recording_pitch_notes[NTRK][NSTEP] __attribute__((section(".pool")));
static uint32_t recording_pitch_dirty[NTRK][(NSTEP + 31u) / 32u];
static uint16_t recording_pitch_key[NTRK],recording_pitch_count[NTRK*NPAT];
static uint32_t recording_pitch_current(const track_t *t)
{return 1u+t->pattern+(uint32_t)t->p[P_SLEN]*NPAT;}
static uint32_t recording_pitch_note_key(const step_t *s)
{return s->note[0]|(uint32_t)s->note[1]<<8|(uint32_t)s->note[2]<<16|(uint32_t)s->note[3]<<24;}
static uint16_t recording_pitch_mask(uint32_t note,uint32_t key)
{
    /* Exact zero-byte markers without cross-byte borrow. Pack their high
     * bits into the four note-position bits. Inverse default lane mapping
     * follows eng_drum.c DRUM_LANE_NOTE; the scanner tests every MIDI pitch. */
    static const uint8_t lane[21]={1,64,2,4,0,0,8,0,0,32,16,0,0,0,0,0,0,0,0,0,128};
    _Static_assert(NLANE==8u,"recording lane mask width");
    uint32_t x=key^(note*0x01010101u);
    uint32_t z=~(((x&0x7f7f7f7fu)+0x7f7f7f7fu)|x|0x7f7f7f7fu);
    uint32_t result=((z>>7)*0x01020408u)>>24;
    if(note>=36u && note<=56u)result|=(uint32_t)lane[note-36u]<<8;
    return (uint16_t)result;
}
static void recording_pitch_invalidate(void)
{memset(recording_pitch_key,0,sizeof recording_pitch_key);}
static void recording_pitch_remove(uint32_t i)
{
    const recorded_note_t *r=&recording[i];uint32_t tr=recorded_owner(r)/NPAT,view=recording_view(&trk[tr],r);
    if(r->vel && view<NSTEP && recorded_owner(r)==recording_owner(&trk[tr]) && recording_pitch_key[tr]==recording_pitch_current(&trk[tr]))
        recording_pitch_dirty[tr][view>>5]|=1u<<(view&31u);
}
static void recording_pitch_add(uint32_t i)
{
    const recorded_note_t *r=&recording[i];uint32_t tr=recorded_owner(r)/NPAT,view=recording_view(&trk[tr],r);
    if(r->vel && view<NSTEP && recorded_step(r)<(uint32_t)trk[tr].p[P_SLEN] &&
       recorded_owner(r)==recording_owner(&trk[tr]) && recording_pitch_key[tr]==recording_pitch_current(&trk[tr]))
        recording_pitch[tr][view]|=recording_pitch_mask(recorded_note(r),recording_pitch_notes[tr][view]);
}
static void recording_pitch_prepare(track_t *t)
{
    uint32_t tr=trk_index(t),key=recording_pitch_current(t);
    uint32_t dirty[(NSTEP+31u)/32u],any=0;
    for(uint32_t w=0;w<NELEM(dirty);w++){
        dirty[w]=recording_pitch_key[tr]!=key?UINT32_MAX:recording_pitch_dirty[tr][w];any|=dirty[w];
    }
    if(!any)return;
    for(uint32_t v=0;v<NSTEP;v++)if((dirty[v>>5]>>(v&31u))&1u){
        recording_pitch[tr][v]=0;recording_pitch_notes[tr][v]=recording_pitch_note_key(&t->step[v]);
    }
    for(uint32_t i=recording_head[recording_owner(t)];i<RECORD_MAX;i=recording_next[i]){
        const recorded_note_t *r=&recording[i];uint32_t view=recording_view(t,r);
        if(r->vel && recorded_step(r)<(uint32_t)t->p[P_SLEN] && view<NSTEP && ((dirty[view>>5]>>(view&31u))&1u))
            recording_pitch[tr][view]|=recording_pitch_mask(recorded_note(r),recording_pitch_notes[tr][view]);
    }
    recording_pitch_key[tr]=(uint16_t)key;memset(recording_pitch_dirty[tr],0,sizeof recording_pitch_dirty[tr]);
}
#else
#define recording_pitch_invalidate() ((void)0)
#define recording_pitch_remove(i) ((void)0)
#define recording_pitch_add(i) ((void)0)
#define recording_pitch_prepare(t) ((void)0)
#endif
/* This mask suppresses only overview duplicates. Timed onset/release ordering
 * and the separately maintained sounding-pitch reference counts are unchanged. */
static uint32_t recording_step_skip(track_t *t,const step_t *s,uint32_t skip)
{
    if(!(s->flags&SF_RECORDED))return skip;
#if MELODEE_RECORD_INDEX
    /* Very sparse banks are cheaper with the original scanner. Counts follow
     * the same links, so density changes need no additional traversal. */
    if(recording_pitch_count[recording_owner(t)]>=4u){
        /* Edits and bank/LEN changes pay one bounded maintenance scan. */
        if(t->seq_idx>=NSTEP || t->step[t->seq_idx].time!=ST_NOTE || !(t->step[t->seq_idx].flags&SF_RECORDED))return skip;
        if(s!=&t->step[t->seq_idx])goto scan;
        uint32_t tr=trk_index(t),view=t->seq_idx;
        if(recording_pitch_note_key(s)!=recording_pitch_notes[tr][view])recording_pitch_dirty[tr][view>>5]|=1u<<(view&31u);
        recording_pitch_prepare(t);
        return skip | (recording_pitch[tr][view] & (((1u<<s->n)-1u)|0xff00u));
    }
#endif
scan:
    for(uint32_t r=recording_head[recording_owner(t)];r<RECORD_MAX;r=recording_next[r])if(recording_active(t,r) && recording_view(t,&recording[r])==t->seq_idx){
        for(uint32_t k=0;k<s->n;k++)if(s->note[k]==recorded_note(&recording[r]))skip|=1u<<k;
        for(uint32_t k=0;k<NLANE;k++)if(DRUM_LANE_NOTE[k]==recorded_note(&recording[r]))skip|=1u<<(8u+k);
    }
    return skip;
}
static int recording_owns(const track_t *t, uint32_t note)
{
    return recording_refs[trk_index(t)][note & 127u] != 0;
}
static void recording_off(track_t *t, uint32_t note)
{
    if (recording_owns(t, note) || t->arp_note == note ||
        (live_held[trk_index(t)][note >> 5] & (1u << (note & 31u)))) return;
    for (uint32_t k = 0; k < t->seq_n; k++) if (t->seq_notes[k] == note) return;
    trk_note_off(t, note);
}
/* Reference counts keep overlapping same-pitch releases constant-time even
 * when a dense take is snapped to one boundary. */
static void recording_end(track_t *t, uint32_t i)
{
    if (!recording_sounding(i)) return;
    uint32_t note = recorded_note(&recording[i]);
#if MELODEE_RECORD_QUEUE
    note = recording_message[i] & 127u;
    recording_queue_remove(i);
#endif
    recording_run[i].left = 0;
    uint16_t *count = &recording_refs[trk_index(t)][note];
    if (*count) --*count;
    recording_off(t, note);
}
#if MELODEE_RECORD_QUEUE
static void recording_queue_refresh(track_t *t)
{
    uint32_t tr = trk_index(t);
    /* A sparse edit may invalidate a buried node; scanning stable slot IDs
     * avoids visiting an entry twice or skipping one as the heap moves. */
    for (uint32_t i = 0; i < RECORD_MAX; i++)
        if (recording_sounding(i) && recorded_owner(&recording[i]) / NPAT == tr && !recording_active(t, i)) recording_end(t, i);
    memset(recording_watch[tr], 0, sizeof recording_watch[tr]);
    memset(recording_watch_bits[tr], 0, sizeof recording_watch_bits[tr]);
    for (uint32_t at = 0; at < recording_heap_count[tr]; at++) {
        uint32_t i = RQ(tr, at), view = recording_view(t, &recording[i]);
        recording_queue_set_view(i, view);
        recording_watch[tr][view]++;
        recording_watch_bits[tr][view >> 5] |= 1u << (view & 31u);
    }
}
#endif
static void recording_reindex(void)
{
    recording_pitch_invalidate();
#if MELODEE_RECORD_INDEX
    memset(recording_pitch_count,0,sizeof recording_pitch_count);
#endif
    recording_generation++;
    for (uint32_t k = 0; k < NTRK * NPAT; k++) recording_head[k] = RECORD_MAX;
    recording_tracks = 0;
    memset(recording_refs, 0, sizeof recording_refs);
#if MELODEE_RECORD_QUEUE
    memset(recording_heap_count, 0, sizeof recording_heap_count);
    memset(recording_heap_free, 0, sizeof recording_heap_free);
    recording_heap_taken = 0;
    memset(recording_watch, 0, sizeof recording_watch);
    memset(recording_watch_bits, 0, sizeof recording_watch_bits);
#endif
#if MELODEE_RECORD_CURSOR
    for (uint32_t tr = 0; tr < NTRK; tr++) recording_live_head[tr] = RECORD_MAX;
    for (uint32_t i = 0; i < RECORD_MAX; i++) recording_flags[i] &= (uint8_t)~2u;
#endif
    for (uint32_t i = RECORD_MAX; i-- > 0;) if (recording[i].vel) {
        uint32_t owner = recorded_owner(&recording[i]);
#if MELODEE_RECORD_INDEX
        recording_pitch_count[owner]++;
#endif
        recording_next[i] = recording_head[owner]; recording_head[owner] = (uint16_t)i;
        recording_tracks |= (uint8_t)(1u << (owner / NPAT));
        if (recording_sounding(i) && trk[owner / NPAT].pattern == owner % NPAT) {
            recording_refs[owner / NPAT][recorded_note(&recording[i])]++;
#if MELODEE_RECORD_QUEUE
            uint32_t tr = owner / NPAT, view = recording_view(&trk[owner / NPAT], &recording[i]);
            recording_queue_set_view(i, view);
            recording_watch[tr][view]++;
            recording_watch_bits[tr][view >> 5] |= 1u << (view & 31u);
            recording_heap_room(tr);    /* (never full: the store was reset above, each slot is visited once) */
            uint32_t at = recording_heap_count[tr]++;
            recording_heap_fix(tr, at, i);
#endif
        }
        if ((recording_sounding(i) || (recording_flags[i] & 1u)) && trk[owner / NPAT].pattern == owner % NPAT) recording_live_add(i);
    }
    memset(recording_schedule, 0, sizeof recording_schedule);
}
static void recording_unlink(uint32_t i)
{
    if (!recording[i].vel) return;
    recording_pitch_remove(i);
    recording_live_detach(i);
    recording_schedule[recorded_owner(&recording[i]) / NPAT].period = 0;
    uint16_t *link = &recording_head[recorded_owner(&recording[i])];
    while (*link < RECORD_MAX && *link != i) link = &recording_next[*link];
    if (*link == i) {
        *link = recording_next[i];
#if MELODEE_RECORD_INDEX
        recording_pitch_count[recorded_owner(&recording[i])]--;
#endif
    }
}
static void recording_reset(void)
{
    recording_undo_clear();
    memset(recording, 0, sizeof recording);
    memset(recording_run, 0, sizeof recording_run);
#if MELODEE_RECORD_QUEUE
    memset(recording_message, 0, sizeof recording_message);
    memset(recording_clock, 0, sizeof recording_clock);
#endif
    memset(recording_flags, 0, sizeof recording_flags);
    memset(recording_fraction, 0, sizeof recording_fraction);
    recording_full = 0;
    recording_quant_blocked = 0;
    recording_reindex();
}
static recorded_note_t recording_snapshot(uint32_t i)
{
    recorded_note_t r = recording[i];
    if (recording_flags[i] & 1u) {
        uint32_t held = recording_run[i].held, exponent = 0;
        while (held > 65535u && exponent < 7u) { held >>= 1; exponent++; }
        r.duration = (uint16_t)clamp((int32_t)held, 1, 65535);
        recorded_set_exponent(&r, exponent);
    }
    return r;
}
static void recording_finish(uint32_t i)
{
    recording[i] = recording_snapshot(i);
    recording_generation++;
    recording_flags[i] &= (uint8_t)~1u;
}
static void recording_stop(track_t *t)
{
    uint32_t owner = recording_owner(t);
    uint8_t clear = 4u | (!song.playing && !t->p[P_RECQ] ? 8u : 0u);
    for (uint32_t i = recording_head[owner]; i < RECORD_MAX; i = recording_next[i]) {
        if ((recording_flags[i] & 1u)) recording_finish(i);
        recording_end(t, i);
        recording_flags[i] &= (uint8_t)~clear;
    }
#if MELODEE_RECORD_CURSOR
    uint32_t tr = trk_index(t);
    for (uint32_t i = recording_live_head[tr]; i < RECORD_MAX; i = recording_live_next[i]) recording_flags[i] &= (uint8_t)~2u;
    recording_live_head[tr] = RECORD_MAX;
#endif
}
/* Punch-out finishes the take without releasing the pattern's playback voices. */
static void recording_seal(track_t *t)
{
    uint8_t clear = t->p[P_RECQ] ? 0u : 8u;
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i]) {
        if (recording_flags[i] & 1u) recording_finish(i);
        recording_flags[i] &= (uint8_t)~clear;
    }
}
/* Onsets are fractions within their original swung step. Durations are
 * nominal-step fractions with a compact exponent. Optional quantization finds the nearest swung grid boundary,
 * considering the end of the loop as another copy of zero. */
static uint32_t recording_raw_on(const track_t *t, const recorded_note_t *r, uint32_t period)
{
    uint32_t loop = recording_loop(t, period);
    uint32_t span = step_samples(t, period, recorded_step(r));
    return (recording_prefix(t, period, recorded_step(r)) + (uint32_t)(((uint64_t)r->on * span + RECORD_UNIT / 2u) / RECORD_UNIT)) % loop;
}
/* The step an event belongs to on the grid (NOTES selection, live erase): the unswung step its onset plays in.
 * SWING stretches each even step over the next one's straight onset, so a note played on the grid for an odd step
 * is stored in the even interval. Straight, this is its actual interval. */
static uint32_t recording_grid_step(const track_t *t, const recorded_note_t *r)
{
    if (!track_swing(t)) return recorded_step(r);
    uint32_t period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    return recording_raw_on(t, r, period) / period % (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
}
static uint32_t recording_quant_on(const track_t *t, const recorded_note_t *r, uint32_t period, uint32_t q)
{
    uint32_t loop = recording_loop(t, period), at = recording_raw_on(t, r, period);
    if (!q) return at;
    uint32_t grid = seq_div_samples(q - 1u), a = step_samples(t, grid, 0), b = step_samples(t, grid, 1);
    uint32_t base = at / (a + b) * (a + b), best = 0, distance = at;
    uint32_t candidates[] = {base, base + a, base + a + b, loop};
    for (uint32_t k = 0; k < NELEM(candidates); k++) {
        uint32_t v = candidates[k];
        if (v > loop) continue;
        uint32_t d = v > at ? v - at : at - v;
        if (d < distance || (d == distance && v > best)) { best = v; distance = d; }
    }
    return best == loop ? 0u : best;
}
static uint32_t recording_on(const track_t *t, const recorded_note_t *r, uint32_t period)
{
    return recording_raw_on(t, r, period);
}
static uint32_t recording_key(const track_t *t)
{
    return (uint32_t)t->p[P_RECQ] | (uint32_t)t->p[P_SSWING] << 4 |
        (uint32_t)song.g[G_SWING] << 11 | (uint32_t)t->p[P_SLEN] << 18 | (uint32_t)t->pattern << 25;
}
static void recording_insert(track_t *t, uint32_t i)
{
    uint32_t owner = recording_owner(t), tr = trk_index(t);
    uint16_t *link = &recording_head[owner];
#if MELODEE_RECORD_CURSOR
    uint32_t period = seq_div_samples((uint32_t)t->p[P_SDIV]), key = recording_key(t);
    if (recording_schedule[tr].ordered_period == period && recording_schedule[tr].ordered_key == key) {
        /* Inserting one captured/edited event does not recalculate or sort
         * every retained onset. Equal onsets retain the old prepend order. */
        recording_due[i] = recording_on(t, &recording[i], period);
        while (*link < RECORD_MAX && recording_due[*link] < recording_due[i]) link = &recording_next[*link];
    } else recording_schedule[tr].ordered_period = 0;
#endif
    recording_next[i] = *link; *link = (uint16_t)i;
#if MELODEE_RECORD_INDEX
    recording_pitch_count[owner]++;
#endif
    recording_pitch_add(i);
    recording_schedule[tr].period = 0;
}
/* Stable bottom-up merge sort of the bank's existing index. No new event
 * copies, scratch array, allocation, or recursion in the audio owner. */
static void recording_sort(uint32_t owner)
{
#if MELODEE_RECORD_CURSOR
    for (uint32_t width = 1;; width *= 2u) {
        uint32_t p = recording_head[owner], merges = 0;
        uint16_t head = RECORD_MAX, *tail = &head;
        while (p < RECORD_MAX) {
            uint32_t q = p, pn = 0, qn = width;
            for (uint32_t k = 0; k < width && q < RECORD_MAX; k++) { pn++; q = recording_next[q]; }
            merges++;
            while (pn || (qn && q < RECORD_MAX)) {
                uint32_t i;
                if (!pn) { i = q; q = recording_next[q]; qn--; }
                else if (!qn || q >= RECORD_MAX || recording_due[p] <= recording_due[q]) {
                    i = p; p = recording_next[p]; pn--;
                } else { i = q; q = recording_next[q]; qn--; }
                *tail = (uint16_t)i; tail = &recording_next[i];
            }
            p = q;
        }
        *tail = RECORD_MAX;
        recording_head[owner] = head;
        if (merges <= 1u) break;
    }
#endif
}
static void recording_sync(track_t *t, uint32_t period)
{
    uint32_t tr = trk_index(t), key = recording_key(t);
    if (recording_schedule[tr].period == period && recording_schedule[tr].key == key) return;
    recording_schedule[tr].period = period; recording_schedule[tr].key = key;
#if MELODEE_RECORD_QUEUE
    if (recording_heap_count[tr] && (recording_schedule[tr].ordered_period != period || recording_schedule[tr].ordered_key != key)) recording_queue_refresh(t);
#endif
    if (!MELODEE_RECORD_CURSOR || recording_schedule[tr].ordered_period != period || recording_schedule[tr].ordered_key != key) {
        for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i])
            recording_due[i] = recording_on(t, &recording[i], period);
        recording_sort(recording_owner(t));
        recording_schedule[tr].ordered_period = period;
        recording_schedule[tr].ordered_key = key;
    }
    recording_schedule[tr].cursor = recording_head[recording_owner(t)];
}
static uint32_t recording_play_on(const track_t *t, uint32_t i, uint32_t period)
{
    (void)t; (void)period;
    return recording_due[i];
}
#if MELODEE_CAPTURE_FAST
static void recording_remove(track_t *t, uint32_t i);
#endif
static int recording_note(track_t *t, uint32_t note, uint32_t vel, uint32_t step)
{
#if MELODEE_CAPTURE_FAST
    uint32_t owner = recording_owner(t), period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    uint32_t actual = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_idx;
    uint32_t span = step_samples(t, period, actual), pos = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_pos;
    uint32_t on = (uint32_t)(((uint64_t)pos * RECORD_UNIT + span / 2u) / span), free = RECORD_MAX;
    if (on > 65535u) on = 65535u;
    /* Every event of this owner, and only those, is on its bank list. One
     * walk retires overwritten overview cells (as recording_prune does, in
     * list order), seals this pitch's live hold and keeps the highest-index
     * same-time replacement, which wins as it does in the full scan. */
    for (uint32_t i = recording_head[owner]; i < RECORD_MAX;) {
        uint32_t next = recording_next[i];
        recorded_note_t *r = &recording[i];
        uint32_t view = recording_view(t, r);
        if (view >= NSTEP || t->step[view].time != ST_NOTE || !(t->step[view].flags & SF_RECORDED)) {
            recording_remove(t, i);
        } else if (recorded_note(r) == note) {
            if (recording_flags[i] & 1u) recording_finish(i);
            if (recorded_step(r) == actual && r->on == on && (free == RECORD_MAX || i > free)) free = i;
        }
        i = next;
    }
    /* Otherwise the lowest empty or retired slot. This owner's remaining
     * events were just found present, so they can never qualify. */
    for (uint32_t i = 0; free == RECORD_MAX && i < RECORD_MAX; i++) {
        const recorded_note_t *r = &recording[i];
        if (!r->vel || (recorded_owner(r) != owner && !recording_present(r) && !recording_sounding(i) && !(recording_flags[i] & 1u)))
            free = i;
    }
#else
    /* Retire overwritten overview cells before a new take can own them again. */
    recording_prune(t);
    uint32_t owner = recording_owner(t), period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    uint32_t actual = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_idx;
    uint32_t span = step_samples(t, period, actual), pos = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_pos;
    uint32_t on = (uint32_t)(((uint64_t)pos * RECORD_UNIT + span / 2u) / span), free = RECORD_MAX;
    if (on > 65535u) on = 65535u;
    for (uint32_t i = 0; i < RECORD_MAX; i++) {
        recorded_note_t *r = &recording[i];
        if (recorded_owner(r) == owner && (recording_flags[i] & 1u) && recorded_note(r) == note) recording_finish(i);
        /* Repeated passes replace the same pitch at the same captured time.
         * Distinct hits in one step remain separate events. */
        uint32_t same_time = r->on == on;
        if (r->vel && recorded_owner(r) == owner && recorded_note(r) == note && recorded_step(r) == actual && same_time) free = i;
        else if (free == RECORD_MAX && (!r->vel || (!recording_present(r) && !recording_sounding(i) && !(recording_flags[i] & 1u)))) free = i;
    }
#endif
    if (free == RECORD_MAX) { recording_full = 1; return 0; }
    recording_undo_capture(t);
    if (recording_sounding(free)) recording_end(&trk[recorded_owner(&recording[free]) / NPAT], free);
    recording_unlink(free);
    recorded_note_t *r = &recording[free];
    *r = (recorded_note_t){.on=(uint16_t)on, .duration=1, .note7=(uint8_t)note, .vel=(uint8_t)vel};
    recorded_set_owner(r, owner); recorded_set_step(r, actual, step != actual, step == 0u && actual);
    recording_insert(t, free);
    memset(&recording_run[free], 0, sizeof recording_run[free]);
    recording_flags[free] = 13;
    recording_live_add(free);
    t->step[step].flags |= SF_RECORDED;
    recording_tracks |= (uint8_t)(1u << trk_index(t));
    recording_generation++;
    return 1;
}

/* The UI calls these with audio interrupts paused. Remove only this event,
 * ending its reference-counted voice; history keeps its original bytes. */
static void recording_remove(track_t *t, uint32_t i)
{
    recording_end(t, i);
    recording_unlink(i);
    memset(&recording[i], 0, sizeof recording[i]);
    memset(&recording_run[i], 0, sizeof recording_run[i]);
    recording_flags[i] = 0;
    recording_schedule[trk_index(t)].period = 0;
    recording_generation++;
}
/* Clearing a bank must erase its events too, including dormant events outside
 * LEN. A cleared overview alone is not a deletion: the next take can revive it. */
static void recording_clear(track_t *t)
{
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX;) {
        uint32_t next = recording_next[i];
        recording_remove(t, i);
        i = next;
    }
    t->rh_n = t->rh_ties = t->rskip_n = 0;
    t->rh_elapsed = 0;
}
static void recording_prune(track_t *t)
{
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX;) {
        uint32_t next = recording_next[i];
        if (!recording_present(&recording[i])) recording_remove(t, i);
        i = next;
    }
}
static void recording_restore_note(track_t *t, uint32_t i, recorded_note_t r)
{
    recording_pitch_remove(i);
    recording_end(&trk[recorded_owner(&recording[i]) / NPAT], i);
    recording_live_detach(i);
    recording[i] = r;
    memset(&recording_run[i], 0, sizeof recording_run[i]);
    recording_flags[i] = 0;
    recording_insert(t, i);
    recording_tracks |= (uint8_t)(1u << trk_index(t));
    recording_schedule[trk_index(t)].period = 0;
    recording_generation++;
}

/* the recorded notes of owner (track * NPAT + bank) gone; the index is the caller's to rebuild (recording_reindex) */
static void recording_drop(track_t *t, uint32_t owner)
{
    for (uint32_t i = 0; i < RECORD_MAX; i++) if (recorded_owner(&recording[i]) == owner) {
        recording_end(t, i);
        memset(&recording[i], 0, sizeof recording[i]);
        memset(&recording_run[i], 0, sizeof recording_run[i]); recording_flags[i] = 0;
    }
}
/* Copy recordings atomically with a bank (from track from's to track t's). Check capacity before mutating
 * either the destination's steps, automation or performance. */
static int recording_copy(const track_t *from, uint32_t source, track_t *t, uint32_t dest, int apply)
{
    uint32_t src = trk_index(from) * NPAT + source, dst = trk_index(t) * NPAT + dest, count = 0, free = 0;
    for (uint32_t i = 0; i < RECORD_MAX; i++) {
        if (recording[i].vel && recorded_owner(&recording[i]) == src) count++;
        if (!recording[i].vel || recorded_owner(&recording[i]) == dst) free++;
    }
    if (count > free) return 2;
    if (!apply) return 0;
    recording_drop(t, dst);
    for (uint32_t i = 0; i < RECORD_MAX; i++) if (recording[i].vel && recorded_owner(&recording[i]) == src)
        for (uint32_t j = 0; j < RECORD_MAX; j++) if (!recording[j].vel) {
            recording[j] = recording_snapshot(i); recorded_set_owner(&recording[j], dst);
            memset(&recording_run[j], 0, sizeof recording_run[j]); recording_flags[j] = 0;
            break;
        }
    recording_reindex();
    return 0;
}
/* A doubled bank (motion.c pattern_double): the events of its first len steps copied len steps later, the dormant
 * ones there dropped first. Capacity is checked before anything changes; the index is the caller's to rebuild */
static int recording_double(track_t *t, uint32_t bank, uint32_t len, int apply)
{
    uint32_t owner = trk_index(t) * NPAT + bank, count = 0, free = 0, i, j;
    for (i = 0; i < RECORD_MAX; i++) {
        uint32_t a = recorded_step(&recording[i]), mine = recording[i].vel && recorded_owner(&recording[i]) == owner;
        if (mine && a < len) count++;
        if (!recording[i].vel || (mine && a >= len && a < 2u * len)) free++;
    }
    if (count > free) return 2;
    if (!apply) return 0;
    for (i = 0; i < RECORD_MAX; i++)
        if (recording[i].vel && recorded_owner(&recording[i]) == owner && recorded_step(&recording[i]) >= len &&
            recorded_step(&recording[i]) < 2u * len) {
            recording_end(t, i);
            memset(&recording[i], 0, sizeof recording[i]);
            memset(&recording_run[i], 0, sizeof recording_run[i]); recording_flags[i] = 0;
        }
    for (i = j = 0; i < RECORD_MAX; i++)                /* (a copy lands past len: never copied again) */
        if (recording[i].vel && recorded_owner(&recording[i]) == owner && recorded_step(&recording[i]) < len) {
            while (recording[j].vel) j++;
            recording[j] = recording_snapshot(i);
            recorded_set_step(&recording[j], recorded_step(&recording[j]) + len, recorded_rounded(&recording[j]), recorded_wrapped(&recording[j]));
            /* Rounded forward past the longer loop's end: shown on its first step, as a wrapped original is. */
            if ((recording[j].step & 64u) && recorded_step(&recording[j]) + 1u == 2u * len) recording[j].step |= 128u;
            memset(&recording_run[j], 0, sizeof recording_run[j]); recording_flags[j] = 0;
        }
    return 0;
}
static void recording_release(track_t *t, uint32_t note)
{
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i])
        if (recorded_note(&recording[i]) == note && (recording_flags[i] & 1u))
            recording_finish(i);
}
static uint32_t recording_gate(const recorded_note_t *r, uint32_t period)
{
    uint32_t gate = (uint32_t)(((uint64_t)r->duration << (recorded_exponent(r))) * period / RECORD_UNIT);
    return gate ? gate : 1u;
}
static void recording_fire(track_t *t, uint32_t i, uint32_t period, uint32_t late)
{
    recorded_note_t *r = &recording[i];
    if (recording_flags[i] & 4u) return;
    recording_flags[i] |= 4u;
    if ((recording_flags[i] & 1u) || ((seq_hush >> trk_index(t)) & 1u)) return;   /* (an edit waiting: seq.c) */
    uint32_t chance = step_chance(&t->step[recording_view(t, r)]);
    if (chance < 100u && rng() % 100u >= chance) return;
    uint32_t gate = recording_gate(r, period);
    drum_event_pitch=r->pitch; drum_event_length=recorded_gate(r)?gate:0;
    trk_note_on(t, recorded_note(r), r->vel);
    chord_auto_observe(t, recorded_note(r));
    drum_event_pitch=0; drum_event_length=0;
    uint32_t was = recording_sounding(i), remaining = gate > late ? gate - late : 0u;
#if MELODEE_RECORD_QUEUE
    if (remaining && !recording_queue_add(t, i, remaining)) remaining = 0;
    if (!remaining) { recording_queue_remove(i); recording_run[i].left = 0; }
#else
    recording_run[i].left = remaining;
#endif
    if (remaining && !was) recording_refs[trk_index(t)][recorded_note(r)]++;
    if (!remaining) {
        if (was && recording_refs[trk_index(t)][recorded_note(r)]) recording_refs[trk_index(t)][recorded_note(r)]--;
        recording_off(t, recorded_note(r));
    }
    if (remaining) recording_live_add(i);
}
/* seek: playback starts mid-loop (a song cursor). An onset behind the playhead
 * sounds only while its gate is still open there; the ones already over are
 * heard for this loop, not fired together as a burst. */
static int recording_behind(uint32_t i, uint32_t on, uint32_t phase, uint32_t period, uint32_t seek)
{
    if (!seek || on >= phase || phase - on < recording_gate(&recording[i], period)) return 0;
    recording_flags[i] |= 4u;
    return 1;
}
static void recording_zero(track_t *t, uint32_t seek)
{
    uint32_t period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    uint32_t phase = recording_prefix(t, period, t->seq_idx) + t->seq_pos;
    recording_sync(t, period);
#if MELODEE_RECORD_CURSOR
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i]) recording_flags[i] &= (uint8_t)~4u;
    uint32_t i = recording_head[recording_owner(t)];
    while (i < RECORD_MAX && recording_due[i] <= phase) {
        uint32_t next = recording_next[i];
        if (recording_active(t, i) && !recording_behind(i, recording_due[i], phase, period, seek))
            recording_fire(t, i, period, phase - recording_due[i]);
        i = next;
    }
    recording_schedule[trk_index(t)].cursor = (uint16_t)i;
#else
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i]) {
        recording_flags[i] &= (uint8_t)~4u;
        if (recording_active(t, i)) {
            uint32_t on = recording_play_on(t, i, period);
            if (on <= phase && !recording_behind(i, on, phase, period, seek)) recording_fire(t, i, period, phase - on);
        }
    }
#endif
}
#if MELODEE_RECORD_QUEUE
static void recording_queue_tick(track_t *t, uint32_t n)
{
    uint32_t tr = trk_index(t), owner = recording_owner(t), len = (uint32_t)t->p[P_SLEN];
    recording_clock[tr] += n;
    if (!recording_heap_count[tr]) return;
    int changed = recording_heap_owner[tr] != owner || recording_heap_len[tr] != len;
    recording_heap_owner[tr] = (uint16_t)owner; recording_heap_len[tr] = (uint16_t)len;
    /* Poll only overview cells owning sounding events (at most NSTEP), rather
     * than all sounding notes. A manual overwrite must release immediately,
     * even when that event's deadline is buried inside the heap. */
    for (uint32_t word = 0; word < NELEM(recording_watch_bits[tr]) && !changed; word++) {
        uint32_t bits = recording_watch_bits[tr][word];
        while (bits) {
            uint32_t bit = (uint32_t)__builtin_ctz(bits);
            const step_t *s = &t->step[word * 32u + bit];
            if (s->time != ST_NOTE || !(s->flags & SF_RECORDED)) { changed = 1; break; }
            bits &= bits - 1u;
        }
    }
    if (changed && recording_heap_count[tr]) recording_queue_refresh(t);
    /* Process offs before this block's new onsets, as the countdown path did.
     * Retrigger replaces the indexed deadline; no stale off can end its gate. */
    while (recording_heap_count[tr]) {
        uint32_t i = recording_heap_store[tr * 32u];
        if ((int32_t)(recording_run[i].left - recording_clock[tr]) > 0) break;
        recording_end(t, i);
    }
}
#endif
static void recording_tick(track_t *t, uint32_t n)
{
    uint32_t period = seq_div_samples((uint32_t)t->p[P_SDIV]), tr = trk_index(t);
    uint32_t a = recording_prefix(t, period, t->seq_idx) + t->seq_pos, b = a + n;
    uint32_t elapsed;
    if (n <= CTL) {
        /* At most CTL * 65536 plus one sequencer period: the ordinary
         * audio tick fits 32 bits, including the slowest supported division. */
        uint32_t ticks = n * RECORD_UNIT + recording_fraction[tr];
        recording_fraction[tr] = ticks % period;
        elapsed = ticks / period;
    } else {
        uint64_t ticks = (uint64_t)n * RECORD_UNIT + recording_fraction[tr];
        recording_fraction[tr] = (uint32_t)(ticks % period);
        elapsed = (uint32_t)(ticks / period);
    }
    recording_sync(t, period);
#if MELODEE_RECORD_QUEUE
    recording_queue_tick(t, n);
#endif
#if MELODEE_RECORD_CURSOR
    uint16_t *link = &recording_live_head[tr];
    while (*link < RECORD_MAX) {
        uint32_t i = *link;
        if (recording_flags[i] & 1u) recording_run[i].held = (uint32_t)clamp((int32_t)(recording_run[i].held + elapsed), 0, 8388607);
#if !MELODEE_RECORD_QUEUE
        if (recording_run[i].left) {
            if (!recording_active(t, i) || recording_run[i].left <= n) recording_end(t, i);
            else recording_run[i].left -= n;
        }
#endif
        if (!(recording_flags[i] & 1u) && (MELODEE_RECORD_QUEUE || !recording_run[i].left)) {
            *link = recording_live_next[i];
            recording_flags[i] &= (uint8_t)~2u;
        } else link = &recording_live_next[i];
    }
    uint32_t i = recording_schedule[tr].cursor;
    while (i < RECORD_MAX && recording_due[i] <= b) {
        uint32_t next = recording_next[i];
        if (recording_due[i] > a && recording_active(t, i)) recording_fire(t, i, period, b - recording_due[i]);
        i = next;
    }
    recording_schedule[tr].cursor = (uint16_t)i;
#else
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i]) {
        if ((recording_flags[i] & 1u)) recording_run[i].held = (uint32_t)clamp((int32_t)(recording_run[i].held + elapsed), 0, 8388607);
        if (recording_run[i].left) {
            if (!recording_active(t, i) || recording_run[i].left <= n) {
                recording_end(t, i);
            } else recording_run[i].left -= n;
        }
        if (!(recording_flags[i] & 4u)) {
            uint32_t on = recording_play_on(t, i, period);
            /* Most retained events are nowhere near this block. Avoid
             * looking up their STEP ownership until their onset is due. */
            if (on > a && on <= b && recording_active(t, i)) recording_fire(t, i, period, b - on);
        }
    }
#endif
}
