/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SAMPLER's kits (KIT 1..8, values 9..16): its data records (mod_abi3.h host 6: the firmware keeps them in the pool,
 * rec_store.c). A kit is up to sixteen pad records, one take each, of any length the free store allows (a take never
 * copies the other pads, so one take may fill the whole store), and the kit's name in a record of the host's (none:
 * "KIT n"). SAMPLER writes a pad record while its take is recorded (kit_take_core.c, drum_enc.c through the host's take
 * calls), checks one (kit_store.c drk_pad_check: rec_check) and plays it (drum_real.c), reading the host's list of its
 * sectors in place. Outside the device a kit travels whole (the editor's backup objects 92..99, a .mel file, the web's
 * kit maker): drk_box_t, then its pad records in pad order. */
#pragma once
/* A pad record: this header at the start of its first sector's payload, then the take's slices, two little-endian
 * 32-bit words each, the high one first. Slice k of the payload is at payload byte 8 k (the header's bytes are its
 * first slices): a sector's 4,056 payload bytes hold 507 slices, so none straddles; in flash they sit in two runs, 476
 * from byte 32, then the 8-byte guard at 0xF00 (storage.c: erased), then 31. */
#define DRK_PAD_MAGIC 0x32444150u               /* PAD2 */
#define DRK_NPAD 16u                             /* a kit's pads (the 808's sixteen: drum_map.c) */
#define DRK_NAMELEN 5u                           /* the card's value: a name never cut short */
#define DRK_SECBYTES 4056u                       /* a list record's payload per sector (storage.c ST_LIN_PAYLOAD_MAX) */
#define DRK_SECOFF 32u                           /* where it starts (ST_POOL_PAYLOAD_OFF) */
#define DRK_SECSLICES (DRK_SECBYTES / 8u)        /* 507 */
#define DRK_RUN0 476u                            /* the slices before the guard (storage.c ST_RUN0 / 8) */
#define DRK_SLOTS (DRK_SECSLICES + 1u)           /* 8-byte places from byte 32 to the sector's end, the guard's one too */
/* slice j of a sector: its words from the sector's payload start (the guard's place skipped) */
#define DRK_SLOT(j) (2u * ((j) + ((j) >= DRK_RUN0)))
#define DRK_CODEC_LMS 0u                         /* drk_pad_t.codec: drum_enc.c's slices (the only one so far) */
typedef struct {
    uint32_t magic;                              /* DRK_PAD_MAGIC */
    uint16_t bytes;                              /* this header's bytes: the take's slices begin there (DRK_PAD_BYTES) */
    uint8_t pad, codec;                          /* the pad it was recorded for (0..15); DRK_CODEC_* */
    uint32_t n;                                  /* the take's samples */
    uint32_t loop_at, loop_slice;                /* the sample its tail loops back to (0: one shot), that sample's slice
                                                  * (from the take's first) */
    uint16_t inc;                                /* samples per output sample, Q15 (32768: 44.1 kHz, 16384: 22.05) */
    uint16_t gain;                               /* the take's level to full scale, Q10 (Record kit: its peak's) */
    uint16_t out, ms, pivot;                     /* the pad's level (Q12); -40 dB time (ms; 0: held to its end); TONE's Hz */
    int8_t st, tone;                             /* semitones; TONE's offset, as a knob's */
    uint8_t dark, rsv[7];                        /* a looped tail's darkening per block (Q12) */
    int32_t loop[8];                             /* the predictor at the loop (h, w) */
} drk_pad_t;
_Static_assert(sizeof(drk_pad_t) == 72u && __builtin_offsetof(drk_pad_t, loop) == 40u, "a pad record's header");
#define DRK_PAD_BYTES ((uint32_t)sizeof(drk_pad_t))
#define DRK_FIRST (DRK_PAD_BYTES / 8u)           /* a take's first slice */
/* A kit's name: the host's record (rec_store.c, mod_schema.h rname / rnlen: DRK_NAMELEN characters at byte 8) */
#define DRK_NAME_MAGIC 0x4E54494Bu               /* KITN */
typedef struct {
    uint32_t magic, rsv;
    char name[12];                               /* DRK_NAMELEN characters at most, zeros after */
} drk_name_t;
/* A kit as one blob (KIT2): this head, then each pad's record in pad order, len[] bytes each (0: an empty pad) */
#define DRK_BOX_MAGIC 0x3254494Bu                /* KIT2 */
typedef struct {
    uint32_t magic;                              /* DRK_BOX_MAGIC */
    uint16_t bytes;                              /* this head's (DRK_BOX_BYTES) */
    uint8_t npad, rsv;                           /* DRK_NPAD */
    char name[12];                               /* as drk_name_t's (zeros: no name, "KIT n") */
    uint32_t len[DRK_NPAD];
    uint32_t rsv2;
} drk_box_t;
_Static_assert(sizeof(drk_box_t) == 88u && __builtin_offsetof(drk_box_t, name) == 8u &&
               __builtin_offsetof(drk_name_t, name) == 8u, "a kit's blob, its name where the name record has it");
#define DRK_BOX_BYTES ((uint32_t)sizeof(drk_box_t))

#ifndef DRK_CODEC_ONLY                           /* (web/kit builds the record's format and its decoder alone) */
/* The pad records' places (the host's data records, mod_rec_v3: pad record kit * 16 + pad, its sectors at
 * rec_list[at..at + n)), which the firmware sets in the main loop. drk_user_gen is odd while it changes them: the
 * audio side then plays nothing, and a part that took its pads under an older number stops reading their sectors
 * before a later save can erase them (the host waits two blocks). A host build's built-in engine reads the firmware's
 * own table (pads.h) */
_Static_assert(DK_NUSER <= MOD3_NREC && DRK_NPAD == MOD3_RPAD, "a user kit's pads are data records");
#if MOD_SIDE
#define drk_user mod_imp_rec
#define drk_list mod_imp_rec_list
#define drk_user_gen (*mod_imp_rec_gen)
#define DRK_XIP(off) (mod_imp_rec_base + (off))
#else
#define drk_user rec_tab
#define drk_list rec_list
#define drk_user_gen rec_gen
#define DRK_XIP(off) (REC_BASE + (off))
#endif
#endif
/* a sector's payload by its 4 KB number */
#define DRK_SEC(s) ((const uint32_t *)(const void *)DRK_XIP((uint32_t)(s) * 4096u + DRK_SECOFF))

/* the codec (drum_enc.c encodes, drum_real.c decodes): samples per slice of each depth, the 16 scales, the levels */
static const uint8_t DRK_NS[4] = {58, 29, 19, 14};
static const uint16_t DRK_SF[16] = {1, 2, 3, 6, 11, 20, 36, 66, 120, 218, 398, 724, 1319, 2400, 4370, 7960};
static const int8_t DRK_LV[4][16] = {
    {5, -5},
    {3, -3, 10, -10},
    {3, -3, 10, -10, 18, -18, 28, -28},
    {2, -2, 6, -6, 10, -10, 14, -14, 18, -18, 22, -22, 26, -26, 30, -30},
};
