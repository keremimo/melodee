/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Synth modules (src/eng_module.c, src/module_store.c, src/module_abi.h, src/module_native.h): TRIO built as a module
 * renders bit for bit what the built-in TRIO rendered; SID, DRUM, CZ-1 and FM6 as native modules (firmware/modules/sid,
 * drum, cz, fm6: their own sources, compiled here for the host and linked beside this test) render bit for bit what
 * the built-in engines render (this build, MELODEE_*_BUILTIN 1) and what they rendered before they left the firmware
 * (MP_GOLDEN, tests/module_phrases.h: every preset, POLY and LEGATO, the C64 models, ENV3, every kit, every CZ-1 tone
 * edited as it plays with the matrix's ENV, every FM6 voice with every algorithm, ENGINE and controller, a DX7 bank by
 * SysEx, on CPU0 and CPU1), a native
 * file longer than a record in two, its layout key checked; module files into the pool's slots (head, CRC, address words moved to the block),
 * loaded once at commit, listed at power-on without loading, loaded when picked or used and out of SRAM again when
 * idle, a part kept on its engine while its module is on its way, listed after the built-in engines, refused when
 * bad or when their number is taken, left out after a boot that died early, deleted; a track keeps a module's number
 * when it goes. With
 * build/modules/trio.mmod (tools/mod_build.py) the real pi32v2 file is relocated too and its descriptor read back.
 *   module_test [build/modules/trio.mmod] */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"
#include "../firmware/src/fm6_store.c"         /* (DX7 SysEx in: the FM6 bank phrases) */
#include "../firmware/src/module_store.c"
/* (engines.c has the built-in TRIO, ENG_TRIO: the reference) */
#define MOD_NATIVE
#include "../firmware/modules/trio/trio.c"     /* the modules, built for the host */
#include "../firmware/modules/wheel/wheel.c"
#include "module_phrases.h"
extern const mod_native_t mod_native_sid, mod_native_drum, mod_native_cz, mod_native_fm6;   /* (firmware/modules/sid/
                                                                             * sid.c, drum/drum.c, cz/cz.c, fm6/fm6.c:
                                                                             * host-built) */
static const mod_native_t *nmods(uint32_t e)
{
    return e == 3u ? &mod_native_sid : e == ENGI_DRUM ? &mod_native_drum : e == ENGI_CZ ? &mod_native_cz :
           e == ENGI_FM6 ? &mod_native_fm6 : 0;
}
/* a native module file for the host loader (its image a pattern; the host plays nmods()); key: its layout key */
static uint32_t make_nfile(uint8_t *b, uint32_t engine, const char *name, uint32_t image, uint32_t key, uint32_t npresets)
{
    mod_head_t h;
    memset(&h, 0, sizeof h);
    h.f.magic = MOD_FILE_MAGIC; h.f.abi = MOD_ABI_NATIVE; h.f.engine = (uint8_t)engine;
    h.f.image = image; h.f.bss = 128; h.f.nrel = 12; h.f.version = 1; h.f.npresets = (uint8_t)npresets; h.key = key;
    h.f.total = MOD_NHEAD + image + 4u * h.f.nrel;
    strncpy(h.f.name, name, sizeof h.f.name);
    for (uint32_t i = 0; i < image; i++) b[MOD_NHEAD + i] = (uint8_t)(i * 13u + 5u);
    for (uint32_t k = 0; k < h.f.nrel; k++) {
        uint32_t off = 40u + k * 64u;
        memcpy(b + MOD_NHEAD + image + 4u * k, &off, 4);
    }
    h.f.crc = st_crc32(b + MOD_NHEAD, image + 4u * h.f.nrel);
    memcpy(b, &h, MOD_NHEAD);
    return h.f.total;
}
static int32_t mp_mod[MP_NB * CTL];

static mod_desc_t trio22;                       /* the same module as a new engine (22) */
static const mod_desc_t *native(uint32_t e) { return e == 6u ? &mod_trio : e == 7u ? &mod_wheel : e == 22u ? &trio22 : 0; }

