/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Bounded working-memory arena. Audio owns mutations; a main-loop history
 * or USB mode change may also mutate it with audio interrupts guarded.
 * Fixed handles, no heap,
 * general compaction, or searches proportional to voices/samples. Explicit
 * history resizes may relocate that one handle while the owner is guarded.
 * Linker-derived SRAM tails are separate banks: an allocation never spans a
 * code/data/retained-state boundary. Small banks first preserve the large
 * contiguous POOL bank for engine state and note history. */
enum { RES_ENGINE0, RES_NOTE_HISTORY = RES_ENGINE0 + NPART, RES_FX0,
       RES_RECORD_UNDO = RES_FX0 + FX_STATE_COUNT,
       RES_STEP_UNDO, RES_LOAD_UNDO, RES_SONG_UNDO, RES_SONG_SOUNDS, RES_SONG_BASE, RES_SONG_PREVIEW,
#if MELODEE_USB_AUDIO
       RES_USB_TRACKS,
#endif
       RES_PUNCH, RES_COUNT };
static uint8_t recording_undo_lost;
static uint8_t recording_undo_active, recording_undo_skip;
static struct { void *ptr; uint32_t size; } resource[RES_COUNT];
static uint32_t resource_peak, resource_failures;
#ifdef FM1_IRQ_TARGET
extern uint8_t _resource_start[], _resource_end[];
extern uint8_t _resource_low_start[], _resource_low_end[];
extern uint8_t _resource_data_start[], _resource_data_end[];
extern uint8_t _resource_retained_start[], _resource_retained_end[];
extern uint8_t _resource_cache_start[], _resource_cache_end[];
#if MELODEE_CACHE_RAM
#define RESOURCE_CACHE_AVAILABLE (fm1_cache.status == FM1_CACHE_READY)
#else
#define RESOURCE_CACHE_AVAILABLE 0
#endif
#define RESOURCE_MAIN_CAPACITY ((uint32_t)((uintptr_t)_resource_end - (uintptr_t)_resource_start))
#else
static uint32_t resource_host[40960];
static uint32_t resource_host_low[5120], resource_host_data[512], resource_host_retained[1024];
static uint32_t resource_host_cache[7168], resource_host_cache_enabled;
#define RESOURCE_CACHE_AVAILABLE resource_host_cache_enabled
#define _resource_cache_start ((uint8_t *)resource_host_cache)
#define _resource_cache_end ((uint8_t *)(resource_host_cache + 7168))
#define _resource_start ((uint8_t *)resource_host)
#define _resource_end ((uint8_t *)(resource_host + 40960))
#define _resource_low_start ((uint8_t *)resource_host_low)
#define _resource_low_end ((uint8_t *)(resource_host_low + 5120))
#define _resource_data_start ((uint8_t *)resource_host_data)
#define _resource_data_end ((uint8_t *)(resource_host_data + 512))
#define _resource_retained_start ((uint8_t *)resource_host_retained)
#define _resource_retained_end ((uint8_t *)(resource_host_retained + 1024))
#define RESOURCE_MAIN_CAPACITY ((uint32_t)sizeof resource_host)
#endif
/* UI histories use demand allocations, reclaimable by audio. The entire
 * validated cache bank is shared; guaranteed SRAM remains usable if cache
 * startup fails. No UI partition is held while its history is unused. */
#define UI_CACHE_BYTES 0u
static const struct { uint8_t *start, *end; } resource_banks[] = {
    { _resource_data_start, _resource_data_end },
    { _resource_retained_start, _resource_retained_end },
    { _resource_low_start, _resource_low_end },
    { _resource_cache_start + UI_CACHE_BYTES, _resource_cache_end },
    { _resource_start, _resource_end }
};
#define RESOURCE_BANKS ((uint32_t)(sizeof resource_banks / sizeof resource_banks[0]))
enum { RESOURCE_CACHE_BANK = 3 };
#define RESOURCE_CAPACITY (RESOURCE_MAIN_CAPACITY + \
    (uint32_t)((uintptr_t)_resource_low_end - (uintptr_t)_resource_low_start) + \
    (uint32_t)((uintptr_t)_resource_data_end - (uintptr_t)_resource_data_start) + \
    (uint32_t)((uintptr_t)_resource_retained_end - (uintptr_t)_resource_retained_start) + \
    (RESOURCE_CACHE_AVAILABLE ? (uint32_t)((uintptr_t)_resource_cache_end - (uintptr_t)_resource_cache_start) - UI_CACHE_BYTES : 0u))
