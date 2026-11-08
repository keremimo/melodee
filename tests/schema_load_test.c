/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The synths' patches as generic bytes (patch.c, schema.c) against what Melodee 1.x stored: tests/fixtures/
 * stores-a894c6f7.bin was written by a894c6f7's own code (its persistence harness) for a Prophet, FM6, CZ-1 and DRUM
 * track, each edited (the Prophet's cutoff, an opaque byte and its name; FM6's algorithm, an operator, its functions
 * and switches; the CZ-1's vibrato and a rate; DRUM's offsets): the flash after saving project A, the template and
 * user presets 21..24, the four tracks as song sounds, and the patches each track had before saving and after each
 * load (project A, the template, the user presets, the song sounds; each after a scramble: another preset of the
 * same synth). Here the same setup gives the same patches, the same saves the same flash bytes, and a894c6f7's flash
 * loads to the same patches, byte for byte. */
#define PERSISTENCE_TEST_NO_MAIN 1
#include "persistence_test.c"

#define LF_STAGES 6u
#define LF_BLOB 160u
static uint8_t lf_nor[sizeof nor];
static uint8_t lf_rec[4][sizeof(song_sound_t)];
static uint8_t lf_want[LF_STAGES][4][3u + LF_BLOB], lf_got[LF_STAGES][4][3u + LF_BLOB];
static const uint32_t ENG[4] = {ENGI_PROPHET, ENGI_FM6, ENGI_CZ, ENGI_DRUM};
static const char *const STAGE[LF_STAGES] = {"the edited tracks", "scrambled", "project A loaded", "the template loaded",
                                             "the user presets loaded", "the song sounds put"};

