/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* An outside synth (tests/run_tests.sh outside_ppg): Saadet's PPG (outside/ppg, MPL-2.0; its waves and programs PPG's,
 * from local images: gen_ppg_data.py) built from the SDK alone in a folder of its own and, the same source, for the host
 * from that folder (its object: the code the host plays). The firmware has no line of it.
 *   outside_ppg_test PPG.mmod DIR   installed as an upload after another outside synth that asked for the same number
 *                                   (the first free one), its presets listed and picked from its file with its code not
 *                                   loaded, played (every eighth program, TABLE 31's natural sounds), the 2.2's voices
 *                                   dealt by KEYB through its alloc (one key a voice, all eight on one key, a split),
 *                                   the common pitch bend held off, its pages from its schema (GROUP edits group B,
 *                                   TABLE both, SEMI every voice, the dim hints), a project of it saved and loaded
 *                                   (FBKS), on another device where it has another handle. DIR: scratch.
 *                                   OUTSIDE_PPG_RENDER=DIR also writes each program's phrase (M-kkk.raw: Saadet's
 *                                   comparison) */
#define BACKUP_TEST_NO_MAIN
#define MELODEE_USB_AUDIO 1                         /* (the stems the USB capture takes: mix.c track_capture) */
#include "backup_test.c"
#include "../firmware/src/sysex_route.c"
#include "../firmware/src/module_store.c"
#include "mod_ident_test.h"
#include <sys/stat.h>
#include <assert.h>

/* the module's own factory rows (outside/ppg/ppg_presets.h, gen_ppg_data.py), read here as data: its file's PRST */
typedef struct { char name[21]; uint8_t category; int8_t e[8]; uint8_t env[4]; int8_t fenv; uint8_t mono; } op_prow_t;
#define MOD3_PRESETS_PATCH(n) static const struct { op_prow_t row; uint8_t patch[n]; } op_rows[]
#include "../outside/ppg/ppg_patch.h"
#include "../outside/ppg/ppg_presets.h"

extern const mod_desc_v3 mod_ppg;                 /* (the outside folder's host object) */
static const mod_desc_v3 *op_desc(uint32_t e) { return e == 24u ? &mod_ppg : 0; }   /* (its file asks for 24) */
static uint8_t file[MOD_FILE_MAX], other[1024];
static uint32_t file_n;

static uint32_t load_file(const char *path, uint8_t *b, uint32_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    uint32_t n = (uint32_t)fread(b, 1, max, f);
    fclose(f);
    return n;
}
static int put(uint32_t k, const uint8_t *b, uint32_t n)   /* (as editor_module.c: begin, 256-byte writes, commit) */
{
    int rc = mod_put_begin(k, n);
    for (uint32_t off = 0; !rc && off < n; off += 256u)
        rc = mod_put_write(k, off, b + off, n - off < 256u ? n - off : 256u);
    return rc ? rc : mod_put_commit(k);
}
/* another outside synth's file (acme/OTHER asking for 24: its image a pattern, never loaded here), stored in slot k */
static void install_other(uint32_t k)
{
    mod_file3_t h;
    uint32_t image = 256u, at = MOD3_HEAD + image;
    memset(other, 0, sizeof other);
    memset(&h, 0, sizeof h);
    for (uint32_t i = 0; i < image; i++) other[MOD3_HEAD + i] = (uint8_t)(i * 7u + 1u);
    mod_ident_v3 oid;
    h.uid = test_idnt(other, &at, "acme", "other", "OTHE", 0, &oid);   /* (unsigned) */
    h.magic = MOD3_FILE_MAGIC; h.abi = MOD3_ABI; h.engine = 24; h.host = MOD3_HOST_MIN; h.flags = MOD3_FILE_CHUNKS;
    h.image = image; h.bss = 16; h.version = 1; h.total = at;
    strncpy(h.name, "OTHER", sizeof h.name);
    h.crc = st_crc32(other + MOD3_HEAD, at - MOD3_HEAD);
    memcpy(other, &h, sizeof h);
    assert(!st_save(st_mod_obj(k, 0), other, at));
}
static void boot(void)               /* power-on: the map and the slots read again, the main loop's first turn */
{
    eng_map_service(1);
    for (uint32_t k = 0; k < NMOD; k++) mod_unregister(k, 0), memset(&mods[k], 0, sizeof mods[k]);
    mod_boot();
    mod_service(fm1_ms);
}
/* the main loop until the parts' modules are in SRAM */
static void serve(void) { for (uint32_t i = 0; i < 4u; i++) { fm1_ms += 10u; mod_service(fm1_ms); } }

