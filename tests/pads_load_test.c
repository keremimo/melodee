/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Kits as the firmware stored them before the pads capability: tests/fixtures/pads-70111e91.bin was written by
 * 70111e91's own code (this file built against it: pads_load_test emit) - a user kit recorded through Record kit's path
 * (three takes of the firmware's 808 voices), a project whose two DRUM tracks play it and the 909 with its sounds' offsets
 * edited, grid hits and accents on every lane, notes on lanes and recorded hits with their own pitch and length, and a
 * user preset of the 909 track (its KIT patch and its grid): the flash, then what 70111e91 read back from it (each track
 * after the project's load and the user preset's, the kit's samples played). Here the same flash boots, loads and plays
 * the same, byte for byte and sample for sample, and saving them again writes what 70111e91 writes.
 *   pads_load_test emit FILE   (70111e91: write the fixture)
 *   pads_load_test FILE        (verify) */
static unsigned char nor[0x100000];
#define BACKUP_TEST_NO_MAIN 1
#define BACKUP_TEST_KITS 1
#define REC_BASE ((const uint8_t *)nor)        /* (the pads branch: the data records play from the pool) */
#define DRK_XIP(off) ((const uint8_t *)nor + (off))   /* (70111e91: the user kits') */
#include "backup_test.c"
#include <assert.h>
#ifdef REC_STORE                                 /* (the pads branch: DRUM's take and kit are its module's, built in) */
#define T_LIST(u) rec_tab[u]
#define T_SCAN() rec_scan()
#define T_OBJ(u) rec_obj(u)
#else                                            /* (70111e91) */
#include "../firmware/src/kit_take.c"
#define T_LIST(u) drk_user[u]
#define T_SCAN() kit_scan()
#define T_OBJ(u) kit_obj(u)
#endif

#define PL_SLOT 1u                                /* the project's slot, the user preset's */
#define PL_UP 5u
#define PL_STATE 8192u
static uint8_t pl_want[3][PL_STATE], pl_got[3][PL_STATE];
static uint32_t pl_wn[3], pl_gn[3];
static uint8_t pl_nor[sizeof nor];
static uint8_t pl_rec[2][32768];                  /* the project's record, the preset bank's, as 70111e91 saved them */
static uint32_t pl_rlen[2];

static void put(uint8_t *o, uint32_t *n, const void *p, uint32_t len)
{
    assert(*n + len <= PL_STATE);
    memcpy(o + *n, p, len);
    *n += len;
}
/* tracks 1 and 3 as they are: engine, values, steps, their recorded notes, their patch's bytes */
static void dump(uint32_t s)
{
    uint32_t *n = &pl_gn[s];
    *n = 0;
    for (uint32_t k = 1; k < 4u; k += 2u) {
        const track_t *t = &trk[k];
        uint8_t e = t->eng_req;
        put(pl_got[s], n, &e, 1);
        put(pl_got[s], n, t->p, sizeof t->p);
        put(pl_got[s], n, t->step, sizeof t->step);
        put(pl_got[s], n, patch_of(k), 64);
        for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i])
            if (recording_active(t, i))
                put(pl_got[s], n, &recording[i], sizeof recording[i]);
    }
}
/* the kit on track 1 (KIT 1: its record), each pad struck: the samples' hash */
static void play(uint32_t s)
{
    static const uint8_t NOTES[] = {36, 38, 42, 41, 46};
    static int32_t out[CTL];
    uint32_t h = 2166136261u, *n = &pl_gn[s];
    *n = 0;
    for (uint32_t p = 0; p < NPART; p++) memset(trk[p].v, 0, NVOICE * sizeof *trk[p].v);
    eng_state_reset();
    trk[1].engine = trk[1].eng_req;
    for (uint32_t j = 0; j < sizeof NOTES; j++) {
        trk_note_on(&trk[1], NOTES[j], 100u + j * 5u);
        for (uint32_t b = 0; b < 160u; b++) {
            memset(out, 0, sizeof out);
            track_render(&trk[1], out, CTL);
            for (uint32_t i = 0; i < CTL; i++)
                h = (h ^ (uint32_t)out[i]) * 16777619u;
        }
    }
    uint8_t l = T_LIST(0).n;
    put(pl_got[s], n, &l, 1);
    put(pl_got[s], n, T_LIST(0).sec, l);
    put(pl_got[s], n, &h, sizeof h);
}
static uint32_t record_of(uint32_t obj, uint8_t *out)   /* a record's current payload: its bytes */
{
    st_hdr_t h;
    int copy = st_current(obj, &h);
    if (copy < 0 || h.len > sizeof pl_rec[0] || st_read_range(obj, (uint32_t)copy, 0, out, h.len))
        return 0;
    return h.len;
}
static void boot(void)                           /* the RAM as a power-on finds it, the flash kept */
{
    uint8_t keep[sizeof nor];
    memcpy(keep, nor, sizeof nor);
    reset();
    memcpy(nor, keep, sizeof nor);
    host_tracks_init();
    T_SCAN();
}
static void load_all(void)
{
    boot();
    song.g[G_SLOT] = (int16_t)(PL_SLOT + 1u);
    project_load(PL_SLOT);
    dump(0);
    song.sel = 3;
    assert(!up_load(PL_UP));
    dump(1);
    play(2);
}

