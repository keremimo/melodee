/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Synth modules, ABI 3 (firmware/modules/sdk/mod_abi3.h; src/eng_module.c, src/module_store.c): every module is a
 * file the firmware loads, binds to its host table and plays through the frozen views of its own track_t and voice_t.
 * The modules here are firmware/modules/<name>/<name>.c built for the host ONCE (tests/run_tests.sh: objects linked
 * beside this test; the same objects in every configuration, also a firmware whose private track_t / voice_t fields
 * are padded and reordered, MELODEE_ABI_SHUFFLE): SID, DRUM, CZ-1, FM6 and the Prophet render bit for bit what the
 * built-in engines render (this build, MELODEE_*_BUILTIN 1) and what they rendered before they left the firmware
 * (MP_GOLDEN, tests/module_phrases.h: every preset, POLY and LEGATO, the C64 models, ENV3, every kit, every CZ-1 tone
 * edited as it plays with the matrix's ENV, every FM6 voice with every algorithm, ENGINE and controller, a DX7 bank by
 * SysEx, every Prophet program in POLY and UNISON edited as it plays and bent over its BEND range, on CPU0 and CPU1);
 * TRIO and WHEEL, rewritten on the contract alone (mod_sdk.h), render what the built-in references and their ABI 1
 * modules rendered. Module files into the pool's slots (head, CRC, address words moved to the block), loaded once at
 * commit, listed at power-on without loading, loaded when picked or used and out of SRAM again when idle, a part kept
 * on its engine while its module is on its way, listed after the built-in engines; refused when bad, when their number
 * is taken, when they are an older firmware's (ABI 1, 2, another legacy key: MS_STALE) or need a newer host table
 * (MS_NEWER); left out after a boot that died early, deleted; a track keeps a module's number when it goes. The
 * capabilities that replaced the engine-number checks (bend range, wheel, cost class, reserve) are declared by the
 * modules. With build/modules/trio.mmod (tools/mod_build.py) the real pi32v2 file is relocated too and its descriptor
 * read back.
 *   module_test [build/modules/trio.mmod] */
#define PERSISTENCE_TEST_NO_MAIN
#define MT_OLD_KEY 0x334a6f3cu                   /* a host 1..3 module's legacy data key (any nonzero: another firmware's) */
#include "persistence_test.c"
#include "../firmware/src/sysex_route.c"       /* (the synths' SysEx in: the FM6 bank phrases, through the module) */
#include "../firmware/src/module_store.c"
#include "mod_ident_test.h"                      /* (who a test file is: its IDNT and SIGN chunks) */
#include "module_phrases.h"
#include "module_preset_golden.h"               /* (the factory presets as c53538c9's makers made them) */
#if !MELODEE_FM6_BUILTIN
#include "melodee_fm6.h"                        /* (FM6_INIT for the DX7 voice phrase: no FM6 built in here) */
#endif
extern const mod_desc_v3 mod_sid, mod_drum, mod_cz, mod_fm6, mod_p5, mod_trio, mod_wheel;   /* (firmware/modules/: the
                                                 * host's objects, built once) */
static mod_desc_v3 trio22;                      /* TRIO as a new engine (22) */
static const mod_desc_v3 *hdesc(uint32_t e)
{
    return e == 3u ? &mod_sid : e == ENGI_DRUM ? &mod_drum : e == ENGI_CZ ? &mod_cz : e == ENGI_FM6 ? &mod_fm6 :
           e == ENGI_PROPHET ? &mod_p5 : e == 6u ? &mod_trio : e == 7u ? &mod_wheel : e == 22u ? &trio22 : 0;
}
/* engine e plays module d (its engine copied into the block: the same callbacks) */
static int plays(uint32_t e, const mod_desc_v3 *d)
{
    return ENGINES[e] != &ENG_MOD_IDLE && ENGINES[e] != &ENG_RETIRED && ENGINES[e] != (const engine_t *)(const void *)d->eng &&
           ENGINES[e]->note_on == (void (*)(struct track *, voice_t *))(void (*)(void))d->eng->note_on &&
           ENGINES[e]->render == (void (*)(struct track *, voice_t *, int32_t *, uint32_t, const vmod_t *))(void (*)(void))d->eng->render;
}
/* the chunks after build/modules/<stem>.mmod's address words (its factory presets, module_presets.h: the device
 * build's file, as tools/mod_build.py wrote it) into dst; their length, 0: none */
static uint32_t mmod_chunks(const char *stem, uint8_t *dst, uint32_t cap)
{
    static uint8_t b[MOD_FILE_MAX];
    char path[80];
    mod_file3_t h;
    snprintf(path, sizeof path, "build/modules/%s.mmod", stem);
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return 0;
    uint32_t n = (uint32_t)fread(b, 1, sizeof b, fp), at;
    fclose(fp);
    if (n < MOD3_HEAD)
        return 0;
    memcpy(&h, b, MOD3_HEAD);
    at = MOD3_HEAD + h.image + 4u * h.nrel;
    if (h.abi != MOD3_ABI || !(h.flags & MOD3_FILE_CHUNKS) || at > n || n - at > cap)
        return 0;
    uint32_t got = 0;                               /* (its presets and schema; who it is comes from the test's name) */
    while (at + 8u <= n) {
        uint32_t tag, len;
        memcpy(&tag, b + at, 4);
        memcpy(&len, b + at + 4, 4);
        uint32_t whole = 8u + ((len + 3u) & ~3u);
        if (whole > n - at)
            break;
        if (tag != MOD3_IDNT_TAG && tag != MOD3_SIGN_TAG) {
            memcpy(dst + got, b + at, whole);
            got += whole;
        }
        at += whole;
    }
    return got;
}
/* a module file for the host loader (its image a pattern; the host plays hdesc()); key: its legacy key (0: an SDK
 * module), host: the host table it needs; stem: the device build's file whose chunks it carries (its presets), 0 none */
