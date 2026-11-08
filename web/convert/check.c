/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The converter's other half in the tests: Melodee 2.0's virtual FM-1 (this tree's web/core/melodee_core.c) takes the
 * records web/convert/legacy.c wrote through its own backup protocol, as an FM-1 running 2.0 takes them from the web
 * installer, and plays a project for the parity renders (tests/convert_test.sh):
 *   check_native MODDIR put DIR          each DIR/v2-<id>.bin PUT as object id; rc per object, then the LIST
 *   check_native MODDIR render REC S OUT  a 2.0 project record in slot 1, played S seconds (interleaved float) */
#include "melodee_core.c"
#include "bk_client.h"
#define PP_ID(t) (eng_map[(t)->eng_req % NENGINES].uid)
#define PP_E0(t, v) (v)
#include "preset_play.h"

extern const mod_desc_v3 mod_sid, mod_808, mod_909, mod_sidkit, mod_cr78, mod_fmkit, mod_sampler, mod_cz, mod_fm6, mod_p5;
static int ck_boot_modules(const char *dir)
{
    static const char *const stem[] = {"sid", "808", "909", "sidkit", "cr78", "fmkit", "sampler", "cz", "fm6", "prophet"};
    static const mod_desc_v3 *const desc[] = {&mod_sid, &mod_808, &mod_909, &mod_sidkit, &mod_cr78, &mod_fmkit, &mod_sampler,
                                              &mod_cz, &mod_fm6, &mod_p5};
    core_boot(0);
    core_poll(100);
    for (uint32_t i = 0; i < sizeof stem / sizeof *stem; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.mmod", dir, stem[i]);
        FILE *f = fopen(path, "rb");
        if (!f) { printf("check: no %s\n", path); return 1; }
        uint32_t n = (uint32_t)fread(scratch, 1, sizeof scratch, f);
        fclose(f);
        int32_t rc = 0;
        if (core_mod_link(desc[i], core_head_uid(scratch, n)) || (rc = core_mod_install(scratch, n)) < 0) { printf("check: %s not installed (%d)\n", path, (int)rc); return 1; }
    }
    core_poll(100);
    core_boot(1);
    core_poll(100);
    return 0;
}
static uint8_t ck_buf[1u << 20];
static uint32_t ck_read(const char *path, uint8_t *b, uint32_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t n = (uint32_t)fread(b, 1, max, f);
    fclose(f);
    return n;
}
int main(int argc, char **argv)
{
    if (argc < 3 || ck_boot_modules(argv[1])) { printf("usage: check_native MODDIR put DIR | presets DIR OUT | render REC S OUT\n"); return 2; }
    if (argc == 4 && !strcmp(argv[2], "put")) {
        uint32_t bad = 0;
        for (uint32_t id = 0; id < 100u; id++) {
            char path[600];
            snprintf(path, sizeof path, "%s/v2-%u.bin", argv[3], (unsigned)id);
            uint32_t n = ck_read(path, ck_buf, sizeof ck_buf), rc;
            if (!n) continue;
            rc = lg_bk_put(id, ck_buf, n);
            core_poll(50);
            printf("put %u (%u B): rc %#x\n", (unsigned)id, (unsigned)n, (unsigned)rc);
            bad += rc != 0;
        }
        uint32_t rc = lg_bk_list();
        printf("list: rc %#x\n", (unsigned)rc);
        for (uint32_t id = 0; id < 256u; id++)
            if (lg_list[id] > 1u) printf("  %u: %u B\n", (unsigned)id, (unsigned)lg_list[id] - 1u);
        return bad || rc;
    }
    if (argc == 5 && !strcmp(argv[2], "presets")) {   /* DIR's objects in, each user preset played to OUT-k.f32, the stars */
        for (uint32_t id = 1; id < 100u; id++) {
            char path[600];
            snprintf(path, sizeof path, "%s/v2-%u.bin", argv[3], (unsigned)id);
            uint32_t n = ck_read(path, ck_buf, sizeof ck_buf);
            if (n && lg_bk_put(id, ck_buf, n)) { printf("check: put %u refused\n", (unsigned)id); return 1; }
        }
        core_poll(100);
        for (uint32_t k = 0; k < UP_SLOTS; k++)
            if (up_used(k) && pp_state(k, stdout)) printf("preset %u: not loaded\n", (unsigned)k);
        for (uint32_t e = 0; e < NENGINES; e++)
            for (uint32_t p = 0; e && eng_map[e].uid && p < 256u; p++)
                if (favorite_has(e, p)) printf("star %.16s %u\n", eng_map[e].name, (unsigned)p);
        return 0;
    }
    if (argc == 6 && !strcmp(argv[2], "render")) {
        uint32_t n = ck_read(argv[3], ck_buf, sizeof ck_buf), rc, secs = (uint32_t)atoi(argv[4]);
        if ((rc = core_load_record(ck_buf, n, 0)) != 0) { printf("check: load %u\n", (unsigned)rc); return 1; }
        core_poll(30);
        if ((rc = core_song_prepare(0)) != 0) { printf("check: song %u\n", (unsigned)rc); return 1; }
        FILE *f = fopen(argv[5], "wb");
        if (!f) return 2;
        for (uint32_t left = secs * FS; left;) {
            uint32_t k = left > 1024u ? 1024u : left;
            fwrite(core_render(k), sizeof(float), 2u * k, f);
            left -= k;
        }
        fclose(f);
        printf("check: rendered %u s\n", (unsigned)secs);
        return 0;
    }
    return 2;
}
