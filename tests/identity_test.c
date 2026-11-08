/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Who a synth module is, through real NOR saves (phase 6; phase 8: its ID made from its author's key and slug, the
 * install's refusals of a file whose ID or signature is not its own): its UID (its file's head and IDNT chunk), the device's
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
#include "mod_ident_test.h"
#include <assert.h>

static uint8_t file[MOD_FILE_MAX];
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
    h.uid = test_idnt(file, &at, vendor, test_slug(name), name, 0, &id);   /* (an outside author's, unsigned) */
    h.magic = MOD3_FILE_MAGIC; h.abi = MOD3_ABI; h.engine = (uint8_t)want; h.host = MOD3_HOST_MIN; h.flags = MOD3_FILE_CHUNKS;
    h.image = image; h.bss = 16; h.version = 1; h.total = at;
    strncpy(h.name, name, sizeof h.name);
    h.crc = st_crc32(file + MOD3_HEAD, at - MOD3_HEAD);
    memcpy(file, &h, sizeof h);
    return at;
}
static uint32_t install(uint32_t k, const char *vendor, const char *name, uint32_t want, uint32_t plen)
{
    uint32_t n = make(vendor, name, want, plen), uid;
    for (uint32_t j = 0; j < MOD_RECS; j++)
        if (st_exists(st_mod_obj(k, j))) (void)st_project_delete(st_mod_obj(k, j));
    assert(!st_save(st_mod_obj(k, 0), file, n));
    memcpy(&uid, file + 24, 4);                     /* (the head's uid) */
    return uid;
}
/* ---- phase 8: a file made by an author (seed: its key; 0 unsigned) naming key (0: the seed's own), its ID id (0: the
 * one its key and slug make), signed by signer (0: the seed); version v. Uploaded into slot k as an installer does */
