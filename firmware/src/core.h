/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MELODEE core types: tracks, voices, engines, parameters.
 * Up to eight tracks (all four parts to start with: song.ntrk). Tracks 1..4 are synth parts: their own engine, preset,
 * parameters, voices and 64-step patterns. The parts share one budget of VBUDGET units of sounding voices (voice.c).
 * Drums are a synth that plays pads (pads.c: DRUM, a module) on any part. Tracks 5..8 are MIDI-only: always
 * the MIDI Track engine, sequenced like the parts, never rendered or mixed (NMIDI).
 * Sections: sizes, parameters, voices and engines, tracks and the song, system.
 *
 * MOD_SIDE 1: a synth module's own compile (firmware/modules/sdk/mod_engine.h). Its track_t, voice_t, vmod_t and
 * engine_t are the frozen views of ABI 3 (firmware/modules/sdk/mod_abi3.h: the firmware's structs begin with them),
 * and the firmware's data below is reached through the pointers its bind() takes from the host table. */
#include <stdint.h>
#ifndef MOD_SIDE
#define MOD_SIDE 0
#endif
#include "../modules/sdk/mod_abi3.h"
#include "performance_options.h"
#include "tuning.h"
#include "audio_profile.h"
#include "wire_bits.h"
/* MELODEE_BIG_BSS: a main-loop buffer that may live with the big ones (fm1-base: in .pool, its RAM kept for
 * Bluetooth's stack; platform/melodee_app.c). Nothing in the classic build */
#ifndef MELODEE_USB_VOLUME
#define MELODEE_USB_VOLUME MELODEE_USB_AUDIO     /* the menu's USB VOLUME (fm1-base: without the rest of usb_audio.c) */
#endif
#ifndef MELODEE_BIG_BSS
#define MELODEE_BIG_BSS
#endif
#ifndef MELODEE_MIDI_THRU
#define MELODEE_MIDI_THRU 1     /* the menu's MIDI THRU (midi_thru.c; fm1-base: platform/melodee_app.c midi_thru) */
#endif
#ifndef MELODEE_UPDATE_COUNTDOWN
#define MELODEE_UPDATE_COUNTDOWN 1   /* UPDATE MODE's countdown screen (main.c: OCT- + OCT+ held, ui.uboot); fm1-base: 0,
                                      * its update mode is the base's own (REC held at power-on), ui.uboot never set */
#endif

/* ------------------------------------------------------------ sizes --- */
#define NVOICE 16                /* voice slots per part (FM6 plays Dexed's 16) */
#define NPOLY 8                  /* the voices of an engine without its own cap (its per-voice state: engines.c eng_state) */
#define NPART 4                  /* synth parts: tracks 1..4 */
#ifndef VBUDGET
#define VBUDGET (MELODEE_RAM_AUDIO ? 18u : 16u) /* shared weighted voices; RAM profile uses 18 with CPU1 online
                                                 * at 360 MHz (voice.c voice_budget: scaled with a kept 320) */
#endif
#ifndef MELODEE_ADMISSION
#define MELODEE_ADMISSION 0 /* enable only with a calibrated performance contract */
#endif
#define VOICE_BUDGET_DEFAULT (MELODEE_ADMISSION ? 16u : VBUDGET)
#ifndef MELODEE_AUDIO_BENCH
#define MELODEE_AUDIO_BENCH 0 /* laboratory fixed-voice admission bypass; never a release default */
#endif
#ifndef NMIDI
#define NMIDI 4                  /* MIDI-only tracks: tracks 5..8, the MIDI Track engine and no audio */
#endif
#define NTRK (NPART + NMIDI)     /* tracks: the parts, then the MIDI-only ones (a project's first NPART on its FUN wire) */
/* Shared with engine-buffer placement: the queued recorder needs one runtime
 * counter per event, while the reference countdown implementation needs two. */
#ifndef MELODEE_RECORD_CURSOR
#define MELODEE_RECORD_CURSOR 1
#endif
#ifndef MELODEE_RECORD_QUEUE
#define MELODEE_RECORD_QUEUE MELODEE_RECORD_CURSOR
#endif
#if MELODEE_RECORD_QUEUE && !MELODEE_RECORD_CURSOR
#error "The release queue requires the ordered onset cursor"
#endif
/* Host capacity fixtures can exercise the larger identifier space before a
 * new project schema and the device UI enable it in shipping builds. */
#ifndef NSTEP
#define NSTEP 64
#endif
/* Patterns: 16 on each part (tracks 1..4), 8 on each MIDI-only track; arrays are NPAT wide. Records written before
 * 16 kept 8 a track (NPAT_V1: pattern_store.c reads them with that layout). */
#ifndef NPAT
#define NPAT 16u
#endif
#define NPAT_MIDI 8u
#define PROJ_SLOTS 32u                   /* projects: 1..32, an empty one owns no flash (storage.c) */
#ifndef RES_OK
#define RES_OK() 1                               /* melodee.c: the resource region (res.c) is installed */
#endif
#define NPAT_V1 8u
#define trk_npat(k) ((uint32_t)(k) < NPART ? NPAT : NPAT_MIDI)   /* the patterns track k has */
_Static_assert(NSTEP <= 128u && NTRK * NPAT <= 128u, "recorded-note identifiers (seven bits)");
#define HALF_FRAMES 128          /* I2S half buffer: 2.9 ms at 44.1 kHz (a key waits 0..1 half, then plays 1 half later) */
/* SLICE retired. Keep its stored engine number reserved. */
#ifdef MELODEE_SLICE
#undef MELODEE_SLICE
#endif
#define MELODEE_SLICE 0
#ifndef MELODEE_LEGACY_EXTRAS
#define MELODEE_LEGACY_EXTRAS 0 /* reference tests only: retired TRIO, WHEEL and PHYS; PHASE, VOICE and NOISE (retired
                                 * 2026-10-10: the flash for the redesign; CZ-1 keeps PHASE's phase-distortion core) */
#endif
/* host builds (every build but the device's): the synths Melodee ships built in, the reference their modules are held to
 * (firmware/modules/host: their engines, numbers and names live outside the firmware's sources) */
#ifndef MELODEE_HOST_ENGINES
#ifdef FM1_IRQ_TARGET
#define MELODEE_HOST_ENGINES 0
#else
#define MELODEE_HOST_ENGINES 1
#endif
#endif
#if MELODEE_HOST_ENGINES && !MOD_SIDE
#include "../modules/host/host_engines.h"
#endif
#ifndef HOST_NENG
#define HOST_NENG 0u
#define HOST_BUILTIN(e) 0                        /* (the device: every synth is a module) */
#endif
#define USER_GENERAL 16u
#define USER_NATIVE_P5 20u
#define ENGI_MIDI 21u            /* 20 reserved for the Prophet user-preset namespace */
#define NENGINES 30               /* append engines without reusing preset namespaces */
#define ENGI_MOD0 22u            /* 22..NENGINES-1: synth modules' own numbers (eng_module.c); a module may also take a
                                  * retired engine's number when it is that engine again (TRIO = 6) */
#ifndef MELODEE_MODULES
#define MELODEE_MODULES 1        /* synth modules: engines loaded from the pool into SRAM (eng_module.c) */
#endif
#define NMOD 8u                  /* module slots (pool records ST_MFILE0 + k, past a record's size ST_MXFILE0 + k too:
                                  * storage.c) */