static uint32_t resource_used(void)
{
    uint32_t n = 0;
    for (uint32_t k = 0; k < RES_COUNT; k++) n += resource[k].size;
    return n;
}
static void resource_release(uint32_t id) { resource[id].size = 0; }
static __attribute__((noinline)) void *resource_allocate_impl(uint32_t id, uint32_t bytes, int preserve)
{
    if (bytes > RESOURCE_CAPACITY) { resource_failures++; return 0; }
    uint32_t size = (bytes + 3u) & ~3u;
    if (!size) { resource_release(id); return 0; }
    for (uint32_t bank = 0; bank < RESOURCE_BANKS; bank++) {
        if (bank == RESOURCE_CACHE_BANK && !RESOURCE_CACHE_AVAILABLE) continue;
        uintptr_t at = (uintptr_t)resource_banks[bank].start;
        uintptr_t limit = (uintptr_t)resource_banks[bank].end;
        /* Each collision advances past a live block; at most RES_COUNT passes.
         * Integer addresses permit distinct host arrays and disjoint SRAM banks. */
        for (uint32_t pass = 0; pass <= RES_COUNT; pass++) {
            if (at > limit || size > limit - at) break;
            uintptr_t next = at;
            for (uint32_t k = 0; k < RES_COUNT; k++) {
                if (k == id || !resource[k].size) continue;
                uintptr_t begin = (uintptr_t)resource[k].ptr;
                uintptr_t end = begin + resource[k].size;
                if (at < end && at + size > begin && end > next) next = end;
            }
            if (next == at) {
                void *p = (void *)at;
                uint32_t keep=preserve?resource[id].size:0u;
                if(keep>size)keep=size;
                /* Arena blocks are word aligned. Volatile keeps the
                 * freestanding compiler from replacing overlap-safe loops
                 * with a libc memmove that the firmware does not provide. */
                volatile uint32_t *to=p;
                const volatile uint32_t *from=resource[id].ptr;
                if((uintptr_t)p<(uintptr_t)from){for(uint32_t j=0;j<keep/4u;j++)to[j]=from[j];}
                else if((uintptr_t)p>(uintptr_t)from){for(uint32_t j=keep/4u;j--;)to[j]=from[j];}
                memset((uint8_t *)p+keep,0,size-keep);
                resource[id].ptr = p; resource[id].size = size;
                uint32_t used = resource_used();
                if (used > resource_peak) resource_peak = used;
                return p;
            }
            at = next;
        }
    }
    resource_failures++;
    return 0;                          /* leave an existing allocation intact */
}
static void *resource_allocate(uint32_t id,uint32_t bytes)
{return resource_allocate_impl(id,bytes,0);}
/* History alone opts into preserving resize. No other live handle moves;
 * failure preserves the original allocation and all its bytes. */
static void *resource_resize(uint32_t id,uint32_t bytes)
{
    if(bytes>RESOURCE_CAPACITY){resource_failures++;return 0;}
    uint32_t size=(bytes+3u)&~3u;
    if(!size){resource_release(id);return 0;}
    if(size<=resource[id].size){resource[id].size=size;return resource[id].ptr;}
    return resource_allocate_impl(id,bytes,1);
}

/* Keep the already allocated path cheap in every engine accessor. */
static inline void *resource_get(uint32_t id, uint32_t bytes)
{
    uint32_t size=(bytes+3u)&~3u;
    if(size && size==resource[id].size)return resource[id].ptr;
    void *p=resource_allocate(id,bytes);
    /* UI history never competes with a sounding engine/effect. Retire whole
     * histories only after the ordinary allocation failed; their consumers
     * validate the handle under the audio guard before touching a snapshot. */
    if(!p && bytes && bytes<=RESOURCE_CAPACITY &&
       (id==RES_PUNCH || id<RES_NOTE_HISTORY || (id>=RES_FX0 && id<RES_RECORD_UNDO)
#if MELODEE_USB_AUDIO
        || id==RES_USB_TRACKS
#endif
       ) &&
       (resource[RES_STEP_UNDO].size || resource[RES_LOAD_UNDO].size || resource[RES_SONG_UNDO].size)){
        resource_release(RES_STEP_UNDO);resource_release(RES_LOAD_UNDO);resource_release(RES_SONG_UNDO);
        p=resource_allocate(id,bytes);
    }
    /* A history snapshot must never prevent a voice/effect from acquiring its
     * working memory. It is disposable, unlike sounding engine state. */
    if(!p && bytes && bytes<=RESOURCE_CAPACITY && id!=RES_RECORD_UNDO &&
       id!=RES_STEP_UNDO && id!=RES_LOAD_UNDO && id!=RES_SONG_UNDO &&
       resource[RES_RECORD_UNDO].size){
        resource_release(RES_RECORD_UNDO);recording_undo_lost=1;
        recording_undo_skip|=recording_undo_active;recording_undo_active=0;
        p=resource_allocate(id,bytes);
    }
    return p;
}
