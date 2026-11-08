/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The parts' patches: one generic block of bytes per part that its engine's module owns the layout of (its schema
 * says how many and what they mean: module_schema.h, schema.c). The firmware stores, copies, undoes and saves them
 * as bytes; the engine reads its part's through the host table (mod_abi3.h host patch()), and is told when the main
 * loop wrote it (engine_t.patch_changed, schema.c patch_note).
 *
 * Each patch belongs to one engine number (patch_eng): the one that was asked for when it was written. The audio
 * reads a part's patch for the engine it renders now; while a part is switching engines the old one reads zeros,
 * never the bytes of another synth (patch_play). A MIDI-only track's patch is the shared spare: written, never heard.
 * Included by engines.c before the engines (they read through it when built in). */
static uint8_t part_patch[NPART + 1u][MOD3_PATCH_MAX] __attribute__((section(".pool")));
static uint8_t patch_eng[NPART + 1u];            /* the engine number each patch is for (main loop writes, with the
                                                  * audio held off when it changes) */
static const uint8_t patch_zero[MOD3_PATCH_MAX];
/* track k's patch (main loop: what the stores, the pages and the editor read and write) */
static uint8_t *patch_of(uint32_t k) { return part_patch[k < NPART ? k : NPART]; }
static uint32_t patch_eng_of(uint32_t k) { return patch_eng[k < NPART ? k : NPART]; }
/* a patch whose synth this device has not got (a module a store named: eng_map.c): its bytes kept as they came, this
 * many (its schema's length unknown here), so it goes back into the stores whole; 0 otherwise */
static uint16_t patch_keep[NPART + 1u];
static uint32_t patch_kept(uint32_t k) { return patch_keep[k < NPART ? k : NPART]; }
/* the patch part t's engine plays (the audio, a module's host patch()): zeros while it belongs to another engine */
static uint8_t *patch_play(const track_t *t)
{
    uint32_t k = trk_ix(t);
    return k < NPART && patch_eng[k] == t->engine ? part_patch[k] : (uint8_t *)(uintptr_t)patch_zero;
}
/* (schema.c, after the modules' lists: the patches' lengths and starting bytes, writes told to the engines) */
static uint32_t patch_len(uint32_t e);
static void patch_default(uint32_t e, uint8_t *dst);
static void patch_note(uint32_t k, uint32_t how, uint32_t id);
static void patch_set(uint32_t k, uint32_t e, const uint8_t *src, uint32_t n, uint32_t how);
