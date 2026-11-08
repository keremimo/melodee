/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The single-precision maths the JUNO-106 needs, with no libm: the functions it uses from Saadet's
 * firmware/src/x0x/fastmath.h, unchanged. A module links nothing of the firmware's, so it carries them.
 *
 * The FM-1's FPU is float-only (-mfprev1). Build the host with -ffp-contract=off (the device has no fused
 * multiply-add) and the two compute the same samples.
 *   fm_exp2f rel 3e-7 on [-126, 127], fm_expf rel 6e-7 on [-87, 88], fm_tanf rel 1e-6 on (-pi/2, pi/2),
 *   fm_sqrtf rel 2e-7. None may be called with NaN; none returns a denormal for an in-range argument. */
#pragma once
#include <stdint.h>

#define FM_PI 3.14159265358979f
#define FM_LOG2E 1.44269504088896f

typedef union { float f; uint32_t u; int32_t i; } fm_bits_t;

static inline float fm_fabsf(float x) { fm_bits_t b; b.f = x; b.u &= 0x7FFFFFFFu; return b.f; }
static inline float fm_minf(float a, float b) { return a < b ? a : b; }
static inline float fm_clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* floor for |x| < 2^31 */
static inline float fm_floorf(float x)
{
    int32_t i = (int32_t)x;
    float f = (float)i;
    return f > x ? f - 1.0f : f;
}

/* 2^n * 2^f for an integer n and |f| <= 0.5 (or a little more) */
static inline float fm_exp2_nf(int32_t n, float f)
{
    fm_bits_t b;
    float p;
    if (n < -126)
        return 0.0f;                 /* flush instead of a denormal */
    p = 1.534580891e-04f;            /* 2^f on [-0.5, 0.5]: degree 6, fitted */
    p = p * f + 1.339993158e-03f;
    p = p * f + 9.618488972e-03f;
    p = p * f + 5.550328776e-02f;
    p = p * f + 2.402264689e-01f;
    p = p * f + 6.931472057e-01f;
    p = p * f + 1.0f;
    b.f = p;
    b.u += (uint32_t)n << 23;
    return b.f;
}

/* 2^x. Split x = n + f, f in [-0.5, 0.5]; fitted polynomial for 2^f. */
static inline float fm_exp2f(float x)
{
    float fl;
    if (x < -126.0f)
        return 0.0f;
    if (x > 127.0f)
        x = 127.0f;
    fl = fm_floorf(x + 0.5f);
    return fm_exp2_nf((int32_t)fl, x - fl);
}

/* e^x with Cody-Waite reduction: x = n ln2 + r, so |x| * eps never reaches the result */
static inline float fm_expf(float x)
{
    float n;
    if (x < -87.3f)
        return 0.0f;
    if (x > 88.7f)
        x = 88.7f;
    n = fm_floorf(x * FM_LOG2E + 0.5f);
    x = (x - n * 0.693145752f) - n * 1.42860677e-6f;      /* ln2 split: hi has 16 bits */
    return fm_exp2_nf((int32_t)n, x * FM_LOG2E);            /* |x log2e| <= 0.5 */
}

/* sin on [-pi, pi]: odd polynomial, degree 13 */
static inline float fm_sin_pi(float x)
{
    float x2 = x * x, p;
    p = 1.345176048e-10f;                      /* degree 13, fitted */
    p = p * x2 - 2.467706523e-08f;
    p = p * x2 + 2.752946668e-06f;
    p = p * x2 - 1.984015614e-04f;
    p = p * x2 + 8.333310417e-03f;
    p = p * x2 - 1.666666459e-01f;
    p = p * x2 + 9.999999946e-01f;
    return p * x;
}

/* tan on (-pi/2, pi/2), the filter-coefficient range: cos(x) = sin(pi/2 - |x|), which keeps
 * its relative accuracy next to the pole where cos is small */
static inline float fm_tanf(float x)
{
    float a = fm_fabsf(x), c = (1.57079637f - a) - 4.37113883e-8f;
    return fm_sin_pi(x) / fm_sin_pi(c);
}

static inline float fm_sqrtf(float x)
{
    fm_bits_t b;
    float y;
    if (x <= 0.0f)
        return 0.0f;
    b.f = x;
    b.u = 0x5F375A86u - (b.u >> 1);              /* 1/sqrt seed, then Newton x3 */
    y = b.f;
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
}

/* denormal / tiny-value flush for filter states */
static inline float fm_flush(float x) { return fm_fabsf(x) < 1e-20f ? 0.0f : x; }
