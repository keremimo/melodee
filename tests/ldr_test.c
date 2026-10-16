/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the Melodee update loader (firmware/loader/ldr_core.c): a fake
 * host serves a package over the SysEx protocol, the "device" flash starts as
 * another package's flash.bin (as if that firmware were installed).
 *   ldr_test OLD.fwsc NEW.fwsc
 * -DBOARD_SMK37=1: the SMK-37's places, on SMK-layout packages (PRCT 0x9D000, the staging at 0xBB000), and every
 * protected range (head, BTIF / USRTRIM, USR, key_mac) and Melodee's SMK store untouched. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MELODEE_ID "ota-FM-1_900"
#define MELODEE_OTA_DRYRUN 0
#ifndef BOARD_SMK37
#define BOARD_SMK37 0
#endif
#if BOARD_SMK37
#define T_APP_HI 0x9D000u                /* the end of the app area the loader writes (ldr_core.c LDR_APP_HI) */
#define T_REC_HI 0xC0000u                /* .. and of its record window (LDR_REC_HI) */
#define T_OTA 0xBB000u                   /* the staging area (ota.c OTA_AREA) */
#else
#define T_APP_HI 0x93000u
#define T_REC_HI 0xFC000u
#define T_OTA 0xE0000u
#endif

static uint8_t nor[0x100000], *logical;
static size_t logical_len;
static uint8_t rx[1024];
static uint32_t rx_len, rx_full, now_ms, requests, erases, bad_range, record_cleared, f0_asked;
static int marker_erase_stuck;

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
static uint32_t unpack7h(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}

static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[64], m[600];
    uint32_t addr, len, i, s = 0;
    unpack7h(p + 1, n - 2, u);
    if (u[2] == 0x11) return 0;
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8 | u[13] << 16;
    requests++;
    memcpy(m, "\x00\x59\x30", 3);
    m[3] = (uint8_t)(len + 8); m[4] = (uint8_t)((len + 8) >> 8); m[5] = 0; m[6] = 0;
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len; m[12] = (uint8_t)(len >> 8); m[13] = 0;
    if (addr >= 0xE0000000u) {
        f0_asked += addr == 0xF0000000u;
        memset(m + 14, 0, len);
        memcpy(m + 14, "success", 8);
    } else {
        if (addr + len > logical_len) { printf("read past the package %#x\n", addr); exit(1); }
        memcpy(m + 14, logical + addr, len);
    }
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
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../firmware/src/ota.c"

static int ldr_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static int ldr_erase(uint32_t off)
{
    if (off < 0x4000u || off >= T_REC_HI || (off & 0xFFFu)) bad_range++;
    if (off < 0x4000u) { printf("ERASE IN THE HEAD %#x\n", off); exit(1); }
    erases++;
    if (marker_erase_stuck && off == 0x91000u) return 0;
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static uint32_t prog_fail_after;                    /* a write that fails (the power gone): after this many */
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint32_t i;
    if (prog_fail_after && !--prog_fail_after) return -1;
    /* Programming is restricted to the application. */
    if (off < 0x4000u || off + n > T_APP_HI || ((off & 0xFFu) + n) > 256u) bad_range++;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static void ldr_record_clear(void) { record_cleared++; }
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; }
#include "../firmware/loader/ldr_core.c"

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

static uint32_t flash_off(const uint8_t *lg)        /* flash.bin offset in the logical image */
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) return ota_rd32(e + 8);
    }
    return 0;
}

static int check(const char *what, int ok) { printf("%-56s %s\n", what, ok ? "ok" : "FAIL"); return ok ? 0 : 1; }

#if BOARD_SMK37
/* the SMK's protected ranges and Melodee's store (store A 0x9D000..0xBAFFF, B 0xC2000..0xE9FFF, the stock marker's
 * sector, C 0xEB000..0xF3FFF), each sector a pattern with an erased tail (as storage.c writes them); none of it may
 * change, but the update record in the staging area's last sector */
static uint8_t keep[0x100000];
static void protect_fill(void)
{
    uint32_t s, i;
    for (s = T_APP_HI; s < 0x100000u; s += 0x1000u)
        if (s < T_OTA || s >= T_OTA + 0x5000u)
            for (i = 0; i < 0x1000u; i++)
                nor[s + i] = i >= 0xF00u ? 0xFFu : (uint8_t)(s >> 12 ^ i * 7u);
    memcpy(keep, nor, sizeof keep);
}
static int protect_ok(void)
{
    uint32_t s;
    for (s = T_APP_HI; s < 0x100000u; s += 0x1000u)
        if ((s < T_OTA || s >= T_OTA + 0x5000u) && memcmp(nor + s, keep + s, 0x1000u))
            return 0;
    return 1;
}
#endif

