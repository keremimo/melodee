/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* An outside synth (tests/run_tests.sh outside_hydra): Saadet's HYDRA (outside/hydra, MPL-2.0) built from the SDK alone
 * in a folder of its own (firmware/modules/sdk and outside/hydra copied away; no Melodee tree; signed by a test author
 * key: its ID that key's and "hydra") and, the same source, for the host from that folder (its object: the code the host
 * plays). The firmware has no line of it.
 *   outside_hydra_test HYDRA.mmod DIR      installed as an upload after another outside synth that asked for the same
 *                                          number (the first free one through the engine map), its presets listed and
 *                                          picked from its file with its code not loaded, played (every preset, the
 *                                          same twice), its part's memory sized by the patch (SINGLE / SCAN), a macro
 *                                          (E1) moving its destinations, the CPU1 queue's path (mix_block), its pages
 *                                          from its schema (the macros' names, the module's dim hints and labels, a
 *                                          macro destination set and given back), its native SysEx (in: offered for a
 *                                          user slot and heard on the selected part; out: the part's patch), a project
 *                                          of it saved and loaded (FBKS), on another device where it has another
 *                                          number. DIR: the project and its render. OUTSIDE_HYDRA_RENDER=DIR also writes
 *                                          every preset's phrase (int32 per sample: a comparison with Saadet's) */
#define BACKUP_TEST_NO_MAIN
#include "backup_test.c"
#include "../firmware/src/sysex_route.c"
#include "../firmware/src/module_store.c"
#include "mod_ident_test.h"
#include <sys/stat.h>
#include <assert.h>

/* the module's own factory rows (outside/hydra/hydra_presets.h), read here as data: what its file's PRST must hold */
typedef struct { char name[21]; uint8_t category; int8_t e[8]; uint8_t env[4]; int8_t fenv; uint8_t mono; } oh_prow_t;
#define MOD3_PRESETS_PATCH(n) static const struct { oh_prow_t row; uint8_t patch[n]; } oh_rows[]
#include "../outside/hydra/hydra_patch.h"
#include "../outside/hydra/hydra_presets.h"
enum { OH_WAVESCAN = 1, OH_VOXCHOIR = 2, OH_SUPERSTACK = 4, OH_FMBELL = 5, OH_WARPBASS = 7 };

extern const mod_desc_v3 mod_hydra;               /* (the outside folder's host object) */
static const mod_desc_v3 *oh_desc(uint32_t e) { return e == 23u ? &mod_hydra : 0; }   /* (its file asks for 23) */
static uint8_t file[MOD_FILE_MAX], other[1024], big[BANK_STORE_SIZE + 64];
static uint32_t file_n;
static const char *dir;

