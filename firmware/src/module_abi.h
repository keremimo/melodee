/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Synth modules: engines that are not part of the firmware image. A module is a file (.mmod, tools/mod_build.py)
 * holding one engine's code and data, linked at address 0, with the list of its absolute address words. The firmware
 * keeps installed modules as pool records (eng_module.c: four slots), copies one into SRAM, adds the block's address
 * to every listed word and plays it as engine number `engine` (ENGINES[]: a retired engine's number when the module
 * is that engine again, TRIO = 6, else a module number 22..NENGINES-1).
 *
 * This header is the contract between both sides: the module sees no firmware symbol, only what the descriptor
 * hands over (its functions, called by the firmware) and the host table (the firmware's, called by the module).
 * ABI 1 freezes every layout and number below; a change is ABI 2 (the loader refuses other ABIs).
 *
 * File: mod_file_t, then `image` bytes (code, constants, initialised data; offset 0 = the block's first byte), then
 * `nrel` 32-bit offsets into the image (each one: an unaligned little-endian word that gets the block's address
 * added), then the chunks if flags says so (module_presets.h: the factory presets). `bss` zero bytes follow the image
 * in RAM. `crc` = CRC-32 (st_crc32) of every byte after the header.
 *
 * Calls: note_on and render from the audio interrupt (one part at a time, never two at once), block once per part
 * and audio block before its voices. Nothing may block, allocate or touch hardware. The part's memory (part_bytes,
 * mod_ctx_t.part) is zero whenever the part picks the engine: zero is the module's initial state. */
#pragma once
#include <stdint.h>

#define MOD_ABI 1u
#define MOD_FILE_MAGIC 0x31444D4Du               /* "MMD1" */
#define MOD_DESC_MAGIC 0x4353444Du               /* "MDSC" */
#define MOD_NAME_MAX 16u
#define MOD_STACK_MAX 1024u                      /* a callback's deepest stack, the host's calls included (the
                                                  * interrupt stack's audit counts this much: tools/stack_budget.py;
                                                  * tools/mod_build.py checks each module against it) */

typedef struct {                                 /* the file's head (52 bytes) */
    uint32_t magic;                              /* MOD_FILE_MAGIC */
    uint16_t abi;                                /* MOD_ABI */
    uint8_t engine;                              /* the engine number it plays as */
    uint8_t flags;                               /* MOD_FILE_CHUNKS: chunks follow (module_presets.h), else 0 */
    uint32_t total;                              /* the file's length: 52 + image + 4 nrel (+ the chunks) */
    uint32_t image, bss, nrel;
    uint32_t desc;                               /* the descriptor's offset in the image */
    uint32_t crc;                                /* CRC-32 of the bytes after this head */
    char name[MOD_NAME_MAX];                     /* the engine's name, 0-padded (lists without loading) */
    uint16_t version;                            /* the module's own */
    uint8_t npresets;                            /* the descriptor's (its PRST chunk's count, when it has one) */
    uint8_t rsv;                                 /* 0 */
} mod_file_t;

/* the parameter formats of the EDIT pages (the firmware's F_* numbers, frozen for ABI 1) */
enum { MF_INT, MF_PCT, MF_BIPCT, MF_TIME, MF_LFOHZ, MF_CUTOFF, MF_DB, MF_SEMI, MF_ENUM, MF_BPM, MF_NOTE, MF_ONOFF };
/* mod_desc_t.knob: what HOME's KNOB 1..4 turn */
enum { MK_E1, MK_E2, MK_E3, MK_E4, MK_E5, MK_E6, MK_E7, MK_E8, MK_ATK, MK_DEC, MK_SUS, MK_REL };

typedef struct {                                 /* one EDIT parameter (the firmware's param_desc_t) */
    const char *label;
    uint8_t fmt;                                 /* MF_* */
    int16_t min, max, def;
    const char *const *names;                    /* MF_ENUM: max + 1 names */
    const char *unit;                            /* MF_INT / MF_ENUM optional unit */
} mod_param_t;

typedef struct {                                 /* a factory sound (the firmware's preset_t) */
    const char *name;
    int8_t e[8];                                 /* E1..E8 */
    uint8_t env[4];                              /* ATK DEC SUS REL */
    int8_t fenv;                                 /* ENV -> FILTER (-64..63) */
    uint8_t mono;                                /* 1 = MONO, 0 = POLY */
} mod_preset_t;

