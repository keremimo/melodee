/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of firmware/src/ota.c: a fake M-UPGRADE serves a real .fwsc over
 * the SysEx protocol (pack7, cmd 0x30 reads, 0xE0000000 -> "success") and a
 * simulated NOR plays the flash.
 *   cc -o ota_test tests/ota_test.c && ./ota_test FILE.fwsc [OFFICIAL_V15.fwsc]
 * -DBOARD_SMK37=1 (-DT_MODEL=1 / 2: the MODEL=elite / mke build): the SMK-37's staging area 0xBB000, its model's
 * parm identity, and the board check (ota.c ota_board_check: another board's package refused before any erase)
 * on an SMK-layout package.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MELODEE_OTA_DRYRUN 0
#ifndef BOARD_SMK37
#define BOARD_SMK37 0
#endif
#if BOARD_SMK37
#if T_MODEL == 1
#define SMK_MODEL "SMK-37 Elite"
#elif T_MODEL == 2
#define SMK_MODEL "MKE-P37"
#else
#define SMK_MODEL "SMK-37 Pro"
#endif
#define MELODEE_ID SMK_MODEL "_900"
#define T_AREA 0xBB000u                  /* the staging area (ota.c OTA_AREA) */
#define T_PARM_ID "ota-" SMK_MODEL "_016"
#else
#define MELODEE_ID "FM-1_900"
#define T_AREA 0xE0000u
#define T_PARM_ID "ota-FM-1_015"
#endif
#define T_AREA_HI (T_AREA + 0x5000u)

static uint8_t nor[0x100000];
static uint8_t *logical;
static size_t logical_len;
static uint8_t rx[1024];                 /* one pending host frame (7-bit bytes) */
static uint32_t rx_len, rx_full;
static uint32_t now_ms, requests, ident_replies, mute_after = 0xFFFFFFFFu, bad_erase, erases;
static int committed;
static uint32_t maintenance_begins, maintenance_total, maintenance_done, maintenance_bad;
static uint8_t maintenance_active, maintenance_failed, testing_cleanup;
static int cleanup_fail_after = -1, cleanup_read_error;
static void maintenance_begin(const char *title, uint32_t total)
{
    maintenance_begins++;
    maintenance_total=total;maintenance_done=0;maintenance_active=1;maintenance_failed=0;
    if(strcmp(title,"FINISHING UPDATE"))maintenance_bad++;
}
static void maintenance_progress(uint32_t done)
{
    if(!maintenance_active || done!=maintenance_done+1u || done>maintenance_total)maintenance_bad++;
    maintenance_done=done;
}
static void maintenance_end(int failed){maintenance_active=0;maintenance_failed=(uint8_t)failed;}
#define BOOT_WORK_BEGIN(title,total) maintenance_begin(title,total)
#define BOOT_WORK_PROGRESS(done) maintenance_progress(done)
#define BOOT_WORK_END(failed) maintenance_end(failed)
static uint8_t committed_parm[112];
static int32_t last_code;
static uint8_t *corrupt_at;              /* flip a served byte (loader body) */

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

static void host_reply(const uint8_t *msg, uint32_t n)       /* queue msg as a device-bound frame */
{
    rx_len = pack7(msg, n, rx);
    rx_full = 1;
}

/* ---- hooks ---- */
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[64], m[600];
    uint32_t d, addr, len, i, s = 0;
    if (p[0] != 0xF0 || p[n - 1] != 0xF7) { printf("bad wire framing\n"); exit(1); }
    d = unpack7h(p + 1, n - 2, u);
    if (u[2] == 0x11) { ident_replies++; return 0; }
    if (d != 15 || u[2] != 0x30) { printf("unexpected device msg cmd %02x len %u\n", u[2], d); exit(1); }
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8 | u[13] << 16;
    if (++requests > mute_after)
        return 0;                                         /* host gone */
    memcpy(m, "\x00\x59\x30", 3);
    m[3] = (uint8_t)(len + 8); m[4] = (uint8_t)((len + 8) >> 8); m[5] = 0;
    m[6] = 0;
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len; m[12] = (uint8_t)(len >> 8); m[13] = 0;
    if (addr == 0xE0000000u) {
        memset(m + 14, 0, len);
        memcpy(m + 14, "success", 8);
    } else {
        if (addr + len > logical_len) { printf("read past the package %#x\n", addr); exit(1); }
        memcpy(m + 14, logical + addr, len);
        if (corrupt_at && corrupt_at >= logical + addr && corrupt_at < logical + addr + len)
            m[14 + (corrupt_at - (logical + addr))] ^= 0x01;
    }
    for (i = 6; i < 14 + len; i++) s += m[i];
    m[14 + len] = (uint8_t)~s;
    host_reply(m, 15 + len);
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!rx_full) return 0;
    *p = rx; *n = rx_len;
    return 1;
}
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms += 1; }
static int ota_erase(uint32_t off)
{
    erases++;
    if (off < T_AREA || off >= T_AREA_HI || (off & 0xFFFu)) { bad_erase++; return -8; }
    if(testing_cleanup && !maintenance_active)maintenance_bad++;
    if(cleanup_fail_after==0)return -8;
    if(cleanup_fail_after>0)cleanup_fail_after--;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int ota_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off < T_AREA || off + n > T_AREA_HI) { bad_erase++; return -8; }
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static int ota_fread(uint32_t off, void *dst, uint32_t n) { if(cleanup_read_error)return -1;memcpy(dst, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { if (step == 9) last_code = code; }
static void ota_commit(const uint8_t *parm) { committed = 1; memcpy(committed_parm, parm, 112); }

#include "../firmware/src/ota.c"
#include "../firmware/stub/stub_core.h"

static uint32_t ota_body_off(void)                  /* ota.bin data + 0x20 in the logical image */
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, logical, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 100) return ota_rd32(e + 8) + 0x20u;
    }
    return 0;
}

