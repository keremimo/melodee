/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* An outside synth (tests/run_tests.sh outside_j106): Saadet's JUNO-106 (outside/j106, MPL-2.0) built from the SDK alone
 * in a folder of its own (firmware/modules/sdk and outside/j106 copied away; no Melodee tree, no key: unsigned) and, the
 * same source, for the host from that folder (its object: the code the host plays). The firmware has no line of it.
 *   outside_j106_test J106.mmod DIR        installed as an upload after another outside synth that asked for the same
 *                                          number (the first free one through the engine map, the unsigned warning),
 *                                          its presets listed and picked from its file with its code not loaded, played,
 *                                          its pages from its schema, its SysEx (patch and manual messages imported into
 *                                          user slots, a single control edits the part, the patch out as the
 *                                          instrument's message), a project of it saved and loaded (FBKR), on another
 *                                          device where it has another number. DIR: the project as the device stores
 *                                          it (j106.hpj) and its render, for the .mel round trip (outside_j106_mel.mjs);
 *                                          OUTSIDE_J106_RENDER=DIR also writes the phrases (Saadet's comparison)
 *   outside_j106_test --mel J106.mmod DIR  what the web page's .mel import sent (DIR/out-j106.bin) goes in through the
 *                                          backup PUT on a device whose JUNO has its own number, loads and plays the
 *                                          same samples */
#define BACKUP_TEST_NO_MAIN
#include "backup_test.c"
#include "../firmware/src/sysex_route.c"
#include "../firmware/src/module_store.c"
#include <sys/stat.h>
#include <assert.h>

/* the module's own factory rows (outside/j106/j106_presets.h), read here as data: what its file's PRST must hold */
typedef struct { char name[21]; uint8_t category; int8_t e[8]; uint8_t env[4]; int8_t fenv; uint8_t mono; } oj_prow_t;
#define MOD3_PRESETS_PATCH(n) static const struct { oj_prow_t row; uint8_t patch[n]; } oj_rows[]
#include "../outside/j106/j106_patch.h"
#include "../outside/j106/j106_presets.h"

extern const mod_desc_v3 mod_j106;                /* (the outside folder's host object) */
static const mod_desc_v3 *oj_desc(uint32_t e) { return e == 22u ? &mod_j106 : 0; }   /* (its file asks for 22) */
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
static uint32_t uid_of(const char *s) { uint32_t u = st_crc32(s, (uint32_t)strlen(s)); return u ? u : 1u; }
static int put(uint32_t k, const uint8_t *b, uint32_t n)   /* (as editor_module.c: begin, 256-byte writes, commit) */
{
    int rc = mod_put_begin(k, n);
    for (uint32_t off = 0; !rc && off < n; off += 256u)
        rc = mod_put_write(k, off, b + off, n - off < 256u ? n - off : 256u);
    return rc ? rc : mod_put_commit(k);
}
/* another outside synth's file (acme/OTHER asking for 22: its image a pattern, never loaded here), stored in slot k */
static void install_other(uint32_t k)
{
    mod_file3_t h;
    mod_ident_v3 id;
    uint32_t image = 256u, at = MOD3_HEAD + image, tag = MOD3_IDNT_TAG, n = sizeof id;
    memset(other, 0, sizeof other);
    memset(&h, 0, sizeof h);
    memset(&id, 0, sizeof id);
    for (uint32_t i = 0; i < image; i++) other[MOD3_HEAD + i] = (uint8_t)(i * 7u + 1u);
    id.uid = uid_of("acme/OTHER");
    id.version = MOD3_IDNT_VERSION;
    memcpy(id.tag, "OTHE", 4);
    memcpy(id.vendor, "acme", 4);
    memcpy(other + at, &tag, 4);
    memcpy(other + at + 4, &n, 4);
    memcpy(other + at + 8, &id, n);
    at += 8u + n;
    h.magic = MOD3_FILE_MAGIC; h.abi = MOD3_ABI; h.engine = 22; h.host = MOD3_HOST; h.flags = MOD3_FILE_CHUNKS;
    h.image = image; h.bss = 16; h.version = 1; h.uid = id.uid; h.total = at;
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
static uint32_t slot_named(const char *name)
{
    for (uint32_t k = 0; k < NMOD; k++)
        if (mods[k].len && !strcmp(mods[k].name, name)) return k;
    return NMOD;
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

/* ---- sound: notes on parts 0 and 1 (parts whose synth is the JUNO), n samples of their sum into out */
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
    static int32_t b[CTL];
    voices_clear();
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
        memset(out + i, 0, CTL * sizeof *out);
        for (uint32_t k = 0; k < parts; k++) {
            track_render(&trk[k], b, CTL);
            for (uint32_t j = 0; j < CTL; j++) out[i + j] += b[j];
        }
    }
}
/* preset p, a chord then a melody, on part 0 (tests/outside_author_test.c's phrase, longer) */
static void phrase(uint32_t e, uint32_t p, int32_t *out, uint32_t n)
{
    static const uint8_t NOTE[6] = {48, 55, 60, 64, 67, 72};
    host_tracks_init();
    voices_clear();
    eng_state_reset();
    host_preset(&trk[0], e, p);
    for (uint32_t i = 0; i < n; i += CTL) {
        uint32_t blk = i / CTL;
        if (blk == 0)
            for (uint32_t j = 0; j < 3u; j++) trk_note_on(&trk[0], NOTE[j], 100);
        if (blk == 300u)
            for (uint32_t j = 0; j < 3u; j++) trk_note_off(&trk[0], NOTE[j]);
        if (blk >= 500u && blk < 1100u && (blk - 500u) % 100u == 0) trk_note_on(&trk[0], NOTE[(blk - 500u) / 100u], 90);
        if (blk > 500u && blk <= 1100u && (blk - 500u) % 100u == 60u) trk_note_off(&trk[0], NOTE[(blk - 500u) / 100u]);
        dither_block();
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

#define OJ_N (1400u * CTL)                          /* (about a second) */
static int32_t snd_a[OJ_N], snd_b[OJ_N];

/* the device the project goes to (.mel and "another device"): the JUNO alone, number 22 */
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
    mod_host_desc = oj_desc;
    mod_boot();
    /* ---- installed: the number through the engine map */
    install_other(0);
    boot();
    bad += check("another outside synth (acme/OTHER) asked for 22 first and has it", mods[0].engine == 22u &&
                 !strcmp(eng_name(22), "OTHER"));
    ui.msg[0] = 0;
    int rc = put(3, file, file_n);
    uint32_t e = mods[3].engine, uid = uid_of("saadet/JUNO-106");
    bad += check("JUNO-106 installs as an upload (slot 3), its code in SRAM; it asked for 22 too: the first free number, 23",
                 rc == MS_LOADED && e == 23u && eng_ok(e) && !strcmp(eng_name(e), "JUNO-106") && mod_at[e] == 4u);
    bad += check(".. who it is: CRC-32 of saadet/JUNO-106, in the device's engine map", mods[3].uid == uid &&
                 eng_map[e].uid == uid && eng_map[22].uid == uid_of("acme/OTHER"));
    bad += check(".. unsigned: the one-line warning", mods[3].sig == 2u && !strcmp(ui.msg, "UNSIGNED: JUNO-106"));
    eng_map_service(1);
    boot();
    bad += check("power-on: listed from its record, the same number, its code not loaded", mods[3].state == MS_STORED &&
                 mods[3].engine == e && mod_idle(e));
    /* ---- its presets: names, categories and patches from its file, nothing loaded */
    uint32_t names = 1, cats = 1, bytes = 1, common = 1;
    static preset_entry_t x;
    for (uint32_t k = 0; k < J106_NPRESETS; k++) {
        eng_preset_name(e, k, nm, sizeof nm);
        names &= !strcmp(nm, oj_rows[k].row.name);
        cats &= preset_cat(e, k) == (oj_rows[k].row.category ? oj_rows[k].row.category : cat_of_engine(e));
        preset_fetch_into(e, k, &x, 1);
        bytes &= x.plen == J106_BLOB && !memcmp(x.patch, oj_rows[k].patch, J106_BLOB);
        common &= !memcmp(x.pr.e, oj_rows[k].row.e, 8) && !memcmp(x.pr.env, oj_rows[k].row.env, 4) &&
                  x.pr.fenv == oj_rows[k].row.fenv && x.pr.mono == oj_rows[k].row.mono;
    }
    bad += check("129 presets (INIT, Roland's 128) listed by name from its file (PRST), the module not loaded",
                 eng_npresets(e) == J106_NPRESETS && names && mod_idle(e));
    bad += check(".. their browser categories from the file", cats && preset_cat(e, 1) == 11u && preset_cat(e, 2) == 11u);
    bad += check(".. a pick decodes each preset's exact patch (31 bytes) and common values, still not loaded",
                 bytes && common && mod_idle(e));
    /* ---- it plays */
    host_tracks_init();
    set_engine_of(&trk[0], e);                      /* (the UI's pick: into SRAM first, its schema's first preset) */
    trk[0].engine = trk[0].eng_req;
    serve();
    bad += check("a part picks it (the UI's path): its code loads, the part starts from its first program, A11 BRASS",
                 mods[3].state == MS_LOADED && !mod_idle(e) && trk[0].preset == 1u && patch_eng_of(0) == e &&
                 !memcmp(patch_of(0), oj_rows[1].patch, J106_BLOB));
    uint32_t heard = 1, same = 1, differ = 0;
    uint64_t prev = 0;
    for (uint32_t p = 0; p < J106_NPRESETS; p += 8u) {
        phrase(e, p, snd_a, OJ_N);
        phrase(e, p, snd_b, OJ_N);
        uint64_t en = energy(snd_a, OJ_N);
        heard &= en > 1000u && peak(snd_a, OJ_N) < 2000000;
        same &= !memcmp(snd_a, snd_b, sizeof snd_a);
        differ += en != prev;
        prev = en;
    }
    bad += check("it plays: every eighth preset's phrase sounds (chord, melody), within range, the same twice", heard && same &&
                 differ >= 15u);
    if (getenv("OUTSIDE_J106_RENDER")) {           /* (the phrases for Saadet's comparison: int32 per sample) */
        const char *rd = getenv("OUTSIDE_J106_RENDER");
        for (uint32_t p = 0; p < J106_NPRESETS; p++) {
            char path[512];
            phrase(e, p, snd_a, OJ_N);
            snprintf(path, sizeof path, "%s/j106-%03u.raw", rd, p);
            FILE *f = fopen(path, "wb");
            if (f) { fwrite(snd_a, sizeof *snd_a, OJ_N, f); fclose(f); }
        }
    }
    /* ---- its pages: from its schema (titles, columns, values, dim hints) */
    host_tracks_init();
    host_preset(&trk[0], e, 1);
    song.sel = 0;
    serve();
    uint32_t npg = 0, ncol = 0, texts = 1, lfo = 0, env = 0;
    char seen[400] = "";
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
    bad += check("its pages from its schema: STORE, LFO, DCO, PWM, VCF, VCF 2, ENV, VCA (the knob banks not among them)",
                 npg == 8u && !strcmp(seen, "J106 STORE|J106 LFO|J106 DCO|J106 PWM|J106 VCF|J106 VCF 2|J106 ENV|J106 VCA|") &&
                 lfo == 1u && env == 1u);
    bad += check(".. every column labelled with a value (28 columns)", ncol == 28u && texts);
    {
        sch_param_t pr;
        uint32_t place = NPAGES;
        for (uint32_t i = 0; i < NPAGES && place == NPAGES; i++)
            if (PAGE_TAB[i].scope == SC_MOD && page_at(i) != &SCH_PG_NONE && !strcmp(page_at(i)->title, "J106 PWM"))
                place = i;
        const page_t *pg = page_at(place);
        int dim_off = sch_col_dim(pg, 0), dim_on;   /* (BRASS: no pulse, PWM has no effect) */
        assert(!sch_param(e, J106_PULSE, &pr));
        (void)sch_put(&trk[0], e, patch_of(0), &pr, 1);
        dim_on = sch_col_dim(pg, 0);
        bad += check(".. the module's dim hints: PWM dim without the pulse, lit with it", dim_off && !dim_on);
        int16_t *vp;
        const page_t *dco = 0;
        for (uint32_t i = 0; i < NPAGES && !dco; i++)
            if (PAGE_TAB[i].scope == SC_MOD && page_at(i) != &SCH_PG_NONE && !strcmp(page_at(i)->title, "J106 DCO"))
                dco = page_at(i);
        const param_desc_t *d = dco ? page_desc(dco, 0, &vp) : 0;
        char val[16] = "";
        const char *unit = 0;
        if (d) param_format(d, *vp, val, &unit);
        uint8_t sw1 = patch_of(0)[J106_SW1];
        assert(!sch_param(e, J106_RANGE, &pr));
        int put4 = sch_put(&trk[0], e, patch_of(0), &pr, 2);
        bad += check(".. a switch through the module (RANGE: BRASS's 16' shown, set to 4': SW1's range bits alone)",
                     d && !strcmp(d->label, "RANGE") && !strcmp(val, "16'") && put4 &&
                     patch_of(0)[J106_SW1] == ((sw1 & ~7u) | 4u));
    }
    /* ---- SysEx in: the instrument's messages, as from USB */
    {
        j106_patch_t pa, got;
        uint8_t f[J106_FRAME], out[64];
        memcpy(&pa, oj_rows[43].patch, sizeof pa);   /* (program 42: A63 FRONTIER ORG) */
        uint32_t n = j106_patch_encode(&pa, 0, f, sizeof f);
        msx_reset();
        sx_import.count = 0;
        sx_in(f, n);
        int staged = sx_import.count == 1u && sx_import.engine == e && sx_import.size == J106_BLOB;
        memcpy(&got, sx_import.data, sizeof got);
        bad += check("a patch message (F0 41 30 00 2A ..) is offered for a user slot: its sliders, switches, program, "
                     "named PATCH A63", staged && !memcmp(got.raw, pa.raw, J106_RAW) && got.program == 42u &&
                     !memcmp(got.name, "PATCH A63   ", J106_NAME_LEN));
        ui.uslot = 10; sysex_choose();
        if (ui.confirm == CF_IMPORT) { ui.confirm = 0; sysex_commit(); }
        uint8_t slot[MOD3_PATCH_MAX];
        uint32_t sn = msx_h_slot_get(e, 10, slot);
        bad += check(".. kept in user slot 11 as the JUNO-106's patch", up_used(10) && sn == J106_BLOB &&
                     !memcmp(slot, &got, J106_BLOB));
        f[2] = 0x31u;
        sx_in(f, n);
        memcpy(&got, sx_import.data, sizeof got);
        bad += check(".. a manual-mode message (31): named MANUAL", sx_import.count == 1u &&
                     !memcmp(got.name, "MANUAL      ", J106_NAME_LEN) && !memcmp(got.raw, pa.raw, J106_RAW));
        sysex_cancel();
        uint8_t ctl[7] = {0xF0, 0x41, 0x32, 0x00, J106_VCF_FREQ, 0x21, 0xF7};
        song.sel = 0;
        sx_in(ctl, sizeof ctl);
        bad += check("a single control (F0 41 32 00 05 21: VCF FREQ) edits the selected part's patch",
                     patch_of(0)[J106_VCF_FREQ] == 0x21u && patch_eng_of(0) == e);
        uint8_t bad_f[10] = {0xF0, 0x41, 0x30, 0x00, 0x01, 2, 3, 4, 5, 0xF7};
        ui.msg[0] = 0;
        sx_in(bad_f, sizeof bad_f);
        bad += check("a short patch message: refused, said so", !sx_import.count && !strcmp(ui.msg, "INVALID JUNO-106"));
        memcpy(&pa, patch_of(0), sizeof pa);
        n = msx_dump(e, patch_of(0), MOD3_DUMP_PART, 3, out, sizeof out);
        uint32_t want = j106_patch_encode(&pa, 3, f, sizeof f);
        bad += check("the part's patch out as the instrument's patch message (channel 4)", n == J106_FRAME && n == want &&
                     !memcmp(out, f, n) && out[3] == 3u);
    }
    /* ---- a project of it: saved (FBKR), loaded, on another device */
    host_tracks_init();
    host_preset(&trk[0], e, 9);                      /* ORGAN I */
    host_preset(&trk[1], e, 66);                     /* B12 VIOLIN */
    {
        sch_param_t pr;
        assert(!sch_param(e, J106_VCF_FREQ, &pr));
        (void)sch_put(&trk[1], e, patch_of(1), &pr, 90);
        assert(!sch_param(e, J106_CHORUS, &pr));
        (void)sch_put(&trk[1], e, patch_of(1), &pr, 2);
        patch_note(1, MOD3_PATCH_EDIT, 0xFFFFu);
    }
    serve();
    uint8_t p0[J106_BLOB], p1[J106_BLOB];
    memcpy(p0, patch_of(0), sizeof p0);
    memcpy(p1, patch_of(1), sizeof p1);
    play(snd_a, OJ_N, 2);
    bad += check("two parts of it (ORGAN I, B12 VIOLIN edited: FREQ 90, CHORUS II) play", energy(snd_a, OJ_N) > 1000u);
    bad += check("the project saves as FBKR (an outside synth's patch: its end block)",
                 !project_save_as(1, "JUNO SONG") && proj_meta[1].used);
    {
        st_hdr_t h;
        int copy = st_current(st_proj_obj(1), &h);
        uint32_t n = 0;
        uint32_t m = 0;
        if (copy >= 0) {
            static uint8_t raw[BANK_STORE_SIZE];
            n = h.len;
            if (!st_read_range(st_proj_obj(1), (uint32_t)copy, 0, big, n)) save_file("j106.hpj", big, n);
            if (!pj_image(st_proj_obj(1), (uint32_t)copy, raw)) memcpy(&m, raw, 4);
        }
        bad += check(".. stored as the firmware keeps a project (written for the .mel round trip)", n > 0 && m == BANKR_MAGIC);
        save_file("j106-render.bin", snd_a, sizeof snd_a);
    }
    host_tracks_init();
    project_load(1);
    serve();
    play(snd_b, OJ_N, 2);
    bad += check(".. loaded: both parts JUNO-106, their patches byte for byte, the same samples",
                 trk[0].eng_req == e && trk[1].eng_req == e && !memcmp(patch_of(0), p0, sizeof p0) &&
                 !memcmp(patch_of(1), p1, sizeof p1) && !memcmp(snd_a, snd_b, sizeof snd_a));
    uint32_t e2 = fresh_device();
    host_tracks_init();
    project_load(1);
    serve();
    play(snd_b, OJ_N, 2);
    bad += check("another device (the JUNO-106 alone: number 22): the project's parts play it there, the same samples",
                 e2 == 22u && trk[0].eng_req == 22u && trk[1].eng_req == 22u && !memcmp(patch_of(1), p1, sizeof p1) &&
                 !memcmp(snd_a, snd_b, sizeof snd_a));
    return bad;
}

/* what the page's .mel import sent: into a device whose JUNO-106 has its own number, through the backup PUT */
static int run_mel(void)
{
    int bad = 0;
    static int32_t want[OJ_N];
    reset();
    mod_host_desc = oj_desc;
    mod_boot();
    install_other(0);
    boot();
    uint32_t e = fresh_device();                    /* (OTHER deleted: the JUNO alone, 22) */
    char path[512];
    snprintf(path, sizeof path, "%s/out-j106.bin", dir);
    uint32_t n = load_file(path, big, sizeof big);
    snprintf(path, sizeof path, "%s/j106-render.bin", dir);
    uint32_t nr = load_file(path, (uint8_t *)want, sizeof want);
    bad += check(".mel: the project the page sent (packed) goes in through the backup PUT",
                 n > PJ_HEAD && *(uint32_t *)(void *)big == PZ_MAGIC && put_all(5, big, n, st_crc32(big, n)) == 0);   /* (project 4: the host mirrors four slots) */
    host_tracks_init();
    project_load(3);
    serve();
    play(snd_a, OJ_N, 2);
    bad += check(".mel: .. loads: both parts JUNO-106 (22 here, 23 where it was made), the same samples",
                 nr == sizeof want && e == 22u && trk[0].eng_req == 22u && trk[1].eng_req == 22u &&
                 !memcmp(snd_a, want, sizeof want) && !strcmp(proj_meta[3].name, "JUNO SONG"));
    return bad;
}

int main(int argc, char **argv)
{
    int mel = argc == 4 && !strcmp(argv[1], "--mel");
    if (argc != 3 + mel || !(file_n = load_file(argv[1 + mel], file, sizeof file))) {
        printf("outside_j106_test [--mel] J106.mmod DIR\n");
        return 2;
    }
    dir = argv[2 + mel];
    mkdir(dir, 0755);
    int bad = mel ? run_mel() : run();
    printf("%s\n", bad ? "OUTSIDE JUNO-106 TEST FAILED" : mel ? "outside JUNO-106 .mel round trip passed" :
                                                             "outside JUNO-106 test passed");
    return bad ? 1 : 0;
}
