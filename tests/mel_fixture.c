/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The firmware's side of the .mel project file (web/mel_file.js, tested by web/test_mel.mjs; the firmware itself has
 * no .mel code): the records the web page reads and writes, made and read here by the firmware's own code.
 *   mel_fixture emit DIR    layout.json: where a project record keeps what the page touches; projects as the device
 *                           stores them (.hpj, packed) and whole (.rec); three kits recorded through Record kit's path
 *                           from the firmware's own drum voices, with the takes it was given (take-KIT-PAD.s16: 16-bit,
 *                           44.1 kHz, the whole buffer; web/kit/check.mjs makes the same kits from them); a project
 *                           naming kits in every place a record can (a drum track, a song's and a pattern's own sound,
 *                           automation), beside values that only look like kits, and the same project with its kits
 *                           renamed here, on the decoded state
 *   mel_fixture verify DIR  what the page's import sent (out-*.bin, written by test_mel.mjs) goes through the backup
 *                           protocol's PUT as the device takes it: the kit plays, the project loads, every kit it
 *                           names is the new one, everything else as it was */
static unsigned char nor[0x100000];
#define REC_BASE ((const uint8_t *)nor)        /* (the data records' flash: the player reads them in place) */
#define BACKUP_TEST_NO_MAIN 1
#define BACKUP_TEST_KITS 1
#include "backup_test.c"
/* (DRUM's Record kit take, encoder and record check: built in, modules/drum/kit_take.c) */
#include <assert.h>
#include <sys/stat.h>

static const char *dir;
static uint8_t rec[BANK_STORE_SIZE], rec2[BANK_STORE_SIZE], big[BANK_STORE_SIZE + DRK_KIT_MAX + 64];
static FILE *file(const char *name, const char *mode)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, mode);
    if (!f) { printf("mel: cannot open %s\n", path); exit(1); }
    return f;
}
static void save(const char *name, const void *p, uint32_t n) { FILE *f = file(name, "wb"); fwrite(p, 1, n, f); fclose(f); }
static uint32_t load(const char *name, void *p, uint32_t max) { FILE *f = file(name, "rb"); uint32_t n = (uint32_t)fread(p, 1, max, f); fclose(f); return n; }
/* a saved project's record as the loader reads it (its compact form expanded): 0 */
static int ps_stored(uint32_t obj, uint8_t *dst)
{
    st_hdr_t h;
    int copy = st_current(obj, &h);
    if (copy < 0) return -1;
    return pj_image(obj, (uint32_t)copy, dst);
}
/* the music as it is now: its record whole, and as a save stores it */
static uint32_t emit(const char *name)
{
    char path[64];
    st_stream_t w;
    project_capture(&proj_scratch); assert(bank_pack(rec, &proj_scratch, 1));
    assert(proj_image_capture(1)); assert(!proj_stream_generate(&w, OBJ_BANK0));
    uint32_t n = w.head.len;
    assert(n <= sizeof big && !st_read_range(w.obj, w.head.slot, 0, big, n));
    snprintf(path, sizeof path, "%s.rec", name); save(path, rec, sizeof rec);
    snprintf(path, sizeof path, "%s.hpj", name); save(path, big, n);
    return n;
}

