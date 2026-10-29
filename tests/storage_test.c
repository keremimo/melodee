/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of firmware/src/storage.c against a simulated NOR flash:
 * erase -> 0xFF, program can only clear bits, page writes must not wrap. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t nor[0x100000];
static int fail_after = -1;            /* torn-write injection: stop after N programs */
static int fail_bytes = -1;            /* power loss after any programmed byte */
static int erase_error, write_protected;
static uint32_t fail_erase_off = 0xFFFFFFFFu;
static uint32_t io_calls, program_calls;
static int read_error;

static int st_read(uint32_t off, void *dst, uint32_t n) { io_calls++; if (read_error) return -7; memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    io_calls++;
    if (erase_error || off == fail_erase_off) return -8;
    if (!write_protected) memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    program_calls++;
    io_calls++;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    for (i = 0; i < n; i++) {
        if (fail_bytes == 0) return -9;
        if (fail_bytes > 0) fail_bytes--;
        if (!write_protected) nor[off + i] &= s[i];
    }
    return 0;
}
#include "../firmware/src/storage.c"
#define T_OBJ OBJ_CZBANK0                        /* (an ordinary one-sector object for the A/B tests) */

/* every committed copy of obj damaged, wherever it lives (fixed pair or pool): its payload (head 0) or header */
static void corrupt_all(uint32_t obj, int head)
{
    for (uint32_t c = 0; c < ST_SMALL_U0 + ST_U_N; c++) {
        st_hdr_t h;
        if (st_head(obj, c, &h)) continue;
        nor[st_sector(obj, c) + (head ? 8u : st_payload_off_header(obj, &h) + 10u)] ^= 0x01;
    }
}
static int check(const char *what, int ok)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* Independent legacy fixture: original contiguous base and padded extensions. */
static void legacy_project(uint32_t slot, uint32_t copy, const uint8_t *data, uint32_t len, uint32_t seq)
{
    uint32_t base=0xA0000u+slot*10u*4096u+copy*5u*4096u;
    uint32_t ext=0xEA000u+(slot*2u+copy)*2u*4096u;
    memset(nor+base,0xFF,5u*4096u);memset(nor+ext,0xFF,2u*4096u);
    for(uint32_t i=0;i<len;i++) {
        uint32_t j=i-20224u;
        uint32_t at=i<20224u?base+256u+i:ext+j/3840u*4096u+j%3840u;
        nor[at]=data[i];
    }
    st_hdr_t h={ST_MAGIC,(uint16_t)(OBJ_BANK0+slot),(uint16_t)copy,seq,len,st_crc32(data,len),{0xFFFFFFFFu,0xFFFFFFFFu},0};
    h.hcrc=st_crc32(&h,sizeof h-4u);memcpy(nor+base,&h,sizeof h);
}
static int project_pool_test(void)
{
    int bad=0,ok=1;
    static uint8_t old[27904],next[ST_PROJECT_MAX],back[ST_PROJECT_MAX],snapshot[sizeof nor];
    for(uint32_t i=0;i<sizeof old;i++)old[i]=(uint8_t)(i*37u);
    for(uint32_t i=0;i<sizeof next;i++)next[i]=(uint8_t)(i*113u+7u);
    memset(nor,0xFF,sizeof nor);
    for(uint32_t k=0;k<4u;k++){
        legacy_project(k,0,old,sizeof old,4u);legacy_project(k,1,old,sizeof old,5u);
        ok &= st_load(OBJ_BANK0+k,back,sizeof back)==sizeof old&&!memcmp(back,old,sizeof old);
    }
    memcpy(snapshot,nor,sizeof nor);
    bad+=check("all four legacy A/B projects remain readable",ok);
    for(uint32_t cut=0;cut<170u;cut++){
        memcpy(nor,snapshot,sizeof nor);fail_after=(int)cut;
        int rc=st_save(OBJ_BANK0,next,sizeof next);fail_after=-1;
        ok &= st_load(OBJ_BANK0,back,sizeof back)==(rc?sizeof old:sizeof next)&&!memcmp(back,rc?old:next,rc?sizeof old:sizeof next);
        for(uint32_t k=1;k<4u;k++)ok &= st_load(OBJ_BANK0+k,back,sizeof back)==sizeof old&&!memcmp(back,old,sizeof old);
        if(!rc)break;
    }
    bad+=check("migration survives every page cutoff without changing other slots",ok);
    memcpy(nor,snapshot,sizeof nor);ok=1;
    for(uint32_t round=0;round<4u;round++)for(uint32_t k=0;k<4u;k++){
        next[0]=(uint8_t)(round*4u+k);
        ok &= !st_save(OBJ_BANK0+k,next,sizeof next);
        ok &= st_load(OBJ_BANK0+k,back,sizeof back)==sizeof next&&!memcmp(back,next,sizeof next);
        uint32_t live[ST_UW]={0};
        for(uint32_t j=0;j<4u;j++){
            st_hdr_t h;int copy=st_current(OBJ_BANK0+j,&h);
            if(copy>=2){
                uint32_t mine[ST_UW]={0};st_project_used(mine,OBJ_BANK0+j,(uint32_t)copy,&h);
                for(uint32_t w=0;w<ST_UW;w++){ok &= !(live[w]&mine[w]);live[w]|=mine[w];}
            }
        }
    }
    for(uint32_t k=0;k<4u;k++)ok &= st_load(OBJ_BANK0+k,back,sizeof back)==sizeof next&&back[0]==12u+k;
    bad+=check("four maximum projects and repeated shared-workspace saves fit",ok);
    ok=1;
    uint32_t live[ST_UW]={0};
    for(uint32_t k=0;k<4u;k++){st_hdr_t h;int c=st_current(OBJ_BANK0+k,&h);st_project_used(live,OBJ_BANK0+k,(uint32_t)c,&h);}
    for(uint32_t i=0;i<ST_POOL_SECTORS;i++)if((live[i/32u]>>(i%32u))&1u)for(uint32_t j=3840u;j<4096u;j++)ok &= nor[st_sector(OBJ_BANK0,i+2u)+j]==0xFFu;
    /* The original A0000..C7FFF and EA000..F9FFF regions are the only mutation windows. */
    for(uint32_t i=0;i<sizeof nor;i++)if(!((i>=0xA0000u&&i<0xC8000u)||(i>=0xEA000u&&i<0xFA000u)))ok &= nor[i]==0xFFu;
    bad+=check("SPL scan tails and all non-project regions stay erased",ok);
    uint32_t calls=io_calls;
    bad+=check("pool overflow rejected before flash access",st_save(OBJ_BANK0,next,sizeof next+1u)<0&&io_calls==calls);
    ok=!st_save(OBJ_BANK0,0,0)&&!st_exists(OBJ_BANK0);
    ok &= st_load(OBJ_BANK0+1,back,sizeof back)==sizeof next&&back[0]==13u;
    bad+=check("deleting one slot retains the other full projects",ok);
    memcpy(snapshot,nor,sizeof nor);read_error=1;
    int rc=st_save(OBJ_BANK0,next,sizeof next);read_error=0;
    bad+=check("read failure refuses allocation without any mutation",rc&&!memcmp(nor,snapshot,sizeof nor));
    st_stream_generation=0; /* cold boot reconstructs ownership from flash alone */
    bad+=check("cold boot reconstructs full slot ownership",st_load(OBJ_BANK0+1,back,sizeof back)==sizeof next&&back[0]==13u);
    /* The sequence may wrap, and the address map is authenticated by the header. */
    st_hdr_t h;int copy=st_current(OBJ_BANK0+1,&h);h.seq=0xFFFFFFFFu;h.hcrc=st_crc32(&h,sizeof h-4u);
    memcpy(nor+st_sector(OBJ_BANK0+1,(uint32_t)copy),&h,sizeof h);
    bad+=check("pool sequence wraps safely",!st_save(OBJ_BANK0+1,next,sizeof next)&&st_load(OBJ_BANK0+1,back,sizeof back)==sizeof next&&!memcmp(back,next,sizeof next));
    /* A project record's sectors are read by its own tag (a list of pool indices), never as another object's tags */
    memset(nor,0xFF,sizeof nor);
    h=(st_hdr_t){ST_MAGIC,OBJ_BANK0,3u,1u,ST_PROJECT_MAX,0u,{0u,0u},0u};
    st_list_init(&h);for(uint32_t k=0;k<ST_PROJECT_SECTORS;k++)st_list_put(&h,k,1u+k);
    h.hcrc=st_crc32(&h,sizeof h-4u);memcpy(nor+0xA1000u,&h,sizeof h);
    bad+=check("project sector lists are read by their own tag",!st_head(OBJ_BANK0,3u,&h)&&st_payload_off_header(OBJ_BANK0,&h)==32u);

    return bad;
}

