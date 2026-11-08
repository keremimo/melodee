/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A synth module from the contract alone (ABI 3, mod_abi3.h): no firmware header, the firmware's DSP helpers (dsp.c)
 * as a module can have them, the same arithmetic over the host's tables, so a ported engine renders what it rendered
 * built in. Everything here inlines: a module calls into the firmware only through the host table.
 *
 * A module (one .c file, tools/mod_build.py):
 *     #include "mod_sdk.h"
 *     static void my_note_on(mod_part_v3 *t, mod_voice_v3 *v) { ... }
 *     static void my_render(mod_part_v3 *t, mod_voice_v3 *v, int32_t *out, uint32_t n, const mod_vmod_v3 *m) { ... }
 *     static const mod_engine_v3 MY = { .name = "MINE", ..., .note_on = my_note_on, .render = my_render };
 *     MOD3_EXPORT(my_module) = { MOD3_SDK_HEAD, .engine = 22, .name = "MINE", .npresets = 0, .eng = &MY };
 * Host tests build the same file natively with MOD_HOSTED (no section, the descriptor by its name). */
#pragma once
#include <stdint.h>
#include "mod_abi3.h"

#define MOD_CTL MOD3_CTL                         /* a render call's block, at most */
#define MOD_CTL_LOG2 5
#define MOD_VOICE_FS MOD3_VOICE_FS
#define MOD_NELEM(a) (sizeof(a) / sizeof((a)[0]))
#define md_e(t) ((t)->p + MOD3_P_E0)              /* E1..E8 of part t */

#ifndef MOD3_SDK_HOST
#define MOD3_SDK_HOST MOD3_HOST_MIN              /* the lowest host table version the module needs (define it before
                                                  * this header for more); a later member only when mh->version has it */
#endif
static const mod_host_v3 *mh;                    /* the firmware's table (md_bind) */
static const int16_t *md_sine_tab, *md_tanh_tab; /* its tables the helpers read */
static const uint16_t *md_svf_tab;
/* the descriptor's bind: the host table and the tables the helpers read; 0 ready */
static int md_bind(const mod_host_v3 *h, uint32_t self)
{
    (void)self;
    if (h->version < MOD3_SDK_HOST || h->size < MOD3_HOST_SIZE(MOD3_SDK_HOST))
        return 1;
    mh = h;
    md_sine_tab = (const int16_t *)h->table(MOD3_T_SINE);
    md_tanh_tab = (const int16_t *)h->table(MOD3_T_TANH_Q15);
    md_svf_tab = (const uint16_t *)h->table(MOD3_T_SVF_G);
    return !md_sine_tab || !md_tanh_tab || !md_svf_tab;
}
#ifdef MOD_HOSTED
#define MOD3_EXPORT(sym) const mod_desc_v3 sym
#else
#define MOD3_EXPORT(sym) __attribute__((used, section(".mod_desc"))) const mod_desc_v3 sym
#endif
#define MOD3_SDK_HEAD .magic = MOD3_MAGIC, .abi = MOD3_ABI, .size = sizeof(mod_desc_v3), .host = MOD3_SDK_HOST, \
    .eng_size = sizeof(mod_engine_v3), .key = 0, .bind = md_bind
/* its factory presets, the first one its INIT: rows the builder packs into the file's PRST chunk (never in the image)
 *     MOD3_PRESETS = {{"FAT BASS", 8, {e1..e8}, {atk, dec, sus, rel}, fenv, mono}, ...};
 * category: the browser's (0 none, 1 BASS .. 8 OTHER as the firmware lists them); e: E1..E8; mono: P_VOICE's value */
typedef struct mod3_prow_t {
    char name[21];
    uint8_t category;
    int8_t e[8];
    uint8_t env[4];
    int8_t fenv;
    uint8_t mono;
} mod3_prow_t;
#define MOD3_PRESETS MOD3_PRESETS_PLACE static const mod3_prow_t mod_presets_[]
/* .. the same rows, each with the synth's own patch after it (the first n bytes of its schema's patch: n its patch's
 * length, or less):  MOD3_PRESETS_PATCH(n) = {{{"FAT BASS", 1, {e1..e8}, {atk, ..}, 0, 0}, {patch bytes}}, ...} */