#define USER_NATIVE_FM 17u
#define USER_NATIVE_CZ 18u
#define USER_NONE 65535u
#define ENGI_ANALOG 0u           /* retired in 1.2: never selectable (eng_ok), its stored sounds play silent */
#define ENGI_DIGITAL 1u          /* retired: never selectable (eng_ok), its stored sounds play silent */
#define NENG_SHOWN (1u + HOST_NENG + MELODEE_SLICE)   /* the engines one can pick: PRESETS, the EDIT
                                                * layer, the editor, in the display order of engines.c ENGINE_ORDER */
#define NENG_SHOWN_MAX (NENG_SHOWN + NMOD * MELODEE_MODULES)   /* and the loaded modules after them (eng_nshown) */
#define ENGI_SLICE 13u           /* reserved without MELODEE_SLICE: never selectable (eng_ok) */
#ifdef SMP_USER_SLOTS
#undef SMP_USER_SLOTS
#endif
#define SMP_USER_SLOTS 0u        /* sample material and its upload slots are retired */
#define UP_SLOTS 384u            /* shared user slots: any engine, including native SysEx */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS,
    F_OFS, F_FMNOTE, F_FMFRQ,   /* FM6's pages: 0 at the middle of the range, the DX7 break point, an operator's frequency */
    F_TONE,                     /* an FX slot's TONE: DARK (a high cut) .. FULL .. THIN (a low cut), fx_tone_hz */
    F_BITS, F_ALIAS             /* CRUSH's bits and the rate of its held samples */
};

typedef mod_param_v3 param_desc_t;   /* label, fmt (F_*), min, max, def, names (F_ENUM), unit (frozen: mod_abi3.h) */

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_RECQ,     /* P_RECQ: project recording QNT mirrored here for stored-layout compatibility */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_RESERVED33, P_RESERVED34, P_RESERVED35, P_RESERVED36,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_RESERVED45, P_RESERVED46, P_RESERVED47, P_RESERVED48,      /* retired effect format padding */
    P_M1SRC, P_M1DST, P_M1AMT,                 /* modulation matrix (mod.c): 4 slots of SRC, DST, AMT; */
    P_M2SRC, P_M2DST, P_M2AMT,                 /* new common parameters go just before P_E0 (user presets */
    P_M3SRC, P_M3DST, P_M3AMT,                 /* and projects map by count) */
    P_M4SRC, P_M4DST, P_M4AMT,
    P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL, P_FM1_LEVEL,
    P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL, P_FM2_LEVEL,
    P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL, P_FM3_LEVEL,
    P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL, P_FM4_LEVEL,
    P_CHRD, P_VOIC,                            /* chord keys (chord.c): one key plays a chord; its voicing */
    P_MPCDEG,                                  /* QNT MPC: the scale degree pad 02 of each bank plays, 1 = root */
    P_LN0, P_LN1, P_LN2, P_LN3, P_LN4, P_LN5, P_LN6, P_LN7, /* a kit's lane levels, 127 = original gain */
    P_LSYNC, P_LTRIG, P_LPOL, P_SPRD, /* Felucca 1.4 common LFO controls and stereo spread */
    P_FXTYPE, P_FXMIX, P_FXRATE, P_FXCHAR, /* new FX; legacy effect IDs remain inert */
    P_FX2TYPE, P_FX2MIX, P_FX2RATE, P_FX2CHAR,
    P_FX3TYPE, P_FX3MIX, P_FX3RATE, P_FX3CHAR,
    P_FXSEND, /* post-fader chain send, or send to slot 1 in separate mode */
    P_FXSEND2, P_FXSEND3,
    P_FXTONE, P_FX2TONE, P_FX3TONE, /* each slot's wet-input TONE (fx_tone_hz): appended so existing common IDs stay fixed */
    P_STRUM, P_STROKE, /* chord sweep duration and direction; appended for stored-layout compatibility */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};