static uint32_t make_file(uint8_t *b, uint32_t engine, const char *name, uint32_t image, uint32_t nrel, uint32_t key,
                          uint32_t npresets, const char *stem)
{
    mod_file3_t h;
    memset(&h, 0, sizeof h);
    h.magic = MOD3_FILE_MAGIC; h.abi = MOD3_ABI; h.engine = (uint8_t)engine; h.host = MOD3_HOST_MIN;
    h.image = image; h.bss = 128; h.nrel = nrel; h.version = 3; h.npresets = (uint8_t)npresets; h.key = key;
    strncpy(h.name, name, sizeof h.name);
    for (uint32_t i = 0; i < image; i++) b[MOD3_HEAD + i] = (uint8_t)(i * 13u + 5u);
    for (uint32_t k = 0; k < nrel; k++) {
        uint32_t off = 6u + k * 10u;               /* (unaligned words, as a 6-byte instruction's immediate) */
        memcpy(b + MOD3_HEAD + image + 4u * k, &off, 4);
    }
    uint32_t body = image + 4u * nrel,
             ck = stem ? mmod_chunks(stem, b + MOD3_HEAD + body, MOD_FILE_MAX - MOD3_HEAD - body) : 0u,
             at = MOD3_HEAD + body + ck;
    /* who it is: Melodee's synth of that name (Melodee's key, the test key signing), as tools/mod_build.py makes it */
    h.uid = test_idnt(b, &at, "melodee", test_slug(name), name, MOD_SIGN_KEYS[0], 0);
    test_seal(b, &at, TEST_SEED);
    h.flags = (uint8_t)MOD3_FILE_CHUNKS;
    h.total = at;
    h.crc = st_crc32(b + MOD3_HEAD, at - MOD3_HEAD);
    memcpy(b, &h, MOD3_HEAD);
    return h.total;
}
/* the same file's head made another firmware's: abi, its key, the host it needs (the CRC stays right) */
static void make_other(uint8_t *b, uint32_t abi, uint32_t key, uint32_t host)
{
    mod_file3_t h;
    memcpy(&h, b, MOD3_HEAD);
    h.abi = (uint16_t)abi; h.key = key; h.host = (uint16_t)host;
    memcpy(b, &h, MOD3_HEAD);
}
/* FNV-1a, as tests/module_preset_golden.h hashes */
static uint32_t mpr_fnv(const void *p, uint32_t n, uint32_t h)
{
    const uint8_t *b = p;
    for (uint32_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x01000193u;
    return h;
}
/* the five engines' factory presets as fetched now (pick 0: each preset's entry, its name and category; 1: each one
 * picked on part 2 from zeroed parameters) against c53538c9's makers (MPR_*): how many differ */
static uint32_t mpr_diff(int pick, const char *how)
{
    static preset_entry_t x;
    uint32_t bad = 0, j = 0;
    for (uint32_t i = 0; i < MPR_NE; i++)
        for (uint32_t k = 0; k < MPR_N[i]; k++, j++) {
            uint32_t e = MPR_E[i], h;
            uint8_t c[MOD_PST_COMMON] = {0}, z[2] = {0, (uint8_t)preset_cat(e, k)};
            char nm[MOD_PST_NAME + 1u];
            if (!pick) {
                preset_fetch_into(e, k, &x, 1);
                memcpy(c, x.pr.e, 8); memcpy(c + 8, x.pr.env, 4); c[12] = (uint8_t)x.pr.fenv; c[13] = x.pr.mono;
                h = mpr_fnv(z, 2, mpr_fnv(x.pr.name, (uint32_t)strlen(x.pr.name), 0x811C9DC5u));
                h = mpr_fnv(x.patch, x.plen, mpr_fnv(c, sizeof c, h));
                eng_preset_name(e, k, nm, sizeof nm);
                if (h != MPR_ENTRY[j] || mpr_fnv(z, 2, mpr_fnv(nm, (uint32_t)strlen(nm), 0x811C9DC5u)) != MPR_NAME[j]) {
                    if (!bad) printf("  %s: engine %u preset %u \"%s\" (%u B patch) differs\n", how, e, k, x.pr.name, x.plen);
                    bad++;
                }
            } else {
                track_t *t = &trk[1];
                memset(t->p, 0, sizeof t->p);
                engine_load_of(t, e, k);
                uint32_t pk = (uint32_t)t->p[P_E0] * 17u + (uint32_t)t->p[P_E1];   /* (c53538c9's CZ-1 pick: E1 * 17 + E2) */
                uint8_t pe[2] = {t->preset, t->eng_req}, cp[2] = {(uint8_t)pk, (uint8_t)(pk >> 8)};
                h = mpr_fnv(pe, 2, mpr_fnv(t->p, sizeof t->p, 0x811C9DC5u));
                if (e == ENGI_PROPHET) h = mpr_fnv(patch_of(1), sizeof(p5_patch_t), h);
                if (e == ENGI_CZ) h = mpr_fnv(patch_of(1), CZ_BYTES, h);
                if (e == ENGI_FM6) {                    /* (as 1.x kept it: the voice unpacked, then its switches) */
                    uint8_t v[FP_SIZE + 1u];
                    fm6_voice_of(1, v);
                    h = mpr_fnv(patch_of(1) + FM6_B_ON, 1, mpr_fnv(v, FP_SIZE + 1u, h));
                }
                if (mpr_fnv(cp, 2, h) != MPR_PICK[j]) {
                    if (!bad) printf("  %s: engine %u preset %u picked: the part differs\n", how, e, k);
                    bad++;
                }
            }
        }
    return bad;
}
/* a DX7 voice (FM6's init voice, VCED) arriving over USB, then the main loop's turn (the synths' SysEx: sysex_rx.c,
 * sysex_route.c) */
static void sx_dx7_voice_in(void)
{
    uint8_t f[163] = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B}, v[FP_SIZE + 1u];
    uint32_t s = 0;
    fm6_unpack(FM6_INIT, v);
    memcpy(f + 6, v, 155);
    for (uint32_t i = 6; i < 161u; i++) s += f[i];
    f[161] = (uint8_t)(-s & 127u);
    f[162] = 0xF7;
    wire_release();
    memset(&sx_import, 0, sizeof sx_import);
    for (uint32_t i = 0; i < sizeof f; i++) midi_in_event(0xFu | (uint32_t)f[i] << 8);
    msx_service();
}
static int32_t mp_mod[MP_NB * CTL];
/* FNV-1a of the TRIO / WHEEL phrases as b8b3e092 rendered them (its built-in references = its ABI 1 modules: e 6..7 x
 * preset 0..4 x POLY / LEGATO) */
