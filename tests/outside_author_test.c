/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* An outside author's synth module (tests/run_tests.sh outside_author): firmware/modules/sdk copied alone into a
 * folder of its own with one module's source (TRIO's), the module built there by the SDK's builder (no Melodee tree,
 * no key: unsigned) and, beside this test, the same source built for the host from that folder (its object: the code the
 * host plays). Here the file is installed into a slot as the editor's upload writes it, checked, listed, its number
 * given, loaded and played: sample for sample what the firmware's own reference of the same synth plays (the retired
 * built-in TRIO, firmware/modules/host). It installs with the unsigned warning; with the in-tree build's file (signed)
 * it installs without one, and the two files are the same module (image, address words, presets, who it is).
 *   outside_author_test OUTSIDE.mmod INTREE.mmod */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"
#include "../firmware/src/sysex_route.c"
#include "../firmware/src/module_store.c"

extern const mod_desc_v3 mod_trio;               /* (the outside folder's object) */
static const mod_desc_v3 *oa_desc(uint32_t e) { return e == 6u ? &mod_trio : 0; }
static uint8_t file[2][MOD_FILE_MAX];
static uint32_t file_n[2];
static uint32_t load(const char *path, uint8_t *b)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    uint32_t n = (uint32_t)fread(b, 1, MOD_FILE_MAX, f);
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
/* preset p, three notes, 1.5 s on part 0: into out */
static void phrase(uint32_t p, int32_t *out, uint32_t n)
{
    host_tracks_init();
    for (uint32_t k = 0; k < NPART; k++) memset(trk[k].v, 0, NVOICE * sizeof *trk[k].v);
    eng_state_reset();
    vage = 0;                                       /* (as tests/module_test.c phrase: the same dither, the same ages) */
    dith_r = DITH_SEED;
    host_preset(&trk[0], 6, p);
    for (uint32_t i = 0; i < n; i += CTL) {
        if (i == 0 || i == 128u * CTL || i == 640u * CTL)
            trk_note_on(&trk[0], 45u + (i / CTL) % 24u, 100);
        if (i == 1500u * CTL)
            trk_note_off(&trk[0], 45u);
        dither_block();
        track_render(&trk[0], out + i, CTL);
    }
}
/* a file's body as the device reads it, without its signature (the SIGN chunk, last, if any): its length */
static uint32_t unsigned_len(const uint8_t *b, uint32_t n)
{
    mod_file3_t h;
    memcpy(&h, b, sizeof h);
    uint32_t at = MOD3_HEAD + h.image + 4u * h.nrel, last = n;
    while (at + 8u <= n) {
        uint32_t tag, len;
        memcpy(&tag, b + at, 4);
        memcpy(&len, b + at + 4, 4);
        if (tag == MOD3_SIGN_TAG)
            last = at;
        at += 8u + ((len + 3u) & ~3u);
    }
    return last;
}

int main(int argc, char **argv)
{
    int bad = 0;
    static int32_t a[2048u * CTL], b[2048u * CTL];
    if (argc < 3 || !(file_n[0] = load(argv[1], file[0])) || !(file_n[1] = load(argv[2], file[1]))) {
        printf("outside author: outside_author_test OUTSIDE.mmod INTREE.mmod\n");
        return 1;
    }
    reset();
    mod_host_desc = oa_desc;
    mod_boot();
    ui.msg[0] = 0;
    int rc = put(3, file[0], file_n[0]);
    bad += check("the outside folder's file installs (an upload into slot 3), its number its own (6: TRIO's UID), loaded",
                 rc == MS_LOADED && mods[3].engine == 6u && mods[3].uid == 0xDEA21A21u && eng_ok(6) &&
                 !strcmp(eng_name(6), "TRIO"));
    bad += check(".. with the one-line warning: no Melodee signature", mods[3].sig == 2u && !strcmp(ui.msg, "UNSIGNED: TRIO"));
    uint32_t same = 1, heard = 0;
    for (uint32_t p = 0; p < 5u; p++) {
        phrase(p, a, sizeof a / sizeof a[0]);
        const engine_t *was = ENGINES[6];
        uint8_t at = mod_at[6];
        ENGINES[6] = &ENG_TRIO;                     /* (the firmware's own reference of the synth) */
        mod_at[6] = 0;
        phrase(p, b, sizeof b / sizeof b[0]);
        ENGINES[6] = was;
        mod_at[6] = at;
        for (uint32_t i = 0; i < sizeof a / sizeof a[0]; i++) {
            if (same && a[i] != b[i]) printf("  preset %u: sample %u module %d reference %d\n", p, i, a[i], b[i]);
            same &= a[i] == b[i];
            heard |= a[i] != 0;
        }
    }
    bad += check("it plays: every preset's phrase sample for sample as the firmware's reference TRIO", same && heard);
    mod_unregister(3, 0);
    memset(&mods[3], 0, sizeof mods[3]);
    mod_boot();
    bad += check("power-on: listed from its record, the same number", mods[3].state == MS_STORED && mods[3].engine == 6u);
    (void)mod_delete(3);
    ui.msg[0] = 0;
    rc = put(3, file[1], file_n[1]);
    uint32_t n0 = unsigned_len(file[0], file_n[0]), n1 = unsigned_len(file[1], file_n[1]);
    mod_file3_t h0, h1;
    memcpy(&h0, file[0], sizeof h0);
    memcpy(&h1, file[1], sizeof h1);
    h0.total = h1.total = 0;
    h0.crc = h1.crc = 0;
    bad += check("the in-tree build's file: signed, no warning; the same module as the outside one (head, image, address "
                 "words, presets, who it is)",
                 rc == MS_LOADED && mods[3].sig == 1u && !ui.msg[0] && n0 == n1 && !memcmp(&h0, &h1, sizeof h0) &&
                 !memcmp(file[0] + MOD3_HEAD, file[1] + MOD3_HEAD, n0 - MOD3_HEAD));
    printf("%s\n", bad ? "OUTSIDE AUTHOR TEST FAILED" : "outside author test passed");
    return bad ? 1 : 0;
}
