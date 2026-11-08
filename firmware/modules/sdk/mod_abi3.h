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
#define MOD3_HOST 3u                             /* the host table version this contract describes (2: the patch, its
                                                  * schema's callbacks, the host's drawing; 3: SysEx) */
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
#define MOD3_PATCH_MAX 512u                      /* a part's patch (host 2): the module's own bytes, at most */
#define MOD3_SX_MAX 4104u                        /* a SysEx frame the host collects for a module (host 3), at most: F0 to
                                                  * F7 (a DX7 32-voice bank); also its scratch's bytes (host sx_scratch) */

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
#define MOD3_DESC_ID 0x100u                      /* mod_engine_v3.desc: a schema parameter's (host 2) */
/* mod_engine_v3.patch_changed: what wrote the part's patch (host 2) */
enum {
    MOD3_PATCH_EDIT,                             /* a parameter (id; 0xFFFF: several) edited: a page, a knob, the editor */
    MOD3_PATCH_NEW,                              /* a new sound picked (a preset): its notes stop, the module may set the
                                                  * part's own voice parameters from it (P_VOICE ..) */
    MOD3_PATCH_SET,                              /* the whole patch replaced (a project, an undo, a sent patch): the module
                                                  * decides whether it is another sound */
    MOD3_PATCH_RESTORE,                          /* a snapshot put back as it was (a momentary edit, a compare, a song
                                                  * section): nothing reset */
    MOD3_PATCH_MOTION,                           /* Motion moved parameters (id; 0xFFFF: several), audio interrupt */
    MOD3_PATCH_LOAD                              /* a stored sound loaded (a user preset): the module may set the part's
                                                  * voice parameters from it and decides whether it is another sound */
};
typedef struct mod_area_v3 { int16_t x, y, w, h; } mod_area_v3;   /* a widget's place on the screen (host 2 draw) */
/* mod_engine_v3.sysex (host 3): what came (ev), what the module wants next (its answer) */
enum {
    MOD3_SX_MORE,                                /* more bytes of the frame came (f[0..n): F0 first, n grew) */
    MOD3_SX_END,                                 /* the frame ended: f[n - 1] is F7 */
    MOD3_SX_ABORT,                               /* the frame broke: another status in it, too long, its link gone, the
                                                  * scratch taken by a job; nothing more of it comes */
    MOD3_SX_IDLE                                 /* nothing new (it answered MOD3_SXR_IDLE: a handshake's clock) */
};
enum {
    MOD3_SXR_DONE,                               /* done with this frame (its rest, if any, is dropped) */
    MOD3_SXR_MORE,                               /* call again when bytes come or it ends */
    MOD3_SXR_IDLE                                /* .. and once each main-loop turn meanwhile (a handshake waits) */
};
/* mod_engine_v3.sysex_dump: which patch it is */
enum {
    MOD3_DUMP_PART,                              /* a part's sound, sent on its channel (the STORE page's SEND, the
                                                  * editor): the synth's edit buffer */
    MOD3_DUMP_STORED                             /* a stored patch (a user slot: the user pages' send, the editor), ch 0 */
};

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
    const mod_param_v3 *(*desc)(const mod_part_v3 *t, uint32_t k);   /* E k's descriptor now (else edit[k]); host 2:
                                                  * k = MOD3_DESC_ID + id, its schema's parameter id's (else the
                                                  * schema's: mod_schema.h MOD3_SF_DESC) */
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
    /* host 2: the part's patch (host patch(): the module's bytes, its schema says how many, mod_schema.h) and the
     * parameters of its schema that are not fields of it (MOD3_SK_CALL). patch is the part's own (main loop) */
    void (*patch_changed)(mod_part_v3 *t, uint8_t *patch, uint32_t how, uint32_t id);   /* main loop (MOTION: audio):
                                                  * patch is the part's (the module may correct its own bytes) */
    int32_t (*pget)(const mod_part_v3 *t, const uint8_t *patch, uint32_t id);       /* a MOD3_SK_CALL value */
    int (*pput)(mod_part_v3 *t, uint8_t *patch, uint32_t id, int32_t v);            /* .. set: 1 changed */
    void (*pformat)(const mod_part_v3 *t, const uint8_t *patch, uint32_t id, int32_t v, char *val,
                    const char **unit);          /* a MOD3_SF_FORMAT parameter's text (val: 8 bytes) */
    int (*pactive)(const mod_part_v3 *t, const uint8_t *patch, uint32_t id);        /* 0: drawn dim (no effect now) */
    void (*draw)(const mod_part_v3 *t, const uint8_t *patch, uint32_t widget, const mod_area_v3 *a);  /* MOD3_W_DRAW */
    /* host 3: SysEx, main loop. Frames whose first bytes after F0 are one of its schema's MOD3_R_SYSEX patterns (from
     * USB, Bluetooth or the TRS jack) come here as the host collects them (f: the host's scratch, F0 first): ev
     * MOD3_SX_*, the answer MOD3_SXR_*. A module not in SRAM is loaded for its frame first; the host has its own
     * frames (the editor's, the updater's) */
    uint32_t (*sysex)(const uint8_t *f, uint32_t n, uint32_t ev);
    /* the patch as the synth's SysEx, F0..F7 into out (max bytes): its length, 0 none (what: MOD3_DUMP_*; ch: the
     * channel, 0..15) */
    uint32_t (*sysex_dump)(const uint8_t *patch, uint32_t what, uint32_t ch, uint8_t *out, uint32_t max);
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
    /* TEMPORARY (phase 5, the pads): the DRUM kits' lanes and recorded kits as this firmware lays them out (the
     * module's key: MOD3_PADS_KEY, firmware/src/drum_kit.h); 0 for a module that does not name the key. (Host 1 called
     * it legacy and held the shipped synths' patches too: the patch below replaced them) */
    const struct mod_legacy_v3 *legacy;
    /* host 2 */
    uint8_t *(*patch)(const mod_part_v3 *t);     /* the patch the part plays (MOD3_PATCH_MAX bytes room); while its
                                                  * engine is switching, zeros (never another engine's bytes) */
    uint16_t (*color)(uint32_t role);            /* the screen's colours (MOD3_C_*), RGB565 */
    void (*fill)(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c);              /* in the widget's canvas */
    void (*rrect)(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c);
    void (*text)(int32_t x, int32_t y, const char *s, uint32_t font, uint16_t c);      /* font MOD3_FONT_* */
    uint32_t *paired;                            /* voices an engine rendered on CPU1 outside the worker's queue, paired
                                                  * with CPU0's (the CPU lab's count: AUDIO_STATS) */
    /* host 3: SysEx and the parts' patches from the main loop (mod_engine_v3.sysex, sysex_dump and what they do) */
    mod_part_v3 *(*part)(uint32_t k);            /* part k (0..7) */
    uint8_t *(*part_patch)(uint32_t k, uint32_t engine);   /* part k's patch while it is engine's, else 0: read only
                                                  * (patch_put writes it) */
    int (*patch_put)(uint32_t k, uint32_t engine, const uint8_t *src, uint32_t n, uint32_t how);   /* part k's
                                                  * patch's first n bytes, part k playing engine (else 1, nothing
                                                  * written): told as MOD3_PATCH_* how (patch_changed), redrawn */
    int (*sx_part)(uint32_t engine, uint32_t ch);   /* the part a message on channel ch is for: the selected part
                                                  * if it plays engine, else part ch, else engine's first; -1 none.
                                                  * ch MOD3_SX_SEL: the selected part, whatever it plays */
    int (*sx_send)(const uint8_t *f, uint32_t n);   /* F0..F7 out (USB): 0 sent; n 0: 0 while it can send */
    uint8_t *(*sx_scratch)(void);                /* MOD3_SX_MAX bytes of the main loop's (the frame read is lost) */
    int (*sx_stage)(uint32_t engine, const uint8_t *patches, uint32_t count, uint32_t stride);   /* count patches
                                                  * (engine's stored bytes, stride apart) offered for user slots: the
                                                  * USER page asks where. A bank left in the frame (at MOD3_SX_END)
                                                  * stays there until it is chosen. 1 staged */
    uint32_t (*slot_get)(uint32_t engine, uint32_t slot, uint8_t *patch);   /* user slot's stored patch if it is
                                                  * engine's: its length (its schema's sound bytes are written), else 0 */
    uint32_t (*store_slot)(uint32_t engine);     /* the slot engine's STORE page points at */
    void (*message)(const char *s);              /* a line on the screen (0: only redraw it) */
    const volatile uint32_t *ms;                 /* milliseconds, counting */
} mod_host_v3;
#define MOD3_SX_SEL 0xFFu
enum { MOD3_C_BG, MOD3_C_INK, MOD3_C_DIM, MOD3_C_ACCENT, MOD3_C_THEME, MOD3_C_SURF, MOD3_C_RAISE, MOD3_C_COUNT };
enum { MOD3_FONT_SMALL, MOD3_FONT_LABEL, MOD3_FONT_VALUE };

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