int main(int argc, char **argv)
{
    size_t olen;
    uint8_t *old, head[0x4000];
    uint32_t ofo, nfo;
    int bad = 0, rc, target_stub;
    if (argc < 3) { printf("usage: ldr_test OLD.fwsc NEW.fwsc\n"); return 2; }
    old = load_logical(argv[1], &olen);
    logical = load_logical(argv[2], &logical_len);
    ofo = flash_off(old);
    nfo = flash_off(logical);
    /* device: OLD installed, plus a valid update record at the staging area's end - 256 (FM-1 0xE4F00) */
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, old + ofo, LDR_APP_HI);
    memcpy(head, nor, sizeof head);
#if BOARD_SMK37
    protect_fill();
#endif
    {
        uint8_t first[512];
        uint32_t key;
        if (ldr_chip_key(&key)) return 2;
        memcpy(first, logical + nfo + LDR_APP_LO, sizeof first);
        ldr_sfc(first, sizeof first, 0, key);
        target_stub = ota_memeq(first + 0x128, (const uint8_t *)"CRSTUB", 6);
    }
    {
        uint8_t r[112] = {0};
        r[2] = 0x0D; r[3] = 0x5A; r[4] = 0x01; r[5] = 0x5A; r[6] = 0x41; r[7] = 0x54;
        ota_wr16(r, ota_crc16(r + 2, 78, 0));
        memcpy(nor + T_OTA + 0x4F00u, r, sizeof r);
    }
#if BOARD_SMK37
    prog_fail_after = 2u * 16u + 5u;                   /* power gone while programming the application */
    rc = ldr_session();
    bad += check("interrupted app write leaves the head and the store intact", rc == -10 &&
                 !memcmp(nor,head,sizeof head) && protect_ok());
    bad += check("interrupted update retains the flash resume record", nor[T_OTA + 0x4F06u]==0x41u);
    prog_fail_after = 0;
    rc = ldr_session();
#else
    /* Musical extension sectors lie above the staged loader and retain erased
     * scan tails. Installing firmware must preserve every byte of them. */
    static uint8_t extensions[0x10000];
    for(uint32_t i=0;i<sizeof extensions;i++)extensions[i]=(i%4096u>=3840u)?0xFFu:(uint8_t)(i*31u);
    memcpy(nor+0xEA000u,extensions,sizeof extensions);
    /* the Prophet banks where the smaller app area left them (0x89000): each sector a storage.c commit record */
    static uint8_t prophet_banks[0xA000];
    for(uint32_t i=0;i<10u;i++){
        uint8_t *s=prophet_banks+i*4096u;
        memset(s,(uint8_t)(0xA0u+i),4096u);memset(s+3840u,0xFF,256u);
        ota_wr32(s,0x554C4546u);ota_wr16(s+4,25u+i/2u);ota_wr16(s+6,i&1u);ota_wr32(s+8,7u);ota_wr32(s+12,100u);
        uint32_t c=0xFFFFFFFFu;for(uint32_t k=0;k<28u;k++){c^=s[k];for(uint32_t j=0;j<8u;j++)c=(c>>1)^(0xEDB88320u&(0u-(c&1u)));}
        ota_wr32(s+28,~c);
    }
    memcpy(nor+0x89000,prophet_banks,sizeof prophet_banks);
    static uint8_t current_banks[0xA000];
    memset(current_banks,0x5A,sizeof current_banks);
    for(uint32_t i=3840u;i<sizeof current_banks;i+=4096u)memset(current_banks+i,0xFF,256u);
    memcpy(nor+0x93000,current_banks,sizeof current_banks);
    prog_fail_after = 2u * 16u + 5u;                   /* power gone while programming the application */
    rc = ldr_session();
    bad += check("interrupted app write leaves bootloader and current banks intact", rc == -10 &&
                 !memcmp(nor,head,sizeof head) && !memcmp(nor+0x93000,current_banks,sizeof current_banks));
    bad += check("interrupted update retains the flash resume record", nor[0xE4F06]==0x41u);
    prog_fail_after = 0;
    rc = ldr_session();
    bad += check("legacy Prophet banks are ignored; current bank area is untouched",
                 !memcmp(nor+0x93000,current_banks,sizeof current_banks) &&
                 memcmp(nor+0x89000,prophet_banks,sizeof prophet_banks));
    bad += check("project extensions survive firmware installation",!memcmp(nor+0xEA000u,extensions,sizeof extensions));