/* ------------------------------------------------------------------ projects to size --- */
static uint32_t lcg = 1;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 8) % n; }
static void fill(uint32_t k, uint32_t b, uint32_t density, int drum)   /* a scale walk, chords, ties; a drum grid */
{
    static const uint8_t sc[7] = {0, 2, 3, 5, 7, 8, 10};
    pattern_t p;
    pattern_read(k, b, &p);
    for (uint32_t i = 0; i < NSTEP; i++) {
        step_t s = {{0}, 0, ST_REST};
        if (rnd(100) < density) {
            if (drum) { s.time = ST_NOTE; s.hit = (uint8_t)(1u << rnd(4) | (i % 4u == 0)); s.acc = rnd(4) ? 0 : s.hit; s.vel = 100; }
            else {
                s.time = rnd(8) ? ST_NOTE : ST_TIE; s.n = s.time == ST_NOTE ? 1u + (rnd(5) == 0) * 2u : 0;
                for (uint32_t j = 0; j < s.n; j++) s.note[j] = (uint8_t)(36u + 12u * k + sc[(i + 2u * j + b) % 7u] + 12u * rnd(2));
                s.vel = (uint8_t)(80u + rnd(40)); s.flags = rnd(6) ? 0 : SF_ACCENT;
            }
        }
        p.step[i] = s;
    }
    p.timing[0] = (int16_t)(16 << rnd(3));
    (void)pattern_write(k, b, &p);                  /* (refused: the project is full, as on the device) */
}
static void song_project(uint32_t pats, uint32_t density, uint32_t notes, uint32_t rows, uint32_t sounds, uint32_t motions)
{
    static const uint8_t eng[NPART] = {ENGI_PROPHET, ENGI_FM6, ENGI_CZ, ENGI_DRUM};
    reset(); song.ntrk = NPART; lcg = 7;
    for (uint32_t k = 0; k < NPART; k++) { set_engine_of(&trk[k], eng[k]); apply_preset_to(&trk[k], k); }
    for (uint32_t k = 0; k < NPART; k++) for (uint32_t b = 0; b < pats; b++) fill(k, b, density, k == 3);
    for (uint32_t i = 0; i < notes; i++) {
        recorded_note_t r = {.on = (uint16_t)rnd(65536), .duration = (uint16_t)(2000u + rnd(30000)), .vel = (uint8_t)(60u + rnd(60))};
        recorded_set_note(&r, 40u + rnd(36)); recorded_set_owner(&r, rnd(NPART) * NPAT + rnd(pats)); recorded_set_step(&r, rnd(64), 0, 0);
        recording[i] = r; assert(recording_valid(&r));
    }
    recording_reindex();
    chain_config.count = (uint8_t)rows;
    for (uint32_t r = 0; r < rows; r++) {
        chain_config.row[r] = (chain_row_t){(uint8_t)(r % pats), (uint8_t)(1u + rnd(4))};
        for (uint32_t k = 0; k < NPART; k++) chain_patterns[r][k] = (uint8_t)((r + k) % pats);
        chain_config.row[r].slot = chain_patterns[r][0];
    }
    for (uint32_t i = 0; i < sounds; i++) {
        song_sound_t s;
        char name[16];
        uint32_t k = i % NPART, id;
        snprintf(name, sizeof name, "SOUND %u", (unsigned)(i + 1u));
        apply_preset_to(&trk[k], 3u + i); song_sound_capture(&s, &trk[k], name); assert(song_sound_valid(&s, 0));
        if (!(id = song_sound_store(&s))) break;
        chain_config.sound[i % rows][k] = (uint8_t)id; apply_preset_to(&trk[k], k);
    }
    for (uint32_t i = 0; i < motions; i++) motion_set_event(&trk[i % NPART], rnd(64), P_E1, (int16_t)rnd(60));
}

