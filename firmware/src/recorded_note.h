/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#ifndef MELODEE_RECORDED_NOTE_H
#define MELODEE_RECORDED_NOTE_H
#include <stdint.h>

/* Keep the original duration/rounding bits and extended identifiers. Use
 * helpers for owner/step: their byte fields alone are not complete IDs.
 * The supported ranges need 71 bits. Retain an eighth note bit so the hot
 * owner and step bytes remain byte aligned: 32 timing, eight note, seven
 * velocity, 16 encoded owner/step, six signed pitch and three gate/id bits.
 * The gate shares velocity's byte; high owner/step bits share pitch's byte.
 * This RAM layout is independent of wire schemas; never serialize bitfields.
 * Pack pragmas also apply to host harnesses which strip section attributes.
 * Access through fields lets the target compiler handle odd-stride timing
 * safely; no cast to an aligned uint16_t pointer is permitted. */
#pragma pack(push,1)
typedef struct {
    uint16_t on, duration;
    uint8_t note7;               /* the note (0..127); bit 7: the owner's bit 6 (16 patterns a part) */
    uint8_t vel:7, length:1;
    uint8_t owner, step;
    int8_t pitch:6;
    uint8_t extended:2;
} recorded_note_t;
#pragma pack(pop)
_Static_assert(sizeof(recorded_note_t) == 9u, "packed timed note layout");
static uint32_t recorded_owner(const recorded_note_t *r)
{ return (r->owner & 31u) | ((uint32_t)(r->extended & 1u) << 5) | ((uint32_t)(r->note7 >> 7) << 6); }
static uint32_t recorded_note(const recorded_note_t *r) { return r->note7 & 127u; }
static void recorded_set_note(recorded_note_t *r, uint32_t note) { r->note7 = (uint8_t)((r->note7 & 128u) | (note & 127u)); }
static uint32_t recorded_step(const recorded_note_t *r)
{ return (r->step & 63u) | ((uint32_t)(r->extended & 2u) << 5); }
static uint32_t recorded_exponent(const recorded_note_t *r) { return r->owner >> 5; }
static int recorded_gate(const recorded_note_t *r) { return (r->length & 1u) != 0; }
static int recorded_rounded(const recorded_note_t *r) { return (r->step & 64u) != 0; }
static int recorded_wrapped(const recorded_note_t *r) { return (r->step & 128u) != 0; }
static void recorded_set_owner(recorded_note_t *r, uint32_t owner)
{
    r->owner = (uint8_t)((r->owner & 224u) | (owner & 31u));
    r->extended = (uint8_t)((r->extended & 2u) | ((owner >> 5) & 1u));
    r->note7 = (uint8_t)((r->note7 & 127u) | ((owner >> 6) & 1u) << 7);
}
static void recorded_set_exponent(recorded_note_t *r, uint32_t exponent)
{ r->owner = (uint8_t)((r->owner & 31u) | (exponent << 5)); }
static void recorded_set_gate(recorded_note_t *r, int enabled)
{ r->length = enabled != 0; }
static void recorded_set_step(recorded_note_t *r, uint32_t step, int rounded, int wrapped)
{
    r->step = (uint8_t)((step & 63u) | (rounded ? 64u : 0u) | (wrapped ? 128u : 0u));
    r->extended = (uint8_t)((r->extended & 1u) | ((step >> 5) & 2u));
}
static uint32_t recorded_legacy_length(const recorded_note_t *r)
{return r->length|((uint32_t)r->extended<<1);}
#endif
