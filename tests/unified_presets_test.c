/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Firmware save flows against NOR flash: deferred settings, retries, and failed-save rollback. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static uint32_t irq_start_race, irq_races;
static void fm1_irq_off(void)
{
    if (irq_start_race) {                         /* audio serviced PLAY immediately before main masks it */
        irq_start_race = 0;
        events_block(CTL);
        irq_races++;
    }
}
static void fm1_irq_on(void) {}
static uint32_t lcd_flushes;
static void lcd_sync(void) { lcd_flushes++; }
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#define MELODEE_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;
static int erase_error, fail_after = -1, inject_readback, readback_error;
static uint32_t erases, preset_reads, maintenance_erases, hidden_maintenance;
static uint8_t check_maintenance;
static int st_read(uint32_t off, void *dst, uint32_t n) { preset_reads++; if(readback_error)return -7; memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    erases++;
    if (check_maintenance) {
        maintenance_erases++;
        if (boot_work.state != 1u || !lcd_flushes) hidden_maintenance++;
    }
    if (erase_error) return -8;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0) return -9;
    if (fail_after > 0) fail_after--;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    if(inject_readback && !(off%4096u))readback_error=1;
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* (persist_boot: the expected part) */
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

static int check(const char *what, int ok)
{
    printf("persist: %-66s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void reset(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(&song, 0, sizeof song);
    memset(trk, 0, sizeof trk);trk_voices_bind();
    memset(&chain, 0, sizeof chain);
    chain_defaults(&chain_config);
    memset(proj_slot, 0, sizeof proj_slot);
    up_cache_reset(); native_cache_reset();
    memset(&persist_saved, 0, sizeof persist_saved);
    memset(&settings, 0, sizeof settings);
    memset(&ui, 0, sizeof ui);
    memset(&boot_work, 0, sizeof boot_work);
    lcd_flushes = maintenance_erases = hidden_maintenance = check_maintenance = 0;
    memset(&favorites, 0, sizeof favorites);
    panel = PANEL_DEFAULT;
    host_tracks_init();
    fm1_ms = 0;
    transport_req = 0;
    persist_pending = 0;
    persist_old = tmpl_old = preset_old_write = 0;
    persist_retry_ms = 0;
    flash_ok = 1;
    erase_error = inject_readback = readback_error = 0;
    fail_after = -1;
    up_gen = erases = 0;
    irq_start_race = irq_races = 0;
}


static int fill(uint32_t engine)
{
    int bad=0;reset();up_boot();uint8_t raw[CZ_BYTES];
    if(engine==ENGI_CZ)cz_patch_init(raw);
    else if(engine==ENGI_FM6)memcpy(raw,FM6_INIT,FM6_PACKED);
    else {p5_patch_t p;p5_patch_init(&p);memcpy(raw,&p,sizeof p);}
    for(uint32_t k=0;k<UP_SLOTS;k++)if(native_put(engine,k,raw)!=0)return check("all 384 shared slots accept one engine",0);
    up_boot();
    for(uint32_t k=0;k<UP_SLOTS;k++)if(!native_used(engine,k) || memcmp(native_raw(engine,k),raw,native_size(engine)))return check("all 384 slots survive reboot",0);
    bad+=check("all 384 slots accept one engine and survive reboot",1);
    int tails=1;
    for(uint32_t b=0;b<UP_BANKS;b++)for(uint32_t copy=0;copy<2u;copy++){
        uint32_t base=st_sector(up_obj(b),copy);for(uint32_t off=3840u;off<ST_SECTOR;off++)tails &= nor[base+off]==255u;
    }
    bad+=check("every shared preset copy leaves the loader's sector tail erased",tails);
    bad+=check("slot 384 is rejected without accessing flash",native_put(engine,UP_SLOTS,raw)==1);
    return bad;
}
static int legacy_conversion(void)
{
    int bad=0;reset();uint8_t fm[FM6_PACKED];memcpy(fm,FM6_INIT,sizeof fm);fm[118]='F';
    for(uint32_t b=0;b<4u;b++){
        native_fm_t f;memset(&f,0,sizeof f);f.magic=NATIVE_FM_MAGIC;f.ver=1;f.slots=16;f.used=0xffff;
        for(uint32_t j=0;j<16u;j++)memcpy(f.tone[j],fm,sizeof fm);
        bad+=check("legacy FM bank fixture commits",!st_save(native_fm_obj(b),&f,sizeof f));
    }
    for(uint32_t b=0;b<8u;b++){
        cz_bank_t c;cz_bank_empty(&c,b);c.used=0xffff;c.ver=2;
        for(uint32_t j=0;j<16u;j++){cz_patch_init(c.tone[j].raw);c.tone[j].raw[128]='Z';}
        bad+=check("legacy CZ bank fixture commits",!st_save(OBJ_CZBANK0+b,&c,sizeof c));
    }
    for(uint32_t b=0;b<5u;b++){
        p5_bank_t p;p5_bank_defaults(&p,b);p.used=p5_bank_mask(b);p.favorites=p.used|(b?0u:0x80000000u);
        for(uint32_t j=0;j<26u;j++){p5_patch_init(&p.patch[j]);p.patch[j].raw[65]='P';p.patch[j].raw[66]=(uint8_t)('A'+b);p.patch[j].raw[67]=(uint8_t)('A'+j);}
        bad+=check("legacy Prophet bank fixture commits",!st_save(OBJ_P5BANK0+b,&p,sizeof p));
    }
    for(uint32_t b=0;b<4u;b++){
        uint8_t bytes[3816];memset(bytes,0,sizeof bytes);up_bank_t *bank=(up_bank_t *)bytes;bank->magic=UP_BANK_MAGIC;bank->rsize=238;bank->nslot=16;
        for(uint32_t j=0;j<16u;j++){up_rec_t r;memset(&r,0,sizeof r);r.used=UP_USED;r.ver=UP_VER;r.engine=j?ENGI_DRUM:ENGI_FM6;r.np=P_COUNT;memcpy(r.name,"OLD GENERAL",11);for(uint32_t i=0;i<P_COUNT;i++)up_set_value(&r,i,param_desc_of(r.engine,i)->def);memcpy(bytes+8u+j*sizeof r,&r,sizeof r);}
        bad+=check("legacy general bank fixture commits",!(b<2u?st_pair_save:st_save)(b<2u?OBJ_UPRESET0+b:OBJ_UPRESET_EXT0+b-2u,b<2u?bytes:bytes+8u,b<2u?3816u:3808u));
    }
    favorites.user=1;favorites_user_hi=0;favorites.factory[12][3]|=1;favorites.factory[15][8]|=2;
    check_maintenance=1;
    fail_after=0;up_boot();bad+=check("interrupted conversion blocks user writes",up_migration_error && up_store(383,"NO WRITE")==2);fail_after=-1;
    bad+=check("conversion failure remains visible without reporting completion",boot_work.state==2u && boot_work.done==0u && boot_work.total==21u);
    up_boot();bad+=check("conversion retries successfully",!up_migration_error);
    bad+=check("every migration erase follows a flushed keep-power screen",maintenance_erases && !hidden_maintenance);
    bad+=check("successful migration completes all progress steps",!boot_work.state && boot_work.done==boot_work.total && boot_work.total==21u);
    check_maintenance=0;
    bad+=check("legacy Prophet INIT star is preserved",favorite_has(ENGI_PROPHET,0));
    int ok=1;
    for(uint32_t k=0;k<128u;k++){uint32_t dst=shared_p5_slot(k);p5_patch_t p;memcpy(&p,native_raw(ENGI_PROPHET,dst),sizeof p);ok &= native_used(ENGI_PROPHET,dst)&&p.raw[66]=='A'+k/26u&&p.raw[67]=='A'+k%26u&&up_favorite(dst);}
    bad+=check("all 128 Prophet tones and stars survive spill conversion",ok);
    bad+=check("all 384 preceding slots are retained",up_count()==384u);
    bad+=check("legacy general FM voice is embedded before source conversion",native_used(ENGI_FM6,0)&&!memcmp(native_raw(ENGI_FM6,0),fm,sizeof fm));
    bad+=check("general, FM and CZ favorites follow remapped slots",up_favorite(0)&&up_favorite(72)&&up_favorite(144));
    uint32_t before=erases,flushes=lcd_flushes;up_boot();bad+=check("subsequent boots perform no conversion writes",before==erases && up_count()==384u);
    bad+=check("converted banks skip the maintenance screen on later boots",flushes==lcd_flushes);
    return bad;
}
static int wipe_recovery(void)
{
    int bad=0;reset();
    p5_bank_t old;p5_bank_defaults(&old,0);old.used=p5_bank_mask(0);
    for(uint32_t j=0;j<26u;j++)p5_patch_init(&old.patch[j]);
    st_save(OBJ_P5BANK0,&old,sizeof old);
    uint8_t keep[16]="KEEP THIS DATA";
    st_save(OBJ_SETTINGS,keep,sizeof keep);st_save(OBJ_BANK0,keep,sizeof keep);
    static uint8_t before[sizeof nor];memcpy(before,nor,sizeof nor);
    fail_after=0;up_boot();
    bad+=check("fixture fails migration before recovery",up_migration_error && boot_work.state==2u);
    bad+=check("failed wipe intent cannot erase a source bank",shared_wipe()==2 && !shared_wipe_pending() && st_load(OBJ_P5BANK0,&old,sizeof old)==sizeof old);
    fail_after=18;check_maintenance=1;
    bad+=check("interrupted wipe leaves durable reset intent and blocks writes",shared_wipe()==2 && shared_wipe_pending() && up_migration_error && up_store(383,"BLOCKED")==2);
    fail_after=0;up_boot();
    bad+=check("failed reboot resumes wiping rather than migration",boot_work.state==2u && !strcmp(boot_work.title,"WIPING PRESETS") && shared_wipe_pending());
    fail_after=-1;up_boot();
    bad+=check("reboot completes the confirmed wipe and removes reset intent",!up_count() && !up_migration_error && !shared_wipe_pending() && !boot_work.state && boot_work.done==45u);
    bad+=check("every wipe erase follows a visible keep-power screen",maintenance_erases && !hidden_maintenance);
    check_maintenance=0;
    int copies=1,unrelated=1;up_disk_bank_t disk;
    for(uint32_t b=0;b<UP_BANKS;b++){
        /* the pool: the current copy of every bank is empty, and so is any older copy still readable (none resurrects) */
        st_hdr_t cur;copies &= st_current(up_obj(b),&cur)>=0;
        for(uint32_t i=0;i<st_cands(up_obj(b));i++){
            st_hdr_t h;uint32_t copy=st_cand(up_obj(b),i);
            if(st_head(up_obj(b),copy,&h)||st_body(up_obj(b),copy,&h))continue;
            uint32_t base=st_sector(up_obj(b),copy);memcpy(before+base,nor+base,ST_SECTOR);
            if(st_read_range(up_obj(b),copy,0,&disk,sizeof disk)){copies=0;continue;}
            for(uint32_t j=0;j<UP_PER_BANK;j++)for(uint32_t k=0;k<sizeof(up_disk_rec_t);k++)copies &= !((uint8_t *)&disk.r[j])[k];
            if(h.seq==cur.seq)for(uint32_t k=3840u;k<ST_SECTOR;k++)copies &= nor[base+k]==255u;
        }
    }
    unrelated=!memcmp(before,nor,sizeof nor);
    bad+=check("every readable copy of every shared bank is empty; loader tails erased",copies);
    bad+=check("wipe leaves projects, settings and every other flash region untouched",unrelated);
    uint32_t writes=erases,flushes=lcd_flushes;up_boot();
    bad+=check("later boots neither migrate nor wipe again",erases==writes && lcd_flushes==flushes && !up_count());
    set_engine_of(TSEL,ENGI_CZ);
    bad+=check("all shared slots become writable after a successful wipe",!up_store(383,"NEW START"));
    up_boot();bad+=check("new sounds survive reboot without reset or migration",up_used(383) && !up_migration_error && !shared_wipe_pending());
    /* Interrupt a bank's second copy and both stages of marker removal. */
    const int cuts[]={32,688,704,719};int safe=1;
    for(uint32_t i=0;i<sizeof cuts/sizeof cuts[0];i++){
        reset();p5_bank_defaults(&old,0);old.used=p5_bank_mask(0);
        for(uint32_t j=0;j<26u;j++)p5_patch_init(&old.patch[j]);
        st_save(OBJ_P5BANK0,&old,sizeof old);fail_after=0;up_boot();
        fail_after=cuts[i];safe &= shared_wipe()==2;
        fail_after=-1;memset(&boot_work,0,sizeof boot_work);up_boot();
        safe &= !up_count() && !up_migration_error && !shared_wipe_pending();
        uint32_t erases_before=erases;up_boot();safe &= erases==erases_before;
        set_engine_of(TSEL,ENGI_CZ);safe &= !up_store(383,"AFTER CUT");
    }
    bad+=check("power cuts during copy replacement/marker removal never resurrect presets",safe);
    return bad;
}
/* Independent original serializer, including every opaque legacy phrase byte
 * in the cache. Every program page/commit, erase and readback error restores
 * the complete decoded cache and keeps deferred dirty/migration ownership. */
static int compact_save_parity(void)
{
    int bad=0;reset();up_boot();up_bank_t *cache=up_cache_bank(0);
    up_bank_t original,success;
    uint8_t *bytes=(uint8_t *)&original;
    for(uint32_t i=0;i<sizeof original;i++)bytes[i]=(uint8_t)(i*73u+i/238u);
    original.magic=UP_BANK_MAGIC;original.rsize=sizeof(up_rec_t);original.nslot=UP_PER_BANK;
    up_disk_bank_t disk;disk.magic=UP_DISK_MAGIC;disk.rsize=206u;disk.nslot=18u;
    for(uint32_t i=0;i<UP_PER_BANK;i++){
        original.r[i].used=(uint8_t)(i%3u?UP_USED:0x33u);
        up_meta[i].favorite=(uint8_t)(i&1u);
        memcpy(disk.r[i].head,&original.r[i],160u);
        memcpy(disk.r[i].extra,original.r[i].cz_extra,46u);
        if((disk.r[i].head[0]&~UP_STAR)==UP_USED)
            disk.r[i].head[0]=(uint8_t)(UP_USED|(i&1u?UP_STAR:0));
    }
    success=original;
    for(uint32_t i=0;i<UP_PER_BANK;i++)success.r[i].used=disk.r[i].head[0];
    static uint8_t flash_before[sizeof nor];memcpy(flash_before,nor,sizeof nor);
    int safe=1;
    for(int cut=-3;cut<=16;cut++){
        memcpy(nor,flash_before,sizeof nor);*cache=original;
        up_fav_dirty=up_migrate=1u;
        erase_error=cut==-3;inject_readback=cut==-2;readback_error=0;
        fail_after=cut>=0?cut:-1;
        int rc=up_save_bank(0);readback_error=inject_readback=erase_error=0;fail_after=-1;
        int expected_failure=cut==-3 || cut==-2 || (cut>=0 && cut<16);
        safe &= !!rc==expected_failure;
        safe &= !memcmp(cache,rc?&original:&success,sizeof original);
        safe &= (up_fav_dirty==!!rc && up_migrate==!!rc);
        if(!rc){
            up_disk_bank_t actual;
            safe &= st_load(up_obj(0),&actual,sizeof actual)==sizeof actual && !memcmp(&actual,&disk,sizeof disk);
        }
    }
    bad+=check("in-place save matches original bytes; all page/erase/readback cuts restore cache",safe);
    return bad;
}
int main(void)
{
    int bad=compact_save_parity()+fill(ENGI_CZ)+fill(ENGI_PROPHET)+fill(ENGI_FM6)+legacy_conversion()+wipe_recovery();
    reset();up_boot();p5_patch_t p;p5_patch_init(&p);uint8_t cz[CZ_BYTES];cz_patch_init(cz);
    bad+=check("Prophet occupies a shared slot",!native_put(ENGI_PROPHET,383,(uint8_t *)&p));
    bad+=check("CZ replaces Prophet in the identical slot",!native_put(ENGI_CZ,383,cz)&&native_used(ENGI_CZ,383)&&!native_used(ENGI_PROPHET,383));
    native_load(ENGI_CZ,383,0);bad+=check("highest slot retains its track identity",trk[0].user==384 && user_of(&trk[0])==383);
    fail_after=2;bad+=check("torn replacement reports failure",native_put(ENGI_PROPHET,383,(uint8_t *)&p)==2);fail_after=-1;up_boot();bad+=check("torn replacement keeps previous engine and bytes",native_used(ENGI_CZ,383)&&!memcmp(native_raw(ENGI_CZ,383),cz,sizeof cz));
    bad+=check("favorite can be set at slot 384",favorite_set(USER_GENERAL,383,1));up_boot();bad+=check("favorite survives reboot",favorite_has(USER_GENERAL,383));
    bad+=check("favorite can be cleared",favorite_set(USER_GENERAL,383,0));up_boot();bad+=check("favorite clear survives reboot",!favorite_has(USER_GENERAL,383));
    reset();up_boot();set_engine_of(TSEL,ENGI_DRUM);drum_patch_of(0)->c[15][0]=-24;drum_patch_of(0)->c[15][3]=127;
    bad+=check("custom drum sound occupies any shared slot",!up_store(383,"DRUM MAX"));memset(drum_patch_of(0),0,sizeof(drum_patch_t));up_boot();up_load(383);
    bad+=check("drum offsets and attenuation survive compact storage",drum_patch_of(0)->c[15][0]==-24&&drum_patch_of(0)->c[15][3]==127);
    bad+=check("recent entries retain the ninth slot bit",(recent_push(USER_GENERAL,383),recent[0]==((uint32_t)USER_GENERAL<<16|383)));
    song.playing=1;uint32_t before=erases;bad+=check("favorite during playback is deferred without erasing",favorite_set(USER_GENERAL,383,1)&&favorite_has(USER_GENERAL,383)&&erases==before);
    song.playing=0;settings_poll();up_boot();bad+=check("deferred favorite commits when playback stops",favorite_has(USER_GENERAL,383));
    reset();up_boot();set_engine_of(TSEL,ENGI_CZ);up_store(383,"PROJECT SLOT");up_load(383);static project_t project;static project_store_t wire;
    project_capture(&project);proj_pack(&wire,&project);proj_import(&project,&wire,sizeof wire);native_put(ENGI_CZ,1,cz);TSEL->user=0;project_restore_runtime(&project);
    bad+=check("projects preserve U384's identity independently of embedded tone",TSEL->user==384&&!TSEL->user_native);
    reset();p5_bank_t old_bank;p5_bank_defaults(&old_bank,0);old_bank.used=0x3ffffffu;
    for(uint32_t j=0;j<26u;j++)p5_patch_init(&old_bank.patch[j]);st_save(OBJ_P5BANK0,&old_bank,sizeof old_bank);
    bad+=check("unfinished archive conversion blocks writes to its source bank",native_put(ENGI_PROPHET,288,(uint8_t *)&old_bank.patch[0])==2);
    up_boot();bad+=check("finalizing old archive preserves all 26 source programs",!up_migration_error&&up_count()==26u);
    reset();uint8_t unknown[3816];memset(unknown,0,sizeof unknown);up_rec_t future;memset(&future,0,sizeof future);future.used=UP_USED;future.ver=99;memcpy(future.name,"FUTURE",6);memset(future.note,0x6a,sizeof future.note);memset(future.flags,0x59,sizeof future.flags);
    uint32_t magic=UP_BANK_MAGIC;uint16_t record_size=238,records=16;memcpy(unknown,&magic,4);memcpy(unknown+4,&record_size,2);memcpy(unknown+6,&records,2);memcpy(unknown+8,&future,sizeof future);st_pair_save(OBJ_UPRESET0,unknown,sizeof unknown);up_cache_reset();
    uint8_t original[3816];bad+=check("unknown legacy records retain all phrase/opaque bytes",up_old_bank(0)&&st_load(OBJ_UPRESET0,original,sizeof original)==sizeof original&&!memcmp(original,unknown,sizeof original));
    set_engine_of(TSEL,ENGI_CZ);preset_old_allow(1);fail_after=1;int failed=up_store(0,"REPLACE");fail_after=-1;preset_old_allow(0);
    bad+=check("failed confirmed whole-bank replacement retains opaque source",failed==2&&up_old_bank(0)&&st_load(OBJ_UPRESET0,original,sizeof original)==sizeof original&&!memcmp(original,unknown,sizeof original));
    puts(bad?"UNIFIED PRESET TEST FAILED":"unified preset test passed");return !!bad;
}