/* --------------------------------------------------------------------------- the kits --- */
static int16_t take[FS * 2u + 64u];
static uint8_t kit[3][DRK_KIT_MAX];
static uint32_t kit_n[3];
/* a kit of the firmware's own drums (KIT value k's eleven voices), each recorded as Record kit keeps a take */
static void kit_record(uint32_t u, int32_t k, const char *name)
{
    static const uint8_t GM[DRK_NDRUM] = {36, 38, 41, 45, 48, 37, 39, 42, 46, 49, 51};   /* D9_BD .. D9_RD */
    static int32_t out[FS * 2u];
    for (uint32_t d = 0; d < DRK_NDRUM; d++) {
        uint32_t n = (d == D9_CR || d == D9_RD ? FS * 2u : FS) / CTL * CTL;
        int32_t pk = 1;
        host_tracks_init();
        for (uint32_t p = 0; p < NPART; p++) memset(trk[p].v, 0, NVOICE * sizeof *trk[p].v);
        eng_state_reset();
        host_preset(&trk[0], ENGI_DRUM, 0);
        trk[0].p[P_E0] = (int16_t)k;
        trk_note_on(&trk[0], GM[d], 115);
        for (uint32_t i = 0; i < n; i += CTL) track_render(&trk[0], out + i, CTL);
        for (uint32_t i = 0; i < n; i++) pk = abs(out[i]) > pk ? abs(out[i]) : pk;
        if (pk < 100) continue;                      /* (a voice this kit does not have: an empty pad) */
        for (uint32_t i = 0; i < n; i++) take[i] = (int16_t)((int64_t)out[i] * 20000 / pk);
        char tname[64];
        snprintf(tname, sizeof tname, "take-%s-%u.s16", name, (unsigned)d); save(tname, take, n * 2u);
        uint32_t r = kt_keep(u, d, take, n, n);
        assert(r == KM_KEPT);
    }
    st_hdr_t h;
    int copy = st_current(rec_obj(u), &h);
    assert(copy >= 0 && h.len <= DRK_KIT_MAX && !st_read_range(rec_obj(u), (uint32_t)copy, 0, kit[u], h.len));
    kit_n[u] = h.len;
    char path[64];
    snprintf(path, sizeof path, "kit-%s.bin", name); save(path, kit[u], kit_n[u]);
    printf("mel: kit %-5s %u bytes\n", name, (unsigned)kit_n[u]);
}

