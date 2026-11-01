/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Shared DSP building blocks for the Melodee engines (all fixed point).
 * Voice output convention: add sample * amp to out[], where a full-scale
 * oscillator at amp = 1.0 (Q15 32767) contributes VOICE_FS. */
#define VOICE_FS 24000           /* per-voice level: one voice peaks near -6 dBFS before the master */

/* n bytes packed by tools/gen_tables.py lz_pack (reSID's waveforms, CZ-1's factory tones): c < 0x80: c + 1 bytes
 * follow; c >= 0x80: (c & 0x7F) + 3 bytes copied from d + 1 back, d in the next two bytes. A copy may reach back
 * before o into a dictionary the caller placed there */
static void lz_unpack(uint8_t *o, const uint8_t *b, uint32_t n)
{
    uint8_t *e = o + n;
    while (o < e) {
        uint32_t c = *b++;
        if (c < 0x80u) {
            for (c++; c; c--)
                *o++ = *b++;
        } else {
            const uint8_t *src = o - (b[0] | (uint32_t)b[1] << 8) - 1;
            b += 2;
            for (c = (c & 0x7fu) + 3u; c; c--)
                *o++ = *src++;
        }
    }
}

static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* a * k / 65536 without 64-bit: a up to 2^31, k Q16 */
static inline int32_t mulq16(int32_t a, uint32_t k)
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k) >> 16));
}
/* TPDF dither (#100): the cuts to whole samples (a voice's level, the float engines' output, the track level,
 * MASTER) rounded down or towards zero, so a fading note's error followed the note and its harmonics stood
 * still while it decayed. +-1 LSB of triangular noise before each cut turns that error into a steady floor.
 * One table a block (mix_block), each cut reading it at its own offset; a zero sample stays exact 0 */
#define DITH_N 32u
#define DITH_SEED 0x2545F491u
static int16_t dith[DITH_N];                     /* -32767..32767 in 1/32768 of the cut's LSB */
static float dith_f[2u * DITH_N];                /* the same in LSBs, twice: a voice's row (dith_row) never wraps */
static uint32_t dith_r = DITH_SEED;
static __attribute__((noinline)) void dither_block(void)
{
    uint32_t r = dith_r;
    for (uint32_t k = 0; k < DITH_N; k++) {      /* two uniforms from one LCG step: u1 - u2 is triangular */
        r = r * 1664525u + 1013904223u;
        dith[k] = (int16_t)((int32_t)(r >> 17) - (int32_t)((r >> 2) & 32767u));
        dith_f[k] = dith_f[k + DITH_N] = (float)dith[k] * (1.0f / 32768.0f);
    }
    dith_r = r;
}
/* (p + 1/2 LSB + dither) >> sh, 1 <= sh <= 15; p = 0 stays 0 */
static inline __attribute__((always_inline)) int32_t dith_cut(int32_t p, uint32_t sh, uint32_t k)
{
    int32_t d = ((int32_t)dith[k & (DITH_N - 1u)] >> (15u - sh)) + (1 << (sh - 1u));
    return (p + (d & -(int32_t)(p != 0))) >> sh;
}
/* x to the nearest whole sample with dither d (the add of 1.5 * 2^23 rounds); 0.0f stays 0. dith_row(k)[i], i < CTL:
 * a voice's dither for its block, read without a wrap (k: its offset, once a block) */
static inline __attribute__((always_inline)) const float *dith_row(uint32_t k) { return dith_f + (k & (DITH_N - 1u)); }
static inline __attribute__((always_inline)) int32_t dith_cutf_d(float x, float d)
{
    return x != 0.0f ? (int32_t)(x + d + 12582912.0f) - 12582912 : 0;
}
static inline __attribute__((always_inline)) int32_t dith_cutf(float x, uint32_t k) { return dith_cutf_d(x, dith_row(k)[0]); }
/* Shared sample helpers must inline so SRAM kernels never call into XIP. */
static inline __attribute__((always_inline)) int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Musical pitch only: leave dimensionless ratios (pow2_q16) at their original reference.
 * At 440 Hz this is bit-identical to pitch_inc, including its truncation. */
static inline uint32_t tuned_pitch_inc(uint32_t p)
{
    uint32_t inc = pitch_inc(p);
    return tuning_a4 == A4_DEFAULT ? inc :
           (uint32_t)(((uint64_t)inc * A4_RATIO[tuning_a4 - A4_MIN] + (1u << 23)) >> 24);
}
static inline int32_t tuning_log(void) { return A4_LOG[tuning_a4 - A4_MIN]; }