#define FX_SLOTS 3u
#define FX_PARAMS 4u
/* Append-only effect IDs; shared pickers leave MIDI-only IDs empty. */
enum { FX_OFF, FX_PHASER, FX_WIDTH, FX_FORMANT, FX_TAPE, FX_SPIT, FX_ECHO, FX_ROOM, FX_REVERB, FX_CHORUS, FX_AUDIO_COUNT, FX_MIDIVERB = FX_AUDIO_COUNT, FX_MIDIDELAY, FX_SOFT, FX_HARD, FX_FOLD, FX_FUZZ, FX_CRUSH, FX_FLANGER, FX_PEDAL_PHASER, FX_PEDAL_CHORUS, FX_COUNT };
/* Slot 6 stays retired. Slot 8 is the SLOOP reverb replacement. */
static int fx_retired(int32_t type) { return type == FX_ECHO; }
static int fx_sloop_type(int32_t t){return t>=FX_SOFT && t<=FX_PEDAL_CHORUS;}
static int fx_sloop_swept(int32_t t){return t>=FX_FLANGER && t<=FX_PEDAL_CHORUS;}
static int fx_third_control(int32_t t){return fx_sloop_swept(t) || t==FX_ROOM || t==FX_REVERB;}
static int fx_is_midi(int32_t type) { return type == FX_MIDIVERB || type == FX_MIDIDELAY; }
#define FX_CHAIN_COUNT 1u /* the shared rack only; tracks retain note-effect controls */
#define FX_STATE_COUNT (FX_CHAIN_COUNT*FX_SLOTS)
#define FX_TONE_MAX 63
static int fx_track_type(int32_t type) { return fx_is_midi(type) ? type : FX_OFF; }
static void track_fx_normalize(int16_t *p)
{
    for(uint32_t slot=0;slot<FX_SLOTS;slot++)p[P_FXTYPE+slot*FX_PARAMS]=(int16_t)fx_track_type(p[P_FXTYPE+slot*FX_PARAMS]);
}
static int param_fx_tone(uint32_t id) { return id>=P_FXTONE && id<=P_FX3TONE; }
static int fx_has_tone(int32_t type) { return type==FX_ROOM || type==FX_REVERB; }   /* fx.c FX_ECHO, FX_ROOM, FX_REVERB */
static int param_fx(uint32_t id) { return (id>=P_FXTYPE && id<=P_FX3CHAR) || param_fx_tone(id); }
/* TONE -63..63 -> the cut it sets (Hz), 0 = FULL (none but the effect's own floor, fx_floor_hz): below 0 a high cut,
 * 2640 Hz at -1 down to 120 Hz at -63; above 0 a low cut, 150 Hz at 1 up to 1280 Hz at 63 (squared: fine steps where
 * the ear needs them) */
static int32_t fx_tone_hz(int32_t v)
{
    int32_t a = v < 0 ? -v : v;
    if (a > FX_TONE_MAX) a = FX_TONE_MAX;
    return v > 0 ? 150 + a * a * 1130 / 3969 : v < 0 ? 120 + (64 - a) * (64 - a) * 2520 / 3969 : 0;
}
/* the low cut ECHO (100 Hz), ROOM and REVERB (150 Hz) keep at FULL and under a high cut: what enters them never booms */
#define FX_FLOOR_ECHO 100
#define FX_FLOOR_SPACE 150
static int32_t fx_floor_hz(int32_t type) {
    return type==FX_REVERB ? 0 : FX_FLOOR_SPACE;
}
static int32_t fx_low_cut_hz(int32_t type, int32_t v)   /* an ECHO / ROOM / REVERB slot's low cut at TONE v */
{
    int32_t f = fx_floor_hz(type), t = v > 0 ? fx_tone_hz(v) : 0;
    return t > f ? t : f;
}
static int param_fx_send(uint32_t id) { return id>=P_FXSEND && id<=P_FXSEND3; }


enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_RESERVED4, G_RESERVED5, G_RESERVED6, G_RESERVED7,
    G_RESERVED8, G_RESERVED9, G_RESERVED10, G_RESERVED11,
    G_MIDI, G_DRUMCH, G_ROUTE, G_INFO, /* G_ROUTE: MIDI IN, 0 CH1-4 (channels 1..4 -> parts 1..4), 1 SEL, 2 CH5-8, 3 CH9-12, 4 CH13-16 (seq.c);
                                        * G_DRUMCH (was G_SYNC, unused): a device setting (settings_drumch) */
    G_SLOT, G_BOOT, G_LOAD, G_SAVE,   /* G_BOOT (was G_NAME, unused): a device setting (settings_boot), not song.g */
    G_ENGSEL, G_A4,            /* the editor switches the engine with a SET of G_ENGSEL; G_A4 reuses the unused
                                 * G_ENGGO id, reads tuning_a4 instead of song.g (ids/count stay fixed) */
    G_CLRSEQ, G_INITSND,
    G_RESERVED24, G_RESERVED25, G_RESERVED26, /* retired global format padding */
    G_MFXTYPE, G_MFXMIX, G_MFXRATE, G_MFXCHAR,
    G_MFX2TYPE, G_MFX2MIX, G_MFX2RATE, G_MFX2CHAR,
    G_MFX3TYPE, G_MFX3MIX, G_MFX3RATE, G_MFX3CHAR,
    G_FXRETURN,
    G_FXMODE, /* 0: one shared serial chain; 1: independent slot sends */
    G_BUDGET, /* device-wide shared workload allowance; appended, not a song field */
    G_MFXTONE, G_MFX2TONE, G_MFX3TONE, /* the shared slots' TONE */
    G_COUNT
};
/* First slot wins on import. Explicit edits move the single reverb to the
 * selected slot. Values and parameter IDs keep their existing wire layout. */