/* ------------------------------------------------- a project naming kits everywhere --- */
enum { T_KEYS, T_DRUM2, T_OTHER, T_DRUM };           /* tracks: a synth, drums, a synth, drums */
#define KIT_A 10                                     /* KIT 2: on the drum track, a pattern's sound */
#define KIT_B 13                                     /* KIT 5: the second drum track, a song's sound, automation */
#define KIT_E 16                                     /* KIT 8: named by automation only, and empty */
static uint32_t snap(uint32_t k, const char *name)   /* track k's sound as it is: a sound of the song's */
{
    song_sound_t s;
    song_sound_capture(&s, &trk[k], name); assert(song_sound_valid(&s, 0));
    uint32_t id = song_sound_store(&s); assert(id);
    return id;
}
static void motion_at(uint32_t k, uint32_t pattern, uint32_t step, int16_t value)   /* E0 moved at a step of a pattern */
{
    uint32_t i = motion.count++;
    assert(i < MOTION_MAX);
    motion.event[i] = (motion_event_t){(uint8_t)(k << 6 | step), P_E0, value};
    motion_pattern[i] = (uint8_t)pattern;
    if (pattern) motion.rsv[0] = 1;
    motion.on |= (uint8_t)(1u << k);
}
static uint32_t snd_drum_a, snd_drum_b, snd_keys;
static void kit_project(void)
{
    reset(); song.ntrk = NPART + 1u; lcg = 3;
    set_engine_of(&trk[T_KEYS], ENGI_PROPHET); apply_preset_to(&trk[T_KEYS], 1);
    set_engine_of(&trk[T_DRUM2], ENGI_DRUM); apply_preset_to(&trk[T_DRUM2], 0);
    set_engine_of(&trk[T_OTHER], ENGI_CZ); apply_preset_to(&trk[T_OTHER], 2);
    set_engine_of(&trk[T_DRUM], ENGI_DRUM); apply_preset_to(&trk[T_DRUM], 0);
    trk[T_KEYS].p[P_E0] = KIT_A;                     /* a synth's own E0: not a kit */
    trk[NPART].p[P_E0] = KIT_B;                      /* a MIDI-only track's channel: not a kit */
    for (uint32_t k = 0; k < NPART; k++) for (uint32_t b = 0; b < 5u; b++) fill(k, b, 30, k == T_DRUM || k == T_DRUM2);
    chain_config.count = 4;
    for (uint32_t r = 0; r < 4u; r++) {
        chain_config.row[r] = (chain_row_t){(uint8_t)r, 2};
        for (uint32_t k = 0; k < NTRK; k++) chain_patterns[r][k] = (uint8_t)(k < NPART ? r : 0u);
    }
    /* (a sound is kept once something plays it: each goes to its place before the next is made) */
    trk[T_DRUM].p[P_E0] = KIT_A; snd_drum_a = snap(T_DRUM, "DRUMS A");
    pattern_sound[T_OTHER][2] = (uint8_t)snd_drum_a;          /* a pattern's own sound: drums on a synth's track */
    trk[T_DRUM].p[P_E0] = KIT_B; snd_drum_b = snap(T_DRUM, "DRUMS B");
    chain_config.sound[1][T_DRUM] = (uint8_t)snd_drum_b;      /* a section's own sound */
    snd_keys = snap(T_KEYS, "KEYS");
    chain_config.sound[2][T_KEYS] = (uint8_t)snd_keys;
    pattern_sound[T_DRUM][4] = (uint8_t)snd_keys;             /* a pattern's own: a synth on the drums' track */
    trk[T_DRUM].p[P_E0] = KIT_A;                     /* the drum track: KIT 2 */
    trk[T_DRUM2].p[P_E0] = KIT_B;                    /* the other: KIT 5 */
    motion_at(T_DRUM, 0, 5, KIT_B);                  /* a kit change on the drum track */
    motion_at(T_DRUM2, 1, 9, KIT_E);                 /* .. to a kit that is empty */
    motion_at(T_KEYS, 0, 3, KIT_A);                  /* a synth's E0 moved: not a kit */
    motion_at(T_OTHER, 2, 7, KIT_B);                 /* the pattern that plays drums on the synth's track: a kit */
    motion_at(T_DRUM, 4, 11, KIT_A);                 /* the pattern that plays a synth on the drums' track: not one */
    motion_at(T_DRUM, 0, 6, 5);                      /* a kit that is not the user's (909) */
    assert(motion_valid(&motion));
    str_cpy(proj_name, "KIT SONG", sizeof proj_name);
}
/* the same project with its kits renamed (from[i] -> to[i]), on the decoded state: tracks, sounds, automation */
static int16_t kit_to(int32_t v, const uint8_t *from, const uint8_t *to, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) if (v == from[i]) return to[i];
    return (int16_t)v;
}
static void kit_rename(const uint8_t *from, const uint8_t *to, uint32_t n)
{
    for (uint32_t k = 0; k < NPART; k++)
        if (trk[k].eng_req == ENGI_DRUM) trk[k].p[P_E0] = kit_to(trk[k].p[P_E0], from, to, n);
    for (uint32_t j = 0; j < song_sound_count; j++) {
        song_sound_t *s = song_sound_pool() + j;
        if (s->engine == ENGI_DRUM) s->p[P_E0] = (uint8_t)(kit_to(s->p[P_E0] - 64, from, to, n) + 64);
    }
    for (uint32_t i = 0; i < motion.count; i++) {
        uint32_t k = motion.event[i].place >> 6, id = pattern_sound[k][motion_pattern[i]];
        int drums = id ? song_sound_get(id)->engine == ENGI_DRUM : trk[k].eng_req == ENGI_DRUM;
        if (motion.event[i].param == P_E0 && drums && motion.event[i].value >= DK_USER0)
            motion.event[i].value = kit_to(motion.event[i].value, from, to, n);
    }
}
/* the kit project as the device plays it with its kits at a, b, e */
static int kit_project_is(int32_t a, int32_t b, int32_t e)
{
    static const struct { uint8_t k, pattern, step; int8_t kit; } M[6] = {   /* kit: 1 KIT_B's, 2 KIT_E's, 0 as it was */
        {T_DRUM, 0, 5, 1}, {T_DRUM2, 1, 9, 2}, {T_KEYS, 0, 3, 0}, {T_OTHER, 2, 7, 1}, {T_DRUM, 4, 11, 0}, {T_DRUM, 0, 6, 0}};
    static const int8_t was[6] = {KIT_B, KIT_E, KIT_A, KIT_B, KIT_A, 5};
    uint32_t da = pattern_sound[T_OTHER][2], db = chain_config.sound[1][T_DRUM], ky = chain_config.sound[2][T_KEYS], bad = 0;
#define IS(c) do { if (!(c)) { if (getenv("MEL_WHY")) printf("mel:   not: %s\n", #c); bad++; } } while (0)
    IS(trk_n() == NPART + 1u);
    IS(trk[T_KEYS].eng_req == ENGI_PROPHET && trk[T_KEYS].p[P_E0] == KIT_A);
    IS(trk[NPART].p[P_E0] == KIT_B);
    IS(trk[T_DRUM].eng_req == ENGI_DRUM && trk[T_DRUM].p[P_E0] == a);
    IS(trk[T_DRUM2].eng_req == ENGI_DRUM && trk[T_DRUM2].p[P_E0] == b);
    IS(da && db && ky && pattern_sound[T_DRUM][4] == ky && song_sound_count == 3u);
    if (bad) return 0;
    IS(song_sound_get(da)->engine == ENGI_DRUM && song_sound_get(da)->p[P_E0] == a + 64);
    IS(song_sound_get(db)->engine == ENGI_DRUM && song_sound_get(db)->p[P_E0] == b + 64);
    IS(song_sound_get(ky)->engine == ENGI_PROPHET && song_sound_get(ky)->p[P_E0] == KIT_A + 64);
    IS(motion.count == 6u);
    for (uint32_t i = 0; i < 6u; i++) {
        uint32_t j = 0;
        while (j < motion.count && !(motion.event[j].place == (M[i].k << 6 | M[i].step) && motion_pattern[j] == M[i].pattern)) j++;
        IS(j < motion.count && motion.event[j].param == P_E0 && motion.event[j].value == (M[i].kit == 1 ? b : M[i].kit == 2 ? e : was[i]));
    }
#undef IS
    return !bad;
}

