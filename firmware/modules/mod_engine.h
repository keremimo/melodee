/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* An engine-source synth module (ABI 3, mod_abi3.h): a synth written against the firmware's engine headers (core.h,
 * dsp.c, the generated tables), compiled outside the image. Its track_t, voice_t, vmod_t and engine_t are ABI 3's
 * frozen views (core.h under MOD_SIDE), so the module never sees how the firmware lays out its own structs; what it
 * reaches of the firmware it reaches through the host table, taken once in bind() below (the data and tables as
 * pointers the headers declare under MOD_SIDE: `voice_was`, `dith`, `SINE`...; the firmware's calls as pointers an
 * engine's own declaration of one, `static void voice_scale_pitch(...);`, redeclares).
 *     #include "../mod_engine.h"
 *     #include "eng_x.c"
 *     static x_part_t *x_part(uint32_t part) { return mod_part_mem(part, sizeof(x_part_t)); }
 *     MOD3_ENGINE_EXPORT(mod_x) = {MOD3_ENGINE_HEAD, .engine = 0, .name = "X", .npresets = .., .eng = &ENG_X};
 * tools/mod_build.py builds it with the image's flags (tools/build.py: beside the image, for its board); host tests
 * compile the same file natively (no section, the descriptor by its name).
 * The part's patch is the firmware's bytes, read through host patch() (core.h mod_patch); the engine's own headers
 * say what they mean (modules/<name>/: fm6_patch.h, p5_patch.h, cz_tone.h, drum_patch.h).
 * Host 4's pads and data records: a note's own pitch and length (mod_imp_note_pitch / _len), the records' table
 * (mod_imp_rec, mod_imp_rec_gen, mod_imp_rec_base); the record calls through mod_host. */
#pragma once
#define MOD_SIDE 1
#include "fm1_cc.h"                              /* BOARD_SMK37 */
#include "build_options.h"
#ifndef MELODEE_FLASH                            /* (melodee.c's defaults: KIT_REC's, the module never uses it) */
#define MELODEE_FLASH 1
#endif
#ifndef MELODEE_USB_AUDIO
#define MELODEE_USB_AUDIO 1
#endif
#include <stdint.h>
#include <stddef.h>
#if defined(FM1_IRQ_TARGET) || defined(MOD_NATIVE)   /* (MOD_NATIVE: mod_build.py --native, freestanding AArch64) */
#ifdef FM1_IRQ_TARGET
#include "fm1_time.h"                            /* (MELODEE_AUDIO_PROFILE: the spans' clock) */
#endif
void *memset(void *d, int c, size_t n);          /* (modules/sdk/mod_rt.c: the module carries its own) */
void *memcpy(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
#define __builtin_memset memset
#else
#include <string.h>
#define __attribute__(x)                         /* (as tests/hostsim.c builds the firmware: the same code) */
#endif
#include "sdk/mod_abi3.h"
#include "melodee_tables.h"
#include "core.h"
#define MOD_HANDLE(row) ((uint32_t)MOD3_SELF_ENGINE(mod_self))   /* the module's own handle on this device (bind's self) */
#include "shipped.h"                          /* (the shipped synths' handles in a host build) */
#include "dsp.c"
#include "audio_batch.h"                         /* (the firmware's queue for CPU1: the host's) */
#include "audio_worker.h"                        /* (CPU1's pairing: the host's) */

/* the firmware's calls (an engine's own declaration, `static void voice_scale_pitch(...);`, becomes the pointer's) */
static const mod_host_v3 *mod_host;
static uint32_t mod_self;                        /* (bind's: host->file_read reads this module's own file) */
static void *(*mod_imp_part_mem)(uint32_t part, uint32_t bytes);
static void (*mod_imp_voice_scale_pitch)(const track_t *t, voice_t *v, uint32_t note);
static uint32_t (*mod_imp_trk_nvoice)(const track_t *t);
static int32_t (*mod_imp_input_octave)(const track_t *t);
static const int32_t *mod_imp_note_pitch;      /* (host 4) */
static const uint32_t *mod_imp_note_len;
static const mod_rec_v3 *mod_imp_rec;
static const uint8_t *mod_imp_rec_list;        /* (host 6) */
static const volatile uint32_t *mod_imp_rec_gen;
static const uint8_t *mod_imp_rec_base;
#define voice_scale_pitch (*mod_imp_voice_scale_pitch)
#define trk_nvoice (*mod_imp_trk_nvoice)
#define input_octave (*mod_imp_input_octave)
#define mod_part_mem(part, bytes) mod_imp_part_mem(part, bytes)
#define MOD3_CALL_PATCH(f) ((uint8_t *(*)(const track_t *))(void (*)(void))(f))   /* (track_t: the part's view) */
#define MOD3_TAB(name, id, T) ((name = (T)(uintptr_t)h->table(id)) != 0)
/* the host table into the pointers: 0, or why not (a table or the pads data this firmware does not have) */
static int mod_engine_bind(const mod_host_v3 *h, uint32_t self)
{
    if (h->version < MOD3_HOST_MIN || h->size < MOD3_HOST_SIZE(MOD3_HOST_MIN))
        return 1;
    mod_host = h;
    mod_self = self;
    mod_imp_voice_was = h->was;
    mod_imp_tuning_a4 = h->a4;
    mod_song.g = h->song_g;
    mod_imp_midi_verb = h->midi_rel;
    mod_imp_dith = h->dither;
    mod_imp_dith_f = h->dither_f;
    mod_imp_part_mem = h->part_mem;
    mod_imp_voice_scale_pitch = h->scale_pitch;
    mod_imp_trk_nvoice = h->nvoice;
    mod_imp_input_octave = h->octave;
    mod_imp_patch = MOD3_CALL_PATCH(h->patch);
    mod_imp_paired = h->paired;
    mod_imp_audio_batch_active = h->batch_active;
    mod_imp_audio_batch_push = h->batch_push;
    mod_imp_audio_batch_push_cost = h->batch_push_cost;
    mod_imp_audio_worker_online = h->worker_online;
    mod_imp_audio_worker_submit = h->worker_submit;
    mod_imp_audio_worker_join = h->worker_join;
#if MELODEE_AUDIO_PROFILE
    mod_imp_audio_profile_add = h->profile_add;
#endif
    if (!MOD3_TAB(mod_imp_SINE, MOD3_T_SINE, const int16_t *) || !MOD3_TAB(mod_imp_TANH_Q15, MOD3_T_TANH_Q15, const int16_t *) ||
        !MOD3_TAB(mod_imp_PITCH_OCT, MOD3_T_PITCH_OCT, const uint32_t *) ||
        !MOD3_TAB(mod_imp_A4_RATIO, MOD3_T_A4_RATIO, const uint32_t *) ||
        !MOD3_TAB(mod_imp_A4_LOG, MOD3_T_A4_LOG, const int32_t *) || !MOD3_TAB(mod_imp_P5_TANH, MOD3_T_P5_TANH, const float *) ||
        !MOD3_TAB(mod_imp_FM6_SIN, MOD3_T_FM6_SIN, float *) ||
        !MOD3_TAB(mod_imp_FM6_MKI_LOG, MOD3_T_FM6_MKI_LOG, uint16_t *) ||
        !MOD3_TAB(mod_imp_FM6_MKI_EXP, MOD3_T_FM6_MKI_EXP, uint16_t *) ||
        !MOD3_TAB(mod_imp_FM6_OPL_LOG, MOD3_T_FM6_OPL_LOG, uint16_t *) ||
        !MOD3_TAB(mod_imp_FM6_OPL_EXP, MOD3_T_FM6_OPL_EXP, uint16_t *))
        return 2;
    mod_imp_note_pitch = h->note_pitch;
    mod_imp_note_len = h->note_len;
    mod_imp_rec = h->rec;
    mod_imp_rec_gen = h->rec_gen;
    mod_imp_rec_base = h->rec_base;
    mod_imp_rec_list = h->version >= 6u && h->size >= MOD3_HOST_SIZE(6u) ? h->rec_list : 0;
    return 0;
}
#if defined(FM1_IRQ_TARGET) || defined(MOD_NATIVE)
#define MOD3_ENGINE_EXPORT(sym) __attribute__((used, section(".mod_desc"))) const mod_desc_v3 sym
#else
#define MOD3_ENGINE_EXPORT(sym) const mod_desc_v3 sym      /* (host tests: the descriptor by its name) */
#endif
#ifndef MOD3_ENGINE_HOST
#define MOD3_ENGINE_HOST MOD3_HOST_MIN           /* (a module that needs a later host says so first: SAMPLER's 6) */
#endif
#define MOD3_ENGINE_HEAD .magic = MOD3_MAGIC, .abi = MOD3_ABI, .size = sizeof(mod_desc_v3), .host = MOD3_ENGINE_HOST, \
    .eng_size = sizeof(mod_engine_v3), .key = 0, .bind = mod_engine_bind