static void shared_fx_normalize(int16_t *g)
{
    uint32_t reverb=0;
    for(uint32_t slot=0;slot<FX_SLOTS;slot++){
        int16_t *type=g+G_MFXTYPE+slot*FX_PARAMS;
        if(fx_retired(*type) || fx_is_midi(*type)) *type=FX_OFF;
        if(*type==FX_REVERB){if(reverb)*type=FX_OFF;else reverb=1;}
    }
}
static void shared_fx_select(int16_t *g,uint32_t slot,int32_t type)
{
    if(type==FX_REVERB)for(uint32_t k=0;k<FX_SLOTS;k++)
        if(k!=slot && g[G_MFXTYPE+k*FX_PARAMS]==FX_REVERB)g[G_MFXTYPE+k*FX_PARAMS]=FX_OFF;
    g[G_MFXTYPE+slot*FX_PARAMS]=(int16_t)type;
    shared_fx_normalize(g);
}
static int param_master_fx(uint32_t id){return id>=G_MFXTYPE && id<=G_MFX3CHAR;}
#define G_DRLVL G_RESERVED25         /* (their meaning in a project from before 1.0: proj_drums_to_part) */
/* GLO > WEIGHTS: device settings shown on unused ids (params.c page_desc). Never song.g, never in a project
 * or the editor's tables: global_reserved() stays true for them. */
#define G_WP5 G_RESERVED4
#define G_WCZ G_RESERVED5
#define G_WFM6 G_RESERVED6
#define G_WSID G_RESERVED7
#define G_WDRUM G_RESERVED8
#define G_SAFETY G_RESERVED9
#define G_DRREV G_RESERVED26

#define P_TUNING P_RESERVED47                           /* the project's tuning (scales.h TUNING, 0 Equal): a retired slot,
                                                         * so no stored layout moves. Records before FUN22 / TPLL may hold
                                                         * an old value there: their readers zero it */
static int param_reserved(uint32_t id)
{ return (id>=P_RESERVED33 && id<=P_RESERVED36) || (id>=P_RESERVED45 && id<=P_RESERVED48 && id!=P_TUNING); }
static int global_reserved(uint32_t id)
{ return (id>=G_RESERVED4 && id<=G_RESERVED11) || (id>=G_RESERVED24 && id<=G_RESERVED26); }

/* stored parameters of an older layout -> today's P_* order. A store keeps np = the P_COUNT it was
 * written with; common parameters are only ever added just before P_E0, so the first np - 8 are
 * P_LEVEL.. in order and the last 8 are P_E0..P_E7; the parameters a store does not have take def[]
 * (user presets, projects of formats 1 and 2) */
static void params_by_count(int16_t *out, const int16_t *in, uint32_t np, const int16_t *def)
{
    uint32_t i, nc = np - 8u;
    for (i = 0; i < P_E0; i++)
        out[i] = i < nc ? in[i] : def[i];
    for (i = 0; i < 8u; i++)
        out[P_E0 + i] = in[nc + i];
    for (i = 0; i < FX_SLOTS; i++)
        out[P_FXTYPE + i * FX_PARAMS] = (int16_t)fx_track_type(out[P_FXTYPE + i * FX_PARAMS]);
}

/* engine indices the stores name (engines.c ENGINES[]: append-only) */
#define ENGI_PHYS 9u

/* ------------------------------------------------- voices, engines --- */
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
enum { Q_OFF, Q_SNAP, Q_WHITE, Q_ALL, Q_MPC };  /* P_QUANT (seq.c kb_map, midi_map); 1 = SNAP, the former ON */
#if MOD_SIDE
typedef mod_voice_v3 voice_t;                    /* (a module sees the head only: its voices are vsize apart) */
#else
typedef struct {
    /* the frozen head (mod_abi3.h mod_voice_v3: what engines read and write; eng_module.c asserts it field by field) */
    uint8_t note, vel, gate, active;
    uint8_t stage;               /* env: 0 off, 1 attack, 2 decay/sustain, 3 release, 4 fading out */
    uint8_t mvel;                /* mod.c: the note-on velocity (before UNISON scaling) */
    uint8_t index;               /* its slot in the part (trk_voices_bind) */
    uint8_t rsv0;
    int32_t env;                 /* Q24 */
    int32_t env_out;             /* last control-rate amplitude, Q15 */
    int32_t pitch16, pitch_cur;  /* 1/16 semitone, with glide */
    uint32_t ph[3];
    uint32_t age;
#if MELODEE_LEGACY_EXTRAS
    int32_t s[8];                /* retired VOICE/NOISE also use slots 6 and 7 (over the head's reserved word: the
                                  * firmware's own fields move on, a host build's) */
#else
    int32_t s[6];                /* current engines: slots 0..5, including drum gates */
    uint32_t rsv[1];
#endif
    /* the firmware's own (any layout: no module sees them) */
#if MELODEE_ABI_SHUFFLE                          /* (tests: another firmware's private layout, tests/module_test.c) */
    uint8_t abi_pad[20];
    int16_t mrnd;
    int32_t scale_fine;
    uint8_t side;
    int32_t fine, gstep;
#else
    uint8_t side;                /* stereo spread side, retained while sounding */
    int16_t mrnd;                /* mod.c: the RAND of the note */
    int32_t gstep;               /* glide TIME mode: 1/16 st per control tick, 0 = RATE mode */
    int32_t fine;                /* unison detune: phase increment * (1 + fine / 4096) */
    int32_t scale_fine;          /* note-on snapshot: scale pitch below 1/16 semitone */
#endif
} voice_t;
#endif

typedef mod_vmod_v3 vmod_t;      /* per-voice control-rate modulation, computed in voice.c (frozen: mod_abi3.h) */
typedef mod_preset_v3 preset_t;  /* a sound's common values: name, E1..E8, ATK DEC SUS REL, ENV -> FILTER, MONO
                                  * (frozen: mod_abi3.h; a sound load never touches the steps, the arp is the track's) */