/* the fixture's flash loaded, the project saved again, the user preset stored again from the loaded track: their
 * records */
static void resave(uint8_t rec[2][32768], uint32_t len[2])
{
    memcpy(nor, pl_nor, sizeof nor);
    boot();
    song.g[G_SLOT] = (int16_t)(PL_SLOT + 1u);
    project_load(PL_SLOT);
    assert(!project_save_as(PL_SLOT, "PADS LANES"));
    len[0] = record_of(st_proj_obj(PL_SLOT), rec[0]);
    song.sel = 3;
    assert(!up_load(PL_UP));
    assert(!up_store(PL_UP, "909 GRID"));
    len[1] = record_of(up_obj(PL_UP / UP_PER_BANK), rec[1]);
}
/* 70111e91's side: the kit, the project, the user preset, saved; then read back */
static int16_t take[FS + 64u];
static void make(void)
{
    static int32_t out[FS];
    static const uint8_t PAD[3] = {0, 1, 7}, GM[3] = {36, 38, 42};   /* BD SD CH */
    reset();
    host_tracks_init();
    T_SCAN();
    for (uint32_t d = 0; d < 3u; d++) {          /* (the 808's own voices, recorded as Record kit keeps a take) */
        uint32_t n = FS / 2u / CTL * CTL;
        int32_t pk = 1;
        for (uint32_t p = 0; p < NPART; p++) memset(trk[p].v, 0, NVOICE * sizeof *trk[p].v);
        eng_state_reset();
        host_preset(&trk[0], ENGI_DRUM, 0);
        trk_note_on(&trk[0], GM[d], 115);
        for (uint32_t i = 0; i < n; i += CTL) track_render(&trk[0], out + i, CTL);
        for (uint32_t i = 0; i < n; i++) pk = abs(out[i]) > pk ? abs(out[i]) : pk;
        for (uint32_t i = 0; i < n; i++) take[i] = (int16_t)((int64_t)out[i] * 20000 / pk);
        memset(take + n, 0, 64 * sizeof take[0]);
        assert(kt_keep(0, PAD[d], take, n, n) == KM_KEPT);
    }
    host_tracks_init();
    host_preset(&trk[1], ENGI_DRUM, 0);
    trk[1].p[P_E0] = 9;                           /* KIT 1: the record */
    host_preset(&trk[3], ENGI_DRUM, 1);           /* the 909 */
    {   /* its sounds' offsets edited (TUNE, DECAY, the third, the cut) */
        int8_t *c = (int8_t *)(void *)patch_of(3);
        for (uint32_t i = 0; i < 64u; i++) c[i] = (int8_t)((i % 4u == 0u ? (int32_t)(i % 49u) - 24 : i % 4u == 3u ? (int32_t)(i * 7u % 128u) :
                                                            (int32_t)(i * 13u % 128u) - 64));
    }
    for (uint32_t k = 1; k < 4u; k += 2u)
        for (uint32_t i = 0; i < 32u; i++) {      /* every lane hit, accents, notes on lanes */
            step_t *s = &trk[k].step[i];
            memset(s, 0, sizeof *s);
            s->time = ST_NOTE; s->vel = (uint8_t)(60u + i);
            s->hit = (uint8_t)(1u << (i % 8u) | (i % 3u == 0u ? 1u : 0u));
            s->acc = (uint8_t)(i % 5u == 0u ? s->hit : 0u);
            if (i % 7u == 2u) { s->n = 2; s->note[0] = 41; s->note[1] = 49; s->flags = SF_ACCENT; }
            if (i % 11u == 4u) s->probability = 50;
        }
    trk[1].p[P_SLEN] = trk[3].p[P_SLEN] = 32;
    {   /* recorded hits with their own pitch and length (DRUM HIT) on track 3 */
        song.sel = 3;
        for (uint32_t i = 0; i < 6u; i++) {
            recorded_note_t r = {0};
            recorded_set_note(&r, (uint8_t)(i % 2u ? 38 : 45));
            r.vel = (uint8_t)(90u + i);
            recorded_set_owner(&r, recording_owner(&trk[3]));
            recorded_set_step(&r, 2u + 5u * i, 0, 0);
            r.pitch = (int8_t)((int32_t)i * 3 - 7);
            r.duration = (uint16_t)(8000u + 4000u * i);
            recorded_set_exponent(&r, 1);
            recorded_set_gate(&r, i % 3u != 0u);
            recording_restore_note(&trk[3], i, r);
            trk[3].step[2u + 5u * i].flags |= SF_RECORDED;
        }
    }
    assert(!project_save_as(PL_SLOT, "PADS LANES"));
    song.sel = 3;
    assert(!up_store(PL_UP, "909 GRID"));
    memcpy(pl_nor, nor, sizeof nor);
    load_all();
    resave(pl_rec, pl_rlen);                     /* (what saving again writes: the records the fixture keeps) */
    assert(pl_rlen[0] && pl_rlen[1]);
}