#define MOD3_PRESETS_PATCH(n) MOD3_PRESETS_PLACE static const struct { mod3_prow_t row; uint8_t patch[n]; } mod_presets_[]
#ifdef __ELF__
#define MOD3_PRESETS_PLACE __attribute__((used, section(".mod_presets")))
#else
#define MOD3_PRESETS_PLACE __attribute__((used))
#endif
/* its parameters and pages (mod_schema.h): MOD3_SCHEMA(the mod_sch_src); the builder writes its SCHM chunk from the
 * source built for the host (mod_dump.c) */
#define MOD3_SCHEMA(src) __attribute__((used)) static const mod_sch_src *const mod_schema_ = &(src)
/* the part's working memory (bytes: the engine's .state), 0: none yet */
static inline void *md_part(const mod_part_v3 *t, uint32_t bytes) { return mh->part_mem(t->index, bytes); }

static inline __attribute__((always_inline)) int32_t md_clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}
static inline int32_t md_mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }

/* triangle of a phase, -32768..32767 */
static inline int32_t md_osc_tri(uint32_t ph)
{
    int32_t s = (int32_t)(ph >> 15);
    return s < 65536 ? s - 32768 : 98303 - s;
}
/* sine, linearly interpolated between the host's 1024 points */
static inline int32_t md_sine(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = md_sine_tab[i], b = md_sine_tab[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
/* xorshift32 */
static inline __attribute__((always_inline)) uint32_t md_noise32(int32_t *st)
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}
/* tanh(x / 32768) * 32767, interpolated */
static inline __attribute__((always_inline)) int32_t md_softclip(int32_t x)
{
    const int16_t *t = md_tanh_tab;
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = t[256];
    else
        y = t[i] + (((t[i + 1] - t[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}
/* linear up to k, then a soft knee */
static inline int32_t md_soft_knee(int32_t y, int32_t k)
{
    int32_t a = y < 0 ? -y : y;
    if (a <= k)
        return y;
    a = k + (md_softclip((a - k) * 2) >> 1);
    return y < 0 ? -a : a;
}

/* trapezoidal SVF (A. Simper), Q13 coefficients per block */
typedef struct { int32_t a1, a2, a3; } md_svf_t;
static inline void md_svf_coef_gk(md_svf_t *c, int32_t g, int32_t k)
{
    int32_t den = 4096 + ((g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}
static inline void md_svf_coef_k(md_svf_t *c, int32_t cut, int32_t k)   /* cut 0..127 << 8, k: damping Q12 */
{
    int32_t i, g;
    cut = md_clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = md_svf_tab[i];
    if (i < 127)
        g += ((md_svf_tab[i + 1] - g) * (cut & 255)) >> 8;
    md_svf_coef_gk(c, g, k);
}
static inline int32_t md_svf_lp(const md_svf_t *c, int32_t in, int32_t *ic1, int32_t *ic2)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = md_clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = md_clamp(2 * v2 - *ic2, -150000, 150000);
    return v2;
}

/* the voice's amplitude ramp at sample i of the block */
static inline __attribute__((always_inline)) int32_t md_amp_at(const mod_vmod_v3 *m, uint32_t i)
{
    int32_t x = (m->amp1 - m->amp0) * (int32_t)i;
    return m->amp0 + ((x + ((x >> 31) & (MOD_CTL - 1))) >> MOD_CTL_LOG2);
}
/* (p + 1/2 LSB + dither) >> sh, 1 <= sh <= 15; 0 stays 0 */
static inline __attribute__((always_inline)) int32_t md_dith_cut(int32_t p, uint32_t sh, uint32_t k)
{
    int32_t d = ((int32_t)mh->dither[k & (MOD3_DITH_N - 1u)] >> (15u - sh)) + (1 << (sh - 1u));
    return (p + (d & -(int32_t)(p != 0))) >> sh;
}
/* a voice's sample s (Q15) at sample i, at the voice's level: what render adds to out[i] */
static inline int32_t md_voice_amp(int32_t s, const mod_vmod_v3 *m, uint32_t i)
{
    return md_dith_cut(s * md_mulq15(md_amp_at(m, i), MOD_VOICE_FS), 15, i + (uint32_t)m->amp1);
}
/* the phase increment of pitch16 (1/16 semitone) plus ct cents and fine (1/4096) */
static inline uint32_t md_cents_inc(int32_t pitch16, int32_t ct, int32_t fine)
{
    int32_t d16 = ct * 16 / 100, rem = ct * 16 - d16 * 100;
    uint32_t inc = mh->pitch_inc((uint32_t)md_clamp(pitch16 + d16, 0, 2047));
    return inc + (uint32_t)((int32_t)(inc >> 12) * (rem * 2367 / 16000 + fine));
}
