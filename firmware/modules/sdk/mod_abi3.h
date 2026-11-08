/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee synth modules, ABI 3: the contract between a module file (.mmod, tools/mod_build.py) and every firmware that
 * plays it. A module built once keeps working when the firmware changes, because it sees nothing of the firmware but
 * what this file freezes:
 *
 *  - Views, not copies. The firmware's part (track_t) and voice (voice_t) begin with mod_part_v3 and mod_voice_v3, field
 *    for field; its own fields follow them and change as it likes. A callback gets pointers to the firmware's structs
 *    themselves, read as these heads. Voices are mod_part_v3.vsize bytes apart (mod3_voice): never index them as an
 *    array of mod_voice_v3. A voice's modulation (mod_vmod_v3) is built for each call.
 *  - The host table (mod_host_v3): the firmware's data and calls, handed to bind() once. Append only: a later firmware
 *    adds members at its end and counts version up; a module names the lowest version it needs (mod_desc_v3.host) and
 *    an older firmware refuses it before loading it (module_store.c: MS_NEWER, its engine and name kept).
 *  - The engine (mod_engine_v3): today's engine callbacks, flags and capabilities. The firmware copies it at load
 *    (eng_size bytes; a field a module is too old to have reads 0).
 *  - Frozen numbers: the parameter numbers (P_*, mod_part_v3.p), the global ones (G_*, mod_host_v3.song_g), the cost
 *    classes (AP_*, the CPU1 queue), the parameter formats (F_*), the table ids (MOD3_T_*), the sizes below. The
 *    firmware asserts its own are these (eng_module.c); changing one is another ABI.
 *
 * File: the head (mod_file3_t, 60 bytes), the image (code, constants, initialised data; linked at 0, the descriptor
 * first), `nrel` u32 offsets into the image (each an unaligned little-endian word the loader adds the block's address
 * to), then chunks if flags says so (module_presets.h: the factory presets, PRST). `bss` zero bytes follow the image in
 * RAM. crc = CRC-32 of every byte after the head. Little endian, 32-bit pointers (the host tests' own builds: 64-bit).
 *
 * Calls: note_on, render, block and the other voice and part callbacks from the audio interrupt (one part at a time;
 * batch kernels on CPU1), bind / desc / keys / bend_range / state / preset_make from the main loop. Nothing may block or
 * touch hardware. The part's memory (host part_mem) is zero whenever the part picks the engine. */
#pragma once
#include <stdint.h>

#define MOD3_ABI 3u
#define MOD3_HOST 1u                             /* the host table version this contract describes */
#define MOD3_FILE_MAGIC 0x31444D4Du              /* "MMD1": every module file's (ABI 1 and 2: older firmware's) */
#define MOD3_MAGIC 0x33444F4Du                   /* "MOD3": the descriptor, the image's first bytes */
#define MOD3_FILE_CHUNKS 1u                      /* mod_file3_t.flags: chunks follow the address words */
#define MOD3_NAME_MAX 16u
#define MOD3_IMAGE_MAX 61440u                    /* a module's code and data, at most */
#define MOD3_BSS_MAX 4096u
#define MOD3_STACK_MAX 1536u                     /* a callback's deepest stack, the host's calls included (the audio
                                                  * interrupt's and CPU1's stack audit counts this much for every engine
                                                  * callback: tools/stack_budget.py; tools/mod_build.py checks it) */

