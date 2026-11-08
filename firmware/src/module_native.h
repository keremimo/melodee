/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Native synth modules: an engine's own C source compiled against the firmware's headers (core.h, dsp.c, the
 * generated tables), outside the image. Where an ABI 1 module (module_abi.h) sees only a frozen contract and the
 * firmware copies a voice in and out for it, a native module is the engine as it was built in: it exports its real
 * engine_t, reads track_t / voice_t / vmod_t as the firmware lays them out and keeps its working memory as before.
 * The price: it is tied to the firmware it was built with. Its file's head carries MOD_NATIVE_KEY, a hash of every
 * layout and number it shares with the firmware; the loader (module_store.c) refuses any other key, so the native
 * modules ship with each release (tools/build.py builds them beside the image).
 *
 * What the engine reaches of the firmware it reaches through the import table (mod_imports_t), never by address:
 * the loader hands it over once (mod_native_t.bind), the module keeps its pointers (core.h, dsp.c, drum_map.c,
 * drum_kit.h under MOD_NATIVE_SIDE: `trk` is the firmware's tracks, `voice_was` its byte...), and a call into the
 * firmware is a call through one of them: absolute, so tools/mod_build.py's two links still prove the address list.
 *
 * File: mod_file_t with abi MOD_ABI_NATIVE, then the key (u32: a 56-byte head), then the image as for ABI 1 (its
 * first bytes are mod_native_t, mod_file_t.desc 0), the address words, bss after the image in RAM.
 * Included after core.h, dsp.c, drum_map.c, drum_kit.h and cz_parts.h, on both sides. */
#pragma once
#include "module_abi.h"

#define MOD_ABI_NATIVE 2u
#define MOD_NDESC_MAGIC 0x54414E4Du               /* "MNAT" */
#define MOD_NHEAD 56u                             /* a native file's head: mod_file_t, the key */
#define MOD_NIMAGE_MAX 61440u                     /* a native module's code and data, at most */
#define MOD_NBSS_MAX 4096u
#define MOD_NSTACK_MAX 1536u                      /* a callback's deepest stack, the firmware's calls included (the
                                                  * interrupt's and CPU1's audit counts this much for any engine
                                                  * callback: tools/stack_budget.py; tools/mod_build.py --native
                                                  * checks each native module against it) */

/* the firmware's side, handed over before any call (mod_native_t.bind). Append only, MOD_NATIVE_IMPORTS + 1 */
#define MOD_NATIVE_IMPORTS 2u
typedef struct {
    track_t *im_trk;                                 /* trk[NTRK] */
    song_t *im_song;
    uint8_t *im_voice_was;
    int16_t *im_tuning_a4;
    int16_t *im_dith;                               /* dith[DITH_N], dith_f[2 DITH_N]: the block's dither (dsp.c) */
    float *im_dith_f;
    uint8_t *im_midi_verb;                           /* midi_verb[NTRK] */
    drum_patch_t *im_drum_patch, *im_drum_patch_none;   /* drum_patch[NPART] */
    int32_t *im_drum_event_pitch;
    uint32_t *im_drum_event_length;
    volatile uint8_t *im_drum_flash;                 /* drum_flash[NTRK] */
    drk_user_t *im_drk_user;                         /* drk_user[DK_NUSER] (kit_store.c keeps it) */
    volatile uint32_t *im_drk_user_gen;
    /* the part's working memory (resources.c resource_get(RES_ENGINE0 + part % NPART, bytes)), 0: no room */
    void *(*im_part_mem)(uint32_t part, uint32_t bytes);
    void (*im_voice_scale_pitch)(const track_t *t, voice_t *v, uint32_t note);   /* voice.c */
    /* the generated tables the firmware keeps for its own engines (tools/gen_tables.py shared): read in place */
    const int16_t *im_SINE, *im_TANH_Q15;
    const uint32_t *im_PITCH_OCT, *im_A4_RATIO;
    const float *im_P5_TANH;
    /* 2: CZ-1. The parts' tones (cz_parts.h: cz_patch[NPART], cz_patch_none), FM6's sine (filled at power-on:
     * dsp_tables_init), the presets made from Casio's tones (cz_tones.c: main loop) */
    cz_patch_t *im_cz_patch, *im_cz_patch_none;
    float *im_FM6_SIN;
    void (*im_cz_preset_make)(uint32_t k, preset_t *p, char *name);
} mod_imports_t;

