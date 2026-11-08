/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* What the firmware keeps of FM6 (the engine: eng_fm6.c, a synth module on the device: firmware/modules/fm6): the
 * function settings, the factory voices (F1..F24) as the presets and the editor have them, the patch formats (155-byte
 * voice, 128-byte packed record, INIT VOICE), the parts' patches as the stores, the pages and SysEx write them
 * (fm6_parts.h holds them), and the DX7 SysEx bytes the USB interrupt collects (fm6_store.c serves them). The device's
 * image makes no factory voice: F1..F24 are the FM6 module's presets (its file's PRST chunk, module_presets.h), which
 * tools/mod_build.py writes from the makers below as host builds compile them (MELODEE_FM6_BUILTIN) */
#include "x0x/fastmath.h"
#include "melodee_fm6.h"         /* tools/gen_fm6_patches.py: FM6_INIT, FM6_FACTORY[FM6_NFACTORY] (F1..F8: host builds) */

static const uint8_t FM6_FNMAX[FM6_NFN] = {12, 12, 12, 1, 127, 1, 99, 7, 99, 7, 99, 7, 99, 7, 1, 2};
static const uint8_t FM6_FNDEF[FM6_NFN] = {3, 3, 0, 0, 0, 0, 99, 1, 0, 0, 0, 0, 0, 0, 0, FM6_MARK1};   /* Dexed's */
static void fm6_fn_reset(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        memcpy(fm6_fn[k], FM6_FNDEF, FM6_NFN);
}
static void fm6_fn_set(uint32_t tr, uint32_t k, int32_t v)
{
    if (tr < NTRK && k < FM6_NFN)
        fm6_fn[tr][k] = (uint8_t)clamp(v, 0, FM6_FNMAX[k]);
}
static int fm6_fn_ok(const uint8_t *f)                    /* a stored set: every value in its range */
{
    uint32_t k;
    for (k = 0; k < FM6_NFN; k++)
        if (f[k] > FM6_FNMAX[k])
            return 0;
    return 1;
}

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

