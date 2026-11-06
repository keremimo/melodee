/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee's fm1-base package and felucca-bsp's update loader against Melodee's FM-1 store.
 *   base_ldr_test NEW.fwsc [OLD.fwsc]
 * NEW is the base build's package (platform/build/melodee.fwsc); its ota.bin is the pinned felucca-bsp's
 * loader/ldr_core.c, which this test compiles (tests/run_base_tests.sh: -I the pinned bsp). The device holds OLD's
 * app area (or NEW's with every third sector changed) and Melodee's data everywhere above it: the former preset pool's
 * first four sectors 0x93000..0x96FFF (storage.c ST_SP_AT[0..3], under the bsp's "application window" that ends at
 * 0x97000: bsp/docs/FLASH.md), the store and the resource region, the extensions and pools from 0xE5000, the settings.
 * Classic and fast installs, an install cut short and resumed, the same package again: the app area becomes NEW's,
 * the staging's record goes, and not one byte of Melodee's data changes; no erase or program reaches 0x93000..0x96FFF. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FELUCCA_ID "ota-FM-1_900"
#define FELUCCA_OTA_DRYRUN 0
#define LDR_FAST_CHUNK 4096u
#define OTA_FRAME_MAX 4200u

#define NOR_SIZE 0x100000u
#define POOL_LO 0x93000u                     /* storage.c ST_SP_AT[0..3] (hal/fm1_flash_map.h: the app area's end) */
#define POOL_HI 0x97000u                     /* .. to the bsp's application window end */
#define STG_LO 0xE0000u                      /* ota.c OTA_AREA: the staged loader, 5 sectors */
#define STG_REC_SEC 0xE4000u                 /* its last sector: the record at 0xE4F00 */
#define STG_HI 0xE5000u
#define ST_MAGIC 0x554C4546u                 /* storage.c "FELU" */

static uint8_t nor[NOR_SIZE], keep[NOR_SIZE], *logical, *old_lg;
static size_t logical_len;
static uint32_t nfo, ofo, n_tbl;
static uint8_t tbl[(NOR_SIZE >> 12) * 6u];
static uint32_t pool_ops, wild_ops, finish, prog_fail_after;

/* ---- the host: NEW's logical image, cmd 0x30 reads (the fast install's table at 0xD0000000 + flash.bin's offset) ---- */
static uint8_t rx[4800];
static uint32_t rx_len, rx_full, now_ms;

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return o;
}
static uint32_t unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)(*in++ & 0x7Fu) << nb;
        nb += 7;
        if (nb >= 8) { if (o < max) out[o] = (uint8_t)acc; o++; acc >>= 8; nb -= 8; }
    }
    return o;
}

static void host_data(uint32_t addr, uint8_t *d, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        uint32_t a = addr + i, t = a - (0xD0000000u + nfo);
        if (a >= 0xF0000000u)
            d[i] = i < 8u ? (uint8_t)"success"[i] : 0;
        else if (t < n_tbl * 6u)
            d[i] = tbl[t];
        else
            d[i] = a < logical_len ? logical[a] : 0;
    }
}

static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    static uint8_t u[64], m[4200];
    uint32_t addr, len, i, s = 0;
    if (n < 3 || unpack7(p + 1, n - 2, u, sizeof u) < 8 || u[2] != 0x30)
        return 0;                                    /* 0x11, 0x31, 0x32: nothing to answer */
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8;
    if (len > 4096u) { printf("read of %u bytes\n", len); exit(1); }
    memset(m, 0, sizeof m);
    m[1] = 0x59;
    m[2] = 0x30;
    m[3] = (uint8_t)(len + 8);
    m[4] = (uint8_t)((len + 8) >> 8);
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len;
    m[12] = (uint8_t)(len >> 8);
    host_data(addr, m + 14, len);
    for (i = 6; i < 14 + len; i++) s += m[i];
    m[14 + len] = (uint8_t)~s;
    rx_len = pack7(m, 15 + len, rx);
    rx_full = 1;
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n) { if (!rx_full) return 0; *p = rx; *n = rx_len; return 1; }
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }
static int ota_erase(uint32_t off) { (void)off; return -1; }     /* step 1, the app's: never the loader's */
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "chip/ota.c"

