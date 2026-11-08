/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Immutable Song/Pattern-local sounds. Allocation belongs to stopped foreground edits;
 * section boundaries only copy bounded RAM, without loading the preset library.
 * The byte representation is also the versioned project payload. */
#define SS_PATCH 156u                            /* (Melodee 1.x's: the largest of its four synths' forms) */
#define SS_VOICE_IDS 155u                        /* VOICE: its bytes are the parameters 0..154 */
#define SS_VOICE_ON 144u                         /* .. the patch's switches (VOICE: fm_on) */
typedef struct {
    uint8_t p[P_COUNT], engine, preset, user[2], user_native, fm_on;
    char name[16];
    uint8_t patch[SS_PATCH];                     /* the synth's patch: in RAM its own bytes (at most SS_PATCH), stored
                                                  * in its store format's form (song_sound_stored / song_sound_ram):
                                                  * VOICE its voice's bytes by the parameter ids 0..154 (the 155-byte
                                                  * voice), fm_on its switches; TONE, PROGRAM, KIT the patch's bytes */
} song_sound_t;
_Static_assert(sizeof(song_sound_t) == P_COUNT + 22u + SS_PATCH, "song sound byte layout");
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
/* a song sound as a bank record stores it (main loop): VOICE by its voice's parameter ids and the switches in fm_on
 * (Melodee 1.x's form), the others as in RAM; and back */
static void song_sound_stored(song_sound_t *d, const song_sound_t *s)
{
    *d = *s;
    if (patch_store_tag(s->engine) == MOD_SCH_STORE_VOICE) {
        patch_ids_get(s->engine, s->patch, d->patch, SS_VOICE_IDS);
        d->patch[SS_VOICE_IDS] = 0;
        d->fm_on = s->patch[SS_VOICE_ON];
    }
}
static void song_sound_ram(song_sound_t *s)
{
    if (patch_store_tag(s->engine) == MOD_SCH_STORE_VOICE) {
        uint8_t b[MOD3_PATCH_MAX];
        patch_default(s->engine, b);
        patch_ids_set(s->engine, b, s->patch, SS_VOICE_IDS);
        b[SS_VOICE_ON] = s->fm_on;
        memcpy(s->patch, b, SS_PATCH);
        s->fm_on = 0;
    }
}
static void song_sound_capture(song_sound_t *s, const track_t *t, const char *name)
{
    uint32_t k = (uint32_t)(t - trk);
    memset(s, 0, sizeof *s);
    for (uint32_t i = 0; i < P_COUNT; i++) s->p[i] = (uint8_t)(motion_base_value(t, i) + 64);
    s->engine = t->eng_req; s->preset = t->preset;
    s->user[0] = (uint8_t)t->user; s->user[1] = (uint8_t)(t->user >> 8); s->user_native = t->user_native;
    str_cpy(s->name, name ? name : "", sizeof s->name);
    {
        uint32_t n = patch_len(s->engine);
        memcpy(s->patch, patch_of(k), n < SS_PATCH ? n : SS_PATCH);
    }
}
/* stored: the bank record's form (song_sound_stored), else RAM's */
static int song_sound_valid(const song_sound_t *s, int stored)
{
    uint32_t tag = patch_store_tag(s->engine);
    if (!eng_ok(s->engine) || !s->name[0] || s->name[15] || s->user_native > 1u) return 0;
    for (uint32_t i = 0; i < P_COUNT; i++) {
        int32_t v = (int32_t)s->p[i] - 64;
        const param_desc_t *d = param_desc_of(s->engine, i);
        if (i >= P_E0 && eng_extra_retired(s->engine)) continue;   /* (a synth module not in SRAM: kept as it is) */
        if (v < d->min || v > d->max) return 0;
    }
    for (uint32_t i = 0; s->name[i]; i++) if ((uint8_t)s->name[i] < 32u || (uint8_t)s->name[i] > 126u) return 0;
    if (tag == MOD_SCH_STORE_VOICE) {             /* (the patch as 1.x checked it, by its layout: synths_phase4.h) */
        uint8_t v[SS_PATCH];
        if (stored)
            return p4_voice_valid(s->patch, s->fm_on);
        patch_ids_get(s->engine, s->patch, v, SS_VOICE_IDS);
        return p4_voice_valid(v, s->patch[SS_VOICE_ON]);
    }
    return p4_store_valid(tag, s->patch);         /* (another layout: the module checks its own when it loads) */
}
static void song_sound_apply(uint32_t k, const song_sound_t *s)
{
    track_t *t = &trk[k];
    for (uint32_t i = 0; i < P_COUNT; i++) if (!param_kept(i)) t->p[i] = (int16_t)s->p[i] - 64;
    t->eng_req = s->engine; t->preset = s->preset;
    t->user = s->user[0] | (uint16_t)s->user[1] << 8; t->user_native = s->user_native;
    {
        uint32_t e = s->engine, n = patch_len(e), sl = patch_sound_len(e);   /* (the audio: a song section; from RAM,
                                                                           * no flash read) */
        if (n > SS_PATCH) n = SS_PATCH;
        if (patch_eng_of(k) == e && sl < n)      /* the same synth: its settings past the sound stay (FM6's
                                                  * functions) */
            patch_set(k, e, s->patch, sl, MOD3_PATCH_SET);
        else
            patch_set(k, e, s->patch, n, MOD3_PATCH_SET);   /* (a whole patch: the settings its RAM form starts with) */
        if (patch_store_tag(e) == MOD_SCH_STORE_VOICE && patch_of(k)[SS_VOICE_ON] != s->patch[SS_VOICE_ON]) {
            patch_of(k)[SS_VOICE_ON] = s->patch[SS_VOICE_ON];   /* (a VOICE's switches: the sound's, as 1.x put them) */
            patch_note(k, MOD3_PATCH_EDIT, 0xFFFFu);
        }
    }
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