#if MOD_SIDE
typedef mod_engine_v3 engine_t;
#else
struct track;
typedef struct {                 /* an engine (engines.c ENGINES[]; the eng_*.c files) */
    const char *name;            /* "ANALOG" (PRESETS, the editor) */
    const char *page_title[2];   /* EDIT 1 and EDIT 2 */
    param_desc_t edit[8];        /* P_E0..P_E7 */
    const preset_t *presets;
    uint8_t npresets;
    uint8_t knob[4];             /* HOME: the four parameters on KNOB 1..4 */
    uint8_t poly;                /* voice cap for POLY and UNISON (up to NVOICE), 0 = NPOLY */
    uint8_t sampled;             /* 1 = plays recorded material (a position, not a phase): voice.c keeps
                                  * no phases over a retrigger, spreads none for UNISON, renders at SUS 0 */
    uint8_t keep;                /* bit k: s[k] is kept when a sounding voice is retriggered (filters) */
    uint8_t oneshot;             /* 1 = hits (a kit's): always POLY, no glide; amp ends its voices */
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    /* optional (0 = none): the voice amplitude instead of the ADSR curve, once per control tick;
     * gets the ADSR value (Q15, env_tick already ran: it still gates the voice), returns Q15 */
    int32_t (*amp)(struct track *t, voice_t *v, int32_t adsr);
    /* optional: a mode-dependent descriptor of EDIT k (the same range and default as edit[k],
     * another label / value names), 0 = edit[k] */
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    /* optional: once per block and part, before its voices (also with no voice sounding) */
    void (*block)(struct track *t);
    /* optional: the note of key k (0 = the lowest) when the engine maps the keys itself, else -1
     * (then the scale keyboard of seq.c kb_map) */
    int32_t (*keys)(const struct track *t, uint32_t k);
    /* 1 = the engine's own envelopes are the voice's amplitude (FM6): voice.c renders it at 1.0 (no ADSR, no
     * velocity; LFO -> AMP, the matrix's AMP and the fades still apply; ENV is 0) and the voice ends when
     * done() says so (once per control tick, before the render), not at the end of the ADSR's release */
    uint8_t ownenv;
    int (*done)(struct track *t, voice_t *v);
    /* optional: the POLY voice (0..cap-1) for a new note, the engine's own choice (FM6: Dexed's) */
    uint32_t (*alloc)(struct track *t, uint32_t note);
    /* optional: MONO / LEGATO / UNISON moved a sounding voice to a new note without a new attack */
    void (*legato)(struct track *t, voice_t *v);
    /* optional: a key went down in MONO / LEGATO / UNISON, whether or not it takes the voice */
    void (*mono_key)(struct track *t, uint32_t note);
    /* optional: the part's block after its voices (FM6: Dexed's DC filter); nr: voices rendered */
    void (*post)(struct track *t, int32_t *out, uint32_t n, uint32_t nr);
    /* optional: the voice cap of the part now, instead of poly  */
    uint32_t (*cap)(const struct track *t);
    /* optional: the voice budget units one of its voices takes now  */
    uint32_t (*units)(const struct track *t);
    /* with presets 0: preset k (< npresets) made on demand, its name into name (21 bytes); engines.c eng_preset */
    void (*preset_make)(uint32_t k, preset_t *p, char *name);
    /* What voice.c once asked of an engine by its address (built in or a synth module alike):
     * optional: the part's working memory now (RES_ENGINE0 + part; t 0: the engine's usual one), 0 = none */
    uint32_t (*state)(const struct track *t);
    /* optional: the voice's ENV (ENV DEST, the matrix's ENV) instead of the ADSR's, after amp (SID: ENV3) */
    int32_t (*env_q15)(struct track *t, voice_t *v);
    /* optional: 1 = the keys held are a chord the engine's one voice plays itself (voice.c keeps them in the mono
     * stack; SID's C64 in POLY) */
    int (*chord)(const struct track *t);
    /* optional: the voice a new note retriggers (a kit's: its lane's), 0 = voice.c's choice */
    voice_t *(*reuse)(struct track *t, uint32_t note);
    /* optional: render the part's voices on the audio worker (CPU1): their cost class (AP_*), < 0 = not now;
     * render_p, when set, renders there from p, the E1..E8 and lane levels as the block began (else render) */
    int32_t (*batch)(const struct track *t);
    void (*render_p)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m, const int16_t *p);
    uint8_t wclass;              /* GLO > WEIGHTS: its voices weigh settings_vpoints[wclass - 1]; 0 = none */
    /* optional: the matrix's ENV source instead of the ADSR's, after ENV DEST's pitch, cutoff and shape (CZ-1: its
     * native DCA; env_q15 is ENV DEST's and the matrix's both) */
    int32_t (*menv)(struct track *t, voice_t *v);
    /* optional: CPU1's work for the part comes back here, after its voices and before the spread voices are summed
     * (FM6: a voice its own pairing sent there outside the queue) */
    void (*join)(void);
    /* optional: 1 = the part keeps CPU0's larger service reserve while CPU1 renders this block (FM6: a voice sounds or
     * runs on); state, bytes: the part's working memory as it is (bytes 0: none) */
    int (*reserve)(const void *state, uint32_t bytes);
    /* ABI 3 (mod_abi3.h: the fields above in this order, its mod_engine_v3; eng_module.c asserts them). Declared
     * capabilities, MOD3_CAP_*: WHEEL = it plays the mod wheel itself (the SMK-37's wheel adds no vibrato to it) */
    uint8_t caps;
    /* optional: the pitch wheel's range now, Q8 semitones, instead of the channel's (the Prophet: its program's BEND);
     * voice.c sets the wheel's target from it at the part's block */
    int32_t (*bend_range)(const struct track *t);
    /* host 2 (mod_abi3.h): the part's patch written (patch.c patch_note), its schema's computed parameters, their
     * text, their effect now, a widget the engine draws */
    void (*patch_changed)(struct track *t, uint8_t *patch, uint32_t how, uint32_t id);
    int32_t (*pget)(const struct track *t, const uint8_t *patch, uint32_t id);
    int (*pput)(struct track *t, uint8_t *patch, uint32_t id, int32_t v);
    void (*pformat)(const struct track *t, const uint8_t *patch, uint32_t id, int32_t v, char *val, const char **unit);
    int (*pactive)(const struct track *t, const uint8_t *patch, uint32_t id);
    void (*draw)(const struct track *t, const uint8_t *patch, uint32_t widget, const mod_area_v3 *a);
    /* host 3 (mod_abi3.h): the synth's SysEx (sysex_rx.c routes its schema's patterns' frames to it, main loop), the
     * patch as its SysEx (the sends) */
    uint32_t (*sysex)(const uint8_t *f, uint32_t n, uint32_t ev);
    uint32_t (*sysex_dump)(const uint8_t *patch, uint32_t what, uint32_t ch, uint8_t *out, uint32_t max);
    /* host 4 (mod_abi3.h): its data records (rec_store.c keeps them): one checked before it plays it, a take made a
     * record's pad, whether a record has a pad's take */
    int (*rec_check)(const uint32_t *const *secs, uint32_t nsec, uint32_t len);
    uint32_t (*rec_take)(uint32_t slot, uint32_t pad, int16_t *pcm, uint32_t n, uint32_t cap);
    int (*rec_pad)(uint32_t slot, uint32_t pad);
} engine_t;
#endif
/* voice_start: what the voice it starts did just before (0 free, 1 released, 2 its key down: a steal or a move);
 * engine_t.note_on may read it  */
