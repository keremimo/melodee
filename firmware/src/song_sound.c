/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Immutable Song/Pattern-local sounds. Allocation belongs to stopped foreground edits;
 * section boundaries only copy bounded RAM, without loading the preset library.
 * The byte representation is also the versioned project payload. */
typedef struct {
    uint8_t p[P_COUNT], engine, preset, user[2], user_native, fm_on;
    char name[16];
    union { uint8_t fm[FP_SIZE + 1u]; cz_patch_t cz; p5_patch_t p5; drum_patch_t drum; } patch;
} song_sound_t;
_Static_assert(sizeof(song_sound_t) == P_COUNT + 22u + FP_SIZE + 1u, "song sound byte layout");
static uint8_t song_sound_count, song_base_reserved;
/* Pattern assignments share immutable snapshots with Song, but own their IDs. */
static uint8_t pattern_sound[NTRK][NPAT];
static uint32_t chain_bank(uint32_t row, uint32_t track);
static uint32_t chain_sound_id(uint32_t row, uint32_t track);
static uint32_t pattern_sound_id(uint32_t k, uint32_t bank)
{ return bank < NPAT ? pattern_sound[k][bank] : 0; }
static int pattern_sounds_any(void)
{
    for (uint32_t k = 0; k < NTRK; k++) for (uint32_t b = 0; b < NPAT; b++)
        if (pattern_sound[k][b]) return 1;
    return 0;
}
static uint32_t resolved_sound_id(uint32_t row, uint32_t k)
{
    uint32_t id = chain_sound_id(row, k);
    return id ? id : pattern_sound_id(k, chain_bank(row, k));
}
static struct { uint8_t mask, active[NTRK]; } song_sounds;
static int16_t motion_base_value(const track_t *t, uint32_t id);
static void motion_restore(track_t *t);
static void motion_rebase(track_t *t);
static song_sound_t *song_sound_pool(void) { return resource[RES_SONG_SOUNDS].ptr; }
static const song_sound_t *song_sound_get(uint32_t id)
{ return id && id <= song_sound_count ? song_sound_pool() + id - 1u : 0; }
static void song_sound_reset(void)
{
    /* Keep an import's preflight reservation until its validated tail restores. */
    memset(pattern_sound, 0, sizeof pattern_sound);
    song_sound_count = 0; memset(&song_sounds, 0, sizeof song_sounds);
    if (!song_base_reserved) resource_release(RES_SONG_BASE);
    resource_release(RES_SONG_PREVIEW);
}
static void song_sound_capture(song_sound_t *s, const track_t *t, const char *name)
{
    uint32_t k = (uint32_t)(t - trk);
    memset(s, 0, sizeof *s);
    for (uint32_t i = 0; i < P_COUNT; i++) s->p[i] = (uint8_t)(motion_base_value(t, i) + 64);
    s->engine = t->eng_req; s->preset = t->preset;
    s->user[0] = (uint8_t)t->user; s->user[1] = (uint8_t)(t->user >> 8); s->user_native = t->user_native;
    str_cpy(s->name, name ? name : "", sizeof s->name);
    if (s->engine == ENGI_FM6) { memcpy(s->patch.fm, fm6_patch_of(k), FP_SIZE); s->fm_on = fm6_on[k]; }
    else if (s->engine == ENGI_CZ) s->patch.cz = *cz_patch_of(k);
    else if (s->engine == ENGI_PROPHET) s->patch.p5 = *p5_patch_of(t);
    else if (s->engine == ENGI_DRUM) s->patch.drum = *drum_patch_of(k);
}
static int song_sound_valid(const song_sound_t *s)
{
    if (!eng_ok(s->engine) || !s->name[0] || s->name[15] || s->user_native > 1u || s->fm_on > FM6_ON_ALL) return 0;
    for (uint32_t i = 0; i < P_COUNT; i++) {
        int32_t v = (int32_t)s->p[i] - 64;
        const param_desc_t *d = param_desc_of(s->engine, i);
        if (v < d->min || v > d->max) return 0;
    }
    for (uint32_t i = 0; s->name[i]; i++) if ((uint8_t)s->name[i] < 32u || (uint8_t)s->name[i] > 126u) return 0;
    if (s->engine == ENGI_CZ && !cz_patch_valid(s->patch.cz.raw)) return 0;
    if (s->engine == ENGI_PROPHET && !p5_patch_valid(&s->patch.p5)) return 0;
    if (s->engine == ENGI_DRUM && !drum_patch_valid(&s->patch.drum)) return 0;
    if (s->engine == ENGI_FM6) {
        uint8_t sanitized[FP_SIZE + 1u]; memcpy(sanitized, s->patch.fm, sizeof sanitized); fm6_sanitize(sanitized);
        if (memcmp(sanitized, s->patch.fm, FP_SIZE)) return 0;
    }
    return 1;
}
static void song_sound_apply(uint32_t k, const song_sound_t *s)
{
    track_t *t = &trk[k];
    for (uint32_t i = 0; i < P_COUNT; i++) if (!param_kept(i)) t->p[i] = (int16_t)s->p[i] - 64;
    t->eng_req = s->engine; t->preset = s->preset;
    t->user = s->user[0] | (uint16_t)s->user[1] << 8; t->user_native = s->user_native;
    if (s->engine == ENGI_FM6) { fm6_put_patch(k, s->patch.fm, 1); fm6_on[k] = s->fm_on; }
    else if (s->engine == ENGI_CZ) { *cz_patch_of(k) = s->patch.cz; cz_track_accept(t); }
    else if (s->engine == ENGI_PROPHET) { *p5_patch_of(t) = s->patch.p5; p5_ready[k] = 1; }
    else if (s->engine == ENGI_DRUM) *drum_patch_of(k) = s->patch.drum;
    motion_rebase(t);
}
static int song_sound_prepare(void)
{
    uint8_t mask = 0;
    song_sounds.mask = 0; memset(song_sounds.active, 0, sizeof song_sounds.active);
    for (uint32_t r = 0; r < chain_config.count; r++) for (uint32_t k = 0; k < NTRK; k++) {
        uint32_t id = chain_config.sound[r][k];
        if (!id) continue;
        const song_sound_t *s = song_sound_get(id);
        if (!s || (k >= NPART && s->engine != ENGI_MIDI)) return 0;
        mask |= (uint8_t)(1u << k);
    }
    for (uint32_t k = 0; k < NTRK; k++) for (uint32_t b = 0; b < NPAT; b++) {
        uint32_t id = pattern_sound[k][b];
        if (!id) continue;
        const song_sound_t *s = song_sound_get(id);
        if (!s || (k >= NPART && s->engine != ENGI_MIDI)) return 0;
        mask |= (uint8_t)(1u << k);
    }
    song_sounds.mask = mask;
    if (!song_sounds.mask) { resource_release(RES_SONG_BASE); return 1; }
    song_sound_t *base = resource_get(RES_SONG_BASE, NTRK * sizeof *base);
    if (!base) { song_sounds.mask = 0; return 0; }
    for (uint32_t k = 0; k < NTRK; k++) if (song_sounds.mask >> k & 1u) song_sound_capture(base + k, &trk[k], "");
    return 1;
}
static int song_sound_changed(uint32_t row, uint32_t k)
{ return (song_sounds.mask >> k & 1u) && song_sounds.active[k] != resolved_sound_id(row, k); }
static void song_sound_section(uint32_t row, uint32_t k)
{
    uint32_t id = resolved_sound_id(row, k);
    const song_sound_t *s = id ? song_sound_get(id) : (song_sound_t *)resource[RES_SONG_BASE].ptr + k;
    song_sound_apply(k, s); song_sounds.active[k] = (uint8_t)id;
}
static void song_sound_stop(void)
{
    for (uint32_t k = 0; k < NTRK; k++) if (song_sounds.mask >> k & 1u) {
        motion_restore(&trk[k]);
        song_sound_apply(k, (song_sound_t *)resource[RES_SONG_BASE].ptr + k);
    }
    song_sounds.mask = 0;
    if (!pattern_sounds_any() && !song_base_reserved) resource_release(RES_SONG_BASE);
}