#endif
    printf("  rc %d, %u requests, %u sector erases\n", rc, requests, erases);
    bad += check("install completes", rc == 0);
    bad += check("app area == the new package's flash.bin", !memcmp(nor + 0x4000, logical + nfo + 0x4000, LDR_APP_HI - 0x4000));
    bad += check("flash head [0, 0x4000) untouched", !memcmp(nor, head, sizeof head));
    bad += check("finish asked (0xF0000000)", f0_asked >= 1);
    bad += check("update record cleared (RAM + flash)", record_cleared && nor[T_OTA + 0x4F06u] == 0xFF);
    bad += check("no write outside the allowed windows", bad_range == 0);
    /* same package again: nothing to erase */
    erases = 0; requests = 0;
    rc = ldr_session();
    bad += check("re-run with the same package erases nothing", rc == 0 && erases == 0);
    /* a package for another chip key is refused before any erase */
    {
        uint8_t save = logical[nfo + 0x4000 + 5];
        logical[nfo + 0x4000 + 5] ^= 0x5A;           /* app area head no longer decrypts */
        memcpy(nor, old + ofo, LDR_APP_HI);
        erases = 0;
        rc = ldr_session();
        bad += check("foreign key / damaged app head refused, nothing erased", rc == -6 && erases == 0);
        logical[nfo + 0x4000 + 5] = save;
    }
    if (target_stub) {   /* A stub package with damaged/invalid metadata must never fall back
         * to the legacy write order. Keep the rest of the package untouched. */
        uint32_t key;
        uint8_t saved[32], plain[32];
        ldr_chip_key(&key);
        memcpy(saved, logical+nfo+LDR_MARK_OFF, sizeof saved);
        for (uint32_t k=0;k<5u;k++) {
            memcpy(plain,saved,sizeof plain);
            ldr_sfc(plain,32,LDR_MARK_OFF-LDR_APP_LO,key);
            if (!k) plain[0]^=1u;
            else if (k==1u) plain[16]^=1u;
            else {
                ota_wr32(plain+8,k==2u?0u:k==3u?3u:0x8C004u);
                ota_wr32(plain+16,ldr_crc32(plain,16));
            }
            ldr_sfc(plain,32,LDR_MARK_OFF-LDR_APP_LO,key);
            memcpy(logical+nfo+LDR_MARK_OFF,plain,32);
            erases=0;
            rc=ldr_session();
            bad += check("invalid stub marker refused before any erase",rc==-15 && !erases);
        }
        memcpy(logical+nfo+LDR_MARK_OFF,saved,sizeof saved);
        static uint8_t before[T_APP_HI - 0x4000u];
        memcpy(nor+LDR_APP_LO,logical+nfo+LDR_APP_LO,sizeof before);
        nor[0x5000]^=1u;
        memcpy(before,nor+LDR_APP_LO,sizeof before);
        marker_erase_stuck=1;
        rc=ldr_session();
        bad += check("marker must erase before changing any body byte",rc==-12 && !memcmp(before,nor+LDR_APP_LO,sizeof before));
        marker_erase_stuck=0;
    }
    {   /* A valid UFW entry with an older, smaller flash partition (0x89000; SMK: the FM-1's 0x93000). */
        uint8_t saved[0x400];memcpy(saved,logical,sizeof saved);uint8_t *h=logical;
        uint8_t header[64];memcpy(header,h,64);ota_jl_enc(header,64);uint32_t count=ota_rd16(header+8);
        for(uint32_t k=0;k<count;k++){uint8_t *e=h+0x40+k*0x50u;ota_jl_enc(e,0x50);if(ota_rd16(e)==0)ota_wr32(e+12,BOARD_SMK37?0x93000u:0x89000u);ota_jl_enc(e,0x50);}
        ota_wr16(header+2,ota_crc16(h+0x40,count*0x50u,0));ota_wr16(header,ota_crc16(header+2,62,0));ota_jl_enc(header,64);memcpy(h,header,64);
#if BOARD_SMK37
        erases=0;rc=ldr_session();bad+=check("another partition refused before erasing anything",rc==-3&&!erases&&protect_ok());
#else
        erases=0;rc=ldr_session();bad+=check("older smaller partition refused before erasing anything",rc==-3&&!erases&&!memcmp(nor+0x93000,current_banks,sizeof current_banks));
#endif
        memcpy(logical,saved,sizeof saved);
    }
#if BOARD_SMK37
    bad += check("protected ranges and the store untouched (BTIF, USRTRIM, USR, key_mac)", protect_ok());
#endif
    printf("%s\n", bad ? "LOADER TEST FAILED" : "loader test passed");
    return bad != 0;
}
