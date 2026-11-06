/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The physical output's -6 dB ceiling, after USB capture and the computer's audio: the last cut before the codec. It takes
 * one bit, so one random bit is its dither: an even sample halves exactly, an odd one rounds up or down alike, and the mean
 * follows the signal with no offset. Exact 0 stays 0. One bit a frame, for both sides: mono stays L = R */
#ifndef MELODEE_CEILING_H
#define MELODEE_CEILING_H
#include <stdint.h>
static uint32_t ml_ceiling_r;
static __attribute__((noinline)) void ml_ceiling(int32_t *lr, uint32_t n)
{
    uint32_t r = ml_ceiling_r;
    for (uint32_t i = 0; i < 2u * n; i++) {
        if (!(i & 1u))
            r = r * 1664525u + 1013904223u;
        int32_t v = lr[i];
        v = v > 8388607 ? 8388607 : v < -8388608 ? -8388608 : v;
        lr[i] = (v + (int32_t)(r >> 31)) >> 1;
    }
    ml_ceiling_r = r;
}
#endif
