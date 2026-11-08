/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Shared-slot adapters and conversion of the immediately preceding formats.
 * Only the main loop accesses banks. Audio keeps independent track patches. */
static int up_favorite(uint32_t k)
{
    if(!up_used(k))return 0;
#if MELODEE_FLASH
    return up_meta[k].favorite;
#else
    return !!(up_rec(k)->used&UP_STAR);
#endif
}
static void up_favorite_poll(void)
{
#if MELODEE_FLASH
    if(!flash_ok || transport_busy() || up_migration_error || !up_fav_dirty)return;
    for(uint32_t b=0;b<UP_BANKS;b++)if(up_fav_dirty>>b&1u){(void)up_cache_bank(b);if((up_bank_old>>b&1u) || ((up_migrate>>b&1u)&&!up_converting))return;if(up_save_bank(b))return;up_cache_index(b);}
#endif
}
static int up_favorite_set(uint32_t k,int on)
{
    if(!up_used(k) || up_favorite(k)==!!on)return 0;
#if MELODEE_FLASH
    up_meta[k].favorite=(uint8_t)!!on;up_fav_dirty|=1u<<(k/UP_PER_BANK);up_favorite_poll();up_gen++;
#else
    up_rec(k)->used=(uint8_t)((up_rec(k)->used&~UP_STAR)|(on?UP_STAR:0));up_gen++;
#endif
    return 1;
}
/* a synth whose sound is its patch (its user presets: the native layout, upreset.c up_native_ver) */
static uint32_t native_limit(uint32_t e)
{
    uint32_t v=up_native_ver(e);
    return v!=UP_VER && v!=UP_VER_KIT ? UP_SLOTS : 0u;
}
static void shared_native_record(up_rec_t *r,uint32_t e,const uint8_t *raw)
{
    memset(r,0,sizeof *r);r->used=UP_USED;r->engine=(uint8_t)e;
    r->ver=(uint8_t)up_native_ver(e);
    memcpy(r->packed,raw,native_size(e));
    uint32_t off=0,len=patch_name_place(e,&off),n=0;
    if(len>12u)len=12u;
    for(uint32_t i=0;i<len;i++){uint8_t c=raw[off+i];r->name[i]=c>=32u&&c<=126u?(char)c:' ';if(r->name[i]!=' ')n=i+1u;}
    if(!n)memcpy(r->name,"UNTITLED",8u);
}
static int native_used(uint32_t e,uint32_t k)
{
    return k<UP_SLOTS && native_limit(e) && up_used(k) && up_engine(k)==e;
}
static int native_old(uint32_t e,uint32_t k){(void)e;return up_old(k);}
static const uint8_t *native_raw(uint32_t e,uint32_t k){(void)e;return up_rec(k)->packed;}
static void native_name(uint32_t e,uint32_t k,char *out){(void)e;up_name(k,out);}
static int native_put(uint32_t e,uint32_t k,const uint8_t *raw)
{
    if(!native_limit(e) || k>=UP_SLOTS)return 1;
    if(!raw)return up_put(k,0);
    up_rec_t r;shared_native_record(&r,e,raw);
    if(!up_valid(&r))return 1;
    return up_put(k,&r);
}
static int native_update(uint32_t e,uint32_t k,const uint8_t *raw)
{
    if(!native_used(e,k))return 1;
    up_rec_t old=*up_rec(k),r;int16_t values[P_COUNT];up_values(&old,values);
    shared_native_record(&r,e,raw);r.np=sizeof UP_NATIVE_PARAMS;
    for(uint32_t i=0;i<r.np;i++)r.tone_extra[i]=(uint8_t)(values[UP_NATIVE_PARAMS[i]]+64);
    memcpy(r.name,old.name,12u);return up_valid(&r)?up_put(k,&r):1;
}
static int native_load(uint32_t e,uint32_t k,uint32_t tr){return native_used(e,k)?up_load_to(k,tr):1;}
static int native_store(uint32_t e,uint32_t k,uint32_t tr,const char *name)
{
    return tr<NTRK && trk[tr].eng_req==e && native_limit(e)?up_store_to(k,tr,name):1;
}
static int shared_import(uint32_t e,uint32_t first,uint32_t count,const uint8_t *raw,uint32_t stride,uint32_t *saved)
{
    *saved=0;if(!native_limit(e) || first>=UP_SLOTS || count>UP_SLOTS-first || stride!=native_size(e))return 1;
    for(uint32_t i=0;i<count;i++){up_rec_t r;shared_native_record(&r,e,raw+i*stride);if(!up_valid(&r))return 1;}
#if MELODEE_FLASH
    if(!flash_ok || up_migration_error)return 2;
#endif
    if(transport_busy())return 2;
    for(uint32_t i=0;i<count;i++){
        if(up_old(first+i) && !preset_old_write)return 2;
#if MELODEE_FLASH
        if((up_migrate>>((first+i)/UP_PER_BANK)&1u) && !up_converting)return 2;
#endif
    }
    while(*saved<count){
        uint32_t slot=first+*saved,b=slot/UP_PER_BANK,n=UP_PER_BANK-slot%UP_PER_BANK;if(n>count-*saved)n=count-*saved;
        up_bank_t *bank=up_cache_bank(b);
        if(up_old_bank(slot) && preset_old_write)memset(bank->r,0,sizeof bank->r);
        bank->magic=UP_BANK_MAGIC;bank->rsize=sizeof(up_rec_t);bank->nslot=UP_PER_BANK;
        for(uint32_t j=0;j<n;j++){up_rec_t r;shared_native_record(&r,e,raw+(*saved+j)*stride);uint32_t pos=(slot+j)%UP_PER_BANK;if(up_valid(&bank->r[pos]))r.used|=bank->r[pos].used&UP_STAR;bank->r[pos]=r;}
#if MELODEE_FLASH
        if(up_save_bank(b)){up_cached=255;(void)up_cache_bank(b);return 2;}   /* (the bank as its record has it again) */
        up_bank_old&=~(1u<<b);up_cache_index(b);
#endif
        *saved+=n;up_gen++;sync_reload=1;ui.force=1;
    }
    return 0;
}
#if MELODEE_LEGACY_STORE
/* an old general record's VOICE sidecar k (Melodee 1.x's FM6 banks), else the VOICE synth's starting voice */
static void shared_voice_sidecar(uint32_t k, uint8_t *raw)
{
    uint8_t p[MOD3_PATCH_MAX];
    uint32_t e = patch_store_engine(MOD_SCH_STORE_VOICE);
#if MELODEE_FLASH
    if (patch_store_tag(e) == MOD_SCH_STORE_VOICE && legacy_native_tone(k, raw)) return;   /* (the user bank's cache kept) */
#else
    if (legacy_native_used(e, k)) { memcpy(raw, legacy_native_raw(e, k), MOD_SCH_VOICE_BYTES); return; }
#endif
    patch_default(e, p);
    memcpy(raw, p, MOD_SCH_VOICE_BYTES);
}
#endif