typedef struct {                                 /* a voice's modulation over one block (the firmware's vmod_t) */
    uint32_t inc;                                /* phase increment of the base pitch (tuning, bend, glide, LFO...) */
    int32_t pitch16;                             /* that pitch in 1/16 semitone, 0..2047 */
    int32_t amp0, amp1;                          /* Q15 amplitude ramp over the block (mod_voice_amp) */
    int32_t cutoff;                              /* ENV / LFO -> FLT, 127 << 8 = all the way */
    int32_t shape;                               /* 64 << 8 + ENV / LFO -> SHP */
    int32_t envq15;                              /* the voice's envelope */
    int32_t fine;                                /* residual below 1/16 semitone in inc (1/4096) */
    int32_t plog;                                /* the voice's own offset in Q24 octaves */
} mod_vmod_t;

typedef struct {                                 /* a voice, as the module sees it (copied in and back out) */
    uint8_t note, vel, gate, index;              /* index: the voice's slot 0..15 (per-voice state in the part) */
    uint8_t was;                                 /* note_on: 0 the slot was free, 1 releasing, 2 still held */
    uint8_t rsv[3];
    uint32_t age;                                /* a new number at every note-on */
    uint32_t ph[3];                              /* phases: kept over a retrigger */
    int32_t s[6];                                /* state; desc keep bit k: s[k] kept over a retrigger */
} mod_voice_t;

typedef struct {                                 /* the part the call is for */
    const int16_t *e;                            /* E1..E8 now */
    void *part;                                  /* part_bytes of working memory, zero at first */
    uint8_t part_index;                          /* 0..3 */
    uint8_t voice_mode;                          /* 0 POLY, 1 MONO, 2 LEGATO, 3 UNISON */
    uint16_t held;                               /* note_on: bit k = voice slot k sounds with its key down (the new
                                                  * voice's own bit included); 0 in the other calls */
} mod_ctx_t;

typedef struct {                                 /* the firmware's side, handed over before any call */
    uint16_t abi, size;
    uint32_t rate;                               /* 44100 */
    uint32_t ctl;                                /* a render call's samples, at most (32) */
    int32_t voice_fs;                            /* one full-scale voice at amp 1.0 (mod_voice_amp) */
    const int16_t *sine;                         /* 1025 points of one cycle, Q15 */
    const int16_t *tanh_q15;                     /* tanh(i / 128) * 32767, i = 0..256 */
    const uint16_t *svf_g;                       /* tan(pi fc / FS) Q12 of cutoff 0..127 */
    const int16_t *dither;                       /* 32 values, new each audio block (mod_voice_amp) */
    uint32_t (*pitch_inc)(uint32_t pitch16);     /* the tuned phase increment of a pitch 0..2047 */
    void (*mem_set)(void *d, int c, uint32_t n);
    void (*mem_copy)(void *d, const void *s, uint32_t n);
} mod_host_t;

typedef struct {                                 /* the module's side: the image holds one, at mod_file_t.desc */
    uint32_t magic;                              /* MOD_DESC_MAGIC */
    uint16_t abi, size;                          /* MOD_ABI, sizeof(mod_desc_t) */
    const char *name;                            /* up to 8 characters shown */
    const char *page_title[2];                   /* EDIT 1 (E1..E4) and EDIT 2 (E5..E8) */
    mod_param_t edit[8];
    const mod_preset_t *presets;
    uint8_t npresets;
    uint8_t knob[4];                             /* MK_* */
    uint8_t poly;                                /* voice cap (up to 16), 0 = 8 */
    uint8_t keep;                                /* bit k: s[k] kept over a retrigger */
    uint8_t engine;                              /* the engine number it plays as (= mod_file_t.engine) */
    uint8_t rsv[2];
    uint32_t part_bytes;                         /* per part */
    const mod_host_t **host;                     /* the loader writes the host table's address here */
    void (*note_on)(mod_ctx_t *c, mod_voice_t *v);
    /* add n (<= ctl) samples of voice v into out (mod_voice_amp: the voice's level) */
    void (*render)(mod_ctx_t *c, mod_voice_t *v, int32_t *out, uint32_t n, const mod_vmod_t *m);
    void (*block)(mod_ctx_t *c);                 /* optional: once per audio block, before the voices */
} mod_desc_t;