static int put(uint32_t k, const uint8_t *b, uint32_t n)   /* (as editor_module.c: begin, 256-byte writes, commit) */
{
    int rc = mod_put_begin(k, n);
    for (uint32_t off = 0; !rc && off < n; off += 256u)
        rc = mod_put_write(k, off, b + off, n - off < 256u ? n - off : 256u);
    return rc ? rc : mod_put_commit(k);
}
static uint32_t authored(const char *vendor, const char *slug, const uint8_t *seed, const uint8_t *key,
                         const uint8_t *id, const uint8_t *signer, uint32_t v, mod_ident_v3 *out)
{
    mod_file3_t h;
    mod_ident_v3 m;
    uint8_t pk[32], sg[64];
    uint32_t image = 256u, at = MOD3_HEAD + image;
    memset(file, 0, sizeof file);
    memset(&h, 0, sizeof h);
    for (uint32_t i = 0; i < image; i++) file[MOD3_HEAD + i] = (uint8_t)(i * 5u + 3u);
    if (seed && !key) {
        test_sign(seed, 0, 0, sg, pk);
        key = pk;
    }
    uint32_t idnt = at;
    h.uid = test_idnt(file, &at, vendor, slug, "TEST", seed ? key : 0, &m);
    if (id) {                                       /* (another ID than its key and slug make: a forgery) */
        memcpy(file + idnt + 8u, id, 8);
        memcpy(m.id, id, 8);
        memcpy(&h.uid, id, 4);
    }
    if (seed)
        test_seal(file, &at, signer ? signer : seed);
    h.magic = MOD3_FILE_MAGIC; h.abi = MOD3_ABI; h.engine = 25; h.host = MOD3_HOST_MIN; h.flags = MOD3_FILE_CHUNKS;
    h.image = image; h.bss = 16; h.version = (uint16_t)v; h.total = at;
    strncpy(h.name, "JUNO-106", sizeof h.name);
    h.crc = st_crc32(file + MOD3_HEAD, at - MOD3_HEAD);
    memcpy(file, &h, sizeof h);
    if (out)
        *out = m;
    return at;
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
/* a power cycle with BOOT OFF: the modules, the settings record (the template after it: persist_boot), the template
 * loaded as the new project (project_boot) */
static void power(void)
{
    for (uint32_t k = 0; k < NMOD; k++) mod_unregister(k, 0), memset(&mods[k], 0, sizeof mods[k]);
    memset(&tmpl, 0, sizeof tmpl);
    host_tracks_init();
    mod_boot();
    persist_boot();
    project_boot();
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
    uint32_t juno_uid = install(0, "acme", "JUNO", 22, 32);
    install(1, "acme", "PPG", 22, 0);
    install(2, "acme", "FAKEFM", 12, 0);            /* (asks for a number of Melodee's own) */
    boot();
    uint32_t juno = mods[slot_of_name("JUNO")].engine, ppg = mods[slot_of_name("PPG")].engine,
             fake = mods[slot_of_name("FAKEFM")].engine;
    bad += check("IDs from the files (8 bytes: the head's uid their first 4), Melodee's synths at their manifest rows' "
                 "handles (the Prophet 2, FM6 3, SID 5)", (uint32_t)mods[0].uid == juno_uid && mods[0].uid >> 32 &&
                 eng_map[ENGI_FM6].uid == 0x918284C33909E71AULL && eng_map[ENGI_SID].uid == 0x8757CDD57F9158D4ULL &&
                 eng_map[ENGI_PROPHET].uid == 0x4624E600C585ED74ULL);
    bad += check("handles: the first free ones in slot order past the manifest's (JUNO 7, PPG 8, FAKEFM 9: what a file "
                 "asks for means nothing)", juno == ENG_H_FREE0 && ppg == ENG_H_FREE0 + 1u && fake == ENG_H_FREE0 + 2u &&
                 mods[2].state == MS_STORED && !strcmp(eng_name(juno), "JUNO") && !strcmp(eng_name(ppg), "PPG"));
    eng_map_service(1);                             /* (the main loop: saved once nothing plays) */
    bad += check("phase 9: the numbers are this device's, made again at power-on: no record of them", !st_exists(ST_MAPOBJ));
    (void)mod_delete(slot_of_name("FAKEFM"));
    install(3, "zeta", "OTHER", 23, 0);
    boot();
    bad += check("power-on: the handles again in slot order; a deleted module's free again (OTHER in slot 3: 9)",
                 mods[0].engine == ENG_H_FREE0 && mods[1].engine == ENG_H_FREE0 + 1u && mods[3].engine == ENG_H_FREE0 + 2u &&
                 mods[2].state == MS_EMPTY);
    /* ---- a project naming them */
    host_tracks_init();
    for (uint32_t i = 0; i < sizeof patch; i++) patch[i] = (uint8_t)(0x91u + 3u * i);
    trk[1].engine = trk[1].eng_req = (uint8_t)juno;
    trk[2].engine = trk[2].eng_req = (uint8_t)ppg;
    patch_set(1, juno, patch, sizeof patch, MOD3_PATCH_SET);
    bad += check("fixture: JUNO's patch on part 2 (32 bytes, no 1.x format)", patch_len(juno) == 32u &&
                 patch_store_tag(juno) == MOD_SCH_STORE_NONE && !memcmp(patch_of(1), patch, sizeof patch));
    bad += check("a project naming them saves as FBKS (its entries, JUNO's patch)",
                 !project_save_as(1, "MAPPED") && stored_magic(1) == BANKS_MAGIC);
    host_tracks_init();
    for (uint32_t k = 0; k < NPART; k++) host_preset(&trk[k], TRK_DEF[k][0], TRK_DEF[k][1]);
    bad += check("a project of Melodee's own synths: FBKS too (2.0: every record names its synths)",
                 !project_save_as(2, "PLAIN") && stored_magic(2) == BANKS_MAGIC);
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
    bad += check("on another device: OTHER at 7; JUNO and PPG lent free handles, their names from the project's entries",
                 mods[0].engine == ENG_H_FREE0 && j2 < NENGINES && j2 != ENG_H_FREE0 && p2 < NENGINES && p2 != ENG_H_FREE0 && p2 != j2 &&
                 trk[1].eng_req == j2 && trk[2].eng_req == p2);
    bad += check(".. JUNO's patch kept with it (its number here), the parts silent", patch_eng_of(1) == j2 &&
                 !memcmp(patch_of(1), patch, sizeof patch) && silent);
    bad += check(".. saved again: FBKS, the same patch back after a load", !project_save_as(3, "AGAIN") &&
                 stored_magic(3) == BANKS_MAGIC && (host_tracks_init(), project_load(3), 1) && trk[1].eng_req == j2 &&
                 !memcmp(patch_of(1), patch, sizeof patch) && !strcmp(eng_name(j2), "JUNO"));
    /* ---- a user-preset bank */
    host_tracks_init();
    uint32_t oth = mods[0].engine;                  /* (OTHER's handle here) */
    trk[0].engine = trk[0].eng_req = (uint8_t)oth;  /* OTHER's sound (its module not loaded here: its E values 0) */
    for (uint32_t i = 0; i < 8u; i++) trk[0].p[P_E0 + i] = 0;
    patch_set(0, oth, patch, 16, MOD3_PATCH_SET);
    bad += check("a user preset of OTHER (7) stored", !up_store_to(5, 0, "OTHER SOUND") && up_used(5));
    for (uint32_t k = 0; k < NMOD; k++) (void)mod_delete(k);
    eng_map_service(1);
    eng_map_defaults();
    install(0, "acme", "FIRST", 22, 0);             /* (22 is someone else's here) */
    install(1, "zeta", "OTHER", 22, 16);
    boot();
    up_cache_reset();
    uint32_t o3 = mods[1].engine;
    bad += check("its bank read on a device where OTHER is not 7: the record names OTHER's handle here",
                 o3 != oth && up_rec(5)->engine == o3 && up_used(5) && !memcmp(up_rec(5)->packed, patch, 16));
    /* ---- the data records' owners */
    rec_uid[3] = 0x0123456789ABCDEFULL;
    eng_map_dirty = 1;
    boot();
    bad += check("records' owners: the default (DRUM's, the manifest's General MIDI kit synth) and one kept by the owners "
                 "record (its whole ID)", rec_owner_uid(0) == 0x241AACC57D998FFEULL &&
                 rec_owner_uid(3) == 0x0123456789ABCDEFULL);
    {   /* an editor's PUT (a backup object, a module upload) is one stream over many messages, the main loop running
         * between them: the map record's save waits for it (else the stream goes stale: the PUT fails, rc 4) */
        static uint8_t body[1000];
        st_stream_t w;
        eng_map_dirty = 1;                          /* (a save owed) */
        int rc = st_stream_begin(&w, st_proj_obj(2), sizeof body);
        mod_service(1000);
        uint32_t g = st_stream_generation;
        rc |= st_stream_write(&w, body, 500);
        mod_service(2000);
        rc |= st_stream_write(&w, body + 500, 500);
        mod_service(3000);
        int waited = eng_map_dirty && st_stream_generation == g && w.generation == g;
        mod_service(5100);                          /* (no write for 2 s: an abandoned stream, the map saved) */
        bad += check("the map record waits while a stream is being written (an editor's PUT), at most 2 s past its last "
                     "write", !rc && waited && !eng_map_dirty && st_stream_generation != g);
        eng_map_dirty = 1;
        rc = st_stream_begin(&w, st_proj_obj(2), 8);
        mod_service(5200);
        rc |= st_stream_write(&w, body, 8);
        int held = eng_map_dirty;
        (void)st_stream_publish(&w);                /* (the stream closed: published or refused) */
        mod_service(5300);
        bad += check(".. and once it is published, at once", !rc && held && !eng_map_dirty);
    }
    {   /* ---- the template (SAVE > PROJECT, SLOT TMPL): its end block (ST_TPLX) keeps an outside synth's number and patch */
        uint8_t tp[16];
        for (uint32_t i = 0; i < sizeof tp; i++) tp[i] = (uint8_t)(0x21u + 5u * i);
        host_tracks_init();
        for (uint32_t k = 0; k < NPART; k++) host_preset(&trk[k], TRK_DEF[k][0], TRK_DEF[k][1]);
        persist_old = tmpl_old = 0;
        template_save();
        bad += check("template of Melodee's own synths: no end block (its settings record as it always was)",
                     template_used() && !st_exists(ST_TPLX));
        uint32_t other = mods[slot_of_name("OTHER")].engine;
        trk[2].engine = trk[2].eng_req = (uint8_t)other;
        patch_set(2, other, tp, sizeof tp, MOD3_PATCH_SET);
        template_save();
        bad += check("template with OTHER on track 3 (a 16-byte patch, no 1.x format): its end block", st_exists(ST_TPLX));
        power();
        bad += check(".. a power cycle: track 3 OTHER again, its patch", template_used() && trk[2].eng_req == other &&
                     patch_eng_of(2) == other && !memcmp(patch_of(2), tp, sizeof tp));
        for (uint32_t k = 0; k < NMOD; k++) (void)mod_delete(k);
        eng_map_service(1);
        eng_map_defaults();
        install(0, "zeta", "OTHER", 22, 16);
        power();
        uint32_t here = mods[0].engine;
        bad += check(".. where OTHER has another handle (7 here): that one, its patch", here == ENG_H_FREE0 && here != other &&
                     trk[2].eng_req == here && patch_eng_of(2) == here && !memcmp(patch_of(2), tp, sizeof tp));
        (void)mod_delete(0);
        eng_map_service(1);
        power();
        uint32_t gone = eng_of_name("OTHER");
        bad += check(".. OTHER not on the device: a number of its own, its name, its patch kept", gone < NENGINES &&
                     trk[2].eng_req == gone && patch_eng_of(2) == gone && !memcmp(patch_of(2), tp, sizeof tp));
        template_save();
        power();
        bad += check(".. saved again from there and a power cycle: the same", trk[2].eng_req == eng_of_name("OTHER") &&
                     eng_of_name("OTHER") < NENGINES && !memcmp(patch_of(2), tp, sizeof tp));
        install(1, "zeta", "OTHER", 22, 16);
        install(0, "acme", "FIRST", 22, 0);
        boot();
    }
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
    {   /* ---- a fault's own module by its address (main.c fm1_fault), CPU1 spared for it (fm1_core1_bar.h) */
        static uint8_t blk[64];
        mod_run.magic = 0; mod_run.spared = MODRUN_SPARED; mod_run.alive = 0;   /* power-on: the noinit's garbage */
        boot();
        bad += check("power-on: CPU1's spare back", mod_run.spared == 0);
        void *was = resource[RES_MODULE0 + 1].ptr;
        uint32_t was_n = resource[RES_MODULE0 + 1].size;
        resource[RES_MODULE0 + 1].ptr = blk; resource[RES_MODULE0 + 1].size = sizeof blk;
        uintptr_t in = (uintptr_t)(blk + 40), past = (uintptr_t)(blk + sizeof blk), fw = (uintptr_t)&mod_own_pc;
        bad += check("an address in slot 1's block: slot 1's own (past it, or the firmware's: nobody's)",
                     mod_own_pc(in) == 2u && !mod_own_pc(past) && !mod_own_pc(fw));
        bad += check("a CPU0 crash in a module's own code: CPU1 spared once a power-on, the next crash bars it",
                     mod_spare_core1(in, 1) && mod_run.spared == MODRUN_SPARED && !mod_spare_core1(in, 1));
        bad += check(".. a CPU1 fault in a module's own code: spared (CPU1 held until the next start)",
                     mod_spare_core1(in, 0));
        bad += check("a crash outside every module: never spared", !mod_spare_core1(fw, 1) && !mod_spare_core1(fw, 0));
        mod_run.alive = 1; mod_run.crashed = 1; mod_run.loop = 0; mod_run.audio = 1;   /* the marker: slot 0 */
        mod_fault_own(in);
        bad += check(".. its fault in slot 1's code: slot 1 named, whatever the audio's marker said", mod_run.loop == 2u);
        resource[RES_MODULE0 + 1].ptr = was; resource[RES_MODULE0 + 1].size = was_n;
        boot();
        bad += check(".. the next start leaves out slot 1, not slot 0", mod_blamed == 2u &&
                     mods[1].state == MS_SKIPPED && mods[0].state == MS_STORED);
        bad += check(".. and a soft reset keeps the spare used", mod_run.spared == MODRUN_SPARED);
        mod_run.alive = 0; mod_run.spared = 0;
        boot();
    }
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
        uint8_t pk[32], sig[64];
        static const uint8_t msg[3] = {1, 2, 3};
        test_sign(TEST_SEED, msg, sizeof msg, sig, pk);
        sg_msg = msg;
        mod_sign_begin();
        bad += check(".. the tests' signer (mod_ident_test.h): the test key's seed makes the host's test key, its signature "
                     "verifies", !memcmp(pk, MOD_SIGN_KEYS[1], 32) && !mod_sign_check(sig, pk, sizeof msg, sg_read));
    }
    /* ---- phase 8: an ID is its author's (its key and slug); the install refuses one that is not its own */
    {
        static const uint8_t A[32] = {1, 2, 3}, B[32] = {9, 8, 7};   /* (two authors' keys' seeds) */
        mod_ident_v3 ia, ib, ix;
        uint32_t n;
        int ra, rb, rx;
        for (uint32_t k = 0; k < NMOD; k++) (void)mod_delete(k);
        mod_run.alive = 0; mod_run.crashed = 0; mod_run.loop = 0; bootguard.failed = 0;
        boot();
        mod_skipped = 0; mod_blamed = 0;
        n = authored("Saadet", "juno-106", A, 0, 0, 0, 1, &ia);
        ra = put(4, file, n);
        n = authored("Copycat", "juno-106", B, 0, 0, 0, 1, &ib);
        rb = put(5, file, n);
        bad += check("phase 8: two authors' synths with the same slug (juno-106): two IDs, both installed, signed by their "
                     "authors", memcmp(ia.id, ib.id, 8) && ra != -4 && rb != -4 && mods[4].len && mods[5].len &&
                     mods[4].uid != mods[5].uid && mods[4].sig == MOD_SIG_AUTHOR && mods[5].sig == MOD_SIG_AUTHOR);
        ui.msg[0] = 0;
        n = authored("Copycat", "juno-106", B, 0, ia.id, 0, 1, 0);   /* (Saadet's ID, B's key) */
        rx = put(6, file, n);
        bad += check(".. a file claiming Saadet's ID with another author's key: refused, said so, its slot empty",
                     rx == -4 && !mods[6].len && !st_exists(st_mod_obj(6, 0)) && !strcmp(ui.msg, "NOT ITS OWN: JUNO-106"));
        n = authored("Saadet", "juno-106", A, ia.key, 0, B, 1, 0);   /* (Saadet's key named, B signing) */
        rx = put(6, file, n);
        bad += check(".. a file naming Saadet's key, signed by another: refused", rx == -4 && !mods[6].len);
        n = authored("melodee", "trio", B, MOD_SIGN_KEYS[0], 0, B, 1, 0);   /* (Melodee's key named, an author signing) */
        rx = put(6, file, n);
        bad += check(".. a file naming Melodee's key, signed by an author: refused", rx == -4 && !mods[6].len);
        n = authored("melodee", "trio", TEST_SEED, MOD_SIGN_KEYS[0], 0, TEST_SEED, 1, &ix);
        rx = put(6, file, n);
        bad += check(".. Melodee's key named, one of Melodee's keys signing (the host's test key): Melodee's, TRIO's ID, "
                     "a handle as any module's", rx != -4 && mods[6].sig == MOD_SIG_MELODEE &&
                     mods[6].uid == 0xC273261A2CF92788ULL && mods[6].engine >= ENG_H_FREE0 && mods[6].engine < NENGINES);
        (void)mod_delete(6);
        ui.msg[0] = 0;
        n = authored("Saadet", "juno-106", 0, 0, 0, 0, 1, &ix);
        rx = put(6, file, n);
        bad += check(".. unsigned: installed with the warning, an ID of its own (not the signed one's)",
                     rx != -4 && mods[6].len && mods[6].sig == MOD_SIG_NONE && !strcmp(ui.msg, "UNSIGNED: JUNO-106") &&
                     memcmp(ix.id, ia.id, 8));
        n = authored("Saadet", "juno-106", A, 0, 0, 0, 2, &ix);
        rx = put(4, file, n);
        bad += check(".. an update (Saadet's key, version 2) into its slot: replaces it, the same ID",
                     rx != -4 && mods[4].version == 2u && !memcmp(&mods[4].uid, ia.id, 8) && !memcmp(ix.id, ia.id, 8) &&
                     mods[4].sig == MOD_SIG_AUTHOR);
    }
    /* ---- phase 9: stores name synths by their IDs; engine numbers are this device's handles */
    {
        static char nm[27][8];
        static uint8_t pt[16];
        uint64_t id[27];
        uint32_t maxe = 0, distinct = 1, stored = 1;
        for (uint32_t i = 0; i < sizeof pt; i++) pt[i] = (uint8_t)(0x30u + i);
        for (uint32_t i = 0; i < 27u; i++) snprintf(nm[i], sizeof nm[i], "S%02u", i + 1u);
        eng_map_spare = proj_engine_spare;           /* (persist_boot sets it) */
        for (uint32_t k = 0; k < NMOD; k++) (void)mod_delete(k);
        mod_run.alive = 0; mod_run.crashed = 0; mod_run.loop = 0; bootguard.failed = 0;
        for (uint32_t i = 0; i < 12u; i++) install(i, "acme", nm[i], 22, 16);
        boot();
        mod_skipped = 0; mod_blamed = 0;
        for (uint32_t i = 0; i < 12u; i++) {
            id[i] = mods[i].uid;
            stored &= mods[i].state == MS_STORED;
            maxe = mods[i].engine > maxe ? mods[i].engine : maxe;
            for (uint32_t j = 0; j < i; j++) distinct &= mods[j].engine != mods[i].engine;
        }
        bad += check("phase 9: 12 outside synths installed, a handle each (7..18)", stored && distinct &&
                     maxe == ENG_H_FREE0 + 11u);
        /* a project naming the 12: parts 1..4 on S01..S04, eight song sounds on S05..S12 */
        host_tracks_init();
        song_sound_reset();
        chain_config.count = 8;
        for (uint32_t i = 4; i < 12u; i++) {
            song_sound_t snd;
            trk[0].engine = trk[0].eng_req = mods[i].engine;
            patch_set(0, mods[i].engine, pt, sizeof pt, MOD3_PATCH_SET);
            song_sound_capture(&snd, &trk[0], nm[i]);
            chain_config.sound[i - 4u][0] = (uint8_t)song_sound_store(&snd);
        }
        for (uint32_t k = 0; k < 4u; k++) {
            trk[k].engine = trk[k].eng_req = mods[k].engine;
            patch_set(k, mods[k].engine, pt, sizeof pt, MOD3_PATCH_SET);
        }
        bad += check(".. a project naming them saves (FBKS: 12 entries, by ID)", !project_save_as(4, "TWELVE") &&
                     stored_magic(4) == BANKS_MAGIC);
        /* a device without any of them */
        for (uint32_t k = 0; k < NMOD; k++) (void)mod_delete(k);
        boot();
        host_tracks_init();
        project_load(4);
        uint32_t named = 1, eng[12];
        for (uint32_t k = 0; k < 4u; k++)
            named &= eng_map[eng[k] = trk[k].eng_req].uid == id[k] && !strcmp(eng_name(trk[k].eng_req), nm[k]);
        for (uint32_t r = 0; r < 8u; r++) {
            const song_sound_t *snd = song_sound_get(chain_config.sound[r][0]);
            named &= snd && eng_map[eng[4u + r] = snd->engine].uid == id[4u + r] && !strcmp(eng_name(snd->engine), nm[4u + r]);
        }
        distinct = 1;
        for (uint32_t i = 0; i < 12u; i++)
            for (uint32_t j = 0; j < i; j++) distinct &= eng[i] != eng[j];
        bad += check(".. on a device without them: each lent a number, named by its ID and name, the parts silent",
                     named && distinct && !mod_have(eng[0]));
        bad += check(".. saved again there: every ID kept", !project_save_as(5, "AGAIN12") && stored_magic(5) == BANKS_MAGIC);
        /* a device that has them, installed in the other order (other numbers) */
        for (uint32_t i = 0; i < 12u; i++) install(11u - i, "acme", nm[i], 22, 16);
        boot();
        host_tracks_init();
        project_load(5);
        named = 1;
        for (uint32_t k = 0; k < 4u; k++)
            named &= trk[k].eng_req == mods[11u - k].engine && !memcmp(patch_of(k), pt, sizeof pt);
        for (uint32_t r = 0; r < 8u; r++) {
            const song_sound_t *snd = song_sound_get(chain_config.sound[r][0]);
            named &= snd && snd->engine == mods[11u - (4u + r)].engine;
        }
        bad += check(".. that save on a device that has them (installed in another order: other numbers): every part and "
                     "sound plays its own synth", named && mods[11].engine != eng[0]);
        /* a number given back: 27 synths named one after another on a device without them (23 handles) */
        for (uint32_t k = 0; k < NMOD; k++) (void)mod_delete(k);
        for (uint32_t i = 12; i < 27u; i++) install(i - 12u, "acme", nm[i], 22, 16);
        boot();
        host_tracks_init();
        song_sound_reset();
        chain_config.count = 11;
        for (uint32_t i = 16; i < 27u; i++) {
            song_sound_t snd;
            trk[0].engine = trk[0].eng_req = mods[i - 12u].engine;
            song_sound_capture(&snd, &trk[0], nm[i]);
            chain_config.sound[i - 16u][0] = (uint8_t)song_sound_store(&snd);
        }
        for (uint32_t k = 0; k < 4u; k++) trk[k].engine = trk[k].eng_req = mods[k].engine;
        int saved15 = !project_save_as(6, "FIFTEEN");
        for (uint32_t k = 0; k < NMOD; k++) (void)mod_delete(k);
        boot();
        host_tracks_init();
        project_load(4);                             /* (12 lent) */
        host_tracks_init();
        song_sound_reset();                          /* (another project: nothing names those 12 any more) */
        project_load(6);                             /* (15 more: 27 > 23, the 12 given back as needed) */
        named = 1;
        for (uint32_t k = 0; k < 4u; k++) named &= !strcmp(eng_name(trk[k].eng_req), nm[12u + k]);
        for (uint32_t r = 0; r < 11u; r++) {
            const song_sound_t *snd = song_sound_get(chain_config.sound[r][0]);
            named &= snd && snd->engine < NENGINES && !strcmp(eng_name(snd->engine), nm[16u + r]);
        }
        bad += check(".. 27 synths named one after another where 23 handles are: the ones nothing names any more given "
                     "back, the 15 of the last project all named", saved15 && named);
        /* Motion: a stored native event's synth by this device's number */
        motion_store_t ms;
        memset(&ms, 0, sizeof ms);
        ms.count = 2;
        ms.event[0].param = MO_NATIVE + 1u;
        ms.event[0].value = (int16_t)((MO_TAG_MOD + 7u - ENG_H_SHIP0) << 8 | 7u);
        ms.event[1].param = MO_NATIVE + 2u;
        ms.event[1].value = (int16_t)((MO_TAG_MOD + ENGI_PROPHET - ENG_H_SHIP0) << 8 | 9u);   /* (the Prophet's) */
        eng_remap_reset();
        eng_remap[7] = 13u + 1u;                        /* (the store's 7 is 13 here) */
        eng_remap[ENGI_PROPHET] = ENGI_PROPHET + 1u;    /* (its Prophet this device's) */
        motion_tags_in(&ms);
        eng_remap_reset();
        bad += check(".. Motion: a stored event's synth made this device's (7 there, 13 here: tag 9 -> 15), the "
                     "Prophet's as its store names it", ms.event[0].value == (int16_t)((MO_TAG_MOD + 13u - ENG_H_SHIP0) << 8 | 7u) &&
                     ms.event[1].value == (int16_t)((MO_TAG_MOD + ENGI_PROPHET - ENG_H_SHIP0) << 8 | 9u) &&
                     motion_tag_engine(MO_TAG_MOD) == ENGI_PROPHET);
    }
    printf("%s\n", bad ? "IDENTITY TEST FAILED" : "identity test passed");
    return bad ? 1 : 0;
}