/* Manual transport starts in the audio path: the editor/import reserves this
 * buffer up front. No allocation or library lookup occurs at a loop boundary. */
static void pattern_sound_begin(void)
{
    song_sounds.mask = 0; memset(song_sounds.active, 0, sizeof song_sounds.active);
    if (!resource[RES_SONG_BASE].size) return;
    for (uint32_t k = 0; k < NTRK; k++) {
        int used = 0;
        for (uint32_t b = 0; b < NPAT; b++) used |= pattern_sound[k][b];
        if (!used) continue;
        song_sound_capture((song_sound_t *)resource[RES_SONG_BASE].ptr + k, &trk[k], "");
        song_sounds.mask |= (uint8_t)(1u << k);
        uint32_t id = pattern_sound_id(k, trk[k].pattern);
        if (id) { song_sound_apply(k, song_sound_get(id)); song_sounds.active[k] = (uint8_t)id; }
    }
}
static void pattern_sound_switch(uint32_t k, uint32_t bank)
{
    if (!(song_sounds.mask >> k & 1u)) return;
    uint32_t id = pattern_sound_id(k, bank);
    if (id == song_sounds.active[k]) return;
    const song_sound_t *s = id ? song_sound_get(id) : (song_sound_t *)resource[RES_SONG_BASE].ptr + k;
    song_sound_apply(k, s); song_sounds.active[k] = (uint8_t)id;
}

/* Foreground edits/undo reserve before publishing an assignment. */
static int pattern_sound_reserve(uint32_t k)
{
    if (!resource_get(RES_SONG_BASE, NTRK * sizeof(song_sound_t))) return 0;
    if (song.playing && !(song_sounds.mask >> k & 1u)) {
        song_sound_capture((song_sound_t *)resource[RES_SONG_BASE].ptr + k, &trk[k], "");
        song_sounds.mask |= (uint8_t)(1u << k);
    }
    return 1;
}
