/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Who a synth module is, through real NOR saves (phase 6): its UID (its file's head and IDNT chunk), the device's
 * engine map (eng_map.c: Melodee's numbers by the manifest, a new module the number it asks for or the first free one,
 * kept across power-on in the map record, a deleted module's number free again), a project naming modules another
 * device numbers otherwise (FBKR: its entries and the patch of a synth without a Melodee 1.x format come back on
 * "another device", the missing ones silent under their names, saved again with their entries), a project of
 * Melodee's own synths written as it always was, a user-preset bank's entries, the data records' owners, the crash
 * blame (only the module whose code ran is left out, once), and Ed25519 (RFC 8032's vectors). */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"
#include "../firmware/src/sysex_route.c"
#include "../firmware/src/module_store.c"
#include <assert.h>

static uint8_t file[MOD_FILE_MAX];
static uint32_t idnt_crc(const char *vendor, const char *name)
{
    char b[40];
    snprintf(b, sizeof b, "%s/%s", vendor, name);
    return st_crc32(b, (uint32_t)strlen(b)) ? st_crc32(b, (uint32_t)strlen(b)) : 1u;
}
static void chunk(uint8_t *b, uint32_t *at, uint32_t tag, const void *p, uint32_t n)
{
    memcpy(b + *at, &tag, 4);
    memcpy(b + *at + 4, &n, 4);
    memcpy(b + *at + 8, p, n);
    *at += 8u + ((n + 3u) & ~3u);
}
/* a module file (its image a pattern: never loaded here) for vendor/name asking for engine want; plen: a schema whose
 * patch is plen bytes (no Melodee 1.x format), 0 none */
static uint32_t make(const char *vendor, const char *name, uint32_t want, uint32_t plen)
{
    mod_file3_t h;
    mod_ident_v3 id;
    uint32_t image = 256u, at = MOD3_HEAD + image;
    memset(file, 0, sizeof file);
    memset(&h, 0, sizeof h);
    for (uint32_t i = 0; i < image; i++) file[MOD3_HEAD + i] = (uint8_t)(i * 7u + 1u);
    if (plen) {
        static uint8_t def[64], sch[1024];
        mod_sch_src s;
        memset(&s, 0, sizeof s);
        for (uint32_t i = 0; i < plen; i++) def[i] = (uint8_t)(0x40u + i);
        s.patch = (uint16_t)plen; s.def = def; s.wdef = MOD_SCH_NONE; s.mmap = MOD_SCH_NONE;
        uint32_t n = mod_sch_write(&s, sch, sizeof sch);
        chunk(file, &at, MOD_SCH_TAG, sch, n);
    }
    memset(&id, 0, sizeof id);
    id.uid = idnt_crc(vendor, name);
    id.version = MOD3_IDNT_VERSION;
    memcpy(id.tag, name, strlen(name) < 4u ? strlen(name) : 4u);
    memcpy(id.vendor, vendor, strlen(vendor));
    chunk(file, &at, MOD3_IDNT_TAG, &id, sizeof id);
    h.magic = MOD3_FILE_MAGIC; h.abi = MOD3_ABI; h.engine = (uint8_t)want; h.host = MOD3_HOST; h.flags = MOD3_FILE_CHUNKS;
    h.image = image; h.bss = 16; h.version = 1; h.uid = id.uid; h.total = at;
    strncpy(h.name, name, sizeof h.name);
    h.crc = st_crc32(file + MOD3_HEAD, at - MOD3_HEAD);
    memcpy(file, &h, sizeof h);
    return at;
}
static void install(uint32_t k, const char *vendor, const char *name, uint32_t want, uint32_t plen)
{
    uint32_t n = make(vendor, name, want, plen);
    for (uint32_t j = 0; j < MOD_RECS; j++)
        if (st_exists(st_mod_obj(k, j))) (void)st_project_delete(st_mod_obj(k, j));
    assert(!st_save(st_mod_obj(k, 0), file, n));
}
static uint32_t slot_of_name(const char *name)
{
    for (uint32_t k = 0; k < NMOD; k++)
        if (mods[k].len && !strcmp(mods[k].name, name)) return k;
    return NMOD;
}
static uint32_t eng_of_name(const char *name)
{
    for (uint32_t e = 0; e < NENGINES; e++)
        if (!strcmp(eng_name(e), name)) return e;
    return NENGINES;
}
static uint32_t stored_magic(uint32_t slot)
{
    static uint8_t raw[BANK_STORE_SIZE];
    st_hdr_t h;
    uint32_t obj = st_proj_obj(slot), m = 0;
    int copy = st_current(obj, &h);
    if (copy >= 0 && !pj_image(obj, (uint32_t)copy, raw)) memcpy(&m, raw, 4);
    return m;
}
static void boot(void)                              /* power-on: the map and the slots read again */
{
    eng_map_service(1);
    for (uint32_t k = 0; k < NMOD; k++) mod_unregister(k, 0), memset(&mods[k], 0, sizeof mods[k]);
    mod_boot();
}
/* ---- Ed25519 (RFC 8032 7.1) */
static const uint8_t *sg_msg;
static int sg_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, sg_msg + off, n); return 0; }
static void hex(uint8_t *b, const char *s) { for (uint32_t i = 0; s[2 * i]; i++) sscanf(s + 2 * i, "%2hhx", &b[i]); }

int main(void)
{
    int bad = 0;
    uint8_t patch[32], got[MOD3_PATCH_MAX];
    reset();
    flash_ok = 1;
    mod_boot();
    /* ---- the map */
    install(0, "acme", "JUNO", 22, 32);
    install(1, "acme", "PPG", 22, 0);
    install(2, "acme", "FAKEFM", 12, 0);            /* (asks for a number of Melodee's own) */
    boot();
    uint32_t juno = mods[slot_of_name("JUNO")].engine, ppg = mods[slot_of_name("PPG")].engine,
             fake = mods[slot_of_name("FAKEFM")].engine;
    bad += check("UIDs from the files, Melodee's numbers by the manifest's UIDs (FM6 12, SID 3, the Prophet 19)",
                 mods[0].uid == idnt_crc("acme", "JUNO") && eng_map[12].uid == 0x04AC81A9u && eng_map[3].uid == 0xC4263526u &&
                 eng_map[19].uid == 0xA2CFA9ABu);
    bad += check("numbers: the one asked for (JUNO 22), the first free (PPG 23, its 22 taken), never Melodee's own (FAKEFM "
                 "asked for 12: 24)", juno == 22u && ppg == 23u && fake == 24u && mods[2].state == MS_STORED &&
                 !strcmp(eng_name(22), "JUNO") && !strcmp(eng_name(23), "PPG"));
    eng_map_service(1);                             /* (the main loop: saved once nothing plays) */
    bad += check("kept in the map record (the modules' numbers, nothing of Melodee's)", st_exists(ST_MAPOBJ));
    (void)mod_delete(slot_of_name("FAKEFM"));
    install(3, "zeta", "OTHER", 23, 0);
    boot();
    bad += check("power-on: the numbers kept; a deleted module's number free again (OTHER asked for 23: PPG's; given 24)",
                 mods[0].engine == 22u && mods[1].engine == 23u && mods[3].engine == 24u && mods[2].state == MS_EMPTY);
    /* ---- a project naming them */
    host_tracks_init();
    for (uint32_t i = 0; i < sizeof patch; i++) patch[i] = (uint8_t)(0x91u + 3u * i);
    trk[1].engine = trk[1].eng_req = (uint8_t)juno;
    trk[2].engine = trk[2].eng_req = (uint8_t)ppg;
    patch_set(1, juno, patch, sizeof patch, MOD3_PATCH_SET);
    bad += check("fixture: JUNO's patch on part 2 (32 bytes, no 1.x format)", patch_len(juno) == 32u &&
                 patch_store_tag(juno) == MOD_SCH_STORE_NONE && !memcmp(patch_of(1), patch, sizeof patch));
    bad += check("a project naming them saves as FBKR (its entries, JUNO's patch)",
                 !project_save_as(1, "MAPPED") && stored_magic(1) == BANKR_MAGIC);
    host_tracks_init();
    for (uint32_t k = 0; k < NPART; k++) host_preset(&trk[k], TRK_DEF[k][0], TRK_DEF[k][1]);
    bad += check("a project of Melodee's own synths saves as it always did (no end block)",
                 !project_save_as(2, "PLAIN") && stored_magic(2) != BANKR_MAGIC);
    /* "another device": OTHER first (22), JUNO and PPG not there */
    for (uint32_t k = 0; k < NMOD; k++) (void)mod_delete(k);
    eng_map_service(1);
    eng_map_defaults();
    install(0, "zeta", "OTHER", 22, 16);
    boot();
    host_tracks_init();
    project_load(1);
    uint32_t j2 = eng_of_name("JUNO"), p2 = eng_of_name("PPG");
    int32_t buf[CTL];
    trk_note_on(&trk[1], 60, 100);
    track_render(&trk[1], buf, CTL);
    uint32_t silent = 1;
    for (uint32_t i = 0; i < CTL; i++) silent &= buf[i] == 0;
    bad += check("on another device: OTHER keeps 22; JUNO and PPG take free numbers, their names from the project's entries",
                 mods[0].engine == 22u && j2 < NENGINES && j2 != 22u && p2 < NENGINES && p2 != 22u && p2 != j2 &&
                 trk[1].eng_req == j2 && trk[2].eng_req == p2);
    bad += check(".. JUNO's patch kept with it (its number here), the parts silent", patch_eng_of(1) == j2 &&
                 !memcmp(patch_of(1), patch, sizeof patch) && silent);
    bad += check(".. saved again: FBKR, the same patch back after a load", !project_save_as(3, "AGAIN") &&
                 stored_magic(3) == BANKR_MAGIC && (host_tracks_init(), project_load(3), 1) && trk[1].eng_req == j2 &&
                 !memcmp(patch_of(1), patch, sizeof patch) && !strcmp(eng_name(j2), "JUNO"));
    /* ---- a user-preset bank */
    host_tracks_init();
    trk[0].engine = trk[0].eng_req = 22;            /* OTHER's sound (its module not loaded here: its E values 0) */
    for (uint32_t i = 0; i < 8u; i++) trk[0].p[P_E0 + i] = 0;
    patch_set(0, 22, patch, 16, MOD3_PATCH_SET);
    bad += check("a user preset of OTHER (22) stored", !up_store_to(5, 0, "OTHER SOUND") && up_used(5));
    for (uint32_t k = 0; k < NMOD; k++) (void)mod_delete(k);
    eng_map_service(1);
    eng_map_defaults();
    install(0, "acme", "FIRST", 22, 0);             /* (22 is someone else's here) */
    install(1, "zeta", "OTHER", 22, 16);
    boot();
    up_cache_reset();
    uint32_t o3 = mods[1].engine;
    bad += check("its bank read on a device where OTHER is not 22: the record names OTHER's number here",
                 o3 != 22u && up_rec(5)->engine == o3 && up_used(5) && !memcmp(up_rec(5)->packed, patch, 16));
    /* ---- the data records' owners */
    rec_uid[3] = 0x12345678u;
    eng_map_dirty = 1;
    boot();
    bad += check("records' owners: the default (DRUM's, the manifest's General MIDI kit synth) and one kept by the map record",
                 rec_owner_uid(0) == 0x86C271CFu && rec_owner_uid(3) == 0x12345678u);
    /* ---- the crash blame */
    mod_run.magic = MODRUN_MAGIC; mod_run.alive = 1; mod_run.crashed = 1; mod_run.loop = 2; mod_run.audio = 1;
    boot();
    bad += check("crashed in slot 1's main-loop code: that module alone left out, said so (mod_blamed)",
                 mods[1].state == MS_SKIPPED && mods[0].state == MS_STORED && mod_blamed == 2u && !mod_skipped);
    boot();
    bad += check(".. the next start: back", mods[1].state == MS_STORED && !mod_blamed);
    mod_run.alive = 1; mod_run.crashed = 1; mod_run.loop = 0; mod_run.audio = 1;
    boot();
    bad += check("crashed in the audio's module code (slot 0): slot 0 left out", mods[0].state == MS_SKIPPED &&
                 mods[1].state == MS_STORED && mod_blamed == 1u);
    mod_run.alive = 1; mod_run.crashed = 0; mod_run.loop = 1;
    boot();
    bad += check("a reset that was no crash (a marker set): nothing left out", !mod_blamed && mods[0].state == MS_STORED);
    bootguard.failed = 1;
    mod_run.alive = 0;
    boot();
    bad += check("a boot that died early with no module to blame: all left out, as before", mod_skipped &&
                 mods[0].state == MS_SKIPPED && mods[1].state == MS_SKIPPED);
    bootguard.failed = 0;
    /* ---- Ed25519 */
    {
        static const char *const V[][3] = {
            {"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
             "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
            {"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
             "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
            {"fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
             "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"}};
        uint32_t ok = 0, refused = 0;
        sg_w = (sg_work_t *)(void *)(((uintptr_t)wire_take() + 7u) & ~(uintptr_t)7u);   /* (as mod_signed sets it) */
        for (uint32_t v = 0; v < 3u; v++) {
            uint8_t pk[32], sig[64], msg[8];
            hex(pk, V[v][0]); hex(msg, V[v][1]); hex(sig, V[v][2]);
            sg_msg = msg;
            ok += !mod_sign_check(sig, pk, (uint32_t)strlen(V[v][1]) / 2u, sg_read);
            sig[5] ^= 4u;
            refused += mod_sign_check(sig, pk, (uint32_t)strlen(V[v][1]) / 2u, sg_read) != 0;
        }
        bad += check("Ed25519: RFC 8032's three vectors verify, each with a bit flipped refused", ok == 3u && refused == 3u);
    }
    printf("%s\n", bad ? "IDENTITY TEST FAILED" : "identity test passed");
    return bad ? 1 : 0;
}