#if MOD_SIDE
static const uint8_t *mod_imp_voice_was;
#define voice_was (*mod_imp_voice_was)
#else
static uint8_t voice_was;
#endif

/* ------------------------------------------------- tracks, the song --- */
enum { ST_NOTE, ST_TIE, ST_REST };
#define SF_ACCENT 1u
#define SF_SLIDE 2u
#define SF_RECORDED 4u                         /* step is a view of timed notes, played by recording.c */
#define RECORD_MAX 1024u
#define RECORD_UNIT 65536u                     /* fractional onset within its swung step; duration uses an exponent */
#include "recorded_note.h"
#define NLANE 8                  /* a step's lanes (a kit's grid: pads.c; MOD3_NLANE) */
typedef struct {                 /* acid-style step: up to 4 notes (POLY), time, accent, slide; drum hits */
    uint8_t note[4];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE */
    uint8_t vel;
    uint8_t hit;                 /* bit l: lane l hits (its lane's note, on any engine: pads.c lane_note): the grid */
    uint8_t acc;                 /* hit != 0: lane accents; hit == 0: recorded gate, 1..255 of a step (0 legacy) */
    uint8_t probability;         /* 0 = legacy 100%; 1..100 = percent, 101 = silent */
} step_t;
/* the notes a step's lane hits play, on any synth (the pattern format's lanes: mod_abi3.h MOD3_LANE_NOTE; a synth with
 * pads maps them to its pads) */
#define STEP_LANE_NOTE MOD3_LANE_NOTE
typedef struct {                 /* a step as formats 1..4 (projects to FUN4) stored it: no hits */
    uint8_t note[4];
    uint8_t n, time, flags, vel;
} step8_t;

/* Probability keeps zero-initialized and legacy patterns at 100%. */
static uint32_t step_chance(const step_t *s) { return !s->probability ? 100u : s->probability <= 100u ? s->probability : 0u; }
static void step_set_chance(step_t *s, uint32_t chance) { s->probability = (uint8_t)(chance >= 100u ? 0u : chance ? chance : 101u); }
static uint32_t step_gate(const step_t *s) { return s->hit ? 0u : s->acc; }
static int step_acc_valid(const step_t *s) { return s->hit ? !(s->acc & ~s->hit) : s->time != ST_REST || !s->acc; }
#define MOTION_MAX 64u
/* Four tracks x64 steps fit one byte. Values retain their signed parameter range. */
typedef struct { uint8_t place, param; int16_t value; } motion_event_t;
typedef struct { uint8_t count, on, rsv[2]; motion_event_t event[MOTION_MAX]; } motion_store_t;
_Static_assert(sizeof(motion_store_t) == 260u, "motion disk layout");
#define CHAIN_ROWS 64u
#define CHAIN_LEGACY_ROWS 16u
#define CHAIN_REST NPAT
#define CHAIN_HOLD (NPAT + 1u)
#define CHAIN_LOOP 0u /* chain_config_t.rsv index; persisted by FBKM */
#define CHAIN_FX_EVENTS 4u
#define CHAIN_SOUNDS 64u
/* fx: zero is dry; 1..27 are the stable SLOOP key-effect IDs plus one.
 * Timing is in conductor steps, repeated with each section repeat. */
typedef struct { uint8_t fx, start, length; } chain_fx_t;
typedef struct { uint8_t slot, repeat; } chain_row_t;
typedef struct {
    uint8_t count, rsv[3];
    chain_row_t row[CHAIN_ROWS];
    chain_fx_t fx[CHAIN_ROWS][CHAIN_FX_EVENTS];
    uint8_t sound[CHAIN_ROWS][NTRK]; /* 0: normal track sound; otherwise immutable song snapshot + 1 */
} chain_config_t;
/* FUN records and raw snapshots retain their original arrangement extent. */
typedef struct {
    uint8_t count, rsv[3];
    chain_row_t row[CHAIN_LEGACY_ROWS];
} chain_legacy_t;
_Static_assert(sizeof(chain_legacy_t) == 36u, "legacy song disk layout");