/* ---- sound */
static void voices_clear(void)
{
    for (uint32_t k = 0; k < NPART; k++) memset(trk[k].v, 0, NVOICE * sizeof *trk[k].v), trk[k].rr = trk[k].sp_alt = 0;
    rng_state = 0x1234567u;                         /* (libc.c: the shared generator, as at power-on) */
    trk_voices_bind();
    vage = 0;
    dith_r = DITH_SEED;
}
/* notes on parts 0 and 1 through the mixer (mix.c mix_part), n frames of their mix (L, R) into out */
static void play(int32_t *out, uint32_t n, uint32_t parts)
{
    voices_clear();
    fx_send_on = 0;
    for (uint32_t k = 0; k < parts; k++)            /* (a switch the load asked for: at once, as host_preset does) */
        eng_state_clear(k), trk[k].engine = trk[k].eng_req;
    for (uint32_t i = 0; i < n; i += CTL) {
        uint32_t blk = i / CTL;
        for (uint32_t k = 0; k < parts; k++) {
            if (blk == 0 || blk == 160u)
                trk_note_on(&trk[k], 48u + 7u * k + (blk ? 5u : 0u), 100);
            if (blk == 120u + 40u * k)
                trk_note_off(&trk[k], 48u + 7u * k);
            if (blk == 400u)
                trk_note_off(&trk[k], 53u + 7u * k);
        }
        dither_block();
        memset(mix_l, 0, sizeof mix_l);
        memset(mix_r, 0, sizeof mix_r);
        for (uint32_t k = 0; k < parts; k++)
            mix_part(&trk[k], CTL);
        for (uint32_t j = 0; j < CTL; j++) out[2u * (i + j)] = mix_l[j], out[2u * (i + j) + 1u] = mix_r[j];
    }
}
/* preset p, a chord then a melody, on part 0, as a render outside the mixer's has it (track_render) */
static const uint8_t PH_NOTE[6] = {48, 55, 60, 64, 67, 72};
static void phrase_notes(uint32_t blk)
{
    if (blk == 0)
        for (uint32_t j = 0; j < 3u; j++) trk_note_on(&trk[0], PH_NOTE[j], 100);
    if (blk == 300u)
        for (uint32_t j = 0; j < 3u; j++) trk_note_off(&trk[0], PH_NOTE[j]);
    if (blk >= 500u && blk < 1100u && (blk - 500u) % 100u == 0) trk_note_on(&trk[0], PH_NOTE[(blk - 500u) / 100u], 90);
    if (blk > 500u && blk <= 1100u && (blk - 500u) % 100u == 60u) trk_note_off(&trk[0], PH_NOTE[(blk - 500u) / 100u]);
}
static void phrase(uint32_t e, uint32_t p, int32_t *out, uint32_t n)
{
    host_tracks_init();
    voices_clear();
    eng_state_reset();
    host_preset(&trk[0], e, p);
    for (uint32_t i = 0; i < n; i += CTL) {
        phrase_notes(i / CTL);
        dither_block();
        spread_on = 0;
        mix_st_on = 0;
        track_render(&trk[0], out + i, CTL);
    }
}
static uint64_t energy(const int32_t *a, uint32_t n)
{
    uint64_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += (uint64_t)((int64_t)a[i] * a[i]) >> 8;
    return s;
}
static int32_t peak(const int32_t *a, uint32_t n)
{
    int32_t m = 0;
    for (uint32_t i = 0; i < n; i++) m = a[i] > m ? a[i] : -a[i] > m ? -a[i] : m;
    return m;
}
static void set(uint32_t e, uint32_t part, uint32_t id, int32_t v)   /* a page's edit (sch_put) */
{
    sch_param_t pr;
    assert(!sch_param(e, id, &pr));
    (void)sch_put(&trk[part], e, patch_of(part), &pr, v);
    patch_note(part, MOD3_PATCH_EDIT, id);
}
static int32_t pget(uint32_t e, uint32_t part, uint32_t id)
{
    sch_param_t pr;
    assert(!sch_param(e, id, &pr));
    return sch_get(&trk[part], e, patch_of(part), &pr);
}
/* KEYB k on INIT: keys struck one after another, a block each; the part's sounding voices (keys) after them, the notes
 * of those still sounding in held (a mask of the struck keys) */
