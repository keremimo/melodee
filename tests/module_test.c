/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Synth modules (src/eng_module.c, src/module_store.c, src/module_abi.h): TRIO built as a module renders bit for bit
 * what the built-in TRIO rendered; module files into the pool's slots (head, CRC, address words moved to the block),
 * loaded once at commit, listed at power-on without loading, loaded when picked or used and out of SRAM again when
 * idle, a part kept on its engine while its module is on its way, listed after the built-in engines, refused when
 * bad or when their number is taken, left out after a boot that died early, deleted; a track keeps a module's number
 * when it goes. With
 * build/modules/trio.mmod (tools/mod_build.py) the real pi32v2 file is relocated too and its descriptor read back.
 *   module_test [build/modules/trio.mmod] */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"
#include "../firmware/src/module_store.c"
/* (engines.c has the built-in TRIO, ENG_TRIO: the reference) */
#define MOD_NATIVE
#include "../firmware/modules/trio/trio.c"     /* the modules, built for the host */
#include "../firmware/modules/wheel/wheel.c"

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
    n = make_file(file2, 12, "FM6?", 1024, 4);       /* a built-in engine's number */
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