/* ---- frozen numbers (the firmware's own must be these: eng_module.c) */
#define MOD3_NVOICE 16u                          /* a part's voices */
#define MOD3_NPART 4u                            /* the parts that sound: tracks 0..3 (4..7 MIDI only, never rendered) */
#define MOD3_RATE 44100u
#define MOD3_CTL 32u                             /* samples of one render call, at most */
#define MOD3_VOICE_FS 24000                      /* one voice at amplitude 1.0 (dsp.c voice_amp) */
#define MOD3_DITH_N 32u
#define MOD3_P_COUNT 124u                        /* mod_part_v3.p: the firmware's P_* numbers, E1..E8 from MOD3_P_E0 */
#define MOD3_P_E0 116u
#define MOD3_P_ATK 1u                            /* (a few more P_* numbers by name: HOME's knobs) */
#define MOD3_P_DEC 2u
#define MOD3_P_SUS 3u
#define MOD3_P_REL 4u
#define MOD3_P_VOICE 37u                         /* 0 POLY, 1 MONO, 2 LEGATO, 3 UNISON */
#define MOD3_G_TUNE 3u                           /* mod_host_v3.song_g: TUNE in cents */
#define MOD3_AP_COUNT 32u                        /* cost classes (the firmware's AP_*): the CPU1 queue's, the profile's */
#define MOD3_BATCH_BYTES 148u                    /* a CPU1 job's descriptor bytes, at most (host_v3 batch_push) */

/* the parameter formats of a parameter (the firmware's F_*) */
enum {
    MOD3_F_INT, MOD3_F_PCT, MOD3_F_BIPCT, MOD3_F_TIME, MOD3_F_LFOHZ, MOD3_F_CUTOFF, MOD3_F_DB, MOD3_F_SEMI, MOD3_F_ENUM,
    MOD3_F_BPM, MOD3_F_NOTE, MOD3_F_ONOFF, MOD3_F_OCT, MOD3_F_STEPS, MOD3_F_OFS, MOD3_F_FMNOTE, MOD3_F_FMFRQ,
    MOD3_F_TONE, MOD3_F_BITS, MOD3_F_ALIAS, MOD3_F_COUNT
};
/* the tables the firmware keeps, by id (mod_host_v3.table: read in place; 0 when this firmware has none) */
enum {
    MOD3_T_SINE = 1,                             /* int16_t[1025]: one cycle, Q15 */
    MOD3_T_TANH_Q15,                             /* int16_t[257]: tanh(i / 128) * 32767 */
    MOD3_T_SVF_G,                                /* uint16_t[128]: tan(pi fc / FS) Q12 of cutoff 0..127 */
    MOD3_T_PITCH_OCT,                            /* uint32_t: the phase increments of the top octave (dsp.c) */
    MOD3_T_A4_RATIO,                             /* uint32_t[81]: concert pitch 400..480 Hz / 440, Q24 */
    MOD3_T_A4_LOG,                               /* int32_t[81]: log2 of the same, Q24 */
    MOD3_T_P5_TANH,                              /* float: the interpolated tanh (gen_tables.py) */
    MOD3_T_FM6_SIN,                              /* float[1025]: filled at power-on */
    MOD3_T_FM6_MKI_LOG, MOD3_T_FM6_MKI_EXP,      /* uint16_t[1024] each: Dexed's MkI engine, filled at power-on */
    MOD3_T_FM6_OPL_LOG, MOD3_T_FM6_OPL_EXP,      /* uint16_t[256] each: Dexed's OPL engine, filled at power-on */
    MOD3_T_COUNT
};
/* mod_engine_v3.caps */
#define MOD3_CAP_WHEEL 1u                        /* it plays the mod wheel itself: the SMK-37's wheel adds no vibrato */

/* ---- the views */
typedef struct mod_voice_v3 {                    /* a voice: the firmware's voice_t begins with these, its own follow */
    uint8_t note, vel, gate, active;             /* its key, velocity (1..127), key down, sounding */
    uint8_t stage;                               /* the firmware's ADSR: 0 off, 1 attack, 2 decay / sustain, 3 release,
                                                  * 4 fading out (stolen) */
    uint8_t mvel;                                /* the note-on velocity before UNISON scaled it */
    uint8_t index;                               /* its slot in the part, 0..MOD3_NVOICE - 1 */
    uint8_t rsv0;
    int32_t env;                                 /* the ADSR, Q24 */
    int32_t env_out;                             /* the amplitude of the last block, Q15 */
    int32_t pitch16, pitch_cur;                  /* its pitch in 1/16 semitone: where it goes, where it is (glide) */
    uint32_t ph[3];                              /* phases: kept over a retrigger */
    uint32_t age;                                /* a new number at every note-on */
    int32_t s[6];                                /* the engine's (mod_engine_v3.keep bit k: s[k] kept over a retrigger) */
    uint32_t rsv[1];                             /* 0 */
} mod_voice_v3;