static uint32_t keyb_keys(uint32_t e, uint32_t k, const uint8_t *notes, uint32_t n, uint32_t *held)
{
    static int32_t blk[CTL];
    host_tracks_init();
    voices_clear();
    eng_state_reset();
    host_preset(&trk[0], e, 0);
    set(e, 0, PGX_KEYB, (int32_t)k);
    set(e, 0, PGX_SPLIT, 60);                        /* (KEYB 4..8: B up to C4, A above) */
    *held = 0;
    for (uint32_t i = 0; i < n; i++) {
        trk_note_on(&trk[0], notes[i], 100);
        dither_block();
        track_render(&trk[0], blk, CTL);
    }
    uint32_t on = 0;
    for (uint32_t v = 0; v < NVOICE; v++)
        if (trk[0].v[v].active) {
            on++;
            for (uint32_t i = 0; i < n; i++) *held |= (uint32_t)(trk[0].v[v].note == notes[i]) << i;
        }
    return on;
}

#define OP_N (1400u * CTL)                          /* (about a second) */
static int32_t snd_a[2u * OP_N], snd_b[2u * OP_N], eng_a[OP_N];

/* the device the project goes to ("another device"): the PPG alone, the first free handle */
static uint32_t fresh_device(void)
{
    for (uint32_t k = 0; k < NMOD; k++) (void)mod_delete(k);
    eng_map_service(1);
    eng_map_defaults();
    boot();
    ui.msg[0] = 0;
    return put(5, file, file_n) == MS_LOADED ? mods[5].engine : NENGINES;
}