/* ---- the flash: every erase / program held to the app area, or (the finish) the staging's record sector ---- */
static void op(uint32_t off, uint32_t n, int erase)
{
    if (off < POOL_HI && off + n > POOL_LO) {
        pool_ops++;
        printf("  %s %#x +%#x in Melodee's pool sectors 0x93000..0x96FFF\n", erase ? "ERASE" : "PROGRAM", off, n);
    } else if (!(off >= 0x4000u && off + n <= POOL_LO && !finish) && !(erase && off == STG_REC_SEC && finish)) {
        wild_ops++;
        printf("  %s %#x +%#x outside the app area\n", erase ? "ERASE" : "PROGRAM", off, n);
    }
}
static int ldr_fread(uint32_t off, void *p, uint32_t n)
{
    if (off >= NOR_SIZE || n > NOR_SIZE - off) return -1;
    memcpy(p, nor + off, n);
    return 0;
}
static int ldr_erase(uint32_t off)
{
    op(off, 0x1000u, 1);
    if (off >= NOR_SIZE || (off & 0xFFFu)) return -1;
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint32_t i;
    op(off, n, 0);
    if (prog_fail_after && !--prog_fail_after) return -1;    /* the power gone while programming */
    if (off >= NOR_SIZE || n > NOR_SIZE - off) return -1;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static void ldr_record_clear(void) { finish = 1; }
static int ldr_erase_begin(uint32_t off) { return ldr_erase(off); }
static int ldr_erase_end(void) { return 0; }
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; }
#include "loader/ldr_core.c"

_Static_assert(LDR_APP_HI == POOL_LO, "felucca-bsp's loader writes the app area to 0x93000 (Melodee's app area end)");

/* ---- the packages ---- */
static uint8_t *load_logical(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *raw = malloc(0x200000), *lg;
    size_t n, i;
    if (!f) { perror(path); exit(2); }
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    lg = malloc(n);
    for (i = 0; i < 20; i++) memcpy(lg + i * 47, raw + i * 48, 47);
    memcpy(lg + 20 * 47, raw + 20 * 48, n - 20 * 48);
    *len = n - 20;
    free(raw);
    return lg;
}
static uint32_t flash_bin(const uint8_t *lg, uint32_t *size)   /* flash.bin's offset in the logical image */
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n && i < 11u; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) { *size = ota_rd32(e + 12); return ota_rd32(e + 8); }
    }
    return 0;
}

/* ---- Melodee's data (storage.c's formats) ---- */
static uint32_t rng;
static uint8_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (uint8_t)rng; }
static void record(uint32_t sec, uint32_t end)   /* a commit header at 0, payload to end, erased after it */
{
    uint32_t i;
    rng = sec | 1u;
    for (i = 0; i < 0x1000u; i++) nor[sec + i] = i < end ? rnd() : 0xFFu;
    ota_wr32(nor + sec, ST_MAGIC);
    ota_wr32(nor + sec + 28, ldr_crc32(nor + sec, 28));
}
static void put_update_record(uint8_t *at)       /* ota.c ota_session's layout */
{
    memset(at, 0, 112);
    ota_wr16(at + 2, 0x5A0Du);
    ota_wr16(at + 4, 0x5A01u);
    ota_wr16(at + 6, 0x5441u);
    memcpy(at + 8, "ota-FM-1_900", 12);
    ota_wr32(at + 72, STG_LO);
    ota_wr16(at, ota_crc16(at + 2, 78, 0));
}
static void device(void)
{
    uint32_t s, i;
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, (old_lg ? old_lg + ofo : logical + nfo), POOL_LO);   /* the head (chip key) and the installed app */
    if (!old_lg)
        for (s = 0x5000u; s < POOL_LO; s += 0x3000u)           /* another build: a third of its sectors differ */
            for (i = 0; i < 0x1000u; i++) nor[s + i] ^= 0x5Au;
    /* the former preset pool's first four (ST_SP_AT[0..3]): pool records (payload 32..0xEFF, the last 256 bytes
     * erased) and the Prophet banks' legacy copies they replaced (payload from 256 to the sector's end) */
    record(0x93000u, 0xF00u);
    record(0x94000u, 0xF00u);
    record(0x95000u, 0x1000u);
    record(0x96000u, 0x1000u);
    for (s = POOL_HI; s < STG_LO; s += 0x1000u)              /* the store, the resource region */
        record(s, (s >> 12) & 1u ? 0x1000u : 0xF00u);
    rng = 13;
    for (i = STG_LO; i < STG_HI; i++) nor[i] = rnd();         /* the staged loader ... */
    memset(nor + 0xE4E00u, 0xFF, 0x200);
    put_update_record(nor + 0xE4F00u);                        /* ... and its record */
    for (s = STG_HI; s < 0xFC000u; s += 0x1000u)             /* extensions, the project pool, U33..U64 */
        record(s, (s >> 12) & 1u ? 0x1000u : 0xF00u);
    rng = 15;
    for (i = 0xFC000u; i < NOR_SIZE; i++) nor[i] = rnd();     /* settings, key_mac */
    memcpy(keep, nor, sizeof keep);
}

