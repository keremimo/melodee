/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee 1.x's bank objects, read only: converted into the shared user presets (shared_presets.c), checked in a
 * backup's restore (editor_backup.c). By their store formats (module_schema.h MOD_SCH_STORE_*), their patches checked
 * as their synths' schemas say (schema.c patch_stored_ok): VOICE banks (16 packed voices: the sidecars of old general
 * records too), TONE banks (16 named tones), PROGRAM banks (26 programs and their stars) */
#define NATIVE_FM_MAGIC 0x314D464Eu
typedef struct { uint32_t magic; uint16_t ver,slots; uint32_t used; uint8_t tone[16][MOD_SCH_VOICE_BYTES]; } native_fm_t;
#define TONE_BANK_MAGIC 0x42435A43u
typedef struct { uint32_t magic; uint16_t ver, slots; uint32_t used; char name[16]; uint8_t tone[16][MOD_SCH_TONE_BYTES]; } tone_bank_t;
_Static_assert(sizeof(tone_bank_t)==2332u && sizeof(tone_bank_t)<=3840u,"a TONE bank object");
#define PROGRAM_BANK_MAGIC 0x31553550u
#define PROGRAM_BANK_SLOTS 26u
typedef struct {uint32_t magic,used,favorites;uint8_t patch[PROGRAM_BANK_SLOTS][MOD_SCH_PROGRAM_BYTES];} program_bank_t;
static int tone_bank_valid(const tone_bank_t *b)
{
    if(b->magic!=TONE_BANK_MAGIC || (b->ver!=1 && b->ver!=2) || b->slots!=16 || (b->used>>16) || !b->name[0])return 0;
    for(uint32_t i=0;i<16;i++)if((uint8_t)b->name[i]>126 || (b->name[i] && (uint8_t)b->name[i]<32))return 0;
    for(uint32_t i=0;i<16;i++)if((b->used>>i)&1u)if(!patch_stored_ok(MOD_SCH_STORE_TONE,b->tone[i]))return 0;
    return 1;
}
static uint32_t program_bank_mask(uint32_t bank) { return (1u<<(bank==4u?24u:26u))-1u; }
static int program_bank_head_ok(uint32_t magic,uint32_t used,uint32_t favorites,uint32_t bank)   /* its first 12 bytes */
{
    return bank<5u && magic==PROGRAM_BANK_MAGIC && !(used&~program_bank_mask(bank)) &&
           !(favorites&~(program_bank_mask(bank)|(bank?0u:0x80000000u)));
}
static int program_bank_valid(const program_bank_t *b,uint32_t bank)
{
    if(!program_bank_head_ok(b->magic,b->used,b->favorites,bank))return 0;
    for(uint32_t k=0;k<PROGRAM_BANK_SLOTS;k++)if((b->used>>k&1u)&&!patch_stored_ok(MOD_SCH_STORE_PROGRAM,b->patch[k]))return 0;
    return 1;
}
#ifndef MELODEE_LEGACY_STORE                   /* (fm1_cc.h; host builds of this file alone: on) */
#if BOARD_SMK37
#define MELODEE_LEGACY_STORE 0
#else
#define MELODEE_LEGACY_STORE 1
#endif
#endif
#if MELODEE_FLASH
static native_fm_t *bank_fm(void);
#define native_fm_cache (*bank_fm())
static uint8_t native_fm_cached=255;
#if MELODEE_LEGACY_STORE
static uint32_t native_fm_obj(uint32_t b){return b<2u?OBJ_NATIVEFM0+b:b==2u?OBJ_FM6BANK:OBJ_UPFM6_EXT;}
static native_fm_t *native_fm_bank(uint32_t b)
{
    if(native_fm_cached!=b){bank_claim(BK_FM);int n=flash_ok?st_load(native_fm_obj(b),&native_fm_cache,sizeof native_fm_cache):-1;if(n!=sizeof native_fm_cache)memset(&native_fm_cache,0,sizeof native_fm_cache);native_fm_cached=(uint8_t)b;}
    return &native_fm_cache;
}
/* VOICE sidecar k (bank k/16's tone k%16) into raw, read from its record in pieces and the bank checked as
 * native_fm_valid checks it: the shared bank cache (bank_buf: a user bank being converted, shared_presets.c) is left
 * as it is; 0 none */
static int legacy_native_tone(uint32_t k,uint8_t *raw)
{
    uint32_t obj=native_fm_obj(k/16u),len,h[3];
    int c=flash_ok && k<64u?st_open(obj,&len):-1;
    if(c<0 || len!=sizeof(native_fm_t) || st_read_range(obj,(uint32_t)c,0,h,sizeof h) || h[0]!=NATIVE_FM_MAGIC ||
       h[1]!=(1u|16u<<16) || h[2]>>16 || !(h[2]>>(k%16u)&1u))
        return 0;
    for(uint32_t t=0;t<17u;t++){                   /* (each used tone checked; the one asked for read last) */
        uint32_t i=t<16u?t:k%16u;
        if(t<16u && !(h[2]>>t&1u))continue;
        if(st_read_range(obj,(uint32_t)c,__builtin_offsetof(native_fm_t,tone)+i*MOD_SCH_VOICE_BYTES,raw,MOD_SCH_VOICE_BYTES) ||
           (t<16u && !patch_stored_ok(MOD_SCH_STORE_VOICE,raw)))
            return 0;
    }
    return 1;
}
#endif
#else
static native_fm_t native_fm_host[4] __attribute__((section(".pool")));
static native_fm_t *native_fm_bank(uint32_t b){return &native_fm_host[b];}
#endif
#if MELODEE_LEGACY_STORE
static int native_fm_valid(const native_fm_t *b)
{
    if(b->magic!=NATIVE_FM_MAGIC || b->ver!=1u || b->slots!=16u || b->used>>16)return 0;
    for(uint32_t k=0;k<16u;k++)if(b->used>>k&1u)if(!patch_stored_ok(MOD_SCH_STORE_VOICE,b->tone[k]))return 0;
    return 1;
}
#endif

static void native_cache_reset(void)
{
#if MELODEE_FLASH
    native_fm_cached=255;
#endif
}
#if MELODEE_LEGACY_STORE
static int legacy_native_used(uint32_t e,uint32_t k)
{
    if(patch_store_tag(e)!=MOD_SCH_STORE_VOICE || k>=64u)return 0;
    const native_fm_t *b=native_fm_bank(k/16u);return native_fm_valid(b) && (b->used>>(k%16u)&1u);
}
static const uint8_t *legacy_native_raw(uint32_t e,uint32_t k){(void)e;return native_fm_bank(k/16u)->tone[k%16u];}
#endif
static uint32_t native_size(uint32_t e){return patch_sound_len(e);}   /* (a native slot's bytes: the patch's sound) */
