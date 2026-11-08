/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* DRUM's patch: the part's bytes are the kit's sound offsets, 16 sounds x {semitones -24..24, decay -64..63, character
 * -64..63, attenuation 0..127}; zero keeps the factory kit (what Melodee 1.x stored: a project and a user preset bit
 * packed, a template and a song sound as these bytes). Read by the engine where the firmware keeps it (mod_patch) */
#pragma once
typedef struct { int8_t c[16][4]; } drum_patch_t;
static const drum_patch_t *drum_offs(const track_t *t) { return (const drum_patch_t *)(const void *)mod_patch(t); }