static int run(void)
{
    int bad = 0;
    char nm[MOD_PST_NAME + 1u];
    reset();
    mod_host_desc = op_desc;
    mod_boot();
    /* ---- installed: the number through the engine map */
    install_other(0);
    boot();
    bad += check("another outside synth (acme/OTHER) installed first: the first free handle", mods[0].engine == ENG_H_FREE0 &&
                 !strcmp(eng_name(ENG_H_FREE0), "OTHER"));
    ui.msg[0] = 0;
    int rc = put(3, file, file_n);
    uint32_t e = mods[3].engine;
    bad += check("PPG installs as an upload (slot 3), its code in SRAM, on another free handle (what its file asks for "
                 "means nothing)", rc == MS_LOADED && e != ENG_H_FREE0 && e >= ENG_H_FREE0 && eng_ok(e) && !strcmp(eng_name(e), "PPG") && mod_at[e] == 4u);
    eng_map_service(1);
    boot();
    bad += check("power-on: listed from its record, the same number, its code not loaded", mods[3].state == MS_STORED &&
                 mods[3].engine == e && mod_idle(e));
    /* ---- its presets: names, categories and programs from its file, nothing loaded */
    uint32_t names = 1, cats = 1, bytes = 1, common = 1, valid = 1;
    static preset_entry_t x;
    for (uint32_t k = 0; k < PPG_NPRESETS; k++) {
        eng_preset_name(e, k, nm, sizeof nm);
        names &= !strcmp(nm, op_rows[k].row.name);
        cats &= preset_cat(e, k) == (op_rows[k].row.category ? op_rows[k].row.category : cat_of_engine(e));
        preset_fetch_into(e, k, &x, 1);
        bytes &= x.plen == PPG_BLOB && !memcmp(x.patch, op_rows[k].patch, PPG_BLOB);
        common &= !memcmp(x.pr.e, op_rows[k].row.e, 8) && !memcmp(x.pr.env, op_rows[k].row.env, 4);
        valid &= ppg_patch_valid((const ppg_patch_t *)(const void *)op_rows[k].patch);
    }
    ppg_patch_t init;
    ppg_patch_init(&init);
    bad += check("its presets (INIT WAVE, the 2.2's programs) listed by name from its file (PRST), the module not loaded",
                 eng_npresets(e) == PPG_NPRESETS && names && mod_idle(e));
    bad += check(".. their browser categories from the file", cats);
    bad += check(".. a pick decodes each preset's exact program (115 bytes) and common values, still not loaded; every "
                 "program valid, the first INIT's", bytes && common && valid && mod_idle(e) &&
                 !memcmp(op_rows[0].patch, &init, PPG_BLOB));
    /* ---- it plays */
    host_tracks_init();
    set_engine_of(&trk[0], e);                      /* (the UI's pick: into SRAM first, its schema's first preset) */
    trk[0].engine = trk[0].eng_req;
    serve();
    uint32_t first = PPG_NPRESETS > 1u ? 1u : 0u;
    bad += check("a part picks it (the UI's path): its code loads, the part starts from its first program",
                 mods[3].state == MS_LOADED && !mod_idle(e) && trk[0].preset == first && patch_eng_of(0) == e &&
                 !memcmp(patch_of(0), op_rows[first].patch, PPG_BLOB));
    uint32_t heard = 1, same = 1, differ = 0, tried = 0;
    uint64_t prev = 0;
    for (uint32_t p = 0; p < PPG_NPRESETS; p += 8u) {
        phrase(e, p, snd_a, OP_N);
        phrase(e, p, snd_b, OP_N);
        uint64_t en = energy(snd_a, OP_N);
        heard &= en > 1000u && peak(snd_a, OP_N) < 2000000;
        same &= !memcmp(snd_a, snd_b, OP_N * sizeof *snd_a);
        differ += en != prev;
        prev = en;
        tried++;
    }
    bad += check("it plays: every eighth preset's phrase sounds (chord, melody), within range, the same twice", heard && same &&
                 differ + 1u >= tried);
#ifdef PPG_TEST_SAMPLE
    {
        uint32_t smp = 0, loud = 1;
        for (uint32_t p = 1; p < PPG_NPRESETS; p++)
            if (op_rows[p].patch[PG_TABLE] == PG_SAMPLE) {
                phrase(e, p, snd_a, OP_N);
                loud &= energy(snd_a, OP_N) > 1000u;
                smp++;
            }
        bad += check("TABLE 31: the programs on the 2.2's natural sounds (piano, saxophone) sound", smp && loud);
    }
#endif
    if (getenv("OUTSIDE_PPG_RENDER")) {            /* (the phrases for Saadet's comparison: int32 per sample, program k) */
        for (uint32_t p = 1; p < PPG_NPRESETS; p++) {
            char path[512];
            phrase(e, p, eng_a, OP_N);
            snprintf(path, sizeof path, "%s/M-%03u.raw", getenv("OUTSIDE_PPG_RENDER"), p - 1u);
            FILE *f = fopen(path, "wb");
            if (f) { fwrite(eng_a, sizeof *eng_a, OP_N, f); fclose(f); }
        }
    }
    /* ---- the 2.2's voices, dealt by KEYB (its alloc: Saadet's poly_key) */
    {
        static const uint8_t K9[9] = {48, 50, 52, 53, 55, 57, 59, 60, 62}, K2[2] = {60, 64}, KS[4] = {48, 50, 72, 74};
        uint32_t held, on0 = keyb_keys(e, 0, K9, 9, &held), h0 = held;
        bad += check("KEYB 0: eight keys at once, a voice each; the ninth takes the oldest key's", on0 == 8u &&
                     h0 == 0x1FEu);
        uint32_t on3 = keyb_keys(e, 3, K2, 2, &held);
        bad += check("KEYB 3: all eight voices on one key; the next key takes them, the first key ends", on3 == 1u &&
                     held == 2u && trk_nvoice(&trk[0]) == 1u);
        uint32_t on1 = keyb_keys(e, 1, K9, 5, &held);
        bad += check("KEYB 1: pairs, four keys; the fifth takes the first's pair", on1 == 4u && held == 0x1Eu);
        uint32_t on8 = keyb_keys(e, 8, KS, 4, &held);
        bad += check("KEYB 8: a split, each side one key of four voices (B up to SPLIT, A above): the last of each side "
                     "sounds", on8 == 2u && held == 0xAu);
        int32_t blk[CTL];
        host_tracks_init();
        voices_clear();
        eng_state_reset();
        host_preset(&trk[0], e, 0);
        trk_note_on(&trk[0], 60, 100);
        trk[0].bend_raw = 8191;                      /* (the wheel up; INIT's BEND TO is PITCH, BI 2ND) */
        midi_bend_target[0] = 4096;                  /* (what a channel's range would make of it) */
        for (uint32_t i = 0; i < 8u; i++) dither_block(), track_render(&trk[0], blk, CTL);
        bad += check("the common pitch bend is held off (its bend range 0): the 2.2's own BD / BI bend it",
                     midi_bend_target[0] == 0 && energy(blk, CTL) > 0u);
        trk[0].bend_raw = 0;                         /* (the wheel back: PPG reads the channel's bend itself; the */
        midi_bend_q8[0] = midi_bend_target[0] = 0;   /* smoother this check poked, at rest again) */
    }
    /* ---- its pages: from its schema (titles, columns, values, dim hints) */
    host_tracks_init();
    host_preset(&trk[0], e, 0);
    song.sel = 0;
    serve();
    uint32_t npg = 0, ncol = 0, texts = 1, lfo = 0, env = 0;
    char seen[600] = "";
    for (uint32_t i = 0; i < NPAGES; i++) {
        const page_t *pg = page_at(i);
        if (PAGE_TAB[i].scope != SC_MOD || pg == &SCH_PG_NONE || !sch_page_shown(i))
            continue;
        npg++;
        lfo += (sch_pg_flags(pg) & MOD3_PG_LFO) != 0;
        env += (sch_pg_flags(pg) & MOD3_PG_ENV) != 0;
        strncat(seen, pg->title, sizeof seen - strlen(seen) - 2);
        strncat(seen, "|", sizeof seen - strlen(seen) - 1);
        for (uint32_t c = 0; c < 4u; c++) {
            int16_t *vp;
            const param_desc_t *d = page_desc(pg, c, &vp);
            char val[16];
            const char *unit = 0;
            if (!d)
                continue;
            ncol++;
            val[0] = 0;
            param_format(d, *vp, val, &unit);
            texts &= d->label && d->label[0] && d->label[0] != '-' && val[0];
        }
    }
    bad += check("its pages from its schema: STORE, PROGRAM, WAVES, TUNE, FILTER, ENV 1, LOUD, ENV 3, LFO, PLAY (the knob "
                 "banks not among them)", npg == 16u && lfo == 2u && env == 3u &&
                 !strcmp(seen, "PPG STORE|PPG PROGRAM|PPG TABLE|PPG SWEEP|PPG PITCH|PPG SEMI 1-4|PPG SEMI 5-8|PPG FILTER|"
                               "PPG ENV 1|PPG ENV 2|PPG LEVEL|PPG ENV 3|PPG LFO|PPG LFO TO|PPG TOUCH|PPG VELOCITY|"));
    if (!(npg == 16u)) printf("   pages: %u %s\n", npg, seen);
    bad += check(".. every column labelled with a value (58 columns)", ncol == 58u && texts);
    {
        const page_t *tab = 0;
        for (uint32_t i = 0; i < NPAGES && !tab; i++)
            if (PAGE_TAB[i].scope == SC_MOD && page_at(i) != &SCH_PG_NONE && !strcmp(page_at(i)->title, "PPG TABLE"))
                tab = page_at(i);
        int dim_off = tab && sch_col_dim(tab, 2);    /* (INIT: SUB OFF, its waves have no effect) */
        set(e, 0, PG_SW, 0);
        int dim_on = tab && sch_col_dim(tab, 2);
        bad += check(".. the module's dim hints: SUB dim while it is OFF, lit at PARA", dim_off && !dim_on);
        const ppg_patch_t *pp = (const ppg_patch_t *)(const void *)patch_of(0);
        set(e, 0, PG_SEMI, -12);
        int semi = pget(e, 0, PG_SEMI) == -12 && pget(e, 0, PGX_V1) == -12 && pp->raw[PG_SEMI] == 12u &&
                   pp->raw[PG_SEMI8] == 12u && pp->rawb[PG_SEMI] == PG_SEMI0;
        set(e, 0, PGX_V1, 0);
        semi &= pp->raw[PG_SEMI] == PG_SEMI0 && pp->raw[PG_SEMI2] == 12u;
        bad += check(".. SEMI moves the group's eight voices together (shown signed, stored offset); V1 voice 1 alone", semi);
        set(e, 0, PG_TABLE, 5);
        set(e, 0, PGX_SEL, 1);
        set(e, 0, PG_CUT, 10);
        bad += check(".. TABLE is the program's (both groups); GROUP B: the fields edit group B",
                     pp->raw[PG_TABLE] == 5u && pp->rawb[PG_TABLE] == 5u && pp->sel == 1u && pp->rawb[PG_CUT] == 10u &&
                     pp->raw[PG_CUT] == 63u && pget(e, 0, PG_CUT) == 10);
        set(e, 0, PG_E3AMT, -32);
        set(e, 0, PGX_SPLIT, 96);
        bad += check(".. ENV 3's AMOUNT shown signed (stored offset), SPLIT a key (36..96)", pp->rawb[PG_E3AMT] == 0u &&
                     pget(e, 0, PG_E3AMT) == -32 && pp->split == 60u && pget(e, 0, PGX_SPLIT) == 96);
    }
    /* ---- a project of it: saved (FBKR), loaded, on another device */
    uint32_t pa = PPG_NPRESETS > 9u ? 9u : 0u, pb = PPG_NPRESETS > 66u ? 66u : 0u;
    host_tracks_init();
    host_preset(&trk[0], e, pa);
    host_preset(&trk[1], e, pb);
    set(e, 1, PG_CUT, 40);
    set(e, 1, PG_E1W, 30);
    serve();
    uint8_t p0[PPG_BLOB], p1[PPG_BLOB];
    memcpy(p0, patch_of(0), sizeof p0);
    memcpy(p1, patch_of(1), sizeof p1);
    play(snd_a, OP_N, 2);
    bad += check("two parts of it (two programs, one edited) play", energy(snd_a, 2u * OP_N) > 1000u);
    bad += check("the project saves as FBKS (its synths' entries, an outside synth's patch: its end block)",
                 !project_save_as(1, "PPG SONG") && proj_meta[1].used);
    host_tracks_init();
    project_load(1);
    serve();
    play(snd_b, OP_N, 2);
    bad += check(".. loaded: both parts PPG, their programs byte for byte, the same samples",
                 trk[0].eng_req == e && trk[1].eng_req == e && !memcmp(patch_of(0), p0, sizeof p0) &&
                 !memcmp(patch_of(1), p1, sizeof p1) && !memcmp(snd_a, snd_b, sizeof snd_a));
    uint32_t e2 = fresh_device();
    host_tracks_init();
    project_load(1);
    serve();
    play(snd_b, OP_N, 2);
    bad += check("another device (the PPG alone: the first free handle): the project's parts play it there, the same samples",
                 e2 == ENG_H_FREE0 && trk[0].eng_req == ENG_H_FREE0 && trk[1].eng_req == ENG_H_FREE0 && !memcmp(patch_of(1), p1, sizeof p1) &&
                 !memcmp(snd_a, snd_b, sizeof snd_a));
    return bad;
}

int main(int argc, char **argv)
{
    if (argc != 3 || !(file_n = load_file(argv[1], file, sizeof file))) {
        printf("outside_ppg_test PPG.mmod DIR\n");
        return 2;
    }
    mkdir(argv[2], 0755);
    int bad = run();
    printf("%s\n", bad ? "OUTSIDE PPG TEST FAILED" : "outside PPG test passed");
    return bad ? 1 : 0;
}