static int do_emit(void)
{
    static const char *fx[3] = {"tests/fixtures/fun19-send-chain.bin", "tests/fixtures/fun20-split.bin", "tests/fixtures/fun21-cuts.bin"};
    static const struct { const char *name; uint32_t pats, density, notes, rows, sounds, motions; } S[3] = {
        {"sketch", 2, 35, 0, 1, 0, 0}, {"song", 6, 45, 200, 12, 2, 10}, {"big", 8, 40, 1024, 48, 4, 64}};
    FILE *f = file("layout.json", "w");
    fprintf(f, "{\"REC_BYTES\":%u,\"KIT_MAX\":%u,\"KIT_FIRST\":%u,\"KIT_COUNT\":%u,\"KIT_HEAD\":%u,\"ENGI_DRUM\":%u,\n",
            (unsigned)BANK_STORE_SIZE, (unsigned)DRK_KIT_MAX, (unsigned)DK_USER0, (unsigned)DK_NUSER, (unsigned)DRK_KIT_BYTES, (unsigned)ENGI_DRUM);
    fprintf(f, " \"FUN_BYTES\":%u,\"TAIL_AT\":%u,\"FUN_MAGIC\":%u,\"FBKP\":%u,\"FBKQ\":%u,\"FBKR\":%u,\"KIT_MAGIC\":%u,\"PJ_MAGIC\":%u,\"PZ_MAGIC\":%u,\"PROJECTS\":%u,\n",
            (unsigned)PROJ_STORE_SIZE, (unsigned)BANK_ACTIVE_OFF, (unsigned)PROJ_MAGIC, (unsigned)BANKP_MAGIC, (unsigned)BANKQ_MAGIC,
            (unsigned)BANKR_MAGIC, (unsigned)DRK_KIT_MAGIC, (unsigned)PJ_MAGIC, (unsigned)PZ_MAGIC, (unsigned)PROJ_SLOTS);
    fprintf(f, " \"KIT_OWNER_UID\":\"%016llx\",\n", (unsigned long long)rec_owner_uid(MOD3_NREC));
    fprintf(f, " \"layout\":{\"np\":%u,\"e0\":%u,\"track\":%u,\"tracks\":%u,\"motion\":%u,\"funSum\":%u,\"name\":%u,\"tags\":%u,\"song\":%u,"
            "\"sounds\":%u,\"soundHead\":%u,\"soundBytes\":%u}}\n",
            (unsigned)P_COUNT, (unsigned)P_E0, (unsigned)(P_COUNT + 2u + NSTEP * 8u), 8u + 68u,
            (unsigned)(8u + 68u + PROJ_TRK * (P_COUNT + 2u + NSTEP * 8u) + sizeof proj_scratch.chain), (unsigned)(8u + PROJ_STORE_SIZE - 4u),
            (unsigned)(8u + PROJ_NAME_OFF), (unsigned)BANKP_MOTION_OFF, (unsigned)BANKP_STEPS_OFF,
            (unsigned)(BANKP_STEPS_OFF + BANKM_SONG_BYTES + BANKN_FX_BYTES), (unsigned)BANKO_HEAD_BYTES, (unsigned)sizeof(song_sound_t));
    fclose(f);
    assert(__builtin_offsetof(motion_store_t, event) == 4u && sizeof(motion_event_t) == 4u && __builtin_offsetof(motion_event_t, value) == 2u &&
           __builtin_offsetof(song_sound_t, engine) == P_COUNT && DRK_KIT_BYTES == sizeof(drk_kit_t) && ED_BK_KIT0 == 92u);

    reset(); project_new_blank();
    printf("mel: %-7s stored in %u bytes\n", "blank", (unsigned)emit("blank"));
    assert(*(uint32_t *)(void *)big == PZ_MAGIC);
    for (uint32_t i = 0; i < 3u; i++) {
        static uint8_t b[PROJ_STORE_SIZE];
        char name[16];
        FILE *in = fopen(fx[i], "rb"); assert(in);
        uint32_t n = (uint32_t)fread(b, 1, sizeof b, in); fclose(in);
        reset(); assert(proj_import(&proj_scratch, b, (int)n)); assert(!project_restore_runtime(&proj_scratch));
        snprintf(name, sizeof name, "fun%u", (unsigned)(19u + i));
        printf("mel: %-7s stored in %u bytes\n", name, (unsigned)emit(name));
    }
    for (uint32_t i = 0; i < 3u; i++) {
        song_project(S[i].pats, S[i].density, S[i].notes, S[i].rows, S[i].sounds, S[i].motions);
        printf("mel: %-7s stored in %u bytes\n", S[i].name, (unsigned)emit(S[i].name));
    }

    reset();
    kit_record(0, DK_909, "909"); kit_record(1, DK_808, "808"); kit_record(2, DK_CR78, "cr78");

    kit_project();
    assert(kit_project_is(KIT_A, KIT_B, KIT_E));
    uint32_t n = emit("kits");
    assert(*(uint32_t *)(void *)rec == BANKQ_MAGIC);
    printf("mel: %-7s stored in %u bytes: kits %d and %d, %d named and empty\n", "kits", (unsigned)n, KIT_A, KIT_B, KIT_E);
    {   /* its kits where test_mel.mjs's device has room for them: KIT 5 is there as KIT 1, KIT 2 goes to 4, the empty one to 7 */
        static const uint8_t from[3] = {KIT_A, KIT_B, KIT_E}, to[3] = {12, 9, 15};
        kit_rename(from, to, 3);
        assert(kit_project_is(12, 9, 15));
        emit("kits-moved");
    }
    {   /* the pattern-less form (FBKP): no pattern's own sound */
        kit_project();
        memset(pattern_sound, 0, sizeof pattern_sound);
        chain_config.sound[3][T_OTHER] = (uint8_t)snd_drum_a;      /* (every sound still in use) */
        emit("kits-p");
        assert(*(uint32_t *)(void *)rec == BANKP_MAGIC);
        static const uint8_t from[3] = {KIT_A, KIT_B, KIT_E}, to[3] = {12, 9, 15};
        kit_rename(from, to, 3);
        emit("kits-p-moved");
    }
    puts("mel: fixtures written");
    return 0;
}

