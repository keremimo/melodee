/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Resource lifetimes under cache eviction, audio transitions and exhaustion. */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"

static void audio_resources_reset(void) { memset(resource,0,sizeof resource); }
static int cache_tests(void)
{
    int bad=0;uint8_t fm[FM6_PACKED],cz[CZ_BYTES];char name[13];
    reset();audio_resources_reset();up_boot();fm6_factory(3,fm);cz_patch_init(cz);
    for(uint32_t b=0;b<4u;b++){fm[118]=(uint8_t)('A'+b);bad+=check("native FM cache writes across bank evictions",!native_put(ENGI_FM6,72u+b*18u,fm));}
    for(uint32_t b=0;b<8u;b++){cz[128]=(uint8_t)('A'+b);bad+=check("native CZ cache writes across bank evictions",!native_put(ENGI_CZ,144u+b*18u,cz));}
    for(uint32_t b=0;b<4u;b++){set_engine_of(TSEL, ENGI_SID);TSEL->p[P_E1]=(int16_t)b;   /* (SID's WAVE) */bad+=check("general cache writes across bank evictions",!up_store(b*18u,"BANK SOUND"));}
    up_boot();uint32_t reads=preset_reads;
    for(uint32_t k=0;k<64u;k++){up_used(k);up_engine(k);up_name(k,name);native_used(ENGI_FM6,k);native_name(ENGI_FM6,k,name);}
    for(uint32_t k=0;k<128u;k++){native_used(ENGI_CZ,k);native_name(ENGI_CZ,k,name);}
    bad+=check("browsing every preset name/slot does no flash reads",preset_reads==reads);
    for(uint32_t b=0;b<4u;b++)bad+=check("FM cache reload preserves each independent patch",native_raw(ENGI_FM6,72u+b*18u)[118]=='A'+b);
    for(uint32_t b=0;b<8u;b++)bad+=check("CZ cache reload preserves each independent patch",native_raw(ENGI_CZ,144u+b*18u)[128]=='A'+b);
    for(uint32_t b=0;b<4u;b++)bad+=check("general cache reload preserves parameters",up_value(up_rec(b*18u),P_E1)==(int16_t)b);
    {   /* one bank cache for every kind (upreset.c bank_buf): interleaved reads load the bank asked for and find each
         * kind's contents intact; an edit made while a user bank was the cached one reaches its index on eviction */
        p5_patch_t p5,got;p5_patch_init(&p5);memcpy(p5.raw+P5_NAME,"SHARED BANK P5      ",20);
        uint8_t old[CZ_BYTES];int ok=!native_put(ENGI_PROPHET,30,(const uint8_t *)&p5);
        for(uint32_t r=0;r<3u;r++)for(uint32_t b=0;b<4u;b++){
            ok&=up_value(up_rec(b*18u),P_E1)==(int16_t)b && native_raw(ENGI_FM6,72u+b*18u)[118]=='A'+b &&
                native_raw(ENGI_CZ,144u+(b+4u)*18u)[128]=='A'+b+4u &&
                !p5_user_get(30,&got) && !memcmp(got.raw+P5_NAME,"SHARED BANK P5",14);
        }
        bad+=check("one bank cache: user, FM6, CZ-1 (both views) and Prophet banks interleaved read back intact",ok);
        set_engine_of(TSEL, ENGI_SID);up_store(5,"AFTER EDIT");native_raw(ENGI_FM6,90u);up_name(5,name);
        bad+=check("one bank cache: a user bank evicted after an edit keeps its index (the new name, no reload)",
                   up_cached==5u && !strcmp(name,"AFTER EDIT"));
        bad+=check("one bank cache: one user bank's size",sizeof bank_buf==sizeof(up_bank_t));
    }
    flash_ok=0;
    bad+=check("missing flash refuses mutations without evicting committed indexes",native_put(ENGI_FM6,72,fm)==2 && native_put(ENGI_CZ,144,cz)==2 && up_store(0,"NO FLASH")==2 && native_used(ENGI_FM6,72) && native_used(ENGI_CZ,144) && up_used(0));
    flash_ok=1;
    bad+=check("failed missing-flash writes preserve stored patches",native_raw(ENGI_FM6,72)[118]=='A' && native_raw(ENGI_CZ,144)[128]=='A' && up_value(up_rec(0),P_E0)==0);
    resource_get(RES_NOTE_HISTORY,RESOURCE_MAIN_CAPACITY);
    for(uint32_t b=0;b<RESOURCE_BANKS-1u;b++)if(b!=RESOURCE_CACHE_BANK || RESOURCE_CACHE_AVAILABLE)
        resource_get(RES_ENGINE0+b,(uint32_t)((uintptr_t)resource_banks[b].end-(uintptr_t)resource_banks[b].start));
    resource_release(RES_NOTE_HISTORY);
    for(uint32_t b=0;b<RESOURCE_BANKS-1u;b++)resource_release(RES_ENGINE0+b);
    return bad;
}
static int audio_tests(void)
{
    int bad=0;int32_t in[CTL]={0},out[CTL],c[CTL]={0},d[CTL]={0},r[CTL]={0};
    audio_resources_reset();host_tracks_init();
    const uint8_t engines[4]={0,ENGI_FM6,ENGI_SID,ENGI_808};uint32_t expected=sizeof(fm6_part_t)+sizeof(sid_part_t)+sizeof(drum_lane_t)*DV_NLANE;
    for(uint32_t k=0;k<4u;k++){trk[k].engine=engines[k];eng_state_prepare(&trk[k]);}
    bad+=check("default engine setup and derived caches stay within 16 KiB",resource_used()==expected && expected<=16u*1024u);
    for(uint32_t cycle=0;cycle<2048u;cycle++){
        uint32_t k=cycle%NPART,e=eng_vis(cycle/NPART);
        eng_state_clear(k);trk[k].engine=(uint8_t)e;
        if(!eng_state_prepare(&trk[k])){bad+=check("engine switching does not fragment away capacity",0);break;}
    }
    uint32_t largest=0, largest_engine=0;
    for(uint32_t j=0;j<NENG_SHOWN;j++) {
        uint32_t e=eng_vis(j), size=eng_state_size(e);
        if(size>largest){largest=size;largest_engine=e;}
    }
    uint32_t maximum=largest*NPART;
    for(uint32_t k=0;k<4u;k++){
        eng_state_clear(k);trk[k].engine=(uint8_t)largest_engine;eng_state_prepare(&trk[k]);
    }
    bad+=check("four maximum engine states fit without effect buffers",resource_used()==maximum && maximum<=48u*1024u);
    bad+=check("capacity exhaustion returns failure and preserves live memory",resource_get(RES_ENGINE0,RESOURCE_CAPACITY+4u)==0 && resource[RES_ENGINE0].size==largest);
    bad+=check("zero-byte requests release rather than return an arena pointer",!resource_get(RES_ENGINE0,0) && !resource[RES_ENGINE0].size);
    printf("resources: default engine bytes %u, worst audio bytes %u, host arena %u\n",expected,maximum,RESOURCE_CAPACITY);
    return bad;
}
static int bank_tests(void)
{
    int bad=0;
    audio_resources_reset();
    for(uint32_t b=0;b<RESOURCE_BANKS;b++){
        if(b==RESOURCE_CACHE_BANK && !RESOURCE_CACHE_AVAILABLE)continue;
        uint32_t n=(uint32_t)((uintptr_t)resource_banks[b].end-(uintptr_t)resource_banks[b].start);
        uint8_t *p=resource_get(b,n);
        bad+=check("each disjoint bank can be filled exactly without spanning its boundary",p==resource_banks[b].start && resource[b].size==n);
        if(p)memset(p,(int)(b+1u),n);
    }
    bad+=check("all SRAM tails contribute usable capacity",resource_used()==RESOURCE_CAPACITY && resource_used()>RESOURCE_MAIN_CAPACITY);
    bad+=check("combined capacity cannot be returned as one contiguous allocation",!resource_get(RES_NOTE_HISTORY,RESOURCE_CAPACITY));
    void *saved=resource[0].ptr;uint32_t size=resource[0].size;
    bad+=check("failed growth leaves original allocation and contents intact",!resource_get(0,size+4u) && resource[0].ptr==saved && resource[0].size==size && ((uint8_t *)saved)[size-1u]==1u);
    bad+=check("overflowing requests fail without releasing a live allocation",!resource_get(0,UINT32_MAX) && !resource_get(0,UINT32_MAX-3u) && resource[0].ptr==saved && resource[0].size==size);
    for(uint32_t b=1;b<RESOURCE_BANKS;b++)if(b!=RESOURCE_CACHE_BANK || RESOURCE_CACHE_AVAILABLE)
        bad+=check("exhaustion does not overwrite another bank",((uint8_t *)resource[b].ptr)[0]==b+1u && ((uint8_t *)resource[b].ptr)[resource[b].size-1u]==b+1u);
    resource_release(0);
    uint32_t *p=resource_get(0,5u);
    bad+=check("released tail is reusable and rounded allocation is zeroed",p==saved && resource[0].size==8u && !p[0] && !p[1]);
    bad+=check("same-size reuse preserves contents",(p[0]=0xA5A5A5A5u,resource_get(0,8u)==p && p[0]==0xA5A5A5A5u));
    audio_resources_reset();
    uint32_t rng=1u;int valid=1;
    for(uint32_t cycle=0;cycle<8192u && valid;cycle++){
        rng=rng*1664525u+1013904223u;uint32_t id=(rng>>16)%RES_COUNT;
        rng=rng*1664525u+1013904223u;
        uint32_t bytes=(rng>>16)%36000u;
        if(!(cycle%7u))resource_release(id);else resource_get(id,bytes);
        for(uint32_t a=0;a<RES_COUNT && valid;a++)if(resource[a].size){
            uintptr_t lo=(uintptr_t)resource[a].ptr,hi=lo+resource[a].size;int contained=0;
            for(uint32_t b=0;b<RESOURCE_BANKS;b++)
                if(b!=RESOURCE_CACHE_BANK || RESOURCE_CACHE_AVAILABLE)
                    contained|=lo>=(uintptr_t)resource_banks[b].start && hi<=(uintptr_t)resource_banks[b].end;
            valid&=contained && !(lo&3u) && !(resource[a].size&3u);
            for(uint32_t b=0;b<a;b++)if(resource[b].size)
                valid&=hi<=(uintptr_t)resource[b].ptr || lo>=(uintptr_t)resource[b].ptr+resource[b].size;
        }
    }
    bad+=check("8192 allocation/release transitions stay aligned, inside one bank and disjoint",valid);
    audio_resources_reset();return bad;
}
static int resize_tests(void)
{
    int bad=0;audio_resources_reset();
    uint8_t *a=resource_get(RES_ENGINE0,64),*b=resource_get(RES_NOTE_HISTORY,64);
    memset(b,0x5a,64);resource_release(RES_ENGINE0);
    uint8_t *grown=resource_resize(RES_NOTE_HISTORY,256);
    int valid=grown==a;
    for(uint32_t i=0;i<256u&&valid;i++)valid&=grown[i]==(i<64u?0x5a:0);
    bad+=check("preserving growth can overlap its old block and zeroes only the new tail",valid);
    resource_resize(RES_NOTE_HISTORY,33);
    bad+=check("shrinking preserves address, bytes and word alignment",resource[RES_NOTE_HISTORY].ptr==grown&&resource[RES_NOTE_HISTORY].size==36u&&grown[35]==0x5a);
    uint32_t large=(uint32_t)(resource_banks[RESOURCE_BANKS-1u].end-resource_banks[RESOURCE_BANKS-1u].start);
    uint8_t *moved=resource_resize(RES_NOTE_HISTORY,large);
    valid=moved==resource_banks[RESOURCE_BANKS-1u].start;
    for(uint32_t i=0;i<large&&valid;i++)valid&=moved[i]==(i<36u?0x5a:0);
    bad+=check("preserving growth moves between disjoint SRAM banks",valid);
    bad+=check("failed resize leaves pointer, size and contents available for retry",!resource_resize(RES_NOTE_HISTORY,UINT32_MAX)&&resource[RES_NOTE_HISTORY].ptr==moved&&resource[RES_NOTE_HISTORY].size==large&&moved[35]==0x5a);
    bad+=check("zero resize releases history",!resource_resize(RES_NOTE_HISTORY,0)&&!resource[RES_NOTE_HISTORY].size);
    audio_resources_reset();return bad;
}
int main(void)
{
    resource_host_cache_enabled=0;int bad=bank_tests()+resize_tests();
    uint32_t sram_capacity=RESOURCE_CAPACITY;
    resource_host_cache_enabled=1;bad+=bank_tests();
    bad+=check("validated data-cache bank shares its full 28 KiB with audio and history",RESOURCE_CAPACITY==sram_capacity+28672u && UI_CACHE_BYTES==0);
#ifndef RESOURCES_BANKS_ONLY
    bad+=cache_tests()+audio_tests();
    resource_host_cache_enabled=0;bad+=cache_tests()+audio_tests();
#endif
    printf("Resource test %s\n",bad?"FAILED":"passed");return !!bad;
}