static int write_fixture(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t n = 0, k;
    if (!f) return 1;
    for (k = 0; k < sizeof nor; k += 4096u)
        for (uint32_t i = 0; i < 4096u; i++)
            if (pl_nor[k + i] != 0xFFu) { n++; break; }
    fwrite("PLF1", 1, 4, f);
    fwrite(&n, 4, 1, f);
    for (k = 0; k < sizeof nor; k += 4096u)
        for (uint32_t i = 0; i < 4096u; i++)
            if (pl_nor[k + i] != 0xFFu) { fwrite(&k, 4, 1, f); fwrite(pl_nor + k, 1, 4096, f); break; }
    for (k = 0; k < 2u; k++) { fwrite(&pl_rlen[k], 4, 1, f); fwrite(pl_rec[k], 1, pl_rlen[k], f); }
    for (k = 0; k < 3u; k++) { fwrite(&pl_gn[k], 4, 1, f); fwrite(pl_got[k], 1, pl_gn[k], f); }
    fclose(f);
    printf("pads: fixture %s: %u sectors, records %u + %u bytes, states %u + %u + %u bytes\n", path, n, pl_rlen[0],
           pl_rlen[1], pl_gn[0], pl_gn[1], pl_gn[2]);
    return 0;
}
static int read_fixture(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint32_t n, off, k;
    char m[4];
    if (!f) return 0;
    memset(pl_nor, 0xFF, sizeof pl_nor);
    if (fread(m, 1, 4, f) != 4 || memcmp(m, "PLF1", 4) || fread(&n, 4, 1, f) != 1) return 0;
    while (n--)
        if (fread(&off, 4, 1, f) != 1 || off + 4096u > sizeof pl_nor || fread(pl_nor + off, 1, 4096, f) != 4096) return 0;
    for (k = 0; k < 2u; k++)
        if (fread(&pl_rlen[k], 4, 1, f) != 1 || pl_rlen[k] > sizeof pl_rec[k] || fread(pl_rec[k], 1, pl_rlen[k], f) != pl_rlen[k]) return 0;
    for (k = 0; k < 3u; k++)
        if (fread(&pl_wn[k], 4, 1, f) != 1 || pl_wn[k] > PL_STATE || fread(pl_want[k], 1, pl_wn[k], f) != pl_wn[k]) return 0;
    fclose(f);
    return 1;
}
static int fails;
static void pl_check(const char *s, int ok) { printf("pads: %-100s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 3 && !strcmp(argv[1], "emit")) {
        make();
        return write_fixture(argv[2]);
    }
    if (argc != 2 || !read_fixture(argv[1])) { printf("pads: no fixture (pads_load_test FILE)\n"); return 1; }
    memcpy(nor, pl_nor, sizeof nor);
    load_all();
    pl_check("70111e91's project: both kit tracks (a recorded kit, the 909 with its offsets), every lane's hits and accents, "
          "notes on lanes, recorded hits with their pitch and length: loaded byte for byte", pl_gn[0] == pl_wn[0] &&
          !memcmp(pl_got[0], pl_want[0], pl_wn[0]));
    pl_check("70111e91's user preset of the 909 track (its KIT patch bit packed, its grid) loaded byte for byte",
          pl_gn[1] == pl_wn[1] && !memcmp(pl_got[1], pl_want[1], pl_wn[1]));
    pl_check("70111e91's user kit record found by the boot scan, checked by its synth, its takes played sample for sample",
          pl_gn[2] == pl_wn[2] && !memcmp(pl_got[2], pl_want[2], pl_wn[2]) && T_LIST(0).n);
    {   /* saved again: the same records */
        static uint8_t r[2][32768];
        uint32_t n[2];
        resave(r, n);
        pl_check("the project saved again from 70111e91's flash: its record as 70111e91 writes it",
                 n[0] == pl_rlen[0] && !memcmp(r[0], pl_rec[0], n[0]));
        pl_check("the user preset stored again from the loaded track: the bank's record as 70111e91 writes it",
                 n[1] == pl_rlen[1] && !memcmp(r[1], pl_rec[1], n[1]));
    }
    printf(fails ? "pads: FAILED\n" : "pads: all passed\n");
    return fails != 0;
}