static int lf_read(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint32_t n, off, rs, ns;
    char magic[4];
    if (!f)
        return 0;
    memset(lf_nor, 0xFF, sizeof lf_nor);
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "LDF1", 4) || fread(&n, 4, 1, f) != 1)
        return 0;
    while (n--)
        if (fread(&off, 4, 1, f) != 1 || off + 4096u > sizeof lf_nor || fread(lf_nor + off, 1, 4096, f) != 4096)
            return 0;
    if (fread(&rs, 4, 1, f) != 1 || rs != sizeof(song_sound_t) || fread(lf_rec, sizeof lf_rec, 1, f) != 1)
        return 0;
    if (fread(&ns, 4, 1, f) != 1 || ns != LF_STAGES || fread(lf_want, sizeof lf_want, 1, f) != 1)
        return 0;
    fclose(f);
    return 1;
}
/* track k as the fixture holds it: its engine, its patch's length, its bytes */
static void stage(uint32_t s)
{
    for (uint32_t k = 0; k < 4u; k++) {
        uint8_t *o = lf_got[s][k];
        uint32_t e = trk[k].eng_req, n = patch_eng_of(k) == e ? patch_len(e) : 0u;
        memset(o, 0, sizeof lf_got[s][k]);
        o[0] = (uint8_t)e;
        o[1] = (uint8_t)n;
        o[2] = (uint8_t)(n >> 8);
        memcpy(o + 3, patch_of(k), n < LF_BLOB ? n : LF_BLOB);
    }
}
static int stage_same(uint32_t s)
{
    int ok = 1;
    for (uint32_t k = 0; k < 4u; k++)
        if (memcmp(lf_got[s][k], lf_want[s][k], sizeof lf_got[s][k])) {
            uint32_t i;
            for (i = 0; i < sizeof lf_got[s][k] && lf_got[s][k][i] == lf_want[s][k][i]; i++)
                ;
            printf("schema_load: %s, track %u (engine %u): byte %d is %u, a894c6f7's %u\n", STAGE[s], k + 1u, lf_want[s][k][0],
                   (int)i - 3, lf_got[s][k][i], lf_want[s][k][i]);
            ok = 0;
        }
    return ok;
}
static void scramble(void)                       /* (each track another preset of its synth) */
{
    for (uint32_t k = 0; k < 4u; k++)
        apply_preset_to(&trk[k], k == 3u ? 2u : 9u);
}
int main(int argc, char **argv)
{
    int bad = 0;
    uint8_t raw[CZ_BYTES], v[FP_SIZE + 1u];
    if (!lf_read(argc > 1 ? argv[1] : "tests/fixtures/stores-a894c6f7.bin")) {
        printf("schema_load: tests/fixtures/stores-a894c6f7.bin unreadable\n");
        return 1;
    }
    reset();
    song.playing = 0;
    for (uint32_t k = 0; k < 4u; k++) {
        set_engine_of(&trk[k], ENG[k]);
        apply_preset_to(&trk[k], k == 3u ? 1u : 5u);
    }
    /* the edits a894c6f7 made, written as it wrote them (the bytes, no engine told) */
    { uint8_t *p = patch_of(0); p[P5_CUTOFF] = 77; p[97] = 0x5A; memcpy(p + P5_NAME, "LOAD TEST PROPHET   ", 20); }
    fm6_voice_of(1, v);
    v[FP_ALG] = 21; v[2 * FP_OP + FP_OL] = 61; v[5 * FP_OP + FP_R1] = 13; memcpy(v + FP_NAME, "LOAD FM6  ", 10);
    fm6_pack(v, patch_of(1));
    fm6_fn_of(1)[FN_PTIME] = 9; fm6_fn_of(1)[FN_ENGINE] = FM6_OPL; patch_of(1)[FM6_B_ON] = 0x2D;
    if (cz_ed_put(patch_of(2), LCZ_VDEP, 20, raw)) memcpy(patch_of(2), raw, CZ_BYTES);
    if (cz_ed_put(patch_of(2), LCZ_EBASE(0, 0) + 9u, 40, raw)) memcpy(patch_of(2), raw, CZ_BYTES);
    memcpy(patch_of(2) + 128, "LOAD TEST CZ    ", 16);
    { drum_patch_t *d = drum_patch_of(3); d->c[0][0] = 5; d->c[3][3] = 40; d->c[9][1] = -20; d->c[12][2] = 33; }
    stage(0);
    bad += check("schema load: the same edits give a894c6f7's four patches", stage_same(0));
    {
        song_sound_t s;
        int same = 1;
        for (uint32_t k = 0; k < 4u; k++) {
            song_sound_t d;
            song_sound_capture(&s, &trk[k], "LOADSOUND");
            song_sound_stored(&d, &s);
            same &= !memcmp(&d, lf_rec[k], sizeof d);
        }
        bad += check("schema load: song sounds store as a894c6f7's records", same);
    }
    if (project_save(0))
        bad += check("schema load: project A saves", 0);
    template_save();
    for (uint32_t k = 0; k < 4u; k++) {
        song.sel = (uint8_t)k;
        if (up_store(20 + k, "LOAD USER"))
            bad += check("schema load: the user presets save", 0);
    }
    song.sel = 0;
    {
        uint32_t off, diff = 0;
        for (off = 0; off < sizeof nor; off += 4096u)
            if (memcmp(nor + off, lf_nor + off, 4096)) {
                printf("schema_load: flash sector at 0x%05x differs from a894c6f7's\n", off);
                diff++;
            }
        bad += check("schema load: project, template and user presets write a894c6f7's flash bytes", !diff);
    }
    /* a894c6f7's flash (no cache of this run's saves), then its loads */
    memcpy(nor, lf_nor, sizeof nor);
    up_cache_reset();
    native_cache_reset();
    scramble(); stage(1); project_load(0); stage(2);
    scramble(); template_load(); stage(3);
    scramble();
    for (uint32_t k = 0; k < 4u; k++) {
        song.sel = (uint8_t)k;
        up_load(20 + k);
    }
    song.sel = 0;
    stage(4);
    scramble();
    for (uint32_t k = 0; k < 4u; k++) {
        song_sound_t s;
        memcpy(&s, lf_rec[k], sizeof s);
        song_sound_ram(&s);
        song_sound_apply(k, &s);
    }
    stage(5);
    bad += check("schema load: scrambled as a894c6f7 (another preset each)", stage_same(1));
    bad += check("schema load: a894c6f7's project A loads its four patches byte for byte", stage_same(2));
    bad += check("schema load: a894c6f7's template loads them byte for byte", stage_same(3));
    bad += check("schema load: a894c6f7's user presets load them byte for byte", stage_same(4));
    bad += check("schema load: a894c6f7's song sounds put them byte for byte", stage_same(5));
    printf("schema load: %s\n", bad ? "FAILED" : "Prophet, FM6, CZ-1 and DRUM as Melodee 1.x stored them");
    return bad != 0;
}