/* a module file for the host loader (the image's bytes are a pattern; the host plays native()) */
static uint32_t make_file(uint8_t *b, uint32_t engine, const char *name, uint32_t image, uint32_t nrel)
{
    mod_file_t f;
    memset(&f, 0, sizeof f);
    f.magic = MOD_FILE_MAGIC; f.abi = MOD_ABI; f.engine = (uint8_t)engine;
    f.image = image; f.bss = 64; f.nrel = nrel; f.version = 3; f.npresets = 5;
    f.total = (uint32_t)sizeof f + image + 4u * nrel;
    strncpy(f.name, name, sizeof f.name);
    for (uint32_t i = 0; i < image; i++) b[sizeof f + i] = (uint8_t)(i * 7u + 1u);
    for (uint32_t k = 0; k < nrel; k++) {
        uint32_t off = 6u + k * 10u;               /* (unaligned words, as a 6-byte instruction's immediate) */
        memcpy(b + sizeof f + image + 4u * k, &off, 4);
    }
    f.crc = st_crc32(b + sizeof f, image + 4u * nrel);
    memcpy(b, &f, sizeof f);
    return f.total;
}
static int put(uint32_t k, const uint8_t *b, uint32_t n)
{
    int rc = mod_put_begin(k, n);
    for (uint32_t o = 0; !rc && o < n; o += 200u)
        rc = mod_put_write(k, o, b + o, n - o < 200u ? n - o : 200u);
    return rc ? rc : mod_put_commit(k);
}
static int listed(uint32_t e)
{
    for (uint32_t n = 0; n < eng_nshown; n++)
        if (eng_vis(n) == e) return 1;
    return 0;
}

/* a phrase on part 1 (TRIO preset pi, POLY or LEGATO): notes on and off over 200 blocks, every block rendered */
#define NB 200u
static int32_t phrase_a[NB * CTL], phrase_b[NB * CTL];
static uint8_t nor_kept[sizeof nor];
static void phrase(int32_t *out, uint32_t e, uint32_t pi, uint32_t mono)
{
    memcpy(nor_kept, nor, sizeof nor);             /* (reset() erases the flash: the stored modules stay) */
    reset();
    memcpy(nor, nor_kept, sizeof nor);
    vage = 0;
    dith_r = DITH_SEED;
    host_preset(&inst, e, pi);
    inst.p[P_VOICE] = (int16_t)(mono ? V_LEGATO : V_POLY);
    for (uint32_t b = 0; b < NB; b++) {
        if (b == 4) trk_note_on(&inst, 48, 100);
        if (b == 40) trk_note_on(&inst, 55, 80);
        if (b == 60) trk_note_off(&inst, 48);
        if (b == 70) trk_note_on(&inst, 64, 127);
        if (b == 71) trk_note_on(&inst, 67, 60);
        if (b == 120) { trk_note_off(&inst, 55); trk_note_off(&inst, 64); trk_note_off(&inst, 67); }
        if (b == 130) inst.p[P_E0 + 5] = 30;      /* CUT moves */
        if (b == 140) trk_note_on(&inst, 36, 90);
        if (b == 180) trk_note_off(&inst, 36);
        dither_block();
        track_render(&inst, out + b * CTL, CTL);
    }
}