typedef struct mod_part_v3 {                     /* a part (a track): the firmware's track_t begins with these */
    int16_t p[MOD3_P_COUNT];                     /* its parameters by P_* number, E1..E8 from MOD3_P_E0 */
    uint16_t size;                               /* sizeof(mod_part_v3) as this firmware lays it out */
    uint8_t index;                               /* the track: 0..MOD3_NPART - 1 for the parts that sound */
    uint8_t engine;                              /* the engine number the audio renders it with now */
    void *voices;                                /* its MOD3_NVOICE voices, vsize bytes apart (mod3_voice) */
    uint16_t vsize;
    uint8_t nmono;                               /* keys in the mono stack (MONO / LEGATO / UNISON, a chord's keys) */
    uint8_t mono_stack[16];                      /* .. in press order */
    uint8_t mw, at, foot, breath, porta;         /* its channel's mod wheel, aftertouch, CC4, CC2, CC65 (0..127) */
    int16_t bend_raw;                            /* its channel's pitch bend as sent, signed 14-bit */
    uint32_t rsv[4];                             /* 0 */
} mod_part_v3;
#define mod3_voice(t, i) ((mod_voice_v3 *)(void *)((uint8_t *)(t)->voices + (uint32_t)(i) * (t)->vsize))

typedef struct mod_vmod_v3 {                     /* a voice's modulation over one render call (the firmware's vmod_t) */
    uint32_t inc;                                /* the phase increment of its pitch (tuning, bend, glide, LFO..) */
    int32_t pitch16;                             /* that pitch, 1/16 semitone, 0..2047 */
    int32_t amp0, amp1;                          /* the amplitude ramp over the block, Q15 */
    int32_t cutoff;                              /* ENV / LFO -> FLT, 127 << 8 = all the way */
    int32_t shape;                               /* 64 << 8 + ENV / LFO -> SHP */
    int32_t envq15;                              /* the voice's envelope (ENV DEST, the matrix) */
    int32_t fine;                                /* the residual below 1/16 semitone in inc, 1/4096 */
    int32_t plog;                                /* its own offset (glide, LFO / ENV pitch, the matrix's, unison detune),
                                                  * without TUNE and bend, Q24 octaves */
} mod_vmod_v3;                                   /* (no reserved words: modules put it in CPU1 descriptors; a later host
                                                  * version appends fields) */

typedef struct mod_param_v3 {                    /* one parameter of an EDIT page (the firmware's param_desc_t) */
    const char *label;
    uint8_t fmt;                                 /* MOD3_F_* */
    int16_t min, max, def;
    const char *const *names;                    /* MOD3_F_ENUM: max + 1 names */
    const char *unit;                            /* MOD3_F_INT / F_ENUM: an optional unit */
} mod_param_v3;

typedef struct mod_preset_v3 {                   /* a sound's common values (the firmware's preset_t) */
    const char *name;
    int8_t e[8];                                 /* E1..E8 */
    uint8_t env[4];                              /* ATK DEC SUS REL */
    int8_t fenv;                                 /* ENV -> FILTER, -64..63 */
    uint8_t mono;                                /* 1 = MONO, 0 = POLY */
} mod_preset_v3;

typedef void (*mod3_batch_fn)(const void *data, int32_t *pcm);   /* a CPU1 job: its descriptor's bytes, its block */