#ifndef MELODEE_LEGACY_STORE                   /* (fm1_cc.h; host builds of this file alone: on) */
#if BOARD_SMK37
#define MELODEE_LEGACY_STORE 0
#else
#define MELODEE_LEGACY_STORE 1
#endif
#endif
#if MELODEE_FLASH
#if MELODEE_LEGACY_STORE
/* P5's 26-record banks spill their final eight records into the two unused
 * records in each old 16-record bank. The final six records use the new pair.
 * Destinations are deterministic, so a reboot retries the identical writes. */
static uint32_t shared_p5_slot(uint32_t slot)
{
    uint32_t b=slot/26u,k=slot%26u;
    if(k<18u)return (16u+b)*UP_PER_BANK+k;
    uint32_t spill=b*8u+k-18u;
    return spill<32u?(spill/2u)*UP_PER_BANK+16u+spill%2u:378u+spill-32u;
}
#endif
static int up_decode_bank(uint32_t b,int n)
{
    if(n==(int)sizeof(up_disk_bank_t) && up_cache.magic==UP_DISK_MAGIC && up_cache.rsize==sizeof(up_disk_rec_t) && up_cache.nslot==UP_PER_BANK){
        for(uint32_t j=UP_PER_BANK;j--;){up_disk_rec_t r;memcpy(&r,(uint8_t *)&up_cache+8u+j*sizeof r,sizeof r);memset(&up_cache.r[j],0,sizeof(up_rec_t));memcpy(&up_cache.r[j],r.head,160u);memcpy(up_cache.r[j].tone_extra,r.extra,46u);}
    }
#if MELODEE_LEGACY_STORE
    else if(b<4u && ((b<2u && n==3816 && up_cache.magic==UP_BANK_MAGIC && up_cache.rsize==238u && up_cache.nslot==16u) || (b>=2u && n==3808))){
        if(b>=2u)for(uint32_t j=16u;j--;){up_rec_t r;memcpy(&r,(uint8_t *)&up_cache+j*sizeof r,sizeof r);up_cache.r[j]=r;}
        for(uint32_t j=0;j<16u;j++)if(up_valid(&up_cache.r[j])){
            uint32_t k=b*16u+j;int star=b<4u?((k<32u?favorites.user:favorites_user_hi)>>(k%32u)&1u):b<8u?((favorites.factory[ship_store(MOD_SCH_STORE_VOICE)][(24u+(b-4u)*16u+j)/8u]>>((24u+(b-4u)*16u+j)%8u))&1u):((favorites.factory[ship_store(MOD_SCH_STORE_TONE)][(65u+(b-8u)*16u+j)/8u]>>((65u+(b-8u)*16u+j)%8u))&1u);
            if(star)up_cache.r[j].used|=UP_STAR;
        }
        memset(&up_cache.r[16],0,2u*sizeof(up_rec_t));
        for(uint32_t j=0;j<16u;j++)if(up_cache.r[j].used && !up_valid(&up_cache.r[j])){
            up_cache.magic=UP_BANK_MAGIC;up_cache.rsize=sizeof(up_rec_t);up_cache.nslot=UP_PER_BANK;return 2;
        }
        up_migrate|=1u<<b;
    }else if(b>=4u && b<8u && n==(int)sizeof(native_fm_t) && native_fm_valid((const native_fm_t *)&up_cache)){
        uint32_t used=((const native_fm_t *)&up_cache)->used;
        for(uint32_t j=16u;j--;){uint8_t raw[MOD_SCH_VOICE_BYTES];memcpy(raw,((const native_fm_t *)&up_cache)->tone[j],sizeof raw);if(used>>j&1u)shared_native_record(&up_cache.r[j],ship_store(MOD_SCH_STORE_VOICE),raw);else memset(&up_cache.r[j],0,sizeof(up_rec_t));}
        for(uint32_t j=0;j<16u;j++)if(up_valid(&up_cache.r[j])){
            uint32_t k=b*16u+j;int star=b<4u?((k<32u?favorites.user:favorites_user_hi)>>(k%32u)&1u):b<8u?((favorites.factory[ship_store(MOD_SCH_STORE_VOICE)][(24u+(b-4u)*16u+j)/8u]>>((24u+(b-4u)*16u+j)%8u))&1u):((favorites.factory[ship_store(MOD_SCH_STORE_TONE)][(65u+(b-8u)*16u+j)/8u]>>((65u+(b-8u)*16u+j)%8u))&1u);
            if(star)up_cache.r[j].used|=UP_STAR;
        }
        memset(&up_cache.r[16],0,2u*sizeof(up_rec_t));up_migrate|=1u<<b;
    }else if(b>=8u && b<16u && n==(int)sizeof(tone_bank_t) && tone_bank_valid((const tone_bank_t *)&up_cache)){
        uint32_t used=((const tone_bank_t *)&up_cache)->used;
        for(uint32_t j=16u;j--;){uint8_t raw[MOD_SCH_TONE_BYTES];memcpy(raw,((const tone_bank_t *)&up_cache)->tone[j],sizeof raw);if(used>>j&1u)shared_native_record(&up_cache.r[j],ship_store(MOD_SCH_STORE_TONE),raw);else memset(&up_cache.r[j],0,sizeof(up_rec_t));}
        for(uint32_t j=0;j<16u;j++)if(up_valid(&up_cache.r[j])){
            uint32_t k=b*16u+j;int star=b<4u?((k<32u?favorites.user:favorites_user_hi)>>(k%32u)&1u):b<8u?((favorites.factory[ship_store(MOD_SCH_STORE_VOICE)][(24u+(b-4u)*16u+j)/8u]>>((24u+(b-4u)*16u+j)%8u))&1u):((favorites.factory[ship_store(MOD_SCH_STORE_TONE)][(65u+(b-8u)*16u+j)/8u]>>((65u+(b-8u)*16u+j)%8u))&1u);
            if(star)up_cache.r[j].used|=UP_STAR;
        }
        memset(&up_cache.r[16],0,2u*sizeof(up_rec_t));up_migrate|=1u<<b;
    }else if(b>=16u && b<21u && n==(int)sizeof(program_bank_t) && program_bank_valid((const program_bank_t *)&up_cache,b-16u)){
        uint32_t used=((const program_bank_t *)&up_cache)->used,stars=((const program_bank_t *)&up_cache)->favorites;
        for(uint32_t j=18u;j--;){uint8_t raw[MOD_SCH_PROGRAM_BYTES];memcpy(raw,((const program_bank_t *)&up_cache)->patch[j],sizeof raw);if(used>>j&1u)shared_native_record(&up_cache.r[j],ship_store(MOD_SCH_STORE_PROGRAM),raw);else memset(&up_cache.r[j],0,sizeof(up_rec_t));}
        for(uint32_t j=0;j<18u;j++)if((used&stars)>>j&1u)up_cache.r[j].used|=UP_STAR;
        up_migrate|=1u<<b;
    }
#endif
    else return 0;
    up_cache.magic=UP_BANK_MAGIC;up_cache.rsize=sizeof(up_rec_t);up_cache.nslot=UP_PER_BANK;
    return 1;
}
/* A durable reset marker lives in the last, unused record of bank 21.
 * Commit it BEFORE touching source banks. A power loss then resumes wiping,
 * never conversion. Clear both A/B copies so an older bank cannot resurrect.
 * Only remove the marker after every preceding copy is empty and committed. */