int main(int argc, char **argv)
{
    static uint8_t file[32768], file2[32768];
    int bad = 0;
    reset();
    mod_host_native = native;
    trio22 = mod_trio;
    trio22.engine = 22;
    trio22.name = "TRIO2";

    bad += check("modules: numbers past the built-in engines, none loaded at first",
                 NENGINES == 30 && ENGI_MOD0 == 22 && !eng_ok(6) && !eng_ok(22) && eng_nshown == NENG_SHOWN &&
                 ENGINES[6] == &ENG_RETIRED && eng_extra_retired(22) && eng_sound_idx(22) == 22);

    /* a file into slot 0: published, loaded, playing as engine 6 */
    uint32_t n = make_file(file, 6, "TRIO", 1024, 20);
    int rc = put(0, file, n);
    uint8_t *blk = resource[RES_MODULE0].ptr;
    int moved = blk != 0;
    for (uint32_t k = 0; moved && k < 20u; k++) {   /* every listed word: the block's address added */
        uint32_t off = 6u + k * 10u, was, now;
        memcpy(&was, file + sizeof(mod_file_t) + off, 4);
        memcpy(&now, blk + off, 4);
        moved = now == was + (uint32_t)(uintptr_t)blk;
    }
    bad += check("module file: committed, loaded into the arena, address words moved by the block's address",
                 rc == MS_LOADED && mods[0].state == MS_LOADED && moved && st_exists(ST_MFILE0));
    bad += check("loaded module: engine 6 again (TRIO's old number), named, its presets, browsable after the others",
                 eng_ok(6) && ENGINES[6] != &ENG_RETIRED && !strcmp(ENGINES[6]->name, "TRIO") &&
                 ENGINES[6]->npresets == 5 && !strcmp(ENGINES[6]->presets[3].name, "RING BELL") &&
                 eng_nshown == NENG_SHOWN + 1 && eng_vis(NENG_SHOWN) == 6 && listed(6) &&
                 ENGINES[6]->knob[0] == P_E0 + 5 && !strcmp(ENGINES[6]->edit[0].names[9], "SYNC") &&
                 mods[0].version == 3 && !strcmp(mods[0].name, "TRIO"));

    /* bit for bit: the module against the built-in engine, every preset, POLY and LEGATO */
    n = make_file(file2, 7, "WHEEL", 1024, 8);
    rc = put(2, file2, n);
    bad += check("WHEEL module (engine 7, block + part memory) loaded beside TRIO",
                 rc == MS_LOADED && eng_ok(7) && ENGINES[7]->block && !strcmp(ENGINES[7]->name, "WHEEL") &&
                 eng_state_size(7) == sizeof(drw_part_t));
    int same = 1, heard = 1;
    for (uint32_t e = 6; e <= 7u; e++)
    for (uint32_t pi = 0; pi < 5u; pi++)
        for (uint32_t mono = 0; mono < 2u; mono++) {
            const engine_t *mod_eng = ENGINES[e];
            uint8_t at = mod_at[e];
            phrase(phrase_a, e, pi, mono);
            ENGINES[e] = e == 6u ? &ENG_TRIO : &ENG_WHEEL; mod_at[e] = 0;   /* the same phrase, built in */
            phrase(phrase_b, e, pi, mono);
            ENGINES[e] = mod_eng; mod_at[e] = at;
            int64_t e = 0;
            for (uint32_t i = 0; i < NB * CTL; i++) e += phrase_a[i] < 0 ? -phrase_a[i] : phrase_a[i];
            heard &= e > 1000000;
            if (memcmp(phrase_a, phrase_b, sizeof phrase_a)) {
                same = 0;
                for (uint32_t i = 0; i < NB * CTL; i++)
                    if (phrase_a[i] != phrase_b[i]) {
                        printf("  engine %u preset %u mono %u: sample %u module %d built-in %d\n", e, pi, mono, i, phrase_a[i],
                               phrase_b[i]);
                        break;
                    }
            }
        }
    bad += check("TRIO and WHEEL modules render bit for bit what the built-in engines render (5 presets x POLY/LEGATO)",
                 same && heard);
    (void)mod_delete(2);

    /* refusals */
    memcpy(file2, file, n);
    file2[0] ^= 1u;                                 /* not a module file: never published */
    rc = put(1, file2, n);
    bad += check("not a module file: refused before it is published", rc == -2 && !st_exists(ST_MFILE0 + 1));
    n = make_file(file2, ENGI_MIDI, "MIDI?", 1024, 4);   /* a built-in engine's number (the MIDI Track's) */
    bad += check("a built-in engine's number: refused", put(1, file2, n) == -2);
    n = make_file(file2, 6, "TRIO B", 1024, 4);      /* the same number as slot 0 */
    bad += check("a number another slot has: refused", put(1, file2, n) == -3 && !st_exists(ST_MFILE0 + 1));
    n = make_file(file2, 22, "TRIO2", 1024, 4);
    file2[sizeof(mod_file_t) + 100] ^= 0x40u;        /* the body damaged after its CRC */
    rc = put(1, file2, n);
    bad += check("a damaged body (CRC): stored but not loaded, the arena block given back",
                 rc == MS_BAD && mods[1].state == MS_BAD && !resource[RES_MODULE0 + 1].size && !eng_ok(22));
    n = make_file(file2, 22, "TRIO2", 1024, 4);
    rc = put(1, file2, n);
    bad += check("a new number (22): loaded beside TRIO, both browsable",
                 rc == MS_LOADED && eng_ok(22) && listed(22) && listed(6) && eng_nshown == NENG_SHOWN + 2 &&
                 !strcmp(ENGINES[22]->name, "TRIO2"));

    /* power-on: every slot again; after a boot that died early, none */
    for (uint32_t k = 0; k < NMOD; k++) mod_unregister(k, 0), memset(&mods[k], 0, sizeof mods[k]);
    bad += check("unloaded: the engines retired again, the browser without them",
                 !eng_ok(6) && !eng_ok(22) && eng_nshown == NENG_SHOWN && ENGINES[22] == &ENG_RETIRED);
    bootguard.failed = 0;
    mod_boot();
    bad += check("power-on: both stored modules listed by name and preset count, nothing in SRAM",
                 eng_ok(6) && eng_ok(22) && mods[0].state == MS_STORED && mods[1].state == MS_STORED && !mod_skipped &&
                 ENGINES[6] == &ENG_MOD_IDLE && !resource[RES_MODULE0].size && !resource[RES_MODULE0 + 1].size &&
                 !strcmp(eng_name(22), "TRIO2") && eng_npresets(6) == 5 && listed(6) && listed(22) &&
                 eng_extra_retired(6) && !mod_at[6]);

    /* lazy: a part told to switch (as from an interrupt) keeps its engine until the main loop has the module */
    host_preset(&trk[1], ENGI_PROPHET, 1);
    trk[1].eng_req = 22;
    engine_block(&trk[1]);                          /* (the audio's switch: seq.c, every block) */
    int kept = trk[1].engine == ENGI_PROPHET && mod_waiting(22);
    fm1_ms = 1000;
    mod_service(fm1_ms);
    engine_block(&trk[1]);
    bad += check("lazy: a part switching to a stored module keeps its engine until mod_service loads it, then switches",
                 kept && mods[1].state == MS_LOADED && trk[1].engine == 22 && ENGINES[22] != &ENG_MOD_IDLE &&
                 mods[0].state == MS_STORED);
    /* picking an engine loads it on the spot, with its preset */
    set_engine_of(&trk[2], 6);
    bad += check("lazy: picking a stored module loads it at once and applies its first preset",
                 mods[0].state == MS_LOADED && trk[2].eng_req == 6 && trk[2].p[P_E0] == mod_trio.presets[0].e[0] &&
                 resource[RES_MODULE0].size);
    /* nothing on it any more: out of SRAM after MOD_IDLE_MS, listed still */
    engine_block(&trk[2]);
    int on6 = trk[2].engine == 6;
    set_engine_of(&trk[2], ENGI_FM6);
    engine_block(&trk[2]);
    mod_service(fm1_ms += 100u);
    int grace = mods[0].state == MS_LOADED;
    mod_service(fm1_ms += MOD_IDLE_MS);
    bad += check("lazy: an unused module stays MOD_IDLE_MS, then leaves SRAM, its engine still listed",
                 on6 && grace && mods[0].state == MS_STORED && !resource[RES_MODULE0].size && ENGINES[6] == &ENG_MOD_IDLE &&
                 eng_ok(6) && listed(6) && mods[1].state == MS_LOADED);
    /* the browser lists a stored module's presets from its head; naming one (ui_graph.c entry_label) loads it */
    {
        int in_list = entry_ok(6, 4) && !entry_ok(6, 5) && mods[0].state == MS_STORED;
        mod_want(6);
        bad += check("lazy: the browser lists a stored module's presets unloaded; wanting one loads it",
                     in_list && mods[0].state == MS_LOADED && !strcmp(eng_preset(ENGINES[6], 3)->name, "RING BELL"));
    }
    set_engine_of(&trk[1], ENGI_FM6);
    engine_block(&trk[1]);
    for (uint32_t k = 0; k < NMOD; k++) mod_unregister(k, 0), memset(&mods[k], 0, sizeof mods[k]);
    bootguard.failed = 1;
    mod_boot();
    bad += check("power-on after a boot that died early: modules listed but left out",
                 mod_skipped && !eng_ok(6) && !eng_ok(22) && mods[0].state == MS_SKIPPED && mods[0].engine == 6);
    bootguard.failed = 0;
    mod_skipped = 0;
    mod_boot();

    /* a track on a module whose module goes: it keeps the number, silent */
    mod_want(22);
    host_preset(&trk[1], 22, 2);
    trk_note_on(&trk[1], 60, 100);
    int32_t buf[CTL];
    track_render(&trk[1], buf, CTL);
    rc = mod_delete(1);
    bad += check("delete: record gone, engine retired, its track keeps number 22 and goes silent",
                 rc == 0 && !st_exists(ST_MFILE0 + 1) && !eng_ok(22) && trk[1].engine == 22 &&
                 !trk[1].v[0].active && !resource[RES_ENGINE0 + 1].size && mods[1].state == MS_EMPTY);
    track_render(&trk[1], buf, CTL);
    int silent = 1;
    for (uint32_t i = 0; i < CTL; i++) silent &= buf[i] == 0;
    bad += check("its track renders silence", silent);

    /* native modules: SID, DRUM, CZ-1 and FM6, the engines themselves (module_native.h) */
    {
        static uint8_t nf[4][MOD_FILE_MAX];
        const engine_t *sid_in = ENGINES[3], *drum_in = ENGINES[ENGI_DRUM], *cz_in = ENGINES[ENGI_CZ],
                       *fm6_in = ENGINES[ENGI_FM6];      /* (built in on the host, or retired) */
        for (uint32_t k = 0; k < NMOD; k++) mod_delete(k);
        mod_host_nmod = nmods;
        ENGINES[3] = ENGINES[ENGI_DRUM] = ENGINES[ENGI_CZ] = ENGINES[ENGI_FM6] = &ENG_RETIRED;   /* (the numbers free
                                                                             * for the modules) */
        uint32_t ns = make_nfile(nf[0], 3, "SID", 12000, MOD_NATIVE_KEY, MP_SID_PRESETS);
        mod_file_t sid_head;
        memcpy(&sid_head, nf[0], sizeof sid_head);
        uint32_t nd = make_nfile(nf[1], ENGI_DRUM, "DRUM", 48000, MOD_NATIVE_KEY, MP_DRUM_PRESETS);
        uint32_t nc = make_nfile(nf[2], ENGI_CZ, "CZ-1", 9000, MOD_NATIVE_KEY, MP_CZ_PRESETS);
        uint32_t nm = make_nfile(nf[3], ENGI_FM6, "FM6", 20000, MOD_NATIVE_KEY, MP_FM6_PRESETS);
        int rs = put(4, nf[0], ns), rd = put(5, nf[1], nd), rc7 = put(7, nf[2], nc), rf3 = put(3, nf[3], nm);
        if (rs != MS_LOADED || rd != MS_LOADED || rc7 != MS_LOADED || rf3 != MS_LOADED)
            printf("  put: SID %d DRUM %d CZ-1 %d FM6 %d\n", rs, rd, rc7, rf3);
        bad += check("native SID and DRUM files: committed and loaded, DRUM in two records (longer than one)",
                     rs == MS_LOADED && rd == MS_LOADED && mods[4].abi == MOD_ABI_NATIVE && mods[5].len == nd &&
                     nd > ST_PROJECT_MAX && st_exists(ST_MFILE0 + 5) && st_exists(ST_MXFILE0 + 5) &&
                     !st_exists(ST_MXFILE0 + 4));
        bad += check("native modules play as engines 3 and 10 with their own engine_t (no copy, no ABI 1 glue)",
                     ENGINES[3] == mod_native_sid.eng && ENGINES[ENGI_DRUM] == mod_native_drum.eng &&
                     !strcmp(eng_name(3), "SID") && !strcmp(eng_name(ENGI_DRUM), "DRUM") && eng_ok(3) &&
                     eng_ok(ENGI_DRUM) && ENGINES[3]->wclass == 4 && ENGINES[ENGI_DRUM]->wclass == 5 &&
                     ENGINES[3]->env_q15 && ENGINES[3]->chord && ENGINES[ENGI_DRUM]->reuse &&
                     ENGINES[ENGI_DRUM]->oneshot && ENGINES[3]->npresets == MP_SID_PRESETS &&
                     ENGINES[ENGI_DRUM]->npresets == MP_DRUM_PRESETS && eng_state_size(3) == ENGINES[3]->state(0) &&
                     listed(3) && listed(ENGI_DRUM));
        bad += check("CZ-1 as a native module: engine 15 with its own engine_t (DCA as the matrix's ENV, CPU1, presets "
                     "made by the firmware from Casio's tones)",
                     rc7 == MS_LOADED && ENGINES[ENGI_CZ] == mod_native_cz.eng && !strcmp(eng_name(ENGI_CZ), "CZ-1") &&
                     eng_ok(ENGI_CZ) && ENGINES[ENGI_CZ]->wclass == 2 && ENGINES[ENGI_CZ]->menv &&
                     ENGINES[ENGI_CZ]->batch && ENGINES[ENGI_CZ]->npresets == MP_CZ_PRESETS &&
                     eng_state_size(ENGI_CZ) == ENGINES[ENGI_CZ]->state(0) && listed(ENGI_CZ) &&
                     !strcmp(eng_preset(ENGINES[ENGI_CZ], 0)->name, "INIT TONE") &&
                     !strcmp(eng_preset(ENGINES[ENGI_CZ], 30)->name, "FUNKY CLAV 1"));
        bad += check("FM6 as a native module: engine 12 with its own engine_t (its envelopes, Dexed's voices, DC filter, "
                     "its own CPU1 jobs, the reserve), its presets; the patches the firmware's",
                     rf3 == MS_LOADED && ENGINES[ENGI_FM6] == mod_native_fm6.eng && !strcmp(eng_name(ENGI_FM6), "FM6") &&
                     eng_ok(ENGI_FM6) && ENGINES[ENGI_FM6]->wclass == 3 && ENGINES[ENGI_FM6]->ownenv &&
                     ENGINES[ENGI_FM6]->alloc && ENGINES[ENGI_FM6]->legato && ENGINES[ENGI_FM6]->mono_key &&
                     ENGINES[ENGI_FM6]->post && ENGINES[ENGI_FM6]->block && ENGINES[ENGI_FM6]->reserve &&
                     ENGINES[ENGI_FM6]->poly == NVOICE && ENGINES[ENGI_FM6]->npresets == MP_FM6_PRESETS &&
                     eng_state_size(ENGI_FM6) == ENGINES[ENGI_FM6]->state(0) && listed(ENGI_FM6) &&
                     eng_rank(ENGI_FM6) == 1 && !strcmp(eng_preset(ENGINES[ENGI_FM6], 10)->name, "SOLID BASS") &&
                     !strcmp(eng_preset(ENGINES[ENGI_FM6], 24)->name, "INIT VOICE"));
        /* every phrase: the module against the golden render, and (built in here) against the engine sample by sample */
        uint32_t golden = 0, same = 1, heard = 1, first = MP_COUNT;
        for (uint32_t k = 0; k < MP_COUNT; k++) {
            const char *w; uint32_t a, b, h = mp_run(k, &w, &a, &b);
            int64_t e = 0;
            for (uint32_t i = 0; i < MP_NB * CTL; i++) e += mp_out[i] < 0 ? -mp_out[i] : mp_out[i];
            heard &= e > (k >= MP_CZ0 ? 10000 : 100000) || !strncmp(w, "DRUM KIT", 8) && a >= 9u;   /* (the user kits:
                                                         * none on the host; CZ-1's slow tones: quiet in a phrase) */
            if (k >= MP_FM60 && e <= 100000) printf("  phrase %u %s %u %u: quiet (%lld)\n", k, w, a, b, (long long)e);
            golden += h == MP_GOLDEN[k];
            if (h != MP_GOLDEN[k] && first == MP_COUNT) { first = k; printf("  phrase %u %s %u %u: %08x, golden %08x\n", k, w, a, b, h, MP_GOLDEN[k]); }
            if (sid_in != &ENG_RETIRED) {
                const engine_t *ms = ENGINES[3], *md = ENGINES[ENGI_DRUM], *mc = ENGINES[ENGI_CZ], *mf = ENGINES[ENGI_FM6];
                uint8_t as = mod_at[3], ad = mod_at[ENGI_DRUM], ac = mod_at[ENGI_CZ], af = mod_at[ENGI_FM6];
                memcpy(mp_mod, mp_out, sizeof mp_mod);
                ENGINES[3] = sid_in; ENGINES[ENGI_DRUM] = drum_in; ENGINES[ENGI_CZ] = cz_in; ENGINES[ENGI_FM6] = fm6_in;
                mod_at[3] = mod_at[ENGI_DRUM] = mod_at[ENGI_CZ] = mod_at[ENGI_FM6] = 0;
                (void)mp_run(k, &w, &a, &b);
                ENGINES[3] = ms; ENGINES[ENGI_DRUM] = md; ENGINES[ENGI_CZ] = mc; ENGINES[ENGI_FM6] = mf;
                mod_at[3] = as; mod_at[ENGI_DRUM] = ad; mod_at[ENGI_CZ] = ac; mod_at[ENGI_FM6] = af;
                for (uint32_t i = 0; i < MP_NB * CTL; i++)
                    if (mp_mod[i] != mp_out[i]) {
                        if (same) printf("  phrase %u %s %u %u: sample %u module %d built-in %d\n", k, w, a, b, i, mp_mod[i], mp_out[i]);
                        same = 0;
                        break;
                    }
            }
        }
        printf("persist: %u phrases, %u as golden%s\n", MP_COUNT, golden, sid_in != &ENG_RETIRED ? ", each against the built-in engine" : "");
        bad += check("SID, DRUM, CZ-1 and FM6 modules render what the built-in engines rendered (every preset, POLY / "
                     "LEGATO, C64, ENV3, every kit, every CZ-1 tone edited as it plays, every FM6 voice, algorithm, ENGINE "
                     "and controller, a DX7 bank by SysEx, CPU0 and CPU1)",
                     golden == MP_COUNT && same && heard);
        /* the layout key: a module built for another firmware is refused before it is published */
        uint32_t nk = make_nfile(nf[0], 3, "SID", 12000, MOD_NATIVE_KEY + 1u, MP_SID_PRESETS);
        int rk = put(6, nf[0], nk);
        bad += check("a native module of another firmware (layout key): refused, not published",
                     rk == -2 && !st_exists(ST_MFILE0 + 6));
        /* .. and one stored before the firmware changed (a record already there): known by engine and name, not
         * played, not counted as the engine's module; the installer (web/fm1mods.js) replaces it in its slot */
        int shipped = SHIPPED_OK();
        int saved = !st_project_save(ST_MFILE0 + 6, nf[0], nk);
        int rs6 = mod_catalog(6);
        bad += check("a stored native module of another firmware: MS_STALE, its engine and name kept, not listed",
                     saved && rs6 == MS_STALE && mods[6].engine == 3 && !strcmp(mods[6].name, "SID") &&
                     mods[6].key == MOD_NATIVE_KEY + 1u && mod_slot_of(3) == 4 && ENGINES[3] == mod_native_sid.eng);
        bad += check("the slots report what the installer compares: each file's CRC and layout key (editor command 84 op 5)",
                     mods[4].crc == sid_head.crc && mods[4].len == sid_head.total &&
                     mods[5].key == MOD_NATIVE_KEY && mods[4].key == MOD_NATIVE_KEY && mods[5].abi == MOD_ABI_NATIVE);
        (void)mod_delete(6);
        bad += check("SID, DRUM, CZ-1 and FM6 on the device: the start asks for no setup (main.c, ui_boot.c)", shipped);
        /* power-on: listed from their heads, nothing in SRAM; wanting DRUM reads its two records */
        for (uint32_t k = 0; k < NMOD; k++) mod_unregister(k, 0), memset(&mods[k], 0, sizeof mods[k]);
        mod_boot();
        int idle = mods[4].state == MS_STORED && mods[5].state == MS_STORED && ENGINES[ENGI_DRUM] == &ENG_MOD_IDLE &&
                   eng_npresets(ENGI_DRUM) == 6 && !resource[RES_MODULE0 + 5].size;
        mod_want(ENGI_DRUM);
        bad += check("native modules at power-on: listed unloaded; DRUM loaded from its two records when wanted",
                     idle && mods[5].state == MS_LOADED && ENGINES[ENGI_DRUM] == mod_native_drum.eng &&
                     mods[4].state == MS_STORED);
        bad += check("CZ-1 at power-on: listed unloaded; without its module the start asks for setup, unless built in",
                     mods[7].state == MS_STORED && ENGINES[ENGI_CZ] == &ENG_MOD_IDLE && eng_npresets(ENGI_CZ) == 65 &&
                     SHIPPED_OK() && !mod_delete(7) && ENGINES[ENGI_CZ] == &ENG_RETIRED &&
                     SHIPPED_OK() == MELODEE_CZ_BUILTIN);
        /* FM6, the firmware's fallback engine: at power-on listed from its head; without its module retired as any
         * other (its sounds keep the number, silent; a retired sound's place: FM6's, then the first engine's) */
        int fm6_idle = mods[3].state == MS_STORED && ENGINES[ENGI_FM6] == &ENG_MOD_IDLE &&
                       eng_npresets(ENGI_FM6) == MP_FM6_PRESETS && eng_ok(ENGI_FM6) && SHIPPED_OK() == MELODEE_CZ_BUILTIN;
        mod_want(ENGI_FM6);
        int fm6_back = mods[3].state == MS_LOADED && ENGINES[ENGI_FM6] == mod_native_fm6.eng && eng_rank(4) == 1;
        host_preset(&trk[1], ENGI_FM6, 10);
        trk_note_on(&trk[1], 48, 100);
        int32_t fb[CTL];
        uint32_t ok_gone = !mod_delete(3) && ENGINES[ENGI_FM6] == &ENG_RETIRED && trk[1].engine == ENGI_FM6 &&
                           eng_sound_idx(ENGI_FM6) == ENGI_FM6 && eng_sound_idx(4) == ENGI_FM6;
        track_render(&trk[1], fb, CTL);
        for (uint32_t i = 0; i < CTL; i++) ok_gone &= MELODEE_FM6_BUILTIN || fb[i] == 0;
        bad += check("FM6 at power-on: listed unloaded, loaded when wanted; without its module its tracks keep FM6 and go "
                     "silent, a retired sound's place is FM6's (else the first engine's), the start asks for setup",
                     fm6_idle && fm6_back && ok_gone &&
                     (MELODEE_FM6_BUILTIN || (!eng_ok(ENGI_FM6) && eng_rank(4) == 0 && !listed(ENGI_FM6))) &&
                     SHIPPED_OK() == (MELODEE_CZ_BUILTIN && MELODEE_FM6_BUILTIN));
        host_preset(&trk[1], ENGI_CZ, 0);
        int rdel = mod_delete(5);
        bad += check("delete: both of a long file's records gone, DRUM retired, its tracks keep the number",
                     !rdel && !st_exists(ST_MFILE0 + 5) && !st_exists(ST_MXFILE0 + 5) &&
                     ENGINES[ENGI_DRUM] == &ENG_RETIRED && (MELODEE_DRUM_BUILTIN || !eng_ok(ENGI_DRUM)));
        bad += check("without DRUM's module the start asks for setup (\"Synths missing\"), unless DRUM is built in",
                     SHIPPED_OK() == MELODEE_DRUM_BUILTIN);
        for (uint32_t k = 0; k < NMOD; k++) mod_delete(k);
        ENGINES[3] = sid_in; ENGINES[ENGI_DRUM] = drum_in; ENGINES[ENGI_CZ] = cz_in; ENGINES[ENGI_FM6] = fm6_in;
    }

    /* the real module file (pi32v2): relocated at the block, its descriptor read back through the moved words */
    FILE *fp = fopen(argc > 1 ? argv[1] : "build/modules/trio.mmod", "rb");
    if (fp) {
        n = (uint32_t)fread(file, 1, sizeof file, fp);
        fclose(fp);
        mod_file_t f;
        memcpy(&f, file, sizeof f);
        mod_delete(0);
        rc = put(0, file, n);
        blk = resource[RES_MODULE0].ptr;
        uint32_t base = (uint32_t)(uintptr_t)blk, w[64], ok = rc == MS_LOADED && blk;
        if (ok) {
            memcpy(w, blk + f.desc, sizeof w);
            const char *name = (const char *)blk + (w[2] - base), *label = (const char *)blk + (w[5] - base);
            ok = w[0] == MOD_DESC_MAGIC && w[2] - base < f.image && !strcmp(name, "TRIO") && !strcmp(label, "WAVE") &&
                 w[51] - base < f.image && w[52] - base < f.image && w[50] - base >= f.image &&
                 w[50] - base < f.image + f.bss && blk[f.desc + 191] == 6;
        }
        bad += check("pi32v2 TRIO file: CRC, every address word moved, descriptor and strings found through them", ok);
    } else
        printf("persist: (no build/modules/trio.mmod: tools/mod_build.py firmware/modules/trio/trio.c)\n");

    printf("Module test %s\n", bad ? "FAILED" : "passed");
    return !!bad;
}