static int check(const char *what, int ok)
{
    printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* edit the package's UFW list in place (deciphered, CRCs redone, enciphered again): flash.bin's length (fl_len, 0:
 * kept), or one more entry 50 (a USR image) at target / size (usr); h gets the deciphered header */
static void pkg_edit(uint32_t fl_len, int usr, uint32_t target, uint32_t size, uint8_t *h)
{
    uint8_t w[0x400];
    uint32_t i, n;
    memcpy(h, logical, 0x400);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0 && fl_len)
            ota_wr32(e + 12, fl_len);
    }
    if (usr) {                                         /* as the stock packer writes it */
        uint8_t *e = h + 0x40 + n * 0x50u;
        memset(e, 0, 0x50);
        ota_wr16(e, 50);
        ota_wr32(e + 8, 0x400u);
        ota_wr32(e + 0x1C, target);
        ota_wr32(e + 0x20, size);
        memcpy(e + 0x24, "USR", 3);
        memcpy(e + 0x40, "USR", 3);
        ota_wr16(h + 8, ++n);
    }
    memcpy(w, h, 0x400);
    for (i = 0; i < n; i++)
        ota_jl_enc(w + 0x40 + i * 0x50u, 0x50);
    ota_wr16(h + 2, ota_crc16(w + 0x40, n * 0x50u, 0));   /* (the list's CRC: over the enciphered entries) */
    ota_wr16(h, ota_crc16(h + 2, 0x3E, 0));
    memcpy(w, h, 0x40);
    ota_jl_enc(w, 0x40);
    memcpy(logical, w, 0x40u + n * 0x50u);
}

static int area_erased(void)
{
    uint32_t i;
    for (i = T_AREA; i < T_AREA_HI; i++) if (nor[i] != 0xFF) return 0;
    return 1;
}

static void reset_dev(void)
{
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, logical + 0x400, 0x4000);                 /* device flash head = package flash.bin head */
#if BOARD_SMK37
    memcpy(nor + 0x4060, logical + 0x400 + 0x4060, 0xC0);  /* and its region table: it runs this build */
