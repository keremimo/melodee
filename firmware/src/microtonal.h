/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#pragma once
#include "scales.h"

/* Microtonal notes use the existing 7-bit note identity as a scale-degree address:
 * 60 is degree zero at C4 + ROOT + TRN. This keeps independent releases, chords,
 * recordings and projects intact even when two degrees lie within one semitone.
 * Pitch tables are hundredths of a cent; their last entry is the repeat period. */
static const micro_scale_t *micro_scale(const track_t *t)
{
    int32_t s = t->p[P_SCALE];
    return s >= (int32_t)SCALE_LEGACY && s < (int32_t)SCALE_MICRO_END ? &MICRO_SCALE[s - SCALE_LEGACY] : 0;
}
static int pads_eng(uint32_t e);                  /* (pads.c: a synth that plays pads, a kit: no scale, no tuning) */
static int micro_active(const track_t *t)
{
    return t->p[P_QUANT] != Q_OFF && !pads_eng(t->eng_req) && micro_scale(t) != 0;
}
static int32_t micro_floor(int32_t n, int32_t d)
{
    int32_t q = n / d;
    return q - (n % d < 0);
}
static int32_t micro_pitch(const track_t *t, int32_t note)
{
    const micro_scale_t *s = micro_scale(t);
    int32_t d = note - 60, cycle = micro_floor(d, s->count);
    return (60 + t->p[P_ROOT] + t->p[P_TRANS]) * 10000 +
           cycle * s->pitch[s->count] + s->pitch[d - cycle * s->count];
}
static uint32_t micro_degree_map(const track_t *t, int32_t degree, int32_t offset, int strict)
{
    const micro_scale_t *s = micro_scale(t);
    int32_t note = 60 + degree + offset / 12 * s->count;
    int32_t pitch = micro_pitch(t, note);
    if (note < 0 || note > 127 || (strict && (pitch < 0 || pitch > 1270000)))
        return 255u;
    return (uint32_t)note;
}
static uint32_t micro_snap(const track_t *t, int32_t pitch)
{
    const micro_scale_t *s = micro_scale(t);
    int32_t relative = (pitch - 60 - t->p[P_ROOT] - t->p[P_TRANS]) * 10000;
    int32_t cycle = micro_floor(relative, s->pitch[s->count]), degree = 0;
    relative -= cycle * s->pitch[s->count];
    while (degree + 1 < s->count && s->pitch[degree + 1] <= relative)
        degree++;
    if (s->pitch[degree + 1] - relative < relative - s->pitch[degree])
        degree++;
    return micro_degree_map(t, cycle * s->count + degree, 0, 1);
}
static uint32_t scale_note_period(const track_t *t)
{
    return micro_active(t) ? micro_scale(t)->count : 12u;
}

/* A retired entry: a twelve-note tuning or a maqam once listed as a scale. A project holding one keeps it, on the
 * degree addresses above, so it plays as it did; new work takes a scale and a tuning apart. */
static int scale_retired(uint32_t s) { return s < SCALE_TOTAL && ((SCALE_RETIRED[s / 8u] >> (s % 8u)) & 1u); }
/* A tuning moves the pitch of the twelve keys, their notes stay (scales.h TUNING): temperaments fixed on C, so each key
 * keeps its colour; just and maqam tunings on ROOT + TRN. A microtonal scale tunes itself. */
static int tune_active(const track_t *t)
{
    return t->p[P_TUNING] > 0 && t->p[P_TUNING] < (int32_t)TUNING_COUNT && !pads_eng(t->eng_req) && !micro_scale(t);
}
static int32_t tune_off(const track_t *t, int32_t note)   /* how far from equal, hundredths of a cent */
{
    const tuning_t *tn = &TUNING[t->p[P_TUNING]];
    int32_t at = note - (tn->on_root ? t->p[P_ROOT] + t->p[P_TRANS] : 0), step = at - micro_floor(at, 12) * 12;
    return tn->pitch[step] - step * 10000;
}
static int scale_tuned(const track_t *t) { return micro_active(t) || tune_active(t); }
static int32_t scale_pitch(const track_t *t, int32_t note)   /* (scale_tuned) hundredths of a cent */
{
    int32_t pitch = micro_active(t) ? micro_pitch(t, note) : note * 10000 + tune_off(t, note);
    return pitch < 0 ? 0 : pitch > 1270000 ? 1270000 : pitch;
}
/* (micro_active) The semitone a note sounds nearest and the cents off it: what the player reads, the note stays a degree */
static uint32_t micro_nearest(const track_t *t, int32_t note, int32_t *cents)
{
    int32_t pitch = scale_pitch(t, note), nearest = (pitch + 5000) / 10000, off = pitch - nearest * 10000;
    *cents = off < 0 ? -((50 - off) / 100) : (off + 50) / 100;
    return (uint32_t)nearest;
}
/* A maqam picked brings its tuning; leaving one takes that tuning along, unless it was changed since */
static void scale_tuning_follow(track_t *t, uint32_t was, uint32_t now)
{
    if (was >= SCALE_MICRO_END && was < SCALE_TOTAL && t->p[P_TUNING] == SCALE_LINK[was - SCALE_MICRO_END])
        t->p[P_TUNING] = 0;
    if (now >= SCALE_MICRO_END && now < SCALE_TOTAL)
        t->p[P_TUNING] = SCALE_LINK[now - SCALE_MICRO_END];
}
