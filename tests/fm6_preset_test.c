/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Independent native preset pools and failure recovery on simulated NOR. */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"

static void reboot_presets(void)
{
    up_cache_reset();native_cache_reset();cz_bank_boot();up_boot();
}
static int bank_import(void)
{
    int bad=0;static uint8_t bank[4096],old[4096];uint32_t saved;
    reset();fm6_init();cz_init();up_boot();
    for(uint32_t k=0;k<32;k++)fm6_factory(k%FM6_NFAC,bank+k*128u);
    uint32_t writes=erases;
    bad+=check("32-voice import commits two storage objects, not 32 saves",native_fm_import32(36,bank,&saved)==0 && saved==32 && erases==writes+2);
    reboot_presets();int ok=1;
    for(uint32_t k=0;k<32;k++)ok &= native_used(ENGI_FM6,36+k) && !memcmp(native_raw(ENGI_FM6,36+k),bank+k*128u,128);
    bad+=check("all imported VMEM patches survive reboot in F033-F064",ok && up_count()==32 && !native_used(ENGI_FM6,0));
    memcpy(old,bank,sizeof bank);bank[4095]=0x80;writes=erases;
    bad+=check("bank validation finishes before any flash write",native_fm_import32(36,bank,&saved)==1 && saved==0 && erases==writes);bank[4095]=old[4095];
    song.playing=1;bad+=check("bank import refuses transport without changing flash",native_fm_import32(36,bank,&saved)==2 && !saved && erases==writes);song.playing=0;
    for(uint32_t k=0;k<32;k++)bank[k*128u+118]='X';
    fail_after=2;bad+=check("failure in first bank object leaves all old patches in RAM",native_fm_import32(36,bank,&saved)==2 && !saved && !memcmp(native_raw(ENGI_FM6,36),old,128));fail_after=-1;reboot_presets();
    bad+=check("interrupted first object retains prior bank after reboot",!memcmp(native_raw(ENGI_FM6,36),old,128) && !memcmp(native_raw(ENGI_FM6,67),old+31*128u,128));
    fail_after=17;bad+=check("failure in second object reports exactly 18 committed patches",native_fm_import32(36,bank,&saved)==2 && saved==18 && !memcmp(native_raw(ENGI_FM6,36),bank,128) && !memcmp(native_raw(ENGI_FM6,54),old+18*128u,128));fail_after=-1;reboot_presets();
    bad+=check("partial import RAM matches the durable objects after reboot",!memcmp(native_raw(ENGI_FM6,53),bank+17*128u,128) && !memcmp(native_raw(ENGI_FM6,54),old+18*128u,128));
    bad+=check("resending completes the interrupted bank",!native_fm_import32(36,bank,&saved) && saved==32);reboot_presets();
    bad+=check("resend commits the final voice",!memcmp(native_raw(ENGI_FM6,67),bank+31*128u,128));
    return bad;
}
int main(void)
{
    int bad=0;uint8_t fm[FM6_PACKED],cz[CZ_BYTES],got[FM6_PACKED];
    reset();fm6_init();cz_init();up_boot();
    fm6_factory(3,fm);memcpy(fm+118,"MY VOICE  ",10);cz_patch_init(cz);memcpy(cz+128,"MY CZ TONE      ",16);cz[20]=0x37;
    bad+=check("every engine addresses the same 384 shared slots",native_limit(ENGI_FM6)==384 && native_limit(ENGI_CZ)==384 && UP_SLOTS==384 && !up_count());
    bad+=check("F064 stores original 128-byte DX7 VMEM",native_put(ENGI_FM6,63,fm)==0 && !memcmp(native_raw(ENGI_FM6,63),fm,128));
    bad+=check("Z128 stores all original 144 CZ-1 bytes",native_put(ENGI_CZ,127,cz)==0 && !memcmp(native_raw(ENGI_CZ,127),cz,144));
    bad+=check("shared slots expose their actual engine without duplicate entries",up_used(63) && up_used(127) && up_count()==2 && !native_used(ENGI_CZ,63) && !native_used(ENGI_FM6,127));
    TSEL->p[P_RESERVED33]=47;TSEL->p[P_LD_PIT]=12;TSEL->step[0]=(step_t){.time=ST_NOTE,.n=1,.note={60}};
    native_load(ENGI_FM6,63,0);fm6_pack(fm6_patch_of(0),got);
    bad+=check("native FM6 load keeps effects, modulation and pattern",!memcmp(got,fm,128) && !TSEL->p[P_RESERVED33] && TSEL->p[P_LD_PIT]==TP[P_LD_PIT].def && TSEL->step[0].note[0]==60);
    native_load(ENGI_CZ,127,0);
    bad+=check("native CZ load keeps effects, modulation and pattern",!memcmp(cz_patch_of(0)->raw,cz,144) && !TSEL->p[P_RESERVED33] && TSEL->p[P_LD_PIT]==TP[P_LD_PIT].def && TSEL->step[0].note[0]==60);
    uint32_t total,cur=preset_all_pos(&total),k,e=preset_all_at(cur-1u,&k);
    bad+=check("Z128 follows the final CZ factory tone in the real browser",e==ENGI_CZ && k==64u && user_of(TSEL)==127);
    preset_go(cur-1u);bad+=check("global preset scrolling loads the preceding factory tone",TSEL->eng_req==ENGI_CZ && TSEL->preset==64u && !TSEL->user);
    preset_go(cur);bad+=check("global preset scrolling returns to Z128",TSEL->user==128 && !memcmp(cz_patch_of(0)->raw,cz,144));
    cur=preset_all_pos(&total);e=preset_all_at(cur,&k);
    bad+=check("global preset browser identifies native slots as user sounds",e==USER_GENERAL && k==127);
    favorite_set(USER_NATIVE_FM,63,1);favorite_set(USER_NATIVE_CZ,127,1);favorites.filter=1;
    bad+=check("native presets participate in the regular favorites filter",preset_pos(&total)<2 && total==2);
    preset_go(0);bad+=check("filtered global scrolling loads F064 directly",TSEL->eng_req==ENGI_FM6 && TSEL->user==64);
    settings_save();memset(&favorites,0,sizeof favorites);persist_t persisted;int n=st_load(OBJ_SETTINGS,&persisted,sizeof persisted);settings_import(&persisted,n);
    bad+=check("native favorites persist without changing the settings layout",favorite_has(USER_NATIVE_FM,63) && favorite_has(USER_NATIVE_CZ,127));favorites.filter=0;
    reboot_presets();
    bad+=check("both native pools survive reboot byte for byte",native_used(ENGI_FM6,63) && native_used(ENGI_CZ,127) && !memcmp(native_raw(ENGI_FM6,63),fm,128) && !memcmp(native_raw(ENGI_CZ,127),cz,144));
    uint8_t newer[128];memcpy(newer,fm,128);newer[118]='N';fail_after=2;
    bad+=check("torn native save reports failure and rolls RAM back",native_put(ENGI_FM6,63,newer)==2 && !memcmp(native_raw(ENGI_FM6,63),fm,128));fail_after=-1;reboot_presets();
    bad+=check("torn native save keeps the last flash commit",!memcmp(native_raw(ENGI_FM6,63),fm,128));
    song.playing=1;uint32_t writes=erases;bad+=check("native writes refuse playback without flash erase",native_put(ENGI_CZ,0,cz)==2 && erases==writes);song.playing=0;
    native_put(ENGI_CZ,127,0);native_put(ENGI_FM6,63,0);reboot_presets();
    bad+=check("erased native presets stay erased across boot",!native_used(ENGI_CZ,127) && !native_used(ENGI_FM6,63));

    /* An FM6 general preset keeps its voice in the native slot of the same number. */
    reset();fm6_init();set_engine_of(TSEL,ENGI_FM6);
    uint8_t voice[FP_SIZE+1];fm6_unpack(fm,voice);fm6_set_patch(0,voice);
    up_store(47,"MIDDLE");reboot_presets();
    bad+=check("an FM6 user preset's voice survives reboot in its native slot",up_used(47) && native_used(ENGI_FM6,47) && !memcmp(native_raw(ENGI_FM6,47),fm,118) && !memcmp(native_raw(ENGI_FM6,47)+118,"MIDDLE    ",10));
    int layout=1;
    /* (the legacy projects are retired: the Prophet banks took their place when the app area grew) */
    #define RETIRED(o) st_retired(o)
    for(uint32_t a=0;a<OBJ_COUNT;a++)for(uint32_t ac=0;ac<2;ac++){
        uint32_t lo=st_sector(a,ac),len=st_banked(a)?5u*ST_SECTOR:ST_SECTOR;
        if(RETIRED(a))continue;
        layout &= !(lo<0xE5000u && lo+len>0xE0000u) && lo>=0x93000u;   /* (above the app area) */
        for(uint32_t b=a+1;b<OBJ_COUNT;b++)for(uint32_t bc=0;bc<2;bc++){uint32_t blo=st_sector(b,bc),blen=st_banked(b)?5u*ST_SECTOR:ST_SECTOR;if(RETIRED(b))continue;layout &=lo+len<=blo || blo+blen<=lo;}
    }
    bad+=check("native storage stays separate from projects, general presets and OTA",layout);
    bad+=bank_import();
    printf("Native preset test %s\n",bad?"FAILED":"passed");return !!bad;
}