#endif
    rx_full = 0; requests = 0; ident_replies = 0; committed = 0; last_code = 0;
    mute_after = 0xFFFFFFFFu; corrupt_at = NULL; bad_erase = 0; erases = 0;
}

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *raw;
    size_t n, i;
    int bad = 0, rc;
    if (argc < 2) { printf("usage: ota_test FILE.fwsc\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    raw = malloc(0x200000);
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    logical = malloc(n);
    for (i = 0; i < 20; i++) memcpy(logical + i * 47, raw + i * 48, 47);   /* drop the marker bytes */
    memcpy(logical + 20 * 47, raw + 20 * 48, n - 20 * 48);
    logical_len = n - 20;

    /* handshake outside a session */
    reset_dev();
    {
        uint8_t m[7] = {0, 0x59, 0x11, 0, 0, 0, 0xFF};
        host_reply(m, 7);
        ota_service();
        bad += check("handshake answered", ident_replies == 1);
        bad += check("identity frame is " MELODEE_ID,
                     ota_msg[2] == 0x11 && !memcmp(ota_msg + 6, MELODEE_ID, sizeof MELODEE_ID - 1));
    }

#if BOARD_SMK37
    {   /* the board check: another board's package is refused before anything is erased (ota.c ota_board_check) */
        static uint8_t keep[0x400], h[0x400], pk[0xC0], dv[0xC0];
        memcpy(keep, logical, sizeof keep);
        pkg_edit(0, 0, 0, 0, h);
        bad += check("this package: flash.bin 0x9D000, the board check passes",
                     ota_board_check(h, 0x400u, 0x9D000u, 0, pk, dv) == 0 &&
                     ota_board_check(h, 0x400u, 0x9D000u, 1, pk, dv) == 0);
        memcpy(logical, keep, sizeof keep);
        pkg_edit(0x93000u, 0, 0, 0, h);                    /* the FM-1's flash.bin length */
        reset_dev();
        rc = ota_session();
        bad += check("flash.bin 0x93000 (the FM-1's) -> -15, nothing erased", rc == -15 && !committed && erases == 0 &&
                     area_erased() && bad_erase == 0);
        bad += check("... the official loader -> -15", ota_board_check(h, 0x400u, 0x93000u, 1, pk, dv) == -15);
        memcpy(logical, keep, sizeof keep);
        pkg_edit(0x89000u, 0, 0, 0, h);                    /* an older Melodee's partition */
        reset_dev();
        rc = ota_session();
        bad += check("flash.bin 0x89000 -> -15, nothing erased", rc == -15 && !committed && erases == 0);
        memcpy(logical, keep, sizeof keep);
        reset_dev();
        nor[0x4070] ^= 0x01;                               /* another region table on the device */
        bad += check("official loader, another region table -> -16",
                     ota_board_check(h, 0x400u, 0x9D000u, 1, pk, dv) == -16 &&
                     ota_board_check(h, 0x400u, 0x9D000u, 0, pk, dv) == 0);
        reset_dev();
        pkg_edit(0, 1, 0xEA000u, 0x12000u, h);             /* the FM-1's USR image (0xEA000) */
        bad += check("official loader, a USR image for 0xEA000 -> -17",
                     ota_board_check(h, 0x400u, 0x9D000u, 1, pk, dv) == -17);
        memcpy(logical, keep, sizeof keep);
        pkg_edit(0, 1, 0xF4000u, 0xA000u, h);
        bad += check("... a USR image for 0xF4000 / 0xA000 passes", ota_board_check(h, 0x400u, 0x9D000u, 1, pk, dv) == 0);
        memcpy(logical, keep, sizeof keep);
    }
#else
    {   /* A valid UFW entry with an older Melodee's smaller flash partition (0x89000). */
        uint8_t saved[0x400];memcpy(saved,logical,sizeof saved);uint8_t *h=logical;
        uint8_t header[64];memcpy(header,h,64);ota_jl_enc(header,64);uint32_t count=ota_rd16(header+8);
        for(uint32_t k=0;k<count;k++){uint8_t *e=h+0x40+k*0x50u;ota_jl_enc(e,0x50);if(ota_rd16(e)==0)ota_wr32(e+12,0x89000u);ota_jl_enc(e,0x50);}
        ota_wr16(header+2,ota_crc16(h+0x40,count*0x50u,0));ota_wr16(header,ota_crc16(header+2,62,0));ota_jl_enc(header,64);memcpy(h,header,64);
        reset_dev();rc=ota_session();bad+=check("an older Melodee partition (0x89000) still installs (its own loader)",rc==0&&committed);
        memcpy(logical,saved,sizeof saved);
    }
#endif
    /* An application-only loader must also work on a different compatible head. */
    reset_dev();
    nor[0x100] ^= 1u;
    rc = ota_session();
    bad += check("own loader accepts a different device head without changing it",
                 rc == 0 && committed && nor[0x100] == (uint8_t)(logical[0x500] ^ 1u));
    /* full session */
    reset_dev();
    rc = ota_session();
    printf("  %u requests\n", requests);
    bad += check("session commits", rc == 0 && committed);
    bad += check("headers + loader + confirm, no head reads", requests >= 10 && requests <= 30);
    {
        uint8_t *h = nor + T_AREA, *p = committed_parm;
        bad += check("staged body == the package's loader", !memcmp(nor + T_AREA + 0x20u, logical + ota_body_off(), 64));
        bad += check("LOADER.BIN head CRC",
                     (h[0] | h[1] << 8) == ota_crc16(h + 2, 30, 0) && !memcmp(h + 16, "LOADER.BIN", 10));
        bad += check("record at the area's end - 256 == RAM record", !memcmp(nor + T_AREA_HI - 0x100u, p, 112));
        bad += check("record CRC / type / magic / ota_addr",
                     (p[0] | p[1] << 8) == ota_crc16(p + 2, 78, 0) && (p[2] | p[3] << 8) == 0x5A0D &&
                     (p[6] | p[7] << 8) == 0x5441 && ota_rd32(p + 72) == T_AREA);
        bad += check("record: the loader's identity " T_PARM_ID ", zeros behind",
                     !memcmp(p + 8, T_PARM_ID, sizeof T_PARM_ID - 1u) && !p[8 + sizeof T_PARM_ID - 1u] && !p[0x27]);
        bad += check("nothing written outside the staging area", bad_erase == 0);
    }
    /* The streaming app check and the stub's XIP check must agree. Include
     * damage to headers, block metadata, payloads and the unused tail. */
    {
        int mismatch=0;
        bad += check("app and stub accept the actual staged loader",ota_staged() && stub_loader_check(nor+OTA_AREA)==STUB_LD_OK);
        for (uint32_t off=0;off<OTA_AREA_LEN;off++) {
            uint8_t bit=(uint8_t)(1u<<(off&7u));
            nor[OTA_AREA+off]^=bit;
            mismatch |= (ota_staged()!=0)!=(stub_loader_check(nor+OTA_AREA)==STUB_LD_OK);
            nor[OTA_AREA+off]^=bit;
        }
        bad += check("app/stub loader checks agree on 20,480 damaged copies",!mismatch);
    }
    /* A successful install retains its loader for the boot stub. */
    testing_cleanup=1;
    bad += check("boot retains loader, erases only record", ota_boot_cleanup()==2 && ota_staged() && nor[OTA_RES]==0xFFu);
    bad += check("record cleanup reports one completed sector",maintenance_begins==1u && maintenance_done==1u && !maintenance_failed && !maintenance_bad);
    bad += check("next boot keeps loader without erasing",ota_boot_cleanup()==0 && maintenance_begins==1u);
    nor[OTA_RES]=0;cleanup_fail_after=0;
    bad += check("record erase failure keeps loader for retry",ota_boot_cleanup()<0 && ota_staged() && maintenance_failed);
    cleanup_fail_after=-1;
    bad += check("record erase retry keeps loader",ota_boot_cleanup()==2 && ota_staged());
    /* Damaged staging is erased, with existing progress and retry behavior. */
    nor[OTA_AREA]^=1u;maintenance_begins=0;
    /* boot cleanup erases it, visibly, and detects incomplete erases on retry. */
    testing_cleanup=1;
    bad += check("boot cleanup erases the area", ota_boot_cleanup() == 1 && area_erased());
    bad += check("cleanup shows progress before every erase",maintenance_begins==1u && maintenance_done==5u && !maintenance_active && !maintenance_failed && !maintenance_bad);
    bad += check("second boot skips writes and maintenance display", ota_boot_cleanup() == 0 && maintenance_begins==1u);
    nor[T_AREA+0x3001u]=0;
    cleanup_fail_after=2;
    bad += check("cleanup error stops at its last completed sector",ota_boot_cleanup()<0 && maintenance_failed && maintenance_done==2u && nor[T_AREA+0x3001u]==0);
    cleanup_fail_after=-1;
    bad += check("retry finds dirty data even with erased loader headers",ota_boot_cleanup()==1 && area_erased() && maintenance_done==5u && !maintenance_failed && !maintenance_bad);
    cleanup_read_error=1;
    bad += check("cleanup read error is visible",ota_boot_cleanup()<0 && maintenance_failed && maintenance_done==0u);
    cleanup_read_error=0;testing_cleanup=0;

    /* host disappears in the middle */
    reset_dev();
    mute_after = 4;
    rc = ota_session();
    bad += check("host gone -> no commit, area clean", rc < 0 && !committed && area_erased());

    /* host never confirms */
    reset_dev();
    mute_after = 0xFFFFFFFFu;
    {
        uint32_t total;
        rc = ota_session();
        total = requests;                                  /* requests of a full run incl. confirm */
        reset_dev();
        mute_after = total - 1;                            /* answer everything but the confirmation */
        rc = ota_session();
        bad += check("no 'success' -> -13, no commit, area clean", rc == -13 && !committed && area_erased());
    }

    /* Optional local vendor package, never redistributed. Melodee's loader preserves
     * the device's original 0..0x3FFF head; the official loader requires that match. */
    if (argc > 2) {
        f = fopen(argv[2], "rb");
        if (!f) { perror(argv[2]); return 2; }
        n = fread(raw, 1, 0x200000, f);
        fclose(f);
        free(logical);
        logical = malloc(n);
        for (i = 0; i < 20; i++) memcpy(logical + i * 47, raw + i * 48, 47);
        memcpy(logical + 20 * 47, raw + 20 * 48, n - 20 * 48);
        logical_len = n - 20;
        uint8_t head[0x4000];
        reset_dev();
        memcpy(head, nor, sizeof head);
        rc = ota_session();
        bad += check("stock V15 stages and commits its official loader", rc == 0 && committed);
        bad += check("stock return preserves head during staging", !memcmp(head, nor, sizeof head) && !bad_erase);
        bad += check("stock return stages the exact official loader",
                     !memcmp(nor + OTA_AREA + 32, logical + ota_body_off(), OTA_LDR_LEN));
        bad += check("stock return uses the official loader identity",
                     !memcmp(committed_parm + 8, T_PARM_ID, sizeof T_PARM_ID - 1u));
        reset_dev();
        nor[0x100] ^= 1;
        rc = ota_session();
        bad += check("stock return rejects a different device head",
                     rc == -5 && !committed && area_erased());
        reset_dev();
        corrupt_at = logical + ota_body_off() + 100;
        rc = ota_session();
        bad += check("stock return rejects a damaged official loader",
                     rc == -11 && !committed && area_erased());
    }

    printf("%s\n", bad ? "OTA TEST FAILED" : "ota test passed");
    return bad != 0;
}
