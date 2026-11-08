/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The single-precision maths HYDRA needs, with no libm: the functions it uses from Saadet's
 * firmware/src/x0x/fastmath.h, unchanged. A module links nothing of the firmware's, so it carries them.
 *
 * The FM-1's FPU is float-only (-mfprev1). Build the host with -ffp-contract=off (the device has no fused
 * multiply-add) and the two compute the same samples.
 *   fm_log2f abs 3e-7 on [0.25, 4], 4e-6 on [1e-30, 1e30]. None may be called with NaN. */
#pragma once
#include <stdint.h>

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

/* log2(x), x > 0 (denormals and 0 return -126 / -127-ish, never NaN) */
static inline float fm_log2f(float x)
{
    fm_bits_t b;
    float m, t, t2, p;
    int32_t e;
    b.f = x;
    if (b.i <= 0)
        return -127.0f;
    e = ((b.i >> 23) & 0xFF) - 127;
    b.i = (b.i & 0x007FFFFF) | 0x3F800000;     /* mantissa in [1, 2) */
    m = b.f;
    if (m > 1.41421356f) {                     /* centre on 1: m in [0.707, 1.414) */
        m *= 0.5f;
        e += 1;
    }
    /* log2(m) = 2/ln2 * atanh(t), t = (m-1)/(m+1), |t| <= 0.1716 */
    t = (m - 1.0f) / (m + 1.0f);
    t2 = t * t;
    p = 3.428278367e-01f;                       /* odd polynomial in t, fitted */
    p = p * t2 + 4.115337810e-01f;
    p = p * t2 + 5.770866525e-01f;
    p = p * t2 + 9.617966483e-01f;
    p = p * t2 + 2.885390082e+00f;
    return (float)e + p * t;
}

/* denormal / tiny-value flush for filter states */
static inline float fm_flush(float x) { return fm_fabsf(x) < 1e-20f ? 0.0f : x; }