static uint32_t load_file(const char *path, uint8_t *b, uint32_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    uint32_t n = (uint32_t)fread(b, 1, max, f);
    fclose(f);
    return n;
}
static void save_file(const char *name, const void *p, uint32_t n)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(p, 1, n, f); fclose(f); }
}
static uint64_t other_uid;                          /* (acme/OTHER's ID: install_other) */
static int put(uint32_t k, const uint8_t *b, uint32_t n)   /* (as editor_module.c: begin, 256-byte writes, commit) */
{
    int rc = mod_put_begin(k, n);
    for (uint32_t off = 0; !rc && off < n; off += 256u)
        rc = mod_put_write(k, off, b + off, n - off < 256u ? n - off : 256u);
    return rc ? rc : mod_put_commit(k);
}
/* another outside synth's file (acme/OTHER asking for 23: its image a pattern, never loaded here), stored in slot k */
static void install_other(uint32_t k)
{
    mod_file3_t h;
    uint32_t image = 256u, at = MOD3_HEAD + image;
    memset(other, 0, sizeof other);
    memset(&h, 0, sizeof h);
    for (uint32_t i = 0; i < image; i++) other[MOD3_HEAD + i] = (uint8_t)(i * 7u + 1u);
    mod_ident_v3 oid;
    h.uid = test_idnt(other, &at, "acme", "other", "OTHE", 0, &oid);   /* (unsigned) */
    other_uid = mod_le64(oid.id);
    h.magic = MOD3_FILE_MAGIC; h.abi = MOD3_ABI; h.engine = 23; h.host = MOD3_HOST_MIN; h.flags = MOD3_FILE_CHUNKS;
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
/* SysEx in as a computer sends it over USB (CIN 4..7 packets), then the main loop's service */
static void sx_in(const uint8_t *p, uint32_t n)
{
    wire_release();
    for (uint32_t i = 0; i < n;) {
        uint32_t k = n - i > 3u ? 3u : n - i, pkt = i + k == n && p[n - 1] == 0xF7 ? 4u + k : 4u;
        for (uint32_t j = 0; j < k; j++)
            pkt |= (uint32_t)p[i++] << (8u * (j + 1u));
        midi_in_event(pkt);
        mi_r = mi_w;
    }
    msx_service();
}
/* the main loop until the parts' modules are in SRAM */
static void serve(void) { for (uint32_t i = 0; i < 4u; i++) { fm1_ms += 10u; mod_service(fm1_ms); } }

/* ---- sound: notes on parts 0 and 1 through the mixer (mix.c mix_part), n frames of their mix (L, R) into out */
static void voices_clear(void)
{
    for (uint32_t k = 0; k < NPART; k++) memset(trk[k].v, 0, NVOICE * sizeof *trk[k].v), trk[k].rr = trk[k].sp_alt = 0;
    rng_state = 0x1234567u;                         /* (libc.c: the shared generator, as at power-on) */
    trk_voices_bind();
    vage = 0;
    dith_r = DITH_SEED;
}
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
/* preset p, a chord then a melody up to C6 (twice the rate from F4: the oscillators' 2x path), on part 0 as
 * track_render renders it; e1: macro 1's value (E1) */
static const uint8_t PH_NOTE[6] = {48, 55, 60, 64, 79, 84};
static void phrase_notes(uint32_t blk)
{
    if (blk == 0)
        for (uint32_t j = 0; j < 3u; j++) trk_note_on(&trk[0], PH_NOTE[j], 100);
    if (blk == 300u)
        for (uint32_t j = 0; j < 3u; j++) trk_note_off(&trk[0], PH_NOTE[j]);
    if (blk >= 500u && blk < 1100u && (blk - 500u) % 100u == 0) trk_note_on(&trk[0], PH_NOTE[(blk - 500u) / 100u], 90);
    if (blk > 500u && blk <= 1100u && (blk - 500u) % 100u == 60u) trk_note_off(&trk[0], PH_NOTE[(blk - 500u) / 100u]);
}
static uint32_t held_bytes;                         /* (the part's memory as the phrase left it) */
static void phrase_e(uint32_t e, uint32_t p, int32_t e1, int32_t *out, uint32_t n)
{
    host_tracks_init();
    voices_clear();
    eng_state_reset();
    host_preset(&trk[0], e, p);
    trk[0].p[P_E0] = (int16_t)e1;
    for (uint32_t i = 0; i < n; i += CTL) {
        phrase_notes(i / CTL);
        dither_block();
        spread_on = 0;
        track_render(&trk[0], out + i, CTL);
    }
    held_bytes = resource[RES_ENGINE0].size;
}
static void phrase(uint32_t e, uint32_t p, int32_t *out, uint32_t n) { phrase_e(e, p, 0, out, n); }
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
static const page_t *page_titled(const char *title)
{
    for (uint32_t i = 0; i < NPAGES; i++)
        if (PAGE_TAB[i].scope == SC_MOD && page_at(i) != &SCH_PG_NONE && !strcmp(page_at(i)->title, title))
            return page_at(i);
    return 0;
}
static void col_text(const page_t *pg, uint32_t c, char *val, const char **label)
{
    int16_t *vp;
    const param_desc_t *d = pg ? page_desc(pg, c, &vp) : 0;
    const char *unit = 0;
    val[0] = 0;
    *label = d ? d->label : 0;
    if (d) param_format(d, *vp, val, &unit);
}

#define OH_N (1400u * CTL)                          /* (about a second) */
static int32_t snd_a[2u * OH_N], snd_b[2u * OH_N];

/* the device the project goes to: HYDRA alone, the first free handle */
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
    mod_host_desc = oh_desc;
    mod_boot();
    /* ---- installed: the number through the engine map */
    install_other(0);
    boot();
    bad += check("another outside synth (acme/OTHER) installed first: the first free handle", mods[0].engine == ENG_H_FREE0 &&
                 !strcmp(eng_name(ENG_H_FREE0), "OTHER"));
    ui.msg[0] = 0;
    int rc = put(3, file, file_n);
    uint32_t e = mods[3].engine, idnt = 0;
    uint64_t uid = 0;
    for (uint32_t at = MOD3_HEAD + ((mod_file3_t *)(void *)file)->image + 4u * ((mod_file3_t *)(void *)file)->nrel;
         at + 8u <= file_n && !uid; at += 8u + ((file[at + 4] | (uint32_t)file[at + 5] << 8) + 3u & ~3u))
        if (mod_le32(file + at) == MOD3_IDNT_TAG && (idnt = at + 8u)) uid = mod_le64(file + idnt);   /* (its ID: IDNT's) */
    bad += check("HYDRA installs as an upload (slot 3), its code in SRAM, on another free handle (what its file asks for "
                 "means nothing)", rc == MS_LOADED && e != ENG_H_FREE0 && e >= ENG_H_FREE0 && eng_ok(e) && !strcmp(eng_name(e), "HYDRA") && mod_at[e] == 4u);
    bad += check(".. who it is: its ID (its author's key and hydra), in the device's engine map", mods[3].uid == uid &&
                 eng_map[e].uid == uid && eng_map[ENG_H_FREE0].uid == other_uid);
    bad += check(".. signed by its author's key: no warning", mods[3].sig == MOD_SIG_AUTHOR && !ui.msg[0]);
    eng_map_service(1);
    boot();
    bad += check("power-on: listed from its record, the same number, its code not loaded", mods[3].state == MS_STORED &&
                 mods[3].engine == e && mod_idle(e));
    /* ---- its presets: names, categories and patches from its file, nothing loaded */
    uint32_t names = 1, cats = 1, bytes = 1, common = 1;
    static preset_entry_t x;
    for (uint32_t k = 0; k < HY_NPRESETS; k++) {
        eng_preset_name(e, k, nm, sizeof nm);
        names &= !strcmp(nm, oh_rows[k].row.name);
        cats &= preset_cat(e, k) == (oh_rows[k].row.category ? oh_rows[k].row.category : cat_of_engine(e));
        preset_fetch_into(e, k, &x, 1);
        bytes &= x.plen == HY_BYTES && !memcmp(x.patch, oh_rows[k].patch, HY_BYTES);
        common &= !memcmp(x.pr.e, oh_rows[k].row.e, 8) && !memcmp(x.pr.env, oh_rows[k].row.env, 4) &&
                  x.pr.fenv == oh_rows[k].row.fenv && x.pr.mono == oh_rows[k].row.mono;
    }
    bad += check("17 presets (INIT, Saadet's sixteen) listed by name from its file (PRST), the module not loaded",
                 eng_npresets(e) == HY_NPRESETS && names && mod_idle(e));
    bad += check(".. their browser categories from the file", cats && preset_cat(e, OH_FMBELL) == 13u &&
                 preset_cat(e, OH_WARPBASS) == 1u);
    bad += check(".. a pick decodes each preset's exact patch (144 bytes) and common values, still not loaded",
                 bytes && common && mod_idle(e));
    /* ---- it plays */
    host_tracks_init();
    set_engine_of(&trk[0], e);                      /* (the UI's pick: into SRAM first, its schema's first preset) */
    trk[0].engine = trk[0].eng_req;
    serve();
    bad += check("a part picks it (the UI's path): its code loads, the part starts from its first sound, WAVESCAN",
                 mods[3].state == MS_LOADED && !mod_idle(e) && trk[0].preset == 1u && patch_eng_of(0) == e &&
                 !memcmp(patch_of(0), oh_rows[OH_WAVESCAN].patch, HY_BYTES));
    uint32_t heard = 1, same = 1, differ = 0;
    uint64_t prev = 0;
    for (uint32_t p = 0; p < HY_NPRESETS; p++) {
        phrase(e, p, snd_a, OH_N);
        phrase(e, p, snd_b, OH_N);
        uint64_t en = energy(snd_a, OH_N);
        heard &= en > 1000u && peak(snd_a, OH_N) < 2000000;
        same &= !memcmp(snd_a, snd_b, OH_N * sizeof *snd_a);
        differ += en != prev;
        prev = en;
        if (en <= 1000u || peak(snd_a, OH_N) >= 2000000)
            printf("   preset %u %s: energy %llu peak %d\n", p, oh_rows[p].row.name, (unsigned long long)en,
                   (int)peak(snd_a, OH_N));
    }
    bad += check("it plays: every preset's phrase sounds (chord, melody to C6), within range, the same twice", heard &&
                 same && differ == HY_NPRESETS);
    if (getenv("OUTSIDE_HYDRA_RENDER")) {          /* (the phrases for a comparison with Saadet's: int32 per sample) */
        for (uint32_t p = 0; p < HY_NPRESETS; p++) {
            char path[512];
            phrase(e, p, snd_a, OH_N);
            snprintf(path, sizeof path, "%s/hydra-%02u.raw", getenv("OUTSIDE_HYDRA_RENDER"), p);
            FILE *f = fopen(path, "wb");
            if (f) { fwrite(snd_a, sizeof *snd_a, OH_N, f); fclose(f); }
        }
    }
    /* ---- the part's memory sized by its patch: one wave a SINGLE oscillator, eight a SCAN one */
    {
        uint32_t init, scan1, scan2;
        phrase(e, 0, snd_a, OH_N); init = held_bytes;
        phrase(e, OH_WAVESCAN, snd_a, OH_N); scan1 = held_bytes;
        phrase(e, OH_VOXCHOIR, snd_a, OH_N); scan2 = held_bytes;
        bad += check("its part's memory by the patch (.state): INIT 3 wave rows, WAVESCAN 10, VOX CHOIR 17 (512 bytes a row)",
                     init && scan1 == init + 7u * 512u && scan2 == scan1 + 7u * 512u);
    }
    /* ---- a macro: E1 moves its destinations (WAVESCAN's macro 1: SCAN 1) */
    {
        phrase_e(e, OH_WAVESCAN, 0, snd_a, OH_N);
        phrase_e(e, OH_WAVESCAN, 50, snd_b, OH_N);
        uint32_t diff = 0;
        for (uint32_t i = 0; i < OH_N; i++) diff += snd_a[i] != snd_b[i];
        bad += check("a macro moves its destinations: WAVESCAN with E1 at +50 (SCAN 1 further on) sounds otherwise",
                     diff > OH_N / 4u && energy(snd_b, OH_N) > 1000u);
    }
    /* ---- the audio's whole block (mix_block: the CPU1 queue's path, the master) */
    {
        static int32_t blk[2u * CTL];
        uint32_t loud = 0, peakv = 0;
        host_tracks_init();
        voices_clear();
        eng_state_reset();
        host_preset(&trk[0], e, OH_SUPERSTACK);
        dc_l = dc_r = dce_l = dce_r = 0;
        for (uint32_t i = 0; i < 8u * CTL; i += CTL)
            mix_block(blk, CTL);
        for (uint32_t i = 0; i < 400u * CTL; i += CTL) {
            phrase_notes(i / CTL);
            mix_block(blk, CTL);
            for (uint32_t j = 0; j < 2u * CTL; j++) {
                uint32_t a = (uint32_t)(blk[j] < 0 ? -blk[j] : blk[j]);
                loud += blk[j] != 0;
                peakv = a > peakv ? a : peakv;
            }
        }
        bad += check("the audio's whole block (mix_block: the firmware's CPU1 queue path, the master): SUPERSTACK sounds",
                     loud > 100u * CTL && peakv > 100u);
    }
    /* ---- its pages: from its schema (titles, columns, values, dim hints, the macros' names) */
    host_tracks_init();
    host_preset(&trk[0], e, OH_WAVESCAN);
    song.sel = 0;
    serve();
    uint32_t npg = 0, ncol = 0, texts = 1, lfo = 0, env = 0;
    for (uint32_t i = 0; i < NPAGES; i++) {
        const page_t *pg = page_at(i);
        if (PAGE_TAB[i].scope != SC_MOD || pg == &SCH_PG_NONE || !sch_page_shown(i))
            continue;
        npg++;
        lfo += (sch_pg_flags(pg) & MOD3_PG_LFO) != 0;
        env += (sch_pg_flags(pg) & MOD3_PG_ENV) != 0;
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
    bad += check("its pages from its schema: 46 (STORE, OSC 1..3, MUTANT 1..4, MIXER, FILTER 1, 2, ENV 1, MACRO 1..8; the "
                 "knob bank not among them), none the ENV or LFO button's (the amplitude is the part's ADSR)",
                 npg == 46u && !lfo && !env);
    bad += check(".. every column labelled with a value (162 columns; the waves, the macros' names and destinations the "
                 "module's words)", ncol == 162u && texts);
    {
        char val[16];
        const char *label;
        bad += check(".. E1..E8 named by the sound's macros (WAVESCAN: SCAN, MOTION, BRIGHT, RES)",
                     !strcmp(track_desc(&trk[0], P_E0)->label, "SCAN") && !strcmp(track_desc(&trk[0], P_E0 + 1)->label, "MOTION") &&
                     !strcmp(track_desc(&trk[0], P_E0 + 2)->label, "BRIGHT") && !strcmp(track_desc(&trk[0], P_E0 + 3)->label, "RES"));
        const page_t *mp = page_titled("HY MACRO 1"), *mb = page_titled("HY MACRO 1B");
        char v0[16], v2[16];
        const char *l0, *l2;
        col_text(mp, 0, v0, &l0);
        col_text(mp, 2, v2, &l2);
        bad += check(".. MACRO 1's page: its NAME (SCAN) and its first destination (SCAN 1) through the module (pget)",
                     l0 && !strcmp(v0, "SCAN") && l2 && !strcmp(l2, "DEST 1") && !strncmp(v2, "SCAN", 4));
        sch_param_t pr;
        uint8_t before[HY_BYTES];
        memcpy(before, patch_of(0), HY_BYTES);
        assert(!sch_param(e, HY_BYTES + 2u, &pr));   /* (macro 1's DEST 2: none yet) */
        int set = sch_put(&trk[0], e, patch_of(0), &pr, HT_CUT);
        int32_t got = sch_get(&trk[0], e, patch_of(0), &pr);
        col_text(mb, 0, val, &label);
        int dim_after = sch_col_dim(mb, 2);       /* (DEST 3: free slots left, the next to set: lit) */
        assert(!sch_param(e, HY_BYTES + 2u, &pr));
        int32_t k = -1;                           /* (the slot it took: the first free one) */
        for (uint32_t j = 0; j < HY_MSLOTS && k < 0; j++)
            if (!HY_SLOT_TGT(before[HY_MA + 2u * j])) k = (int32_t)j;
        int took = k >= 0 && patch_of(0)[HY_MA + 2u * (uint32_t)k] == HY_SLOT(0, HT_CUT) &&
                   patch_of(0)[HY_MA + 2u * (uint32_t)k + 1u] == HY_DEPTH0 + 64u;
        int back = sch_put(&trk[0], e, patch_of(0), &pr, 0);
        before[HY_MA + 2u * (uint32_t)(k < 0 ? 0 : k) + 1u] = HY_DEPTH0;   /* (a slot given back: its depth 0) */
        bad += check(".. a macro's next destination set (DEST 2 = CUTOFF) takes the pool's first free slot at half its "
                     "range; NONE gives it back", set && took && got == HT_CUT && !strcmp(val, "CUTOFF") && !dim_after &&
                     back && !memcmp(before, patch_of(0), HY_BYTES));
        const page_t *mu = page_titled("HY MUTANT 1"), *o1 = page_titled("HY OSC 1");
        int wave_dim = sch_col_dim(o1, 1), scan_lit = !sch_col_dim(o1, 2);   /* (WAVESCAN: OSC 1 scans) */
        assert(!sch_param(e, HY_M1 + HM_MODE, &pr));
        (void)sch_put(&trk[0], e, patch_of(0), &pr, MU_WARP);
        col_text(mu, 2, val, &label);
        int warp = label && !strcmp(label, "PARTS");
        (void)sch_put(&trk[0], e, patch_of(0), &pr, MU_OFF);
        int off_dim = sch_col_dim(mu, 1);
        bad += check(".. the module's dim hints and labels: OSC 1's WAVE dim while it scans, its SCAN lit; MUTANT 1's RATIO "
                     "named PARTS in WARP, its DEPTH dim when OFF", wave_dim && scan_lit && warp && off_dim);
        patch_note(0, MOD3_PATCH_EDIT, 0xFFFFu);
    }
    /* ---- SysEx: the native message, as from USB */
    {
        hy_patch_t pa, got;
        uint8_t f[HY_FRAME], out[HY_FRAME + 8u];
        memcpy(pa.raw, oh_rows[OH_FMBELL].patch, HY_BYTES);
        uint32_t n = hy_patch_encode(&pa, f, sizeof f);
        host_tracks_init();
        host_preset(&trk[0], e, OH_WAVESCAN);
        song.sel = 0;
        serve();
        msx_reset();
        sx_import.count = 0;
        sx_in(f, n);
        int staged = sx_import.count == 1u && sx_import.engine == e && sx_import.size == HY_BYTES;
        memcpy(got.raw, sx_import.data, HY_BYTES);
        bad += check("a native message (F0 7D 48 59 01 ..) is offered for a user slot, FM BELL's patch byte for byte",
                     staged && !memcmp(got.raw, pa.raw, HY_BYTES));
        bad += check(".. and heard at once on the selected part (it plays HYDRA)", patch_eng_of(0) == e &&
                     !memcmp(patch_of(0), pa.raw, HY_BYTES));
        ui.uslot = 10; sysex_choose();
        if (ui.confirm == CF_IMPORT) { ui.confirm = 0; sysex_commit(); }
        uint8_t slot[MOD3_PATCH_MAX];
        uint32_t sn = msx_h_slot_get(e, 10, slot);
        bad += check(".. kept in user slot 11 as HYDRA's patch", up_used(10) && sn == HY_BYTES && !memcmp(slot, pa.raw, HY_BYTES));
        f[10] = 0x40u;                              /* (a nibble out of range) */
        ui.msg[0] = 0;
        sx_import.count = 0;
        sx_in(f, n);
        bad += check("a broken message: refused, said so", !sx_import.count && !strcmp(ui.msg, "INVALID HYDRA"));
        sysex_cancel();
        n = msx_dump(e, patch_of(0), MOD3_DUMP_PART, 3, out, sizeof out);
        uint32_t want = hy_patch_encode(&pa, f, sizeof f);
        bad += check("the part's patch out as the native message", n == HY_FRAME && n == want && !memcmp(out, f, n));
    }
    /* ---- a project of it: saved (FBKR), loaded, on another device */
    host_tracks_init();
    host_preset(&trk[0], e, OH_VOXCHOIR);
    host_preset(&trk[1], e, OH_WARPBASS);
    trk[0].p[P_E0 + 1] = 30;                        /* (VOX CHOIR's macro 2: STACK) */
    {
        sch_param_t pr;
        assert(!sch_param(e, HY_F1_CUT, &pr));
        (void)sch_put(&trk[1], e, patch_of(1), &pr, 90);
        assert(!sch_param(e, HY_F1_TYPE, &pr));
        (void)sch_put(&trk[1], e, patch_of(1), &pr, F1_MSLP);
        patch_note(1, MOD3_PATCH_EDIT, 0xFFFFu);
    }
    serve();
    uint8_t p0[HY_BYTES], p1[HY_BYTES];
    memcpy(p0, patch_of(0), sizeof p0);
    memcpy(p1, patch_of(1), sizeof p1);
    play(snd_a, OH_N, 2);
    bad += check("two parts of it (VOX CHOIR with E2 at 30, WARP BASS edited: CUTOFF 90, MS LP) play",
                 energy(snd_a, 2u * OH_N) > 1000u);
    bad += check("the project saves as FBKS (its synths' entries, an outside synth's patch: its end block)",
                 !project_save_as(1, "HYDRA SONG") && proj_meta[1].used);
    {
        st_hdr_t h;
        int copy = st_current(st_proj_obj(1), &h);
        uint32_t n = 0, m = 0;
        if (copy >= 0) {
            static uint8_t raw[BANK_STORE_SIZE];
            n = h.len;
            if (!st_read_range(st_proj_obj(1), (uint32_t)copy, 0, big, n)) save_file("hydra.hpj", big, n);
            if (!pj_image(st_proj_obj(1), (uint32_t)copy, raw)) memcpy(&m, raw, 4);
        }
        bad += check(".. stored as the firmware keeps a project", n > 0 && m == BANKS_MAGIC);
        save_file("hydra-render.bin", snd_a, sizeof snd_a);
    }
    host_tracks_init();
    project_load(1);
    serve();
    play(snd_b, OH_N, 2);
    bad += check(".. loaded: both parts HYDRA, their patches byte for byte, E2 kept, the same samples",
                 trk[0].eng_req == e && trk[1].eng_req == e && !memcmp(patch_of(0), p0, sizeof p0) &&
                 !memcmp(patch_of(1), p1, sizeof p1) && trk[0].p[P_E0 + 1] == 30 && !memcmp(snd_a, snd_b, sizeof snd_a));
    uint32_t e2 = fresh_device();
    host_tracks_init();
    project_load(1);
    serve();
    play(snd_b, OH_N, 2);
    bad += check("another device (HYDRA alone: the first free handle): the project's parts play it there, the same samples",
                 e2 == ENG_H_FREE0 && trk[0].eng_req == ENG_H_FREE0 && trk[1].eng_req == ENG_H_FREE0 && !memcmp(patch_of(1), p1, sizeof p1) &&
                 !memcmp(snd_a, snd_b, sizeof snd_a));
    return bad;
}

int main(int argc, char **argv)
{
    if (argc != 3 || !(file_n = load_file(argv[1], file, sizeof file))) {
        printf("outside_hydra_test HYDRA.mmod DIR\n");
        return 2;
    }
    dir = argv[2];
    mkdir(dir, 0755);
    int bad = run();
    printf("%s\n", bad ? "OUTSIDE HYDRA TEST FAILED" : "outside HYDRA test passed");
    return bad ? 1 : 0;
}