static int fails;
static void check(const char *what, int ok) { printf("  %-64s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }
static int same(uint32_t lo, uint32_t hi) { return !memcmp(nor + lo, keep + lo, hi - lo); }
static int erased(uint32_t lo, uint32_t hi) { uint32_t i; for (i = lo; i < hi; i++) if (nor[i] != 0xFFu) return 0; return 1; }
static int melodee_data_kept(void)               /* everything above the app area but the staging */
{
    return same(POOL_LO, STG_LO) && same(STG_HI, NOR_SIZE);
}

static int run(int fast)
{
    rx_full = 0;
    finish = 0;
    ldr_fast_on = (uint8_t)fast;
    return ldr_session();
}

static void after_install(int rc)
{
    check("the install finished", rc == 0);
    check("app area 0x4000..0x92FFF == the package's flash.bin",
          !memcmp(nor + 0x4000u, logical + nfo + 0x4000u, POOL_LO - 0x4000u));
    check("the head (SPL, isd_config) as it was", same(0, 0x4000u));
    check("staged loader's record gone, its sector erased", !ldr_is_record(nor + 0xE4F00u) && erased(STG_REC_SEC, STG_HI));
    check("0x93000..0x96FFF (ST_SP_AT[0..3]) byte for byte as it was", same(POOL_LO, POOL_HI));
    check("no erase or program in 0x93000..0x96FFF", pool_ops == 0);
    check("Melodee's data 0x93000..0xDFFFF and 0xE5000..0xFFFFF as it was", melodee_data_kept());
    check("nothing written outside the app area and the record's sector", wild_ops == 0);
}

int main(int argc, char **argv)
{
    uint32_t size, osize, k;
    int fast, rc;
    if (argc < 2) { printf("usage: base_ldr_test NEW.fwsc [OLD.fwsc]\n"); return 2; }
    logical = load_logical(argv[1], &logical_len);
    if (!(nfo = flash_bin(logical, &size)) || size != LDR_APP_HI) {
        printf("%s: flash.bin %#x bytes, the loader's app area ends at %#x\n", argv[1], size, LDR_APP_HI);
        return 1;
    }
    if (argc > 2) {
        size_t olen;
        old_lg = load_logical(argv[2], &olen);
        if (!(ofo = flash_bin(old_lg, &osize)) || osize < POOL_LO) { printf("%s: no flash.bin\n", argv[2]); return 1; }
    }
    n_tbl = size >> 12;                              /* the fast install's table, as the host makes it */
    for (k = 0; k < n_tbl; k++) {
        ota_wr16(tbl + k * 6u, ota_crc16(logical + nfo + k * 0x1000u, 0x1000u, 0));
        ota_wr32(tbl + k * 6u + 2, ldr_crc32(logical + nfo + k * 0x1000u, 0x1000u));
    }
    printf("base_ldr_test: %s (flash.bin %#x B) over %s\n", argv[1], size, argc > 2 ? argv[2] : "its own app, changed");
    for (fast = 0; fast < 2; fast++) {
        printf(" %s install\n", fast ? "fast" : "classic");
        device();
        pool_ops = wild_ops = 0;
        rc = run(fast);
        after_install(rc);

        printf(" %s install: the same package again\n", fast ? "fast" : "classic");
        put_update_record(nor + 0xE4F00u);
        memcpy(keep, nor, sizeof keep);
        pool_ops = wild_ops = 0;
        rc = run(fast);
        after_install(rc);

        printf(" %s install: the power gone while programming, then resumed\n", fast ? "fast" : "classic");
        device();
        pool_ops = wild_ops = 0;
        prog_fail_after = 16u * 7u + 3u;
        rc = run(fast);
        prog_fail_after = 0;
        check("the cut install failed, its record kept", rc != 0 && ldr_is_record(nor + 0xE4F00u));
        check("0x93000..0x96FFF as it was after the cut", same(POOL_LO, POOL_HI) && pool_ops == 0);
        rc = run(fast);
        after_install(rc);
    }
    /* the check itself: a wiped pool sector is seen */
    memset(nor + 0x95000u, 0xFF, 0x1000);
    check("(the check sees a pool sector erased)", !same(POOL_LO, POOL_HI) && !melodee_data_kept());
    printf("%s: %d failures\n", fails ? "BASE LOADER TEST FAILED" : "base loader test passed", fails);
    return fails != 0;
}