#if MOD_SIDE
typedef mod_part_v3 track_t;                     /* (a module sees the head only) */
#else
typedef struct track {
    /* the frozen head (mod_abi3.h mod_part_v3: what engines read and write; eng_module.c asserts it field by field) */
    int16_t p[P_COUNT];
    uint16_t size;               /* sizeof(mod_part_v3) (trk_voices_bind) */
    uint8_t index;               /* the track's own number, 0..NTRK-1 (trk_voices_bind) */
    uint8_t engine;              /* what the audio ISR renders */
    voice_t *v;                  /* NVOICE voices: the part's (part_voice), a MIDI-only track the silent midi_voice */
    uint16_t vsize;              /* sizeof(voice_t): a module's stride over v */
    uint8_t nmono;
    uint8_t mono_stack[NVOICE];  /* keys held, in press order (as many as Dexed keeps voices for) */
    /* modulation matrix (mod.c): MIDI performance controllers of the track's channel, 0..127 */
    uint8_t mw, at;              /* CC1 mod wheel, channel aftertouch */
    uint8_t foot, breath, porta; /* CC4 foot, CC2 breath, CC65 portamento on: FM6's DX7 controllers */
    int16_t bend_raw;            /* the pitch bend as sent (signed 14-bit): FM6 bends by its own range */
    uint32_t rsv[4];
    /* the firmware's own (any layout: no module sees them) */
#if MELODEE_ABI_SHUFFLE                          /* (tests: another firmware's private layout, tests/module_test.c) */
    uint8_t abi_pad0[36];
#endif
    uint8_t preset;
    uint8_t eng_req;             /* engine the UI asked for (the ISR switches at a block start) */
    int8_t mpc_octave;           /* MPC bank's input octave, -3..3; independent of song.octave */
    uint16_t user;                /* user preset slot + 1 the sound came from (UI), 0 = none */
    uint8_t user_native;         /* legacy project origin only; new presets use the shared bank */
#if !MELODEE_ABI_SHUFFLE
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;             /* Q15 */
    int32_t lfo_fade;            /* Q15 ramp after note-on */
    uint32_t lfo_rnd;
    int16_t lfo_prev;
    uint8_t sp_alt;
#endif
    /* keyboard / arp input: held notes in press order */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;            /* keys physically held for the arp */
    uint8_t latched;             /* HOLD: keep notes after release */
    /* arp runtime */
    uint32_t arp_pos;            /* samples (INT), or MIDI musical units, into the arp step */
    uint32_t arp_idx;
    uint8_t arp_note;            /* sounding arp note, 0 = none */
    uint32_t arp_off;            /* remaining gate in the same clock units */
    /* sequencer */
    step_t step[NSTEP];
    volatile uint8_t pattern, pattern_next;             /* active bank, queued bank (0xff: none) */
    volatile uint32_t pattern_gen;                      /* invalidates editor history even for identical banks */
    uint32_t seq_pos;            /* samples (INT), or MIDI musical units, into the step */
    uint16_t seq_idx;
    uint8_t seq_notes[4 + NLANE];   /* sounding seq notes (the step's notes, then its hits) */
    uint8_t seq_n;
    uint8_t seq_hold;            /* last step slides: keep the notes until the next step */
    uint8_t slide_glide;         /* next legato note glides (slide) */
    uint32_t seq_off;
    uint8_t seq_active;          /* any step programmed */
    uint8_t rskip_idx;           /* live recording put notes into the step about to play: */
    uint8_t rskip_n, rskip[NLANE];   /* do not trigger them again there (they sound already) */
    /* live recording of held notes (seq.c rec_hold): the steps they are held into become TIEs */
    uint8_t rh_n, rh_note[4];    /* recorded notes still held, 0 = none */
    uint8_t rh_start;            /* the step they were recorded into */
    uint8_t rh_ties;             /* TIE steps written after it */
    uint8_t rh_last;             /* the last of them; rh_bak: what it held (an early release puts it back) */
    step_t rh_bak;
    uint32_t rh_elapsed;         /* actual held duration before onset quantization */
#if MELODEE_ABI_SHUFFLE
    uint8_t abi_pad1[52];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;
    int32_t lfo_fade;
    uint32_t lfo_rnd;
    int16_t lfo_prev;
    uint8_t sp_alt;
#endif
    /* mono (the stack: the head's) */
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t peak;
    int16_t armp, aholdp;        /* P_AMODE / P_AHOLD as last seen by the ISR */
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    int16_t pe_old[8];           /* P_E0..P_E7 of the sounding engine: the fade renders with these */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
    /* modulation matrix (mod.c; the controllers: the head's) */
    uint8_t ex_off;              /* 127 - CC11 expression (0: full, the MIDI default) */
    uint8_t m_vel, m_key, m_vi;  /* the latest note-on: velocity, note, voice index (per-block destinations) */
    int16_t m_rnd;               /* .. its RAND */
    int32_t m_env;               /* the amp envelope of voice m_vi, last block (Q15) */
} track_t;
#endif

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t grid;                /* 1: the keys are the DRUM grid (ui.c grid_on): only the lane keys play (seq.c);
                                  * 2: NAME (ui_name.c): no key plays */
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t rec_wait, rec_pass;   /* waiting for loop zero, or recording since pattern start */
    uint8_t rec_latch;           /* explicit REC stays armed across pattern ends: bit per track */
    uint8_t sel;                 /* selected track 0..trk_n()-1: keys, pages, editor */
    uint8_t ntrk;                /* tracks in use: 1..ntrk play, the rest wait empty and silent (0: the NPART parts) */
    int8_t octave;
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
} song_t;

#if MOD_SIDE                /* (a module: the song's global parameters, the MIDI insert's releases; no track_t but
                             * the one a callback gets) */