/* sine, linearly interpolated between the 1024 table points (plain lookup: THD -55 dB) */
static inline int32_t sine_i(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], b = SINE[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
static inline int32_t osc_sine(uint32_t ph) { return sine_i(ph); }

/* polyBLEP residual (Q15) around a wrap of a phase accumulator */
static inline int32_t blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);                       /* 0..32767 */
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);       /* -32767..0 */
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}

static inline int32_t osc_tri(uint32_t ph)
{
    int32_t s = (int32_t)(ph >> 15);                 /* 0..131071 */
    return s < 65536 ? s - 32768 : 98303 - s;
}

/* tanh(x / 32768) * 32767, linearly interpolated (a plain lookup gives
 * 256-unit steps, audible on quiet signals) */
static inline __attribute__((always_inline)) int32_t softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = TANH_Q15[256];                           /* continuous with the table */
    else
        y = TANH_Q15[i] + (((TANH_Q15[i + 1] - TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}

/* xorshift32 */
static inline __attribute__((always_inline)) uint32_t noise32(int32_t *st)
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}

/* Trapezoidal SVF (A. Simper), unconditionally stable. Coefficients per
 * block in Q13; signals stay within +-150000 so products fit in 32 bits. */
typedef struct { int32_t a1, a2, a3; } tsvf_t;
static inline void tsvf_coef_gk(tsvf_t *c, int32_t g, int32_t k)   /* g = tan(pi fc / FS) Q12, k: damping Q12 */
{
    int32_t den = 4096 + ((g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}
static inline void tsvf_coef_k(tsvf_t *c, int32_t cut, int32_t k)   /* cut: 0..127 << 8, k: damping Q12 */
{
    int32_t i, g;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)                                       /* between table points: sweeps without 128 steps */
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    tsvf_coef_gk(c, g, k);
}

static inline int32_t tsvf_lp(const tsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return v2;
}

/* amplitude ramp over the block. Blocks are always CTL long, so x / CTL is a
 * shift (rounded towards zero, as a division); the compiler at -Os keeps
 * even constant divisions as a hardware divide, once per sample and voice */
#define CTL_LOG2 5
#if (1 << CTL_LOG2) != CTL
#error "CTL_LOG2 does not match CTL"
#endif
static inline __attribute__((always_inline)) int32_t amp_at(const vmod_t *m, uint32_t i)
{
    int32_t x = (m->amp1 - m->amp0) * (int32_t)i;
    return m->amp0 + ((x + ((x >> 31) & (CTL - 1))) >> CTL_LOG2);
}

/* a voice's sample s (Q15) at the amplitude ramp's sample i, scaled to the voice level (VOICE_FS);
 * engines whose signal peaks near half scale add it << 1. One dithered cut (the ramp's end picks the voice's
 * place in the table) */
static inline int32_t voice_amp(int32_t s, const vmod_t *m, uint32_t i)
{
    return dith_cut(s * mulq15(amp_at(m, i), VOICE_FS), 15, i + (uint32_t)m->amp1);
}

/* linear up to k, then a soft knee: only the peaks above k saturate (|y| - k must stay < 2^30) */
static inline int32_t soft_knee(int32_t y, int32_t k)
{
    int32_t a = y < 0 ? -y : y;
    if (a <= k)
        return y;
    a = k + (softclip((a - k) * 2) >> 1);
    return y < 0 ? -a : a;
}

/* phase increment of pitch16 (1/16 semitone) plus ct cents: whole 1/16 semitones from the table,
 * the rest (and fine, in 1/4096) as a factor (1 ct = 2.367 / 4096) */
static inline uint32_t cents_inc(int32_t pitch16, int32_t ct, int32_t fine)
{
    int32_t d16 = ct * 16 / 100, rem = ct * 16 - d16 * 100;          /* rem: 1/1600 semitone */
    uint32_t inc = tuned_pitch_inc(clamp(pitch16 + d16, 0, 2047));
    return inc + (uint32_t)((int32_t)(inc >> 12) * (rem * 2367 / 16000 + fine));
}

/* 2^(i/192), Q16: pitch ratios in 1/16 semitones, d16 >= -3072 (16 octaves down) */
static uint32_t pow2_q16(int32_t d16)
{
    uint32_t u = (uint32_t)(d16 + 192 * 16), oct = u / 192u;     /* no loop for negative d16 */
    uint32_t r = pitch_inc(1600 + u % 192u) / (pitch_inc(1600) >> 16);   /* 2^(d/192) via the pitch table */
    return oct >= 16u ? r << (oct - 16u) : r >> (16u - oct);
}
