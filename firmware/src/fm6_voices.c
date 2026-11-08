/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* What the firmware keeps of FM6 (the engine: eng_fm6.c, a synth module on the device: firmware/modules/fm6): the
 * factory voices (F1..F24) as the presets and the editor have them and the DX7 SysEx bytes the USB interrupt
 * collects (fm6_store.c serves them); the patch, its formats and writes: modules/fm6/fm6_patch.h, synths_phase4.h
 * (TEMPORARY until phase 4). The device's
 * image makes no factory voice: F1..F24 are the FM6 module's presets (its file's PRST chunk, module_presets.h), which
 * tools/mod_build.py writes from the makers below as host builds compile them (MELODEE_FM6_BUILTIN) */
#include "x0x/fastmath.h"
#include "melodee_fm6.h"         /* tools/gen_fm6_patches.py: FM6_INIT, FM6_FACTORY[FM6_NFACTORY] (F1..F8: host builds) */

#if MELODEE_FM6_BUILTIN
/* a Melodee factory voice: OP1..OP6 in patch order within an operator (DET 0..14, 7 = none), ALG 0..31,
 * TRNSP 24 = no transposition */
typedef struct {
    uint8_t op[6][FP_OP];
    uint8_t pr[4], pl[4], alg, fb, oks, lfs, lfd, lpmd, lamd, lfks, lfw, lpms, trnsp;
    char name[11];
} fm6_rom_t;
#include "eng_fm6_rom.h"         /* FM6_ROM[FM6_NROM]: F9.. */
_Static_assert(FM6_NROM == 16u && FM6_NFACTORY + FM6_NROM == FM6_NFAC, "FM6: F1..F8, F9..F24");
#endif

/* (the patch formats: modules/fm6/fm6_patch.h) */
#if MELODEE_FM6_BUILTIN
static void fm6_rom_patch(const fm6_rom_t *r, uint8_t *v)   /* a Melodee factory voice -> 155-byte voice */
{
    uint32_t n, i;
    for (n = 1; n <= 6u; n++)
        for (i = 0; i < FP_OP; i++)
            v[(6u - n) * FP_OP + i] = r->op[n - 1u][i];
    for (i = 0; i < 4u; i++) {
        v[FP_PR1 + i] = r->pr[i];
        v[FP_PL1 + i] = r->pl[i];
    }
    v[FP_ALG] = r->alg;
    v[FP_FB] = r->fb;
    v[FP_OKS] = r->oks;
    v[FP_LFS] = r->lfs;
    v[FP_LFD] = r->lfd;
    v[FP_LPMD] = r->lpmd;
    v[FP_LAMD] = r->lamd;
    v[FP_LKS] = r->lfks;
    v[FP_LFW] = r->lfw;
    v[FP_LPMS] = r->lpms;
    v[FP_TRNSP] = r->trnsp;
    for (i = 0; i < 10u; i++)
        v[FP_NAME + i] = (uint8_t)(r->name[i] ? r->name[i] : ' ');
    fm6_sanitize(v);
}

#endif
/* factory patch k (F1..F24) as a packed record: made on host builds, the FM6 module's preset k on the device; INIT
 * VOICE without either */
static void fm6_factory(uint32_t k, uint8_t *pk)
{
#if MELODEE_FM6_BUILTIN
    uint8_t v[FP_SIZE + 1u];
    if (!RES_OK()) { memcpy(pk, FM6_INIT, FM6_PACKED); return; }   /* (res.c: the factory voices not installed) */
    if (k < FM6_NFACTORY) {
        memcpy(pk, FM6_FACTORY[k], FM6_PACKED);
        return;
    }
    fm6_rom_patch(&FM6_ROM[(k - FM6_NFACTORY) % FM6_NROM], v);
    memset(pk, 0, FM6_PACKED);
    fm6_pack(v, pk);
#else
    if (k >= FM6_NFAC || !preset_patch(ENGI_FM6, k, pk, FM6_PACKED))
        memcpy(pk, FM6_INIT, FM6_PACKED);           /* (no module: no factory voices) */
#endif
}