/* ---- the engine: what the firmware calls (every callback optional unless named; 0 = none) */
typedef struct mod_engine_v3 {
    const char *name;                            /* the lists' (the file head's name too) */
    const char *page_title[2];                   /* EDIT 1 (E1..E4), EDIT 2 (E5..E8) */
    mod_param_v3 edit[8];                        /* E1..E8 */
    const mod_preset_v3 *presets;                /* 0: the file's (PRST) */
    uint8_t npresets;
    uint8_t knob[4];                             /* HOME's KNOB 1..4: P_* numbers */
    uint8_t poly;                                /* voices for POLY / UNISON, 0 = 8 */
    uint8_t sampled;                             /* 1 = plays a position, not a phase */
    uint8_t keep;                                /* bit k: s[k] kept over a retrigger */
    uint8_t oneshot;                             /* 1 = hits: always POLY, no glide, amp ends its voices */
    void (*note_on)(mod_part_v3 *t, mod_voice_v3 *v);                                               /* (needed) */
    void (*render)(mod_part_v3 *t, mod_voice_v3 *v, int32_t *out, uint32_t n, const mod_vmod_v3 *m); /* (needed) */
    int32_t (*amp)(mod_part_v3 *t, mod_voice_v3 *v, int32_t adsr);   /* the amplitude instead of the ADSR's, Q15 */
    const mod_param_v3 *(*desc)(const mod_part_v3 *t, uint32_t k);   /* E k's descriptor now (else edit[k]) */
    void (*block)(mod_part_v3 *t);               /* once per block and part, before its voices */
    int32_t (*keys)(const mod_part_v3 *t, uint32_t k);              /* key k's note, the engine's own map; -1 none */
    uint8_t ownenv;                              /* 1: its own envelopes are the amplitude; done() ends a voice */
    int (*done)(mod_part_v3 *t, mod_voice_v3 *v);
    uint32_t (*alloc)(mod_part_v3 *t, uint32_t note);               /* the POLY voice for a new note */
    void (*legato)(mod_part_v3 *t, mod_voice_v3 *v);                /* a voice moved to a new note, no attack */
    void (*mono_key)(mod_part_v3 *t, uint32_t note);                /* a key went down in MONO / LEGATO / UNISON */
    void (*post)(mod_part_v3 *t, int32_t *out, uint32_t n, uint32_t nr);   /* the part's block after its voices */
    uint32_t (*cap)(const mod_part_v3 *t);       /* the voice cap now, instead of poly */
    uint32_t (*units)(const mod_part_v3 *t);     /* voice budget units of one of its voices now */
    void (*preset_make)(uint32_t k, mod_preset_v3 *p, char *name);  /* (presets made on demand) */
    uint32_t (*state)(const mod_part_v3 *t);     /* the part's working memory (t 0: its usual), 0 none */
    int32_t (*env_q15)(mod_part_v3 *t, mod_voice_v3 *v);            /* the voice's ENV for ENV DEST and the matrix */
    int (*chord)(const mod_part_v3 *t);          /* 1: the keys held are a chord its one voice plays */
    mod_voice_v3 *(*reuse)(mod_part_v3 *t, uint32_t note);           /* the voice a note retriggers */
    int32_t (*batch)(const mod_part_v3 *t);      /* render its voices on CPU1: their cost class, < 0 not now */
    void (*render_p)(mod_part_v3 *t, mod_voice_v3 *v, int32_t *out, uint32_t n, const mod_vmod_v3 *m, const int16_t *p);
    uint8_t wclass;                              /* its cost class (GLO > WEIGHTS: 1..5), 0 none */
    int32_t (*menv)(mod_part_v3 *t, mod_voice_v3 *v);               /* the matrix's ENV source */
    void (*join)(void);                          /* CPU1's work sent outside the queue comes back */
    int (*reserve)(const void *state, uint32_t bytes);              /* 1: keep CPU0's larger service reserve */
    uint8_t caps;                                /* MOD3_CAP_* */
    int32_t (*bend_range)(const mod_part_v3 *t); /* the pitch wheel's range now, Q8 semitones (else the channel's) */
} mod_engine_v3;

