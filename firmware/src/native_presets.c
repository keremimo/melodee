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
#if MELODEE_FLASH
static native_fm_t *bank_fm(void);
#define native_fm_cache (*bank_fm())
static uint8_t native_fm_cached=255;
#else
static native_fm_t native_fm_host[4] __attribute__((section(".pool")));
static native_fm_t *native_fm_bank(uint32_t b){return &native_fm_host[b];}
#endif

static void native_cache_reset(void)
{
#if MELODEE_FLASH
    native_fm_cached=255;
#endif
}
static uint32_t native_size(uint32_t e){return patch_sound_len(e);}   /* (a native slot's bytes: the patch's sound) */