static void fm6_name(char *d, const uint8_t *v)          /* the voice name, trailing spaces cut */
{
    uint32_t i, n = 0;
    for (i = 0; i < 10u; i++) {
        d[i] = (char)v[FP_NAME + i];
        if (d[i] != ' ')
            n = i + 1u;
    }
    d[n] = 0;
}

/* A factory preset loads its corresponding voice directly. */
static void fm6_track_loaded(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk);
    if (tr < NTRK && t->eng_req == ENGI_FM6) {
        uint8_t pk[FM6_PACKED], v[FP_SIZE + 1u];
        if (t->preset < FM6_NFAC)
            fm6_factory(t->preset, pk);
        fm6_unpack(t->preset < FM6_NFAC ? pk : FM6_INIT, v);   /* (past F24: INIT VOICE) */
        fm6_put_patch(tr, v, 1);
    }
}

/* ------------------------------------------------------ DX7 SysEx in --- */
/* Frames that start F0 43 (Yamaha), collected by the USB ISR (usb.c sysex_byte) for the main loop
 * (fm6_store.c fm6_service): a 32-voice dump is the longest. One frame at a time; a frame arriving while one
 * is waiting is dropped.
 * The frame lives in the main loop's serialized scratch (project.c's jobs: settings, DX7 dumps out, archives, project
 * streams, the update entry), never both at once: a job takes it (wire_take) until the main loop's next turn
 * (wire_release), dropping a frame being collected or waiting and an import staged there; the ISR collects only while
 * no job holds it, and writes nothing before the 43 (another maker's SysEx leaves the scratch alone) */
#define FM6_RX 4104u
#if MELODEE_FLASH && !MELODEE_AUDIO_PROFILE
#define WIRE_SCRATCH_BYTES FM6_RX
#else
#define WIRE_SCRATCH_BYTES 27904u                       /* a whole bank record (project.c asserts it fits) */
#endif
/* In data SRAM, not the pool: the saving enlarges the contiguous engine arena */
static union { uint32_t align; uint8_t raw[WIRE_SCRATCH_BYTES]; } proj_wire_u MELODEE_BIG_BSS;
#define fm6_rx proj_wire_u.raw
static uint8_t proj_wire_gen;                /* +1 when a scratch client invalidates an archive snapshot/upload */
static volatile uint8_t wire_job;            /* a main-loop job holds the scratch this turn */
static uint32_t fm6_rx_n;
static volatile uint8_t fm6_rx_ready;
static uint8_t fm6_rx_on;

static void fm6_sx_byte(uint8_t b)
{
    if (b == 0xF0) {
        fm6_rx_on = !fm6_rx_ready && !wire_job;
        if (fm6_rx_on)
            fm6_rx_n = 0;                               /* a pending frame belongs to the main loop, the scratch to a job */
    }
    if (!fm6_rx_on || b >= 0xF8u)
        return;                                          /* (realtime may occur anywhere in SysEx) */
    if ((b & 0x80u) && b != 0xF0u && b != 0xF7u) {
        fm6_rx_on = 0;                                  /* a channel/system status cancels the frame */
        return;
    }
    if (wire_job || fm6_rx_n >= FM6_RX || (fm6_rx_n == 1u && b != 0x43)) {
        fm6_rx_on = 0;                                   /* a job took the scratch, too long, or not Yamaha */
        return;
    }
    if (fm6_rx_n == 1u) {                                /* Yamaha's: the scratch holds this frame now */
        proj_wire_gen++;
        fm6_rx[0] = 0xF0;
    }
    if (fm6_rx_n)
        fm6_rx[fm6_rx_n] = b;
    fm6_rx_n++;
    if (b == 0xF7) {
        fm6_rx_on = 0;
        RING_PUBLISH();
        fm6_rx_ready = 1;
    }
}