/* ---- the firmware's side, handed to bind() (append only) */
struct mod_legacy_v3;
typedef struct mod_host_v3 {
    uint16_t version;                            /* the members this firmware fills: MOD3_HOST or later */
    uint16_t size;                               /* sizeof(mod_host_v3) as it has it */
    /* data */
    const uint8_t *was;                    /* in note_on: the voice was 0 free, 1 releasing, 2 its key still down */
    const int16_t *a4;                    /* concert pitch, Hz */
    const int16_t *song_g;                       /* the song's global parameters by G_* number */
    const uint8_t *midi_rel;                     /* by track: the MIDI insert's release override (0: the sound's) */
    const int16_t *dither;                       /* the block's dither: MOD3_DITH_N values */
    const float *dither_f;                       /* .. as floats in LSBs, twice over */
    const void *(*table)(uint32_t id);           /* MOD3_T_* */
    /* calls */
    void *(*part_mem)(uint32_t part, uint32_t bytes);   /* the part's working memory, 0: no room */
    void (*scale_pitch)(const mod_part_v3 *t, mod_voice_v3 *v, uint32_t note);   /* a voice to a note on its scale */
    uint32_t (*nvoice)(const mod_part_v3 *t);    /* the voices the part plays now (its mode, the engine's cap) */
    uint32_t (*pitch_inc)(uint32_t pitch16);     /* the tuned phase increment of a pitch 0..2047 */
    int32_t (*octave)(const mod_part_v3 *t);     /* the octave the part's keys play in now, -3..3 (keys()) */
    /* CPU1: the firmware's queue (cost classes: AP_*), CPU1's own pairing (without CPU1: never online) */
    const uint8_t *batch_active;
    void (*batch_push)(mod3_batch_fn run, const void *data, uint32_t bytes, int32_t *out, uint32_t n, uint32_t cls);
    void (*batch_push_cost)(mod3_batch_fn run, const void *data, uint32_t bytes, int32_t *out, uint32_t n, uint32_t cls,
                            uint32_t cost);
    const int *worker_online;
    int (*worker_submit)(void (*fn)(void *), void *context);
    int (*worker_join)(void);
    void (*profile_add)(uint32_t cls, uint32_t ticks);   /* (0: the firmware does not profile) */
    /* the main loop's */
    void (*lz_unpack)(uint8_t *dst, const uint8_t *src, uint32_t n);   /* dsp.c's format (module_presets.h) */
    int (*file_read)(uint32_t self, uint32_t off, void *dst, uint32_t n);   /* the module's own file (bind's self) */
    /* TEMPORARY (until the patch blob): the firmware's own data of the synths it shipped with, laid out as it has it
     * (the module's key: MOD3_LEGACY_KEY, firmware/src/module_legacy.h); 0 for a module that does not name the key */
    const struct mod_legacy_v3 *legacy;
} mod_host_v3;

/* ---- the module's side: the image's first bytes */
typedef struct mod_desc_v3 {
    uint32_t magic;                              /* MOD3_MAGIC */
    uint16_t abi, size;                          /* MOD3_ABI, sizeof(mod_desc_v3) */
    uint16_t host;                               /* the lowest host table version it needs */
    uint16_t eng_size;                           /* sizeof(mod_engine_v3) it was built with */
    uint8_t engine, npresets;                    /* the engine number it plays as; its presets (the PRST chunk's) */
    uint8_t flags, rsv;                          /* 0 */
    uint32_t key;                                /* the legacy data's key it was built against, 0: it reads none */
    const char *name;                            /* its engine's */
    const mod_engine_v3 *eng;
    int (*bind)(const mod_host_v3 *h, uint32_t self);   /* the first call: 0 ready, else refused */
} mod_desc_v3;

typedef struct mod_file3_t {                     /* the file's head (60 bytes) */
    uint32_t magic;                              /* MOD3_FILE_MAGIC */
    uint16_t abi;                                /* MOD3_ABI */
    uint8_t engine;                              /* = the descriptor's */
    uint8_t flags;                               /* MOD3_FILE_CHUNKS or 0 */
    uint32_t total;                              /* the file's length */
    uint32_t image, bss, nrel;
    uint32_t desc;                               /* 0 */
    uint32_t crc;                                /* CRC-32 of the bytes after the head */
    char name[MOD3_NAME_MAX];                    /* = the descriptor's, 0-padded (the lists without loading) */
    uint16_t version;                            /* the module's own */
    uint8_t npresets;                            /* = the descriptor's */
    uint8_t rsv;
    uint32_t key;                                /* = the descriptor's */
    uint16_t host;                               /* = the descriptor's */
    uint16_t rsv2;
} mod_file3_t;
#define MOD3_HEAD 60u