#define SHARED_WIPE_TAG "WIPE-PENDING"
_Static_assert(UP_SLOTS < UP_BANKS * UP_PER_BANK,"reset intent requires an unused final bank record");
static int shared_wipe_pending(void)
{
    const up_rec_t *r=&up_cache_bank(UP_BANKS-1u)->r[UP_PER_BANK-1u];
    return !(up_bank_old>>(UP_BANKS-1u)&1u) && !r->used &&
           !memcmp(r->name,SHARED_WIPE_TAG,sizeof r->name);
}
static int shared_wipe_bank(uint32_t b,int pending)
{
    up_bank_t *bank=up_cache_bank(b);
    memset(bank,0,sizeof *bank);
    bank->magic=UP_BANK_MAGIC;bank->rsize=sizeof(up_rec_t);bank->nslot=UP_PER_BANK;
    if(pending)memcpy(bank->r[UP_PER_BANK-1u].name,SHARED_WIPE_TAG,12u);
    up_fav_dirty&=~(1u<<b);
    return up_save_bank(b);
}
static int shared_wipe(void)
{
    uint32_t done=0;
    BOOT_WIPE_BEGIN("WIPING PRESETS",1u+UP_BANKS*2u);
    if(!flash_ok || transport_busy())goto failed;
    up_converting=1;up_migration_error=1;
    if(!shared_wipe_pending() && shared_wipe_bank(UP_BANKS-1u,1))goto failed;
    BOOT_WORK_PROGRESS(++done);
    for(uint32_t b=0;b<UP_BANKS-1u;b++)for(uint32_t copy=0;copy<2u;copy++){
        if(shared_wipe_bank(b,0))goto failed;
        BOOT_WORK_PROGRESS(++done);
    }
    for(uint32_t copy=0;copy<2u;copy++){
        if(shared_wipe_bank(UP_BANKS-1u,0))goto failed;
        BOOT_WORK_PROGRESS(++done);
    }
    up_cache_reset();native_cache_reset();
    for(uint32_t b=0;b<UP_BANKS;b++)(void)up_cache_bank(b);
    for(uint32_t tr=0;tr<NTRK;tr++)trk[tr].user=trk[tr].user_native=0;
    recent_n=0;up_gen++;sync_reload=1;
    BOOT_WORK_END(0);
    return 0;
failed:
    /* Drop uncommitted cache edits, retain the last committed source/marker,
     * and keep writes locked until recovery is complete. */
    up_cache_reset();native_cache_reset();
    for(uint32_t b=0;b<UP_BANKS;b++)(void)up_cache_bank(b);
    up_migration_error=1;
    BOOT_WORK_END(1);
    return 2;
}
/* Each conversion owns one bank. Separate calls keep the three exclusive
 * bank snapshots out of a combined, permanently live caller stack frame. */