/* ------------------------------------------------------- patch formats --- */
/* the highest value of each byte of the 155-byte voice */
static uint32_t fm6_max(uint32_t i)
{
    static const uint8_t OPMAX[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
    static const uint8_t VMAX[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
    if (i < 126u)
        return OPMAX[i % 21u];
    if (i < FP_NAME)
        return VMAX[i - 126u];
    return 126u;
}

/* every value inside its range (a name byte outside 32..126 becomes a space) */
static void fm6_sanitize(uint8_t *v)
{
    uint32_t i;
    for (i = 0; i < FP_SIZE; i++) {
        if (i >= FP_NAME)
            v[i] = v[i] < 32u || v[i] > 126u ? ' ' : v[i];
        else if (v[i] > fm6_max(i))
            v[i] = (uint8_t)fm6_max(i);
    }
    v[FP_SIZE] = 0;
}

/* 128-byte packed record (VMEM, the 32-voice bank's) -> 155-byte voice; bits a record does not use are ignored */
static void fm6_unpack(const uint8_t *b, uint8_t *v)
{
    uint32_t k, i;
    for (k = 0; k < 6u; k++) {
        const uint8_t *o = b + k * 17u;
        uint8_t *d = v + k * FP_OP;
        for (i = 0; i < 11u; i++)
            d[i] = o[i] & 0x7Fu;
        d[FP_LC] = o[11] & 3u;
        d[FP_RC] = (o[11] >> 2) & 3u;
        d[FP_RS] = o[12] & 7u;
        d[FP_DET] = (o[12] >> 3) & 15u;
        d[FP_AMS] = o[13] & 3u;
        d[FP_KVS] = (o[13] >> 2) & 7u;
        d[FP_OL] = o[14] & 0x7Fu;
        d[FP_MODE] = o[15] & 1u;
        d[FP_FC] = (o[15] >> 1) & 31u;
        d[FP_FF] = o[16] & 0x7Fu;
    }
    for (i = 0; i < 9u; i++)
        v[FP_PR1 + i] = b[102 + i] & 0x7Fu;              /* pitch EG, algorithm */
    v[FP_ALG] &= 31u;
    v[FP_FB] = b[111] & 7u;
    v[FP_OKS] = (b[111] >> 3) & 1u;
    for (i = 0; i < 4u; i++)
        v[FP_LFS + i] = b[112 + i] & 0x7Fu;
    v[FP_LKS] = b[116] & 1u;
    v[FP_LFW] = (b[116] >> 1) & 7u;
    v[FP_LPMS] = (b[116] >> 4) & 7u;
    v[FP_TRNSP] = b[117] & 0x7Fu;
    for (i = 0; i < 10u; i++)
        v[FP_NAME + i] = b[118 + i] & 0x7Fu;
    fm6_sanitize(v);
}

/* 155-byte voice -> 128-byte packed record (7-bit bytes: it travels in SysEx as it is) */
static void fm6_pack(const uint8_t *v, uint8_t *b)
{
    uint32_t k, i;
    for (k = 0; k < 6u; k++) {
        const uint8_t *o = v + k * FP_OP;
        uint8_t *d = b + k * 17u;
        for (i = 0; i < 11u; i++)
            d[i] = o[i] & 0x7Fu;
        d[11] = (uint8_t)((o[FP_LC] & 3u) | (o[FP_RC] & 3u) << 2);
        d[12] = (uint8_t)((o[FP_RS] & 7u) | (o[FP_DET] & 15u) << 3);
        d[13] = (uint8_t)((o[FP_AMS] & 3u) | (o[FP_KVS] & 7u) << 2);
        d[14] = o[FP_OL] & 0x7Fu;
        d[15] = (uint8_t)((o[FP_MODE] & 1u) | (o[FP_FC] & 31u) << 1);
        d[16] = o[FP_FF] & 0x7Fu;
    }
    for (i = 0; i < 9u; i++)
        b[102 + i] = v[FP_PR1 + i] & 0x7Fu;
    b[110] &= 31u;
    b[111] = (uint8_t)((v[FP_FB] & 7u) | (v[FP_OKS] & 1u) << 3);
    for (i = 0; i < 4u; i++)
        b[112 + i] = v[FP_LFS + i] & 0x7Fu;
    b[116] = (uint8_t)((v[FP_LKS] & 1u) | (v[FP_LFW] & 7u) << 1 | (v[FP_LPMS] & 7u) << 4);
    b[117] = v[FP_TRNSP] & 0x7Fu;
    for (i = 0; i < 10u; i++)
        b[118 + i] = v[FP_NAME + i] & 0x7Fu;
}

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

/* ---------------------------------------------------- the track's patch --- */
/* track tr's patch = v (155 bytes, sanitized). load: a new voice, as a DX7 program change (the notes stop); else
 * an edit, which the sounding notes follow. Main loop: the ISR takes it at its next block */
static void fm6_put_patch(uint32_t tr, const uint8_t *v, int load)
{
    uint8_t s[FP_SIZE + 1u];
    tr %= NTRK;
    memcpy(s, v, FP_SIZE);
    fm6_sanitize(s);                                /* not in place: the ISR must never copy an unsanitized byte */
    memcpy(fm6_patch_of(tr), s, FP_SIZE + 1u);
    if (load) {
        fm6_on[tr] = FM6_ON_ALL;
        fm6_lgen[tr]++;
    }
    RING_PUBLISH();
    fm6_pgen[tr]++;
}

/* .. a load when its name or TRANSPOSE is another (a patch sent whole, an undo, a project), else an edit */
static void fm6_set_patch(uint32_t tr, const uint8_t *v)
{
    const uint8_t *o = fm6_patch_of(tr % NTRK);
    fm6_put_patch(tr, v, memcmp(o + FP_NAME, v + FP_NAME, 10) || o[FP_TRNSP] != v[FP_TRNSP]);
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

/* power-on: every track the init voice (what a project stores for the tracks that never played FM6), the
 * function settings Dexed's (a project or the template loaded then brings its own) */
static void fm6_init(void)
{
    uint8_t v[FP_SIZE + 1u];
    uint32_t tr;
    fm6_fn_reset();
    fm6_unpack(FM6_INIT, v);
    for (tr = 0; tr < NTRK; tr++) {
        fm6_put_patch(tr, v, 1);
    }
}

/* an operator frequency the pages show (params.c F_FMFRQ): 2^(x / 65536) in Q16 */
static inline __attribute__((always_inline)) uint32_t fm6_pow2(int32_t x)
{
    return (uint32_t)fm_minf(fm_exp2f((float)x * (1.0f / 65536.0f)) * 65536.0f, 4294967040.0f);
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