/* The layout key: what a native module and the firmware must agree on, hashed (any change: another key) */
#define MNK(i, v) ((uint32_t)(v) * (0x9E3779B1u * (uint32_t)(i) | 1u) + (uint32_t)(i) * 0x85EBCA77u)
#define MNK_T(i, T, f) (MNK(i, __builtin_offsetof(T, f)) + MNK((i) + 100u, sizeof(((T *)0)->f)))
#define MOD_NATIVE_KEY (MNK(1, MOD_NATIVE_IMPORTS) + MNK(2, sizeof(mod_imports_t)) + MNK(3, sizeof(void *)) + \
    MNK(4, sizeof(track_t)) + MNK(5, sizeof(voice_t)) + MNK(6, sizeof(vmod_t)) + MNK(7, sizeof(engine_t)) +      \
    MNK(8, sizeof(param_desc_t)) + MNK(9, sizeof(preset_t)) + MNK(10, sizeof(song_t)) +                         \
    MNK(11, sizeof(drum_patch_t)) + MNK(12, sizeof(drk_user_t)) + MNK(13, sizeof(step_t)) +                    \
    MNK_T(14, track_t, p) + MNK_T(15, track_t, engine) + MNK_T(16, track_t, v) + MNK_T(17, track_t, mono_stack) + \
    MNK_T(18, track_t, nmono) + MNK_T(19, track_t, held) + MNK_T(20, track_t, nheld) +                            \
    MNK_T(21, voice_t, note) + MNK_T(22, voice_t, env) + MNK_T(23, voice_t, env_out) + MNK_T(24, voice_t, pitch16) + \
    MNK_T(25, voice_t, pitch_cur) + MNK_T(26, voice_t, ph) + MNK_T(27, voice_t, s) + MNK_T(28, voice_t, age) +     \
    MNK_T(29, vmod_t, amp1) + MNK_T(30, vmod_t, plog) + MNK_T(31, song_t, octave) + MNK_T(32, song_t, g) +        \
    MNK_T(33, engine_t, presets) + MNK_T(34, engine_t, note_on) + MNK_T(35, engine_t, done) +                      \
    MNK_T(36, engine_t, preset_make) + MNK_T(37, engine_t, state) + MNK_T(38, engine_t, wclass) +                \
    MNK(39, P_COUNT) + MNK(40, P_E0) + MNK(41, P_LN0) + MNK(42, P_VOICE) + MNK(43, P_ATK) + MNK(44, P_SUS) +       \
    MNK(45, P_REL) + MNK(46, P_QUANT) + MNK(47, G_COUNT) + MNK(48, NVOICE) + MNK(49, NPART) + MNK(50, NTRK) +       \
    MNK(51, CTL) + MNK(52, FS) + MNK(53, VOICE_FS) + MNK(54, DITH_N) + MNK(55, AP_COUNT) + MNK(56, AP_SID) +       \
    MNK(57, AP_808) + MNK(58, AP_909) + MNK(59, AP_FM6) + MNK(60, AP_CZ) + MNK(61, F_ONOFF) + MNK(62, F_ALIAS) +   \
    MNK(63, DK_NUSER) + MNK(64, DRK_KSEC) + MNK(65, BOARD_SMK37) + MNK(66, MELODEE_USER_KITS) +                    \
    MNK(67, MELODEE_SID_FAST) + MNK(68, MELODEE_DRUM_FAST) + MNK(69, V_LEGATO) + MNK(70, Q_MPC) + MNK(71, NSTEP) + \
    MNK(72, sizeof(cz_patch_t)) + MNK_T(73, engine_t, menv) + MNK(74, MELODEE_CZ_FAST) + MNK(75, NPOLY))