static __attribute__((noinline)) int shared_repair_bank(uint32_t b)
{
    up_bank_t *bank = up_cache_bank(b);              /* (in its cache: the sidecar reads leave the cache as it is) */
#if MELODEE_LEGACY_STORE
    int legacy = (up_bank_old >> b) & 1u;
#endif
    if (bank->magic != UP_BANK_MAGIC || bank->rsize != sizeof(up_rec_t) || bank->nslot != UP_PER_BANK)
        memset(bank, 0, sizeof *bank);
    else for (uint32_t j = 0; j < UP_PER_BANK; j++)
        if (bank->r[j].used && !up_valid(&bank->r[j])) memset(&bank->r[j], 0, sizeof bank->r[j]);
#if MELODEE_LEGACY_STORE
    /* A partially readable legacy bank can still contain good FM6
     * programs. Embed their sidecars before replacing its source. */
    if (legacy && b < 4u) for (uint32_t j = 0; j < 16u; j++) {
        up_rec_t *r = &bank->r[j];
        if (!up_valid(r) || r->engine != ship_store(MOD_SCH_STORE_VOICE) || up_native(r)) continue;
        up_rec_t kept = *r; int16_t values[P_COUNT]; up_values(&kept, values);
        uint8_t raw[MOD_SCH_VOICE_BYTES];
        shared_voice_sidecar(b*16u+j, raw);
        shared_native_record(r, ship_store(MOD_SCH_STORE_VOICE), raw); memcpy(r->name, kept.name, 12u); r->used |= kept.used & UP_STAR;
        r->np = sizeof UP_NATIVE_PARAMS;
        for (uint32_t i = 0; i < sizeof UP_NATIVE_PARAMS; i++) r->tone_extra[i] = (uint8_t)(values[UP_NATIVE_PARAMS[i]] + 64);
    }
#endif
    bank->magic = UP_BANK_MAGIC; bank->rsize = sizeof(up_rec_t); bank->nslot = UP_PER_BANK;
    if (up_save_bank(b)) {
        up_cached = 255; (void)up_cache_bank(b);
        return -1;
    }
    up_bank_old &= ~(1u << b); up_cache_index(b);
    return 0;
}

