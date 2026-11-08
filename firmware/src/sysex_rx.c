/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The synths' SysEx in: frames whose first bytes after F0 are one of a synth's schema's MOD3_R_SYSEX patterns (a DX7's
 * F0 43, a Casio's F0 44, a Prophet's F0 01 31 / 32), from USB, Bluetooth or the TRS jack, collected by the interrupt
 * that receives them into the main loop's scratch, which hands them to the synth's module (sysex_route.c: msx_service,
 * the module's mod_engine_v3.sysex). The firmware knows no synth's SysEx: only the patterns, from the schemas.
 * One frame at a time: a frame from another link while one is collected, or arriving while one waits for the main loop,
 * is dropped (a librarian sends again). Realtime bytes may come anywhere in a frame; any other status ends it.
 *
 * The scratch is the main loop's, serialized (project.c's jobs: settings, dumps out, archives, project streams, the
 * update entry): a job takes it (wire_take) until the main loop's next turn (wire_release), dropping a frame being
 * collected or waiting and an import staged there; the interrupt collects only while no job holds it, and writes
 * nothing before a pattern matched (another maker's SysEx leaves the scratch alone). Included by engines.c */
#if MELODEE_FLASH && !MELODEE_AUDIO_PROFILE
#define WIRE_SCRATCH_BYTES MOD3_SX_MAX
#else
#define WIRE_SCRATCH_BYTES 27904u                       /* a whole bank record (project.c asserts it fits) */
#endif
/* In data SRAM, not the pool: the saving enlarges the contiguous engine arena */
static union { uint32_t align; uint8_t raw[WIRE_SCRATCH_BYTES]; } proj_wire_u MELODEE_BIG_BSS;
static uint8_t proj_wire_gen;                /* +1 when a scratch client invalidates an archive snapshot/upload */
static volatile uint8_t wire_job;            /* a main-loop job holds the scratch this turn */

/* the patterns (main loop: schema.c msx_routes_sync, from the synths' schemas), read by the interrupts */
#define MSX_ROUTES 8u
static struct { uint8_t engine, n, b[3]; } msx_route[MSX_ROUTES];
static volatile uint8_t msx_nroute;
enum { MSX_USB, MSX_BLE, MSX_TRS };
static struct {
    volatile uint32_t n;                     /* the frame's bytes in the scratch (F0 first), once a pattern matched */
    volatile uint8_t ready;                  /* 1: it ended (F7) and waits; 2: held (an import staged in it) */
    volatile uint8_t abort;                  /* it broke after a pattern matched (the module is told) */
    volatile uint8_t eng;                    /* the matched pattern's engine, 0xFF: none yet */
    uint8_t on, src, got;                    /* collecting; from which link; bytes after F0 matched so far */
    uint8_t head[3];
} msx = {.eng = 0xFFu};
static uint8_t msx_serving;
static int store_send(uint32_t tr, int say);   /* sysex_route.c: part tr's sound as its synth's SysEx (STORE's SEND) */                  /* (main loop) a module's sysex() runs: the frame is its, no job's to drop */

/* one byte of SysEx (or a status that ends it) from link src: the interrupt that received it */
static void msx_byte(uint32_t b, uint32_t src)
{
    uint32_t i, more = 0;
    if (b >= 0xF8u || (msx.on && msx.src != src))
        return;                                 /* (realtime anywhere; another link's frame is being collected) */
    if (b == 0xF0u) {
        if (msx.on && msx.eng != 0xFFu) {       /* (a synth's frame cut short: its rest never comes) */
            RING_PUBLISH();
            msx.abort = 1;
        }
        msx.on = !(msx.ready || msx.eng != 0xFFu || wire_job);   /* (one waits or is served: this one is dropped) */
        msx.src = (uint8_t)src;
        msx.got = 0;
        return;
    }
    if (!msx.on)
        return;
    if ((b & 0x80u) && b != 0xF7u) {            /* a status ends it */
        if (msx.eng != 0xFFu) { RING_PUBLISH(); msx.abort = 1; }
        msx.on = 0;
        return;
    }
    if (msx.eng == 0xFFu) {                     /* not a synth's yet: against the patterns */
        if (b == 0xF7u || wire_job) { msx.on = 0; return; }
        msx.head[msx.got++] = (uint8_t)b;
        for (i = 0; i < msx_nroute; i++) {
            uint32_t j, n = msx_route[i].n, k = n < msx.got ? n : msx.got;
            for (j = 0; j < k && msx_route[i].b[j] == msx.head[j]; j++)
                ;
            if (j < k)
                continue;
            if (n == msx.got) {                 /* matched: the frame is this synth's */
                proj_wire_gen++;
                proj_wire_u.raw[0] = 0xF0u;
                for (j = 0; j < n; j++)
                    proj_wire_u.raw[1u + j] = msx.head[j];
                RING_PUBLISH();
                msx.n = 1u + n;
                RING_PUBLISH();
                msx.eng = msx_route[i].engine;
                return;
            }
            more = 1;                           /* (a longer pattern may still match) */
        }
        if (!more)
            msx.on = 0;
        return;
    }
    if (wire_job || msx.n >= MOD3_SX_MAX) {     /* a job took the scratch, or too long */
        RING_PUBLISH();
        msx.abort = 1;
        msx.on = 0;
        return;
    }
    proj_wire_u.raw[msx.n] = (uint8_t)b;
    RING_PUBLISH();
    msx.n++;
    if (b == 0xF7u) {
        msx.on = 0;
        RING_PUBLISH();
        msx.ready = 1;
    }
}
/* link src went away (a USB bus reset): its frame ends */
static void msx_link_gone(uint32_t src)
{
    if (msx.on && msx.src == src) {
        if (msx.eng != 0xFFu) { RING_PUBLISH(); msx.abort = 1; }
        msx.on = 0;
    }
}