int main(void)
{
    char a[600], b[600], got[600];
    int bad = 0, n;
    memset(nor, 0xFF, sizeof nor);
    memset(a, 'A', sizeof a);
    memset(b, 'B', sizeof b);
    bad += check("empty flash loads nothing", st_load(T_OBJ, got, sizeof got) < 0);
    bad += check("save A", st_save(T_OBJ, a, sizeof a) == 0);
    n = st_load(T_OBJ, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save B", st_save(T_OBJ, b, sizeof b) == 0);
    n = st_load(T_OBJ, got, sizeof got);
    bad += check("load returns B (newer seq)", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 1;                                  /* payload page 1 written, the rest torn */
    st_save(T_OBJ, a, sizeof a);
    fail_after = -1;
    n = st_load(T_OBJ, got, sizeof got);
    bad += check("torn save keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 3;                                  /* all payload pages, header torn */
    st_save(T_OBJ, a, sizeof a);
    fail_after = -1;
    n = st_load(T_OBJ, got, sizeof got);
    bad += check("header not written keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    bad += check("save A again", st_save(T_OBJ, a, sizeof a) == 0);
    n = st_load(T_OBJ, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    corrupt_all(T_OBJ, 0);                            /* bit rot in every copy */
    n = st_load(T_OBJ, got, sizeof got);
    bad += check("both copies corrupt -> nothing", n < 0);
    bad += check("other objects untouched", st_load(T_OBJ + 1, got, sizeof got) < 0);
    bad += check("settings save/load",
                 st_save(OBJ_SETTINGS, "hello", 5) == 0 && st_load(OBJ_SETTINGS, got, 5) == 5 && !memcmp(got, "hello", 5));
    bad += check("CRC-32 is zlib's (check value 0xCBF43926)", st_crc32("123456789", 9) == 0xCBF43926u);
    memset(nor, 0xFF, sizeof nor);
    st_save(T_OBJ + 2, a, sizeof a);
    st_save(T_OBJ + 2, b, sizeof b);              /* B is newer, in the other copy */
    {
        st_hdr_t h;
        int cur = st_current(T_OBJ + 2, &h);
        nor[st_sector(T_OBJ + 2, (uint32_t)cur) + ST_PAYLOAD_OFF + 300] ^= 0x10;   /* rot in the newer copy */
    }
    n = st_load(T_OBJ + 2, got, sizeof got);
    bad += check("newer copy rotten -> the older one loads", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save over the rotten copy", st_save(T_OBJ + 2, b, sizeof b) == 0);
    n = st_load(T_OBJ + 2, got, sizeof got);
    bad += check("... loads the new data", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    corrupt_all(T_OBJ + 2, 1);                        /* every header broken */
    bad += check("both headers broken -> nothing", st_load(T_OBJ + 2, got, sizeof got) < 0);
    bad += check("data stays in the Melodee regions",
                 st_sector(OBJ_SETTINGS, 1) + 4096 <= 0xFF000 && st_sector(T_OBJ + 3, 1) + 4096 <= 0xE0000 &&
                     st_sector(OBJ_UPRESET0, 0) >= 0xDC000 && st_sector(OBJ_UPRESET0 + 1, 1) + 4096 <= 0xE0000);
    /* the FM6 patch bank: the two free sectors, 0x9F000 (after the projects) and 0xFE000 (after the settings) */
    bad += check("FM6 bank in the free sectors 0x9F000 / 0xFE000",
                 OBJ_FM6BANK + 1 == OBJ_BANK0 && st_sector(OBJ_FM6BANK, 0) == 0x9F000u &&
                     st_sector(OBJ_FM6BANK, 1) == 0xFE000u && st_sector(OBJ_SETTINGS, 1) + 4096 == 0xFE000u);
    {   /* the legacy single-pattern projects: retired, nothing loads or saves them (the Prophet banks' place) */
        uint32_t k, calls = io_calls;
        int ok = 1;
        for (k = 0; k < 4u; k++)
            ok &= st_load(OBJ_PROJECT0 + k, got, sizeof got) < 0 && st_save(OBJ_PROJECT0 + k, a, sizeof a) < 0;
        bad += check("legacy project objects retired: no load, no save, no flash access", ok && io_calls == calls);
    }
    {
        static uint8_t bank[3612], back[3612];                 /* (any record up to a sector) */
        uint32_t i;
        for (i = 0; i < sizeof bank; i++) bank[i] = (uint8_t)(i * 7u);
        bad += check("FM6 bank save / load (A then B)", st_save(OBJ_FM6BANK, bank, sizeof bank) == 0 &&
                     st_load(OBJ_FM6BANK, back, sizeof back) == (int)sizeof back && !memcmp(bank, back, sizeof bank) &&
                     (bank[0] ^= 1, st_save(OBJ_FM6BANK, bank, sizeof bank) == 0) &&
                     st_load(OBJ_FM6BANK, back, sizeof back) == (int)sizeof back && back[0] == bank[0]);
        {
            int erased = 1;
            for (i = 256u + sizeof bank; i < 4096u; i++) erased &= nor[0x9F000u + i] == 0xFFu && nor[0xFE000u + i] == 0xFFu;
            bad += check("FM6 bank: the sector tails stay erased", erased);
        }
    }
    {
        uint8_t copies[2 * ST_SECTOR];
        uint32_t off = st_sector(T_OBJ, 0), cut;
        int ok = 1;
        memset(nor, 0xFF, sizeof nor);
        st_save(T_OBJ, a, sizeof a);
        st_save(T_OBJ, b, sizeof b);
        memcpy(copies, nor + off, sizeof copies);
        for (cut = 0; cut < sizeof a + sizeof(st_hdr_t); cut++) {
            memcpy(nor + off, copies, sizeof copies);
            fail_bytes = (int)cut;
            ok &= st_save(T_OBJ, a, sizeof a) != 0;
            fail_bytes = -1;
            ok &= st_load(T_OBJ, got, sizeof got) == (int)sizeof b && !memcmp(got, b, sizeof b);
        }
        bad += check("every payload/header byte cut keeps old data", ok);
        erase_error = 1;
        ok = st_save(T_OBJ, a, sizeof a) != 0;
        erase_error = 0;
        bad += check("erase failure keeps old data", ok && st_load(T_OBJ, got, sizeof got) == (int)sizeof b &&
                                                      !memcmp(got, b, sizeof b));
        write_protected = 1;
        ok = st_save(T_OBJ, a, sizeof a) != 0;
        write_protected = 0;
        bad += check("write protection is a save error", ok && st_load(T_OBJ, got, sizeof got) == (int)sizeof b &&
                                                            !memcmp(got, b, sizeof b));
    }
    {
        st_hdr_t h;
        uint32_t copy;
        for (copy = 0; copy < 2u; copy++) {
            uint32_t off = st_sector(T_OBJ, copy);
            memcpy(&h, nor + off, sizeof h);
            h.seq = 0xFFFFFFFEu + copy;
            h.hcrc = st_crc32(&h, sizeof h - 4u);
            memcpy(nor + off, &h, sizeof h);
        }
        bad += check("save sequence wraps", st_save(T_OBJ, a, sizeof a) == 0);
        n = st_load(T_OBJ, got, sizeof got);
        bad += check("wrapped sequence loads new data", n == (int)sizeof a && !memcmp(got, a, sizeof a));
        bad += check("save after sequence wrap", st_save(T_OBJ, b, sizeof b) == 0 &&
                                                st_load(T_OBJ, got, sizeof got) == (int)sizeof b &&
                                                !memcmp(got, b, sizeof b));
    }
    {
        uint32_t calls = io_calls;
        bad += check("invalid object load does no flash access", st_load(OBJ_COUNT, got, sizeof got) < 0 && io_calls == calls);
        bad += check("invalid object save does no flash access", st_save(OBJ_COUNT, a, sizeof a) < 0 && io_calls == calls);
        bad += check("wrapped object rejected", st_save(0xFFFFFFFFu, a, sizeof a) < 0 && io_calls == calls);
        memset(got, 'X', sizeof got);
        bad += check("small destination rejects whole object", st_load(T_OBJ, got, sizeof got - 1u) < 0 && got[0] == 'X');
        bad += check("oversized save rejected", st_save(T_OBJ, a, ST_PAYLOAD_MAX + 1u) < 0);
    }
    bad += project_pool_test();
    printf("%s\n", bad ? "STORAGE TEST FAILED" : "storage test passed");
    bad += check("CZ banks have disjoint A/B sectors outside project and preset data",
        st_sector(OBJ_CZBANK0,0)==0xC8000u && st_sector(OBJ_CZBANK0+7,1)+ST_SECTOR==0xD8000u &&
        st_capacity(OBJ_CZBANK0)==ST_PAYLOAD_MAX && st_sector(OBJ_UPRESET0,0)==0xDC000u);
    return bad != 0;
}