#if MELODEE_LEGACY_STORE
static __attribute__((noinline)) int shared_migrate_general(uint32_t b)
{
    up_bank_t *bank=up_cache_bank(b);              /* (in its cache: the sidecar reads leave the cache as it is) */
    if(!(up_migrate>>b&1u))return 0;
    for(uint32_t j=0;j<16u;j++)if(up_valid(&bank->r[j]) && bank->r[j].engine==ship_store(MOD_SCH_STORE_VOICE) && !up_native(&bank->r[j])){
        int16_t values[P_COUNT];up_values(&bank->r[j],values);
        uint8_t raw[MOD_SCH_VOICE_BYTES];shared_voice_sidecar(b*16u+j,raw);
        uint8_t star=bank->r[j].used&UP_STAR;char name[12];memcpy(name,bank->r[j].name,12u);shared_native_record(&bank->r[j],ship_store(MOD_SCH_STORE_VOICE),raw);memcpy(bank->r[j].name,name,12u);bank->r[j].used|=star;
        bank->r[j].np=sizeof UP_NATIVE_PARAMS;for(uint32_t i=0;i<sizeof UP_NATIVE_PARAMS;i++)bank->r[j].tone_extra[i]=(uint8_t)(values[UP_NATIVE_PARAMS[i]]+64);
    }
    if(up_save_bank(b))return -1;
    return 1;
}

static __attribute__((noinline)) int shared_migrate_prophet(uint32_t b)
{
    /* the old program bank read in pieces from its record (no copy of it: the boot's stack), checked as
     * program_bank_valid checks it; the writes below never touch it (shared_p5_slot: banks 0..15, 21) */
    uint32_t obj=up_obj(b),len,h[3],ok;uint8_t patch[MOD_SCH_PROGRAM_BYTES];
    int c=st_open(obj,&len);
    ok=c>=0 && len==sizeof(program_bank_t) && !st_read_range(obj,(uint32_t)c,0,h,sizeof h) && program_bank_head_ok(h[0],h[1],h[2],b-16u);
    for(uint32_t k=0;ok && k<PROGRAM_BANK_SLOTS;k++)
        if(h[1]>>k&1u)ok=!st_read_range(obj,(uint32_t)c,__builtin_offsetof(program_bank_t,patch)+k*MOD_SCH_PROGRAM_BYTES,patch,sizeof patch) && patch_stored_ok(MOD_SCH_STORE_PROGRAM,patch);
    if(ok){
        if(b==16u && (h[2]&0x80000000u)){
            if(!favorite_has(ship_store(MOD_SCH_STORE_PROGRAM),0))favorite_set(ship_store(MOD_SCH_STORE_PROGRAM),0,1);
            if(shared_preferences_save())return -1;
        }
        for(uint32_t j=18u;j<26u && (b-16u)*26u+j<128u;j++)if(h[1]>>j&1u){
            uint32_t k=shared_p5_slot((b-16u)*26u+j);up_rec_t r;
            if(st_read_range(obj,(uint32_t)c,__builtin_offsetof(program_bank_t,patch)+j*MOD_SCH_PROGRAM_BYTES,patch,sizeof patch))return -1;
            shared_native_record(&r,ship_store(MOD_SCH_STORE_PROGRAM),patch);
            if(h[2]>>j&1u)r.used|=UP_STAR;
            /* Refuse a foreign/unknown occupied destination. An interrupted
             * conversion may already have committed this exact record. */
            if(up_old(k) || (up_used(k) && memcmp(up_rec(k),&r,sizeof r)) || (!up_used(k) && up_put(k,&r)==2)){
                return -1;
            }
        }
    }
    (void)up_cache_bank(b);
    if(up_migrate>>b&1u){
        if(up_save_bank(b)){return -1;}
        return 1;
    }
    return 0;
}
#endif