static const uint32_t TW_GOLDEN[20] = {
    0x7c336b4au, 0xdb64bb68u, 0xa78f1cb4u, 0x220b8036u, 0x174d19d5u,
    0xa1854a8au, 0xaa4c23e8u, 0xbceabe3bu, 0x90ca2468u, 0x5248ecc9u,
    0xd3db4c42u, 0x83985aeeu, 0x71c4d5beu, 0x306ced21u, 0xcdf155f9u,
    0x523f744du, 0x6eceebbdu, 0xb8026cb4u, 0xe7c250fdu, 0x8f2002cbu,
};
/* as editor_module.c: each chunk in the editor's buffer (main_scratch, the storage's scratch too), 200 bytes so one
 * crosses each record's end */
static int put(uint32_t k, const uint8_t *b, uint32_t n)
{
    int rc = mod_put_begin(k, n);
    for (uint32_t o = 0, m; !rc && o < n; o += 200u) {
        m = n - o < 200u ? n - o : 200u;
        memcpy(main_scratch.transfer, b + o, m);
        rc = mod_put_write(k, o, main_scratch.transfer, m);
    }
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
    mod_host_desc = hdesc;
    trio22 = mod_trio;
    trio22.engine = 22;
    trio22.name = "TRIO2";
#if MELODEE_ABI_SHUFFLE
    printf("persist: another firmware's private layout: track_t %u B (head %u), voice_t %u B (head %u)\n",
           (unsigned)sizeof(track_t), (unsigned)sizeof(mod_part_v3), (unsigned)sizeof(voice_t), (unsigned)sizeof(mod_voice_v3));
    bad += check("ABI stability: this firmware's private track_t / voice_t fields padded and reordered, the modules' "
                 "objects as built once",
                 __builtin_offsetof(track_t, preset) >= sizeof(mod_part_v3) + 32u && sizeof(voice_t) >= sizeof(mod_voice_v3) + 32u &&
                 trk[0].vsize == sizeof(voice_t) && trk[1].index == 1u && part_voice[1][5].index == 5u);
#endif

    bad += check("modules: numbers past the built-in engines, none loaded at first",
                 NENGINES == 48 && ENGI_MOD0 == 22 && !eng_ok(6) && !eng_ok(22) && eng_nshown == NENG_SHOWN &&
                 ENGINES[6] == &ENG_RETIRED && eng_extra_retired(22) && eng_sound_idx(22) == 22);

    /* a file into slot 0: published, loaded, playing as engine 6 */
    uint32_t n = make_file(file, 6, "TRIO", 1024, 20, 0, 5, "trio");
    int rc = put(0, file, n);
    uint8_t *blk = resource[RES_MODULE0].ptr;
    int moved = blk != 0;
    for (uint32_t k = 0; moved && k < 20u; k++) {   /* every listed word: the block's address added */
        uint32_t off = 6u + k * 10u, was, now;
        memcpy(&was, file + MOD3_HEAD + off, 4);
        memcpy(&now, blk + off, 4);
        moved = now == was + (uint32_t)(uintptr_t)blk;
    }
    bad += check("module file: committed, loaded into the arena, address words moved by the block's address",
                 rc == MS_LOADED && mods[0].state == MS_LOADED && moved && st_exists(ST_MFILE0));
    bad += check("loaded module: engine 6 again (TRIO's old number), named, its presets (its file's), browsable after the "
                 "others, its engine a copy in its block",
                 eng_ok(6) && plays(6, &mod_trio) && !strcmp(ENGINES[6]->name, "TRIO") && eng_npresets(6) == 5 &&
                 ENGINES[6]->npresets == 5 && !strcmp(eng_preset(ENGINES[6], 3)->name, "RING BELL") &&
                 (const uint8_t *)ENGINES[6] >= blk && (const uint8_t *)ENGINES[6] < blk + resource[RES_MODULE0].size &&
                 eng_nshown == NENG_SHOWN + 1 && eng_vis(NENG_SHOWN) == 6 && listed(6) &&
                 ENGINES[6]->knob[0] == P_E0 + 5 && !strcmp(ENGINES[6]->edit[0].names[9], "SYNC") &&
                 mods[0].version == 3 && !strcmp(mods[0].name, "TRIO"));

    /* bit for bit: the module against the built-in engine, every preset, POLY and LEGATO */
    n = make_file(file2, 7, "WHEEL", 1024, 8, 0, 5, "wheel");
    rc = put(2, file2, n);
    bad += check("WHEEL module (engine 7, block + part memory) loaded beside TRIO",
                 rc == MS_LOADED && eng_ok(7) && plays(7, &mod_wheel) && ENGINES[7]->block &&
                 !strcmp(ENGINES[7]->name, "WHEEL") && eng_state_size(7) == sizeof(drw_part_t));
    int same = 1, heard = 1, tw_golden = 0, tw = 0;
    for (uint32_t e = 6; e <= 7u; e++)
    for (uint32_t pi = 0; pi < 5u; pi++)
        for (uint32_t mono = 0; mono < 2u; mono++) {
            const engine_t *mod_eng = ENGINES[e];
            uint8_t at = mod_at[e];
            phrase(phrase_a, e, pi, mono);
            tw_golden += mpr_fnv(phrase_a, sizeof phrase_a, 0x811C9DC5u) == TW_GOLDEN[tw++];
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
    printf("persist: TRIO / WHEEL %u phrases, %u as b8b3e092 rendered them\n", tw, tw_golden);
    bad += check("TRIO and WHEEL, rewritten on ABI 3 from the contract alone, render bit for bit what the built-in engines "
                 "render and what their ABI 1 modules rendered (5 presets x POLY/LEGATO)",
                 same && heard && tw_golden == 20u);
    (void)mod_delete(2);

    /* refusals */
    memcpy(file2, file, n);
    file2[0] ^= 1u;                                 /* not a module file: never published */
    rc = put(1, file2, n);
    bad += check("not a module file: refused before it is published", rc == -2 && !st_exists(ST_MFILE0 + 1));
    n = make_file(file2, ENGI_MIDI, "MIDI?", 1024, 4, 0, 0, 0);   /* a built-in engine's number (the MIDI Track's) */
    bad += check("a built-in engine's number: refused", put(1, file2, n) == -2);
    n = make_file(file2, 6, "TRIO", 1024, 4, 0, 5, "trio");   /* the same module (its UID) as slot 0 */
    bad += check("the same module as another slot (its UID): refused", put(1, file2, n) == -3 && !st_exists(ST_MFILE0 + 1));
    n = make_file(file2, 22, "TRIO2", 1024, 4, 0, 5, "trio");
    file2[MOD3_HEAD + 100] ^= 0x40u;                 /* the body damaged after its CRC */
    rc = put(1, file2, n);
    bad += check("a damaged body (CRC): stored but not loaded, the arena block given back",
                 rc == MS_BAD && mods[1].state == MS_BAD && !resource[RES_MODULE0 + 1].size && !eng_ok(22));
    n = make_file(file2, 22, "TRIO2", 1024, 4, 0, 5, "trio");
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
                 mods[0].state == MS_LOADED && trk[2].eng_req == 6 && trk[2].p[P_E0] == ENG_TRIO.presets[0].e[0] &&
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

    /* the engine-source modules: SID, DRUM, CZ-1, FM6 and the Prophet (mod_engine.h: the engines' own sources) */
    {
        static uint8_t nf[5][MOD_FILE_MAX];
        const engine_t *sid_in = ENGINES[3], *drum_in = ENGINES[ENGI_DRUM], *cz_in = ENGINES[ENGI_CZ],
                       *fm6_in = ENGINES[ENGI_FM6], *p5_in = ENGINES[ENGI_PROPHET];   /* (built in on the host, or
                                                                                     * retired) */
        for (uint32_t k = 0; k < NMOD; k++) mod_delete(k);
        ENGINES[3] = ENGINES[ENGI_DRUM] = ENGINES[ENGI_CZ] = ENGINES[ENGI_FM6] = ENGINES[ENGI_PROPHET] =
            &ENG_RETIRED;                                /* (the numbers free for the modules) */
        uint32_t ns = make_file(nf[0], 3, "SID", 12000, 12, 0, MP_SID_PRESETS, "sid");
        mod_file3_t sid_head;
        memcpy(&sid_head, nf[0], sizeof sid_head);
        uint32_t nd = make_file(nf[1], ENGI_DRUM, "DRUM", 48000, 12, 0, MP_DRUM_PRESETS, "drum");
        uint32_t nc = make_file(nf[2], ENGI_CZ, "CZ-1", 9000, 12, 0, MP_CZ_PRESETS, "cz");
        uint32_t nm = make_file(nf[3], ENGI_FM6, "FM6", 20000, 12, 0, MP_FM6_PRESETS, "fm6");
        uint32_t np = make_file(nf[4], ENGI_PROPHET, "PROPHET", 16000, 12, 0, MP_P5_PRESETS, "prophet");
        bad += check("the device build's module files carry their factory presets (build/modules/*.mmod: a PRST chunk "
                     "each, tools/mod_build.py)",
                     sid_head.flags == MOD3_FILE_CHUNKS && ((const mod_file3_t *)(const void *)nf[1])->flags &&
                     ((const mod_file3_t *)(const void *)nf[2])->flags && ((const mod_file3_t *)(const void *)nf[3])->flags &&
                     ((const mod_file3_t *)(const void *)nf[4])->flags);
        int rs = put(4, nf[0], ns), rd = put(5, nf[1], nd), rc7 = put(7, nf[2], nc), rf3 = put(3, nf[3], nm),
            rp2 = put(2, nf[4], np);
        if (rs != MS_LOADED || rd != MS_LOADED || rc7 != MS_LOADED || rf3 != MS_LOADED || rp2 != MS_LOADED)
            printf("  put: SID %d DRUM %d CZ-1 %d FM6 %d PROPHET %d\n", rs, rd, rc7, rf3, rp2);
        bad += check("SID and DRUM files: committed and loaded, DRUM in two records (longer than one)",
                     rs == MS_LOADED && rd == MS_LOADED && mods[4].abi == MOD3_ABI && mods[5].len == nd &&
                     nd > ST_PROJECT_MAX && st_exists(ST_MFILE0 + 5) && st_exists(ST_MXFILE0 + 5) &&
                     !st_exists(ST_MXFILE0 + 4));
        bad += check("SID and DRUM play as engines 3 and 10 with their own callbacks (their engines copied into their blocks)",
                     plays(3, &mod_sid) && plays(ENGI_DRUM, &mod_drum) &&
                     !strcmp(eng_name(3), "SID") && !strcmp(eng_name(ENGI_DRUM), "DRUM") && eng_ok(3) &&
                     eng_ok(ENGI_DRUM) && ENGINES[3]->wclass == 4 && ENGINES[ENGI_DRUM]->wclass == 5 &&
                     ENGINES[3]->env_q15 && ENGINES[3]->chord && ENGINES[ENGI_DRUM]->reuse &&
                     ENGINES[ENGI_DRUM]->oneshot && ENGINES[3]->npresets == MP_SID_PRESETS &&
                     ENGINES[ENGI_DRUM]->npresets == MP_DRUM_PRESETS && eng_state_size(3) == ENGINES[3]->state(0) &&
                     listed(3) && listed(ENGI_DRUM));
        bad += check("CZ-1 as a module: engine 15 with its own callbacks (DCA as the matrix's ENV, CPU1, its presets "
                     "Casio's tones from its file)",
                     rc7 == MS_LOADED && plays(ENGI_CZ, &mod_cz) && !strcmp(eng_name(ENGI_CZ), "CZ-1") &&
                     eng_ok(ENGI_CZ) && ENGINES[ENGI_CZ]->wclass == 2 && ENGINES[ENGI_CZ]->menv &&
                     ENGINES[ENGI_CZ]->batch && ENGINES[ENGI_CZ]->npresets == MP_CZ_PRESETS &&
                     eng_state_size(ENGI_CZ) == ENGINES[ENGI_CZ]->state(0) && listed(ENGI_CZ) &&
                     !strcmp(eng_preset(ENGINES[ENGI_CZ], 0)->name, "INIT TONE") &&
                     !strcmp(eng_preset(ENGINES[ENGI_CZ], 30)->name, "FUNKY CLAV 1"));
        bad += check("FM6 as a module: engine 12 with its own callbacks (its envelopes, Dexed's voices, DC filter, "
                     "its own CPU1 jobs, the reserve), its presets; the patches the firmware's",
                     rf3 == MS_LOADED && plays(ENGI_FM6, &mod_fm6) && !strcmp(eng_name(ENGI_FM6), "FM6") &&
                     eng_ok(ENGI_FM6) && ENGINES[ENGI_FM6]->wclass == 3 && ENGINES[ENGI_FM6]->ownenv &&
                     ENGINES[ENGI_FM6]->alloc && ENGINES[ENGI_FM6]->legato && ENGINES[ENGI_FM6]->mono_key &&
                     ENGINES[ENGI_FM6]->post && ENGINES[ENGI_FM6]->block && ENGINES[ENGI_FM6]->reserve &&
                     ENGINES[ENGI_FM6]->poly == NVOICE && ENGINES[ENGI_FM6]->npresets == MP_FM6_PRESETS &&
                     eng_state_size(ENGI_FM6) == ENGINES[ENGI_FM6]->state(0) && listed(ENGI_FM6) &&
                     eng_rank(ENGI_FM6) == 1 && !strcmp(eng_preset(ENGINES[ENGI_FM6], 10)->name, "SOLID BASS") &&
                     !strcmp(eng_preset(ENGINES[ENGI_FM6], 24)->name, "INIT VOICE"));
        {
            char nm1[21], nm200[21];
            eng_preset_name(ENGI_PROPHET, 1, nm1, sizeof nm1);
            eng_preset_name(ENGI_PROPHET, 200, nm200, sizeof nm200);
            bad += check("the Prophet as a module: engine 19 with its own callbacks (its envelopes, the amp envelope "
                         "as the matrix's ENV, the coupling capacitor, its own CPU1 jobs, its voice count), the first "
                         "engine; its presets Sequential's programs from its file, the parts' programs the firmware's",
                         rp2 == MS_LOADED && plays(ENGI_PROPHET, &mod_p5) &&
                         !strcmp(eng_name(ENGI_PROPHET), "PROPHET") && eng_ok(ENGI_PROPHET) &&
                         ENGINES[ENGI_PROPHET]->wclass == 1 && ENGINES[ENGI_PROPHET]->ownenv &&
                         ENGINES[ENGI_PROPHET]->menv && !ENGINES[ENGI_PROPHET]->env_q15 && ENGINES[ENGI_PROPHET]->post &&
                         ENGINES[ENGI_PROPHET]->block && ENGINES[ENGI_PROPHET]->cap && ENGINES[ENGI_PROPHET]->legato &&
                         ENGINES[ENGI_PROPHET]->units && !ENGINES[ENGI_PROPHET]->batch &&
                         ENGINES[ENGI_PROPHET]->npresets == MP_P5_PRESETS &&
                         eng_state_size(ENGI_PROPHET) == ENGINES[ENGI_PROPHET]->state(0) && listed(ENGI_PROPHET) &&
                         eng_rank(ENGI_PROPHET) == 0 && !strcmp(eng_preset(ENGINES[ENGI_PROPHET], 0)->name, "INIT PROPHET") &&
                         nm1[0] && strcmp(nm1, "INIT PROPHET") && !strcmp(eng_preset(ENGINES[ENGI_PROPHET], 1)->name, nm1) &&
                         !strcmp(eng_preset(ENGINES[ENGI_PROPHET], 200)->name, nm200));
        }
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
                const engine_t *ms = ENGINES[3], *md = ENGINES[ENGI_DRUM], *mc = ENGINES[ENGI_CZ], *mf = ENGINES[ENGI_FM6],
                               *mp = ENGINES[ENGI_PROPHET];
                uint8_t as = mod_at[3], ad = mod_at[ENGI_DRUM], ac = mod_at[ENGI_CZ], af = mod_at[ENGI_FM6],
                        ap = mod_at[ENGI_PROPHET];
                memcpy(mp_mod, mp_out, sizeof mp_mod);
                ENGINES[3] = sid_in; ENGINES[ENGI_DRUM] = drum_in; ENGINES[ENGI_CZ] = cz_in; ENGINES[ENGI_FM6] = fm6_in;
                ENGINES[ENGI_PROPHET] = p5_in;
                mod_at[3] = mod_at[ENGI_DRUM] = mod_at[ENGI_CZ] = mod_at[ENGI_FM6] = mod_at[ENGI_PROPHET] = 0;
                (void)mp_run(k, &w, &a, &b);
                ENGINES[3] = ms; ENGINES[ENGI_DRUM] = md; ENGINES[ENGI_CZ] = mc; ENGINES[ENGI_FM6] = mf;
                ENGINES[ENGI_PROPHET] = mp;
                mod_at[3] = as; mod_at[ENGI_DRUM] = ad; mod_at[ENGI_CZ] = ac; mod_at[ENGI_FM6] = af;
                mod_at[ENGI_PROPHET] = ap;
                for (uint32_t i = 0; i < MP_NB * CTL; i++)
                    if (mp_mod[i] != mp_out[i]) {
                        if (same) printf("  phrase %u %s %u %u: sample %u module %d built-in %d\n", k, w, a, b, i, mp_mod[i], mp_out[i]);
                        same = 0;
                        break;
                    }
            }
        }
        printf("persist: %u phrases, %u as golden%s\n", MP_COUNT, golden, sid_in != &ENG_RETIRED ? ", each against the built-in engine" : "");
        bad += check("SID, DRUM, CZ-1, FM6 and Prophet modules render what the built-in engines rendered (every preset, "
                     "POLY / LEGATO, C64, ENV3, every kit, every CZ-1 tone edited as it plays, every FM6 voice, algorithm, "
                     "ENGINE and controller, a DX7 bank by SysEx, every Prophet program in POLY / UNISON edited as it "
                     "plays, CPU0 and CPU1)",
                     golden == MP_COUNT && same && heard);
        /* the presets: every one read from its module's file as c53538c9's makers made it (built in here: the makers
         * themselves too), entry, name and category, and picked */
        {
            uint32_t fe = mpr_diff(0, "file"), fp = mpr_diff(1, "file"), be = 0, bp = 0;
            if (sid_in != &ENG_RETIRED) {
                const engine_t *ms = ENGINES[3], *md = ENGINES[ENGI_DRUM], *mc = ENGINES[ENGI_CZ],
                               *mf = ENGINES[ENGI_FM6], *mp = ENGINES[ENGI_PROPHET];
                uint8_t at[5] = {mod_at[3], mod_at[ENGI_DRUM], mod_at[ENGI_CZ], mod_at[ENGI_FM6], mod_at[ENGI_PROPHET]};
                ENGINES[3] = sid_in; ENGINES[ENGI_DRUM] = drum_in; ENGINES[ENGI_CZ] = cz_in; ENGINES[ENGI_FM6] = fm6_in;
                ENGINES[ENGI_PROPHET] = p5_in;
                mod_at[3] = mod_at[ENGI_DRUM] = mod_at[ENGI_CZ] = mod_at[ENGI_FM6] = mod_at[ENGI_PROPHET] = 0;
                sch_gen++;                               /* (the engines' schemas as built in: a DRUM kit's category) */
                be = mpr_diff(0, "built in"); bp = mpr_diff(1, "built in");
                sch_gen++;
                ENGINES[3] = ms; ENGINES[ENGI_DRUM] = md; ENGINES[ENGI_CZ] = mc; ENGINES[ENGI_FM6] = mf;
                ENGINES[ENGI_PROPHET] = mp;
                mod_at[3] = at[0]; mod_at[ENGI_DRUM] = at[1]; mod_at[ENGI_CZ] = at[2]; mod_at[ENGI_FM6] = at[3];
                mod_at[ENGI_PROPHET] = at[4];
            }
            uint32_t inits = 1;
            for (uint32_t i = 0; i < MPR_NE; i++)
                inits &= eng_npresets(MPR_E[i]) == MPR_N[i] && preset_init(MPR_E[i]) == MPR_INIT[i];
            printf("persist: %u presets from the files, %u differ (picked: %u)%s\n", MPR_COUNT, fe,
                   fp, sid_in != &ENG_RETIRED ? "; the built-in makers too" : "");
            bad += check("every factory preset decoded from its module's file is byte for byte what the makers made before "
                         "the presets left the firmware (201 Prophet, 65 CZ-1, 25 FM6, 32 SID, 6 DRUM: values, name, "
                         "category, patch; picked: the part and its patch); host builds' makers the same",
                         !fe && !fp && !be && !bp && inits);
        }
        /* the capabilities that replaced the engine-number checks in voice.c, mix.c and main.c: declared by the modules */
        bad += check("capabilities: the Prophet sets its own bend range (its BEND) and plays the wheel itself, FM6 plays the "
                     "wheel itself and keeps CPU0's reserve, cost classes 1..5 (Prophet, CZ-1, FM6, SID, DRUM), no other "
                     "engine claims one",
                     ENGINES[ENGI_PROPHET]->bend_range && (ENGINES[ENGI_PROPHET]->caps & MOD3_CAP_WHEEL) &&
                     (ENGINES[ENGI_FM6]->caps & MOD3_CAP_WHEEL) && ENGINES[ENGI_FM6]->reserve && !ENGINES[ENGI_FM6]->bend_range &&
                     !ENGINES[3]->caps && !ENGINES[3]->bend_range && !ENGINES[ENGI_DRUM]->caps && !ENGINES[ENGI_CZ]->caps &&
                     !ENGINES[ENGI_CZ]->bend_range && !ENGINES[ENGI_DRUM]->reserve && !ENGINES[ENGI_PROPHET]->reserve &&
                     ENGINES[ENGI_PROPHET]->wclass == 1 && ENGINES[ENGI_CZ]->wclass == 2 && ENGINES[ENGI_FM6]->wclass == 3 &&
                     ENGINES[3]->wclass == 4 && ENGINES[ENGI_DRUM]->wclass == 5 && !ENGINES[ENGI_MIDI]->caps);
        {   /* the Prophet's bend: the wheel's target from its program's BEND (voice.c trk_bend_own), as before */
            host_preset(&trk[2], ENGI_PROPHET, 3);
            patch_of(2)[P5_BEND] = 7;
            trk[2].bend_raw = -4096;
            int32_t pb[CTL];
            track_render(&trk[2], pb, CTL);
            int32_t want = -4096 * 8 * 256 / 8192;
            bad += check("the Prophet's pitch wheel: its target from its program's BEND at its block (bend_range)",
                         midi_bend_target[2] == want);
            trk[2].bend_raw = 0;
            track_render(&trk[2], pb, CTL);
        }
        /* another firmware's module is refused before it is published: an older ABI (1, native 2), the legacy data of
         * another layout (its key), a newer host table */
        int rk = 0;
        for (uint32_t w = 0; w < 4u; w++) {
            uint32_t nk = make_file(nf[0], 3, "SID", 12000, 12, 0, MP_SID_PRESETS, "sid");
            make_other(nf[0], w == 0 ? 1u : w == 1 ? 2u : MOD3_ABI, w == 2 ? MT_OLD_KEY : 0u,
                       w == 3 ? MOD3_HOST + 1u : MOD3_HOST_MIN);
            rk += put(6, nf[0], nk) == -2 && !st_exists(ST_MFILE0 + 6);
        }
        bad += check("another firmware's module (ABI 1, ABI 2, another legacy key, a newer host table): refused, not published",
                     rk == 4);
        uint32_t nk = make_file(nf[0], 3, "SID", 12000, 12, MT_OLD_KEY, MP_SID_PRESETS, "sid");
        /* .. and one stored before the firmware changed (a record already there): known by engine and name, not
         * played, not counted as the engine's module; the installer (web/fm1mods.js) replaces it in its slot */
        int shipped = SHIPPED_OK();
        int saved = !st_project_save(ST_MFILE0 + 6, nf[0], nk);
        int rs6 = mod_catalog(6);
        make_file(nf[0], 3, "SID", 12000, 12, 0, MP_SID_PRESETS, "sid");
        make_other(nf[0], 2u, 0x6b73df3fu, 0);          /* (b8b3e092's native SID: ABI 2, its layout key at 52) */
        int saved2 = !st_project_save(ST_MFILE0 + 0, nf[0], nk), rs0 = mod_catalog(0);
        make_file(nf[0], 3, "SID", 12000, 12, 0, MP_SID_PRESETS, "sid");
        make_other(nf[0], MOD3_ABI, 0u, MOD3_HOST + 1u);
        int saved3 = !st_project_save(ST_MFILE0 + 1, nf[0], nk), rs1 = mod_catalog(1);
        bad += check("stored modules of another firmware: an older one (another legacy key, ABI 2) MS_STALE, a newer one "
                     "MS_NEWER; each keeps its engine and name, none listed or played",
                     saved && rs6 == MS_STALE && mods[6].engine == 3 && !strcmp(mods[6].name, "SID") &&
                     mods[6].key == MT_OLD_KEY && saved2 && rs0 == MS_STALE && mods[0].abi == 2u &&
                     mods[0].key == 0x6b73df3fu && saved3 && rs1 == MS_NEWER && mods[1].host == MOD3_HOST + 1u &&
                     !strcmp(mods[1].name, "SID") && mod_slot_of(3) == 4 && plays(3, &mod_sid));
        (void)mod_delete(0);
        (void)mod_delete(1);
        bad += check("the slots report what the installer compares: each file's CRC and layout key (editor command 84 op 5)",
                     mods[4].crc == sid_head.crc && mods[4].len == sid_head.total &&
                     mods[5].key == 0u && mods[4].key == 0u && mods[5].abi == MOD3_ABI);
        (void)mod_delete(6);
        bad += check("SID, DRUM, CZ-1, FM6 and the Prophet on the device: the start asks for no setup (main.c, ui_boot.c)",
                     shipped);
        /* power-on: listed from their heads, nothing in SRAM; wanting DRUM reads its two records */
        for (uint32_t k = 0; k < NMOD; k++) mod_unregister(k, 0), memset(&mods[k], 0, sizeof mods[k]);
        mod_boot();
        int idle = mods[4].state == MS_STORED && mods[5].state == MS_STORED && ENGINES[ENGI_DRUM] == &ENG_MOD_IDLE &&
                   eng_npresets(ENGI_DRUM) == 6 && !resource[RES_MODULE0 + 5].size;
        {   /* the lists with no module loaded: names and categories from the files (the browser's rows, its LIST) */
            uint32_t j = 0, names = 1, none_loaded = 1, total, as, ak, bass = 0;
            for (uint32_t i = 0; i < MPR_NE; i++)
                for (uint32_t k = 0; k < MPR_N[i]; k++, j++) {
                    uint32_t e = MPR_E[i];
                    char nm[MOD_PST_NAME + 1u], lb[13];
                    uint8_t z[2] = {0, (uint8_t)preset_cat(e, k)};
                    eng_preset_name(e, k, nm, sizeof nm);
                    eng_preset_name(e, k, lb, sizeof lb);      /* (a list row's: ui_graph.c entry_label) */
                    names &= mpr_fnv(z, 2, mpr_fnv(nm, (uint32_t)strlen(nm), 0x811C9DC5u)) == MPR_NAME[j] &&
                             !strncmp(lb, nm, 12) && strlen(lb) == (strlen(nm) < 12 ? strlen(nm) : 12) && entry_ok(e, k) &&
                             eng_name(e)[0] && strcmp(eng_name(e), "-");
                }
            list_scan(LM_CAT, 0xFFFFFFFFu, 0, 0xFFFFFFFFu, &as, &ak, &total);   /* (LIST BASS: CAT_ORDER's first) */
            for (uint32_t n = 0; n < total; n++) {           /* (its factory rows; the user slots: the SysEx phrases') */
                list_scan(LM_CAT, 0xFFFFFFFFu, 0, n, &as, &ak, &total);
                bass += !src_slot(as) && preset_cat(as, ak) == CAT_BASS;
            }
            for (uint32_t k = 0; k < NMOD; k++)
                none_loaded &= mods[k].state != MS_LOADED && !resource[RES_MODULE0 + k].size;
            if (bass != MPR_BASS) printf("  LIST BASS: %u factory sounds, want %u\n", bass, MPR_BASS);
            bad += check("the browser with no module loaded: every factory preset's name (a row's, a card's) and category "
                         "read from its module's file, LIST BASS as the makers' categories make it, nothing loaded",
                         idle && names && bass == MPR_BASS && none_loaded && preset_init(ENGI_FM6) == 24u);
        }
        mod_want(ENGI_DRUM);
        bad += check("modules at power-on: listed unloaded; DRUM loaded from its two records when wanted",
                     idle && mods[5].state == MS_LOADED && plays(ENGI_DRUM, &mod_drum) &&
                     mods[4].state == MS_STORED);
        /* the Prophet, the first engine shown: at power-on listed from its head; without its module retired as any
         * other (its tracks keep the number, silent; its programs and their names the firmware's), the next engine
         * first, the start asks for setup; back in its slot, first again */
        {
            char nm[21], gone_nm[21];
            int p5_idle = mods[2].state == MS_STORED && ENGINES[ENGI_PROPHET] == &ENG_MOD_IDLE &&
                          eng_npresets(ENGI_PROPHET) == MP_P5_PRESETS && eng_ok(ENGI_PROPHET) &&
                          !strcmp(eng_name(ENGI_PROPHET), "PROPHET") && SHIPPED_OK();
            mod_want(ENGI_PROPHET);
            int p5_back = mods[2].state == MS_LOADED && plays(ENGI_PROPHET, &mod_p5) &&
                          eng_rank(ENGI_PROPHET) == 0;
            eng_preset_name(ENGI_PROPHET, 5, nm, sizeof nm);
            host_preset(&trk[1], ENGI_PROPHET, 5);
            trk_note_on(&trk[1], 48, 100);
            int32_t pb[CTL];
            uint32_t p5_gone = !mod_delete(2) && ENGINES[ENGI_PROPHET] == &ENG_RETIRED && trk[1].engine == ENGI_PROPHET &&
                               eng_sound_idx(ENGI_PROPHET) == ENGI_PROPHET;
            track_render(&trk[1], pb, CTL);
            for (uint32_t i = 0; i < CTL; i++) p5_gone &= MELODEE_P5_BUILTIN || pb[i] == 0;
            eng_preset_name(ENGI_PROPHET, 5, gone_nm, sizeof gone_nm);
            p5_preset_loaded(&trk[1], 7);                /* (a program loaded, its bytes kept: the firmware's) */
            patch_of(1)[P5_CUTOFF] = 33;
            p5_gone &= patch_of(1)[P5_CUTOFF] == 33 && nm[0] && strcmp(nm, "-") &&
                       !strcmp(gone_nm, "-") && !eng_npresets(ENGI_PROPHET);
            bad += check("the Prophet at power-on: listed unloaded, loaded when wanted, first; without its module its "
                         "tracks keep 19 and go silent, its parts' programs stay editable, its factory presets go with its "
                         "file, the next engine is first, the start asks for setup; put back, first again",
                         p5_idle && p5_back && p5_gone &&
                         (MELODEE_P5_BUILTIN || (!eng_ok(ENGI_PROPHET) && !listed(ENGI_PROPHET) &&
                                                 eng_vis(0) != ENGI_PROPHET && eng_rank(ENGI_PROPHET) == 0)) &&
                         SHIPPED_OK() == MELODEE_P5_BUILTIN && put(2, nf[4], np) == MS_LOADED &&
                         plays(ENGI_PROPHET, &mod_p5) && eng_rank(ENGI_PROPHET) == 0 && SHIPPED_OK());
        }
        {
            int cz_idle = mods[7].state == MS_STORED && ENGINES[ENGI_CZ] == &ENG_MOD_IDLE && eng_npresets(ENGI_CZ) == 65 &&
                          SHIPPED_OK(), cz_gone = !mod_delete(7) && ENGINES[ENGI_CZ] == &ENG_RETIRED &&
                          SHIPPED_OK() == MELODEE_CZ_BUILTIN;
            bad += check("CZ-1 at power-on: listed unloaded; without its module the start asks for setup, no factory tones",
                         cz_idle && cz_gone && !eng_npresets(ENGI_CZ));
        }
        /* FM6, the firmware's fallback engine: at power-on listed from its head; without its module retired as any
         * other (its sounds keep the number, silent; a retired sound's place: FM6's, then the first engine's) */
        int fm6_idle = mods[3].state == MS_STORED && ENGINES[ENGI_FM6] == &ENG_MOD_IDLE &&
                       eng_npresets(ENGI_FM6) == MP_FM6_PRESETS && eng_ok(ENGI_FM6) && SHIPPED_OK() == MELODEE_CZ_BUILTIN;
        sx_dx7_voice_in();                           /* (its module not in SRAM: loaded for the frame, which it takes) */
        int sx_lazy = mods[3].state == MS_LOADED && sx_import.count == 1u && sx_import.engine == ENGI_FM6 &&
                      msx.eng == 0xFFu && !msx.ready;
        memset(&sx_import, 0, sizeof sx_import);
        mod_unregister(3, 1);
        mods[3].state = MS_STORED;                    /* (out again: as at power-on) */
        mod_want(ENGI_FM6);
        int fm6_back = mods[3].state == MS_LOADED && plays(ENGI_FM6, &mod_fm6) && eng_rank(4) == 1;
        host_preset(&trk[1], ENGI_FM6, 10);
        trk_note_on(&trk[1], 48, 100);
        int32_t fb[CTL];
        uint32_t ok_gone = !mod_delete(3) && ENGINES[ENGI_FM6] == &ENG_RETIRED && trk[1].engine == ENGI_FM6 &&
                           eng_sound_idx(ENGI_FM6) == ENGI_FM6 && eng_sound_idx(4) == ENGI_FM6;
        track_render(&trk[1], fb, CTL);
        for (uint32_t i = 0; i < CTL; i++) ok_gone &= MELODEE_FM6_BUILTIN || fb[i] == 0;
        sx_dx7_voice_in();                           /* (no module: the frame dropped, the collector free) */
        int sx_gone = !sx_import.count && msx.eng == 0xFFu && !msx.ready;
        memset(&sx_import, 0, sizeof sx_import);
        bad += check("a synth's SysEx: its module not in SRAM is loaded for the frame and takes it; without its module the "
                     "frame is dropped and the collector free", fm6_idle && sx_lazy && sx_gone);
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
        ENGINES[ENGI_PROPHET] = p5_in;
    }

    /* the real module file (pi32v2): relocated at the block, its descriptor read back through the moved words */
    FILE *fp = fopen(argc > 1 ? argv[1] : "build/modules/trio.mmod", "rb");
    if (fp) {
        n = (uint32_t)fread(file, 1, sizeof file, fp);
        fclose(fp);
        mod_file3_t f;
        memcpy(&f, file, sizeof f);
        mod_delete(0);
        rc = put(0, file, n);
        blk = resource[RES_MODULE0].ptr;
        uint32_t base = (uint32_t)(uintptr_t)blk, w[8], ok = rc == MS_LOADED && blk && f.abi == MOD3_ABI && !f.key;
        if (ok) {
            memcpy(w, blk, sizeof w);
            uint32_t eng = w[6] - base, lab, non, ren;
            ok = w[0] == MOD3_MAGIC && w[5] - base < f.image && !strcmp((const char *)blk + (w[5] - base), "TRIO") &&
                 eng + 196u <= f.image && w[7] - base < f.image && blk[12] == 6 && blk[13] == 5;
            if (ok) {
                memcpy(&lab, blk + eng + 12u, 4); memcpy(&non, blk + eng + 188u, 4); memcpy(&ren, blk + eng + 192u, 4);
                ok = lab - base < f.image && !strcmp((const char *)blk + (lab - base), "WAVE") && non - base < f.image &&
                     ren - base < f.image && eng_npresets(6) == 5 && !strcmp(eng_preset(ENGINES[6], 3)->name, "RING BELL");
            }
        }
        bad += check("pi32v2 TRIO file (ABI 3): CRC, every address word moved, descriptor, engine and strings found through "
                     "them, its presets from its PRST chunk", ok);
    } else
        printf("persist: (no build/modules/trio.mmod: tools/mod_build.py firmware/modules/trio/trio.c)\n");

    printf("Module test %s\n", bad ? "FAILED" : "passed");
    return !!bad;
}