/* PUT an object as the web page does: its rc */
static uint32_t put_object(uint32_t id, const uint8_t *p, uint32_t n) { return put_all(id, p, n, n ? st_crc32(p, n) : 0u); }
static int do_verify(void)
{
    int bad = 0;
    uint32_t n;
    reset(); rec_scan();
    /* the device test_mel.mjs imported into: KIT 1 held the 808 kit already; the page sent KIT 4 and the project */
    n = load("kit-808.bin", big, sizeof big);
    bad += check("mel: a kit record goes in through the backup's PUT", put_object(92, big, n) == 0 && rec_len[0] == n);
    bad += check("mel: .. its owner the default (DRUM's): the engine map as it was, nothing to save", !rec_uid[0] && !eng_map_dirty);
    n = load("out-kit-12.bin", big, sizeof big);
    uint32_t n909 = load("kit-909.bin", kit[0], sizeof kit[0]);
    bad += check("mel: the kit the page sent is the file's, byte for byte, and plays", n == n909 && !memcmp(big, kit[0], n) &&
                 put_object(92 + 3, big, n) == 0 && rec_len[3] == n && drk_user[3].n);
    n = load("out-project.bin", big, sizeof big);
    /* (project 3: the host build mirrors the first four slots only; the slot is nothing to the record) */
    bad += check("mel: the project the page sent, packed by the page, is accepted into a free slot",
                 n > PJ_HEAD && *(uint32_t *)(void *)big == PZ_MAGIC && n <= ST_LIN_PAYLOAD_MAX && put_object(4, big, n) == 0);
    load("kits-moved.rec", rec2, sizeof rec2);
    bad += check("mel: .. stored as the firmware's own record of that project", !ps_stored(st_proj_obj(2), rec) && !memcmp(rec, rec2, sizeof rec));
    project_new_blank();
    song.g[G_SLOT] = 3; project_load(2);
    bad += check("mel: .. and loads: every kit it names is the new one, the look-alikes as they were", kit_project_is(12, 9, 15));
    bad += check("mel: .. under its name", !strcmp(proj_meta[2].name, "KIT SONG"));
    /* the same bytes with one changed: refused, the slot as it was */
    big[n / 2u] ^= 1u;
    bad += check("mel: a damaged project is refused by the device", put_object(5, big, n) != 0 && st_current(st_proj_obj(3), 0) < 0);
    /* what the page sends firmware of before the packed form: every firmware reads it */
    n = load("out-project-compact.bin", big, sizeof big);
    bad += check("mel: the same project in the compact form is accepted too", *(uint32_t *)(void *)big == PJ_MAGIC && put_object(5, big, n) == 0 &&
                 !ps_stored(st_proj_obj(3), rec) && !memcmp(rec, rec2, sizeof rec));
    project_new_blank();
    song.g[G_SLOT] = 4; project_load(3);
    bad += check("mel: .. and loads as the same music", kit_project_is(12, 9, 15));
    n = load("kit-909.bin", big, sizeof big);
    big[4] ^= 8u;
    bad += check("mel: a damaged kit is refused by the device", put_object(92 + 5, big, n) != 0 && !rec_len[5]);
    puts(bad ? "mel: FAILED" : "mel: the web page's import as the device takes it: passed");
    return bad != 0;
}
int main(int argc, char **argv)
{
    if (argc != 3 || (strcmp(argv[1], "emit") && strcmp(argv[1], "verify"))) { puts("mel_fixture emit|verify DIR"); return 2; }
    dir = argv[2];
    mkdir(dir, 0755);
    return strcmp(argv[1], "emit") ? do_verify() : do_emit();
}