static void shared_boot(void)
{
    if (boot_work.state == 2u && !boot_work.presets) return; /* preserve a settings-repair error; preset errors retry */
    if(!flash_ok)return;
    if(shared_wipe_pending()){(void)shared_wipe();return;}
    /* Startup replaces unreadable preset banks/entries through A/B commits.
     * Valid legacy banks still take the conversion path below. */
    uint32_t repair = 0, repair_total = 0, repaired = 0;
    for (uint32_t b = 0; b < UP_BANKS; b++) {
        up_bank_t *bank = up_cache_bank(b);
        int damaged = (up_bank_old >> b) & 1u;
        for (uint32_t j = 0; j < UP_PER_BANK; j++)
            damaged |= bank->r[j].used && !up_valid(&bank->r[j]);
        if (damaged) { repair |= 1u << b; repair_total++; }
    }
    if (repair) {
        BOOT_PRESET_BEGIN("RESETTING PRESETS", repair_total);
        up_converting = 1;
        for (uint32_t b = 0; b < UP_BANKS; b++) {
            if (!(repair >> b & 1u)) continue;
            if(shared_repair_bank(b)){
                up_converting=0;up_migration_error=1;BOOT_WORK_END(1);return;
            }
            BOOT_WORK_PROGRESS(++repaired);
        }
        up_converting = 0; up_migration_error = 0; BOOT_WORK_END(0);
    }
#if MELODEE_LEGACY_STORE
    uint32_t total=0,done_banks=0;
    /* Inspect first, so clean boots do not show a migration screen or write. */
    for(uint32_t b=0;b<UP_BANKS;b++)(void)up_cache_bank(b);
    for(uint32_t b=0;b<UP_BANKS;b++)total+=up_migrate>>b&1u;
    if(!total)return;
    BOOT_PRESET_BEGIN("MIGRATING PRESETS",total);
    up_converting=1;
    /* FM sidecars must be embedded before their source objects are converted. */
    for(uint32_t b=0;b<4u;b++){
        int rc=shared_migrate_general(b);
        if(rc<0){up_migration_error=1;goto done;}
        if(rc)BOOT_WORK_PROGRESS(++done_banks);
    }
    for(uint32_t b=4u;b<16u;b++){
        (void)up_cache_bank(b);
        if(up_migrate>>b&1u){
            if(up_save_bank(b)){up_migration_error=1;goto done;}
            BOOT_WORK_PROGRESS(++done_banks);
        }
    }
    for(uint32_t b=16u;b<21u;b++){
        int rc=shared_migrate_prophet(b);
        if(rc<0){up_migration_error=1;goto done;}
        if(rc)BOOT_WORK_PROGRESS(++done_banks);
    }
    for(uint32_t b=0;b<UP_BANKS;b++)(void)up_cache_bank(b);
done:
    up_converting=0;
    BOOT_WORK_END(up_migration_error);
#endif
}
#else
static int shared_wipe(void)
{
    BOOT_WIPE_BEGIN("WIPING PRESETS",UP_BANKS);
    up_cache_reset();
    for(uint32_t tr=0;tr<NTRK;tr++)trk[tr].user=trk[tr].user_native=0;
    recent_n=0;up_gen++;sync_reload=1;
    BOOT_WORK_PROGRESS(UP_BANKS);
    BOOT_WORK_END(0);
    return 0;
}
static void shared_boot(void){}
#endif