static struct { const int16_t *g; } mod_song;
static const uint8_t *mod_imp_midi_verb;
#define song mod_song
#define midi_verb mod_imp_midi_verb
#else
static track_t trk[NTRK];        /* the instrument: four parts, then the MIDI-only tracks */
static voice_t part_voice[NPART][NVOICE];
static voice_t midi_voice[NVOICE];   /* shared by the MIDI-only tracks: never started (their notes go out as MIDI) */
static void trk_voices_bind(void)    /* boot, and a host fixture after clearing trk: every voice silent */
{
    memset(part_voice, 0, sizeof part_voice);
    memset(midi_voice, 0, sizeof midi_voice);
    for (uint32_t k = 0; k < NTRK; k++) {
        trk[k].v = k < NPART ? part_voice[k] : midi_voice;
        trk[k].index = (uint8_t)k;               /* (the views a synth module reads: mod_abi3.h) */
        trk[k].size = (uint16_t)sizeof(mod_part_v3);
        trk[k].vsize = (uint16_t)sizeof(voice_t);
    }
    for (uint32_t i = 0; i < NVOICE; i++) {
        for (uint32_t k = 0; k < NPART; k++) part_voice[k][i].index = (uint8_t)i;
        midi_voice[i].index = (uint8_t)i;
    }
}
/* the part a voice belongs to (NPART: none, midi_voice) */
static inline uint32_t voice_part(const voice_t *v)
{
    uintptr_t d = (uintptr_t)v - (uintptr_t)part_voice;
    return (uintptr_t)v >= (uintptr_t)part_voice && d < sizeof part_voice ? (uint32_t)(d / sizeof part_voice[0]) : NPART;
}
static song_t song;
/* Audio-owner snapshot of the insert release override; zero keeps the sound's envelope. */
static uint8_t midi_verb[NTRK];
#define TSEL (&trk[song.sel])    /* the selected track */

static int32_t input_octave(const track_t *t)
{ return t->p[P_QUANT] == Q_MPC ? t->mpc_octave : song.octave; }
/* The tracks in use: 1..trk_n(), always from track 1 (it clocks the song). The others keep their structs, sound and
 * play nothing (voice.c trk_note_on), are drawn nowhere and get a fresh sound when added (project.c track_add) */
static inline uint32_t trk_n(void) { return song.ntrk - 1u < NTRK ? song.ntrk : NPART; }
/* the parts in use: the tracks the audio renders and mixes (a MIDI-only track never sounds) */
static inline uint32_t trk_parts(void) { uint32_t n = trk_n(); return n < NPART ? n : NPART; }
/* track k can only be a MIDI Track (tracks 5..8) */
static inline int trk_midi_slot(uint32_t k) { return k >= NPART && k < NTRK; }
#endif
/* a part's number, voice i of a part, a voice's slot: what engine sources use (a module reads the views' fields, the
 * firmware its arrays) */
#if MOD_SIDE
#define trk_ix(t) ((uint32_t)(t)->index)
#define trk_voice(t, i) ((voice_t *)mod3_voice(t, i))
#define voice_ix(t, v) ((uint32_t)(v)->index)
#else
#define trk_ix(t) ((uint32_t)((t) - trk))
#define trk_voice(t, i) (&(t)->v[i])
#define voice_ix(t, v) ((uint32_t)((v) - (t)->v))
#endif
/* the patch a part's engine plays (patch.c: the firmware keeps it; a module reads it through the host table) */
#if MOD_SIDE
static uint8_t *(*mod_imp_patch)(const track_t *t);
#define mod_patch(t) mod_imp_patch(t)
static uint32_t *mod_imp_paired;
#define eng_paired (*mod_imp_paired)
#else
static uint8_t *patch_play(const track_t *t);
#define mod_patch(t) patch_play(t)
static uint32_t eng_paired;                      /* voices paired on CPU1 outside the queue (host paired: AUDIO_STATS) */
#endif
/* host 3's main-loop calls (mod_abi3.h: SysEx, the parts' patches): a module's through its host table; a host build's
 * built-in engine calls the firmware's own (sysex_route.c) */
#if MOD_SIDE
#define MOD_HOST3_CALL(f) (mod_host->f)
#define MOD_HOST3_MS() (*mod_host->ms)
#else                                            /* (eng_module.c: through the hooks sysex_route.c sets) */
static mod_part_v3 *mod_h3_part(uint32_t k);
static uint8_t *mod_h3_part_patch(uint32_t k, uint32_t engine);
static int mod_h3_patch_put(uint32_t k, uint32_t engine, const uint8_t *src, uint32_t n, uint32_t how);
static int mod_h3_sx_part(uint32_t engine, uint32_t ch);
static int mod_h3_sx_send(const uint8_t *f, uint32_t n);
static uint8_t *mod_h3_sx_scratch(void);
static int mod_h3_sx_stage(uint32_t engine, const uint8_t *patches, uint32_t count, uint32_t stride);
static uint32_t mod_h3_slot_get(uint32_t engine, uint32_t slot, uint8_t *patch);
static uint32_t mod_h3_store_slot(uint32_t engine);
static void mod_h3_message(const char *s);
#define MOD_HOST3_CALL(f) (mod_h3_##f)
#define MOD_HOST3_MS() fm1_ms
#endif

#if !MOD_SIDE
#include "microtonal.h"

/* SWING of a track's step clock: the track's own plus the global one, at most 100 (#31). The sequencer
 * (seq.c step_samples) times steps with it. At 100 the even
 * steps are 1.4 x the straight length and the odd ones 0.6 x. */
#define SWING_MAX 100
static inline int32_t track_swing(const track_t *t)
{
    int32_t sw = t->p[P_SSWING] + song.g[G_SWING];
    return sw < 0 ? 0 : sw > SWING_MAX ? SWING_MAX : sw;
}
/* the length of step idx of a straight length `base`, swung: even steps longer, odd ones shorter */
static inline uint32_t swing_step_len(const track_t *t, uint32_t base, uint32_t idx)
{
    int32_t sw = track_swing(t) * (int32_t)base / 250;
    return base + (uint32_t)((idx & 1u) ? -sw : sw);
}
#endif

/* ----------------------------------------------------------- system --- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
#if !MOD_SIDE
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
static inline void reset_intended(void)         /* a reset on purpose (UBOOT, an update): not a failed boot */
{
    bootguard.pending = 0;
}

/* GLO solo belongs to the mixer, independently of quick-layer effects. */
static volatile uint8_t track_solo;
static uint8_t track_solo_latch;   /* the editor's solo (TRACKS): what sounds while no GLO solo key is held */
#endif