typedef struct {                                  /* the image's first bytes (mod_file_t.desc 0) */
    uint32_t magic;                               /* MOD_NDESC_MAGIC */
    uint16_t abi, size;                           /* MOD_ABI_NATIVE, sizeof(mod_native_t) */
    uint32_t key;                                 /* MOD_NATIVE_KEY as the module was built */
    uint8_t engine, npresets;                     /* the engine number it plays as; its presets (the head's too) */
    uint8_t flags, rsv;                           /* 0 */
    const char *name;                             /* its engine's (the head's too: the lists without loading) */
    const engine_t *eng;                          /* its engine: ENGINES[engine] while it is in SRAM */
    void (*bind)(const mod_imports_t *im);        /* called once after loading, before anything else */
} mod_native_t;

#if MOD_NATIVE_SIDE
/* the module's side: the firmware's calls (an engine's own declaration of one, `static void voice_scale_pitch(...);`,
 * becomes the pointer's), then its data and calls into the pointers core.h & co. declared, then its descriptor */
static void *(*mod_imp_part_mem)(uint32_t part, uint32_t bytes);
static void (*mod_imp_voice_scale_pitch)(const track_t *t, voice_t *v, uint32_t note);
static void (*mod_imp_cz_preset_make)(uint32_t k, preset_t *p, char *name);   /* (modules/cz/cz.c calls it) */
#define voice_scale_pitch (*mod_imp_voice_scale_pitch)
#define mod_part_mem(part, bytes) mod_imp_part_mem(part, bytes)
static void mod_native_bind(const mod_imports_t *im)
{
    mod_imp_trk = im->im_trk;
    mod_imp_song = im->im_song;
    mod_imp_voice_was = im->im_voice_was;
    mod_imp_tuning_a4 = im->im_tuning_a4;
    mod_imp_dith = im->im_dith;
    mod_imp_dith_f = im->im_dith_f;
    mod_imp_midi_verb = im->im_midi_verb;
    mod_imp_drum_patch = im->im_drum_patch;
    mod_imp_drum_patch_none = im->im_drum_patch_none;
    mod_imp_drum_event_pitch = im->im_drum_event_pitch;
    mod_imp_drum_event_length = im->im_drum_event_length;
    mod_imp_drum_flash = im->im_drum_flash;
    mod_imp_drk_user = im->im_drk_user;
    mod_imp_drk_user_gen = im->im_drk_user_gen;
    mod_imp_part_mem = im->im_part_mem;
    mod_imp_voice_scale_pitch = im->im_voice_scale_pitch;
    mod_imp_SINE = im->im_SINE;
    mod_imp_TANH_Q15 = im->im_TANH_Q15;
    mod_imp_PITCH_OCT = im->im_PITCH_OCT;
    mod_imp_A4_RATIO = im->im_A4_RATIO;
    mod_imp_P5_TANH = im->im_P5_TANH;
    mod_imp_cz_patch = im->im_cz_patch;
    mod_imp_cz_patch_none = im->im_cz_patch_none;
    mod_imp_FM6_SIN = im->im_FM6_SIN;
    mod_imp_cz_preset_make = im->im_cz_preset_make;
}
#ifdef FM1_IRQ_TARGET
#define MOD_NATIVE_EXPORT(sym) __attribute__((used, section(".mod_desc"))) const mod_native_t sym
#else
#define MOD_NATIVE_EXPORT(sym) const mod_native_t sym      /* (host tests: the descriptor by its name) */
#endif
#define MOD_NATIVE_HEAD .magic = MOD_NDESC_MAGIC, .abi = MOD_ABI_NATIVE, .size = sizeof(mod_native_t), \
    .key = MOD_NATIVE_KEY, .bind = mod_native_bind
#endif
