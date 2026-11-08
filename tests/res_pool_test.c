/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The resource region's sectors the factory sounds left (phase 6: 6 of the FM-1's 15, hal/fm1_flash_map.h
 * FL_RES_RESERVE) given to the user pool, on a device installed before: tests/fixtures/pre-phase1-c53538c9-fm1.bin is
 * the data flash of a c53538c9 install (before phase 1: its 53,884-byte resource region in 14 of its 15 sectors), written
 * by c53538c9's own code (its generator in the phase 6 notes): four projects (a song's own sounds, a user kit on a drum
 * track, every synth), user presets of every synth, three recorded user kits, the settings; then what c53538c9 read
 * back (each project's record bytes and its tracks loaded, each preset bank's bytes, each kit's bytes).
 * Here the update loader's resource write is played as ldr_core.c ldr_res does it with this build's res.bin (its window
 * 9: the header's sector erased, the payload's sectors written, the rest of the window blank, the header last), cut
 * after every step, and this firmware boots after each: nothing is lost (every record's bytes as c53538c9 wrote them,
 * every project loads to c53538c9's tracks, the kits listed), the pool stays out of what a cut left undecided, and once
 * the header stands the pool has the 6 sectors more, taken last, their old bytes no trouble.
 *   res_pool_test FIXTURE RES.bin */
static unsigned char nor[0x100000];
#define BACKUP_TEST_NO_MAIN 1
#define BACKUP_TEST_KITS 1
#define REC_BASE ((const uint8_t *)nor)
#include "backup_test.c"
#include <assert.h>
#define FL_DATA_HI 0x000E0000u                      /* (fm1_flash.h) */
static uint8_t res_ok;
const uint8_t _mres_start[1], _mres_end[1];         /* (res_want unpatched: res_ok stays 0, the window is what counts) */
#include "../firmware/src/res.c"

static uint8_t fx_nor[sizeof nor], res_bin[0x10000], rec_buf[BANK_STORE_SIZE + 64];
static uint32_t fx_proj[4][3], fx_bank[UP_BANKS], fx_kit[8], res_n;
static uint32_t rp_fnv(const void *p, uint32_t n, uint32_t h)
{
    const uint8_t *b = p;
    for (uint32_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x01000193u;
    return h;
}
static int fixture(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint32_t n, off;
    char m[4];
    if (!f || fread(m, 1, 4, f) != 4 || memcmp(m, "PRE1", 4) || fread(&n, 4, 1, f) != 1) return 0;
    memset(fx_nor, 0xFF, sizeof fx_nor);
    for (uint32_t k = 0; k < n; k++)
        if (fread(&off, 4, 1, f) != 1 || off < 0x93000u || off + 0x1000u > sizeof fx_nor || fread(fx_nor + off, 1, 0x1000u, f) != 0x1000u)
            return 0;
    int ok = fread(fx_proj, sizeof fx_proj, 1, f) == 1 && fread(fx_bank, sizeof fx_bank, 1, f) == 1 && fread(fx_kit, sizeof fx_kit, 1, f) == 1;
    fclose(f);
    return ok;
}
static uint32_t boot(void)
{
    uint32_t n = 0;
    up_cache_reset();
    (void)res_window();
    st_old_scan();
    if (st_old) (void)st_convert(wire_take(), 0);
    res_boot(st_held(RES_BASE, RES_BASE + RES_RESERVE * ST_SECTOR) == 0);
    rec_scan();
    for (uint32_t i = 0; i < ST_LN; i++) n += st_lin_ok(i);
    return n;
}
/* every record as c53538c9 wrote it: projects' bytes, preset banks', kits'; the kits listed */
static int records_kept(void)
{
    int ok = 1;
    for (uint32_t j = 0; j < 4u; j++) {
        st_hdr_t h;
        uint32_t obj = st_proj_obj(fx_proj[j][0]);
        int copy = st_current(obj, &h);
        ok &= copy >= 0 && pj_image(obj, (uint32_t)copy, rec_buf) == 0 && rp_fnv(rec_buf, BANK_STORE_SIZE, 2166136261u) == fx_proj[j][1];
    }
    for (uint32_t b = 0; b < UP_BANKS; b++) {
        st_hdr_t h;
        int copy = st_current(up_obj(b), &h);
        uint32_t v = copy >= 0 && !st_read_range(up_obj(b), (uint32_t)copy, 0, rec_buf, h.len) ? rp_fnv(rec_buf, h.len, 2166136261u) : 0u;
        ok &= v == fx_bank[b];
    }
    for (uint32_t u = 0; u < 8u; u++) {
        st_hdr_t h;
        int copy = st_current(rec_obj(u), &h);
        uint32_t v = copy >= 0 && !st_read_range(rec_obj(u), (uint32_t)copy, 0, rec_buf, h.len) ? rp_fnv(rec_buf, h.len, 2166136261u) : 0u;
        ok &= v == fx_kit[u] && (rec_tab[u].n != 0) == (fx_kit[u] != 0);
    }
    return ok;
}
/* the update loader's resource write (ldr_core.c ldr_res: this build's window inside the installed one), stopped after
 * `steps` of its flash steps (header sector erased; each sector of the window written or blanked; the header): how
 * many it has */
static uint32_t ldr_write(uint32_t steps)
{
    uint32_t done = 0, want = (uint32_t)res_bin[12] | (uint32_t)res_bin[13] << 8;
    uint8_t sec[ST_SECTOR];
    if (done++ < steps) memset(nor + RES_BASE, 0xFF, ST_SECTOR);                   /* the header's sector first */
    for (uint32_t s = 1; s < want; s++) {
        uint32_t at = s * ST_SECTOR;
        memset(sec, 0xFF, sizeof sec);
        if (at < res_n) memcpy(sec, res_bin + at, res_n - at < ST_SECTOR ? res_n - at : ST_SECTOR);
        if (done++ < steps) memcpy(nor + RES_BASE + at, sec, ST_SECTOR);
    }
    memset(sec, 0xFF, sizeof sec);
    memcpy(sec, res_bin, res_n < ST_SECTOR ? res_n : ST_SECTOR);
    if (done++ < steps) memcpy(nor + RES_BASE + 0x100u, sec + 0x100u, ST_SECTOR - 0x100u);   /* the rest of it */
    if (done++ < steps) memcpy(nor + RES_BASE, sec, 0x100u);                       /* the commit record last */
    return done;
}
static int outside_same(void)                       /* the flash outside the region's window as before the update */
{
    return !memcmp(nor, fx_nor, RES_BASE) && !memcmp(nor + RES_BASE + RES_RESERVE * ST_SECTOR,
                                                    fx_nor + RES_BASE + RES_RESERVE * ST_SECTOR,
                                                    sizeof nor - RES_BASE - RES_RESERVE * ST_SECTOR);
}

int main(int argc, char **argv)
{
    int bad = 0;
    if (argc < 3 || !fixture(argv[1])) { printf("res pool: no fixture (res_pool_test FIXTURE RES.bin)\n"); return 1; }
    FILE *f = fopen(argv[2], "rb");
    if (!f) { printf("res pool: no %s (./build.sh first)\n", argv[2]); return 1; }
    res_n = (uint32_t)fread(res_bin, 1, sizeof res_bin, f);
    fclose(f);
    reset();
    flash_ok = 1;
    memcpy(nor, fx_nor, sizeof nor);
    uint32_t before = boot();
    printf("res pool: c53538c9's install: window %u sectors, pool %u sectors (%u B)\n",
           (unsigned)((st_res_end - RES_BASE) / ST_SECTOR), (unsigned)before, (unsigned)(before * ST_LIN_PAYLOAD_MAX));
    bad += check("before the update: c53538c9's window (15), every record as it wrote it (1.x's: kept, never read as music "
                 "by 2.0, the web installer converts them)", st_res_end == RES_BASE + FL_RES_RESERVE_OLD * ST_SECTOR && records_kept());
    uint32_t total = ldr_write(0xFFFFFFFFu), cut_ok = 1;
    for (uint32_t k = 0; k < total; k++) {          /* the power gone after k of the loader's steps */
        memcpy(nor, fx_nor, sizeof nor);
        ldr_write(k);
        uint32_t n = boot();
        int ok = outside_same() && records_kept() && n >= before - 0u && st_res_end >= RES_BASE + RES_RESERVE * ST_SECTOR;
        if (!ok) printf("  cut after step %u: pool %u, window end %#x\n", (unsigned)k, (unsigned)n, (unsigned)st_res_end);
        cut_ok &= ok;
        boot();                                     /* (a second boot: nothing changes) */
        cut_ok &= outside_same() && records_kept();
    }
    bad += check("the update cut after each of the loader's steps: the store outside its window untouched, every record "
                 "kept, the pool never inside what the loader may still write", cut_ok);
    memcpy(nor, fx_nor, sizeof nor);
    ldr_write(total);
    uint32_t after = boot();
    printf("res pool: this build's region (%u B): window %u sectors, pool %u sectors (%u B)\n", (unsigned)res_n,
           (unsigned)((st_res_end - RES_BASE) / ST_SECTOR), (unsigned)after, (unsigned)(after * ST_LIN_PAYLOAD_MAX));
    bad += check("installed: the window 9, the pool 6 sectors bigger, every record as c53538c9 wrote it, the kits listed",
                 st_res_end == RES_BASE + FL_RES_RESERVE * ST_SECTOR && after == before + ST_RES_GIVEN && records_kept());
    {   /* the pool filled: the region's given sectors taken last, their old bytes erased on the way */
        uint32_t saved = 0, given = 0;
        static uint8_t big[ST_LIN_PAYLOAD_MAX * 8u], back[sizeof big];
        for (uint32_t i = 0; i < sizeof big; i++) big[i] = (uint8_t)(i * 31u + 7u);
        uint32_t s = 4u, small = 0;
        for (; s < ST_NPROJ && !st_save(ST_PFILE0 + s, big, sizeof big); s++) saved++;
        for (; s < ST_NPROJ && !st_save(ST_PFILE0 + s, big, ST_LIN_PAYLOAD_MAX); s++) small++;   /* (one sector each) */
        for (uint32_t s = RES_BASE + RES_RESERVE * ST_SECTOR; s < RES_BASE + FL_RES_RESERVE_OLD * ST_SECTOR; s += ST_SECTOR) {
            st_hdr_t h;
            memcpy(&h, nor + s, sizeof h);
            given += h.magic == ST_MAGIC;
        }
        int same = 1;
        for (uint32_t q = 4u; q < 4u + saved + small; q++) {
            uint32_t len = q < 4u + saved ? sizeof big : ST_LIN_PAYLOAD_MAX;
            same &= st_load(ST_PFILE0 + q, back, sizeof back) == (int)len && !memcmp(back, big, len);
        }
        printf("res pool: filled with %u records of 8 sectors and %u of one, %u of them in the given sectors\n",
               (unsigned)saved, (unsigned)small, (unsigned)given);
        bad += check("filled: the given sectors hold records too (the last ones), each reads back, every older record kept",
                     given > 0u && same && records_kept());
        const char *dump = getenv("RES_POOL_DUMP");   /* (the flash as it is now: an older firmware's check, by hand) */
        FILE *df = dump ? fopen(dump, "wb") : 0;
        if (df) { fwrite(nor, 1, sizeof nor, df); fclose(df); }
    }
    printf("%s\n", bad ? "RES POOL TEST FAILED" : "res pool test passed");
    return bad ? 1 : 0;
}
