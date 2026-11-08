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
    up_cache_reset();
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
        memcpy(disk.r[i].extra,original.r[i].tone_extra,46u);
        if((disk.r[i].head[0]&~UP_STAR)==UP_USED)
            disk.r[i].head[0]=(uint8_t)(UP_USED|(i&1u?UP_STAR:0));
    }
    {   /* (2.0: a used record's engine byte on flash is its synth's place in the bank's IDs + 1, 0 for none) */
        uint8_t place[NENGINES]={0},n=0;
        for(uint32_t i=0;i<UP_PER_BANK;i++){
            uint8_t *h=disk.r[i].head;
            if(!h[0])continue;
            if(!eng_map_own(h[2])){h[2]=0;continue;}
            if(!place[h[2]])place[h[2]]=++n;
            h[2]=place[h[2]];
        }
    }
    success=original;
    for(uint32_t i=0;i<UP_PER_BANK;i++)success.r[i].used=disk.r[i].head[0];
    static uint8_t flash_before[sizeof nor];memcpy(flash_before,nor,sizeof nor);
    int safe=1;
    for(int cut=-3;cut<=17;cut++){              /* a list record: its staged address map, 15 payload pages, its commit record */
        memcpy(nor,flash_before,sizeof nor);*cache=original;
        up_fav_dirty=1u;
        erase_error=cut==-3;inject_readback=cut==-2;readback_error=0;
        fail_after=cut>=0?cut:-1;
        int rc=up_save_bank(0);readback_error=inject_readback=erase_error=0;fail_after=-1;
        int expected_failure=cut==-3 || cut==-2 || (cut>=0 && cut<17);
        safe &= !!rc==expected_failure;
        safe &= !memcmp(cache,rc?&original:&success,sizeof original);
        safe &= up_fav_dirty==!!rc;
        if(!rc){
            up_disk_bank_t actual;
            safe &= st_load(up_obj(0),&actual,sizeof actual)==sizeof actual && !memcmp(&actual,&disk,sizeof disk);
        }
    }
    bad+=check("in-place save matches original bytes; all page/erase/readback cuts restore cache",safe);
    return bad;
}
/* 2.0: a bank names its records' synths by ID after them, ten at most (upreset.c UP_IDS_MAX) */
static int bank_many_synths(void)
{
    int bad=0,ok=1;reset();up_boot();
    for(uint32_t i=0;i<10u;i++){set_engine_of(TSEL,ENGI_PROPHET+i);if(up_store(i,"MANY"))return check("a bank holds the presets of ten synths",0);}
    up_cache_reset();up_boot();
    for(uint32_t i=0;i<10u;i++)ok&=up_used(i)&&up_engine(i)==ENGI_PROPHET+i;
    bad+=check("a bank holds the presets of ten synths, by ID",ok);
    up_rec_t r=*up_rec(0);r.engine=(uint8_t)eng_map_take(0x1122334455667788ULL,"ELEVEN");
    bad+=check("an eleventh synth's preset is refused, the bank as it was",up_put(10,&r)==2&&!up_used(10)&&up_engine(0)==ENGI_PROPHET);
    up_cache_reset();up_boot();
    bad+=check(".. and after a reboot",!up_used(10)&&up_used(9)&&up_engine(9)==ENGI_PROPHET+9u);
    return bad;
}
int main(void)
{
    int bad=compact_save_parity()+bank_many_synths()+fill(ENGI_CZ)+fill(ENGI_PROPHET)+fill(ENGI_FM6);
    reset();up_boot();p5_patch_t p;p5_patch_init(&p);uint8_t cz[CZ_BYTES];cz_patch_init(cz);
    bad+=check("Prophet occupies a shared slot",!native_put(ENGI_PROPHET,383,(uint8_t *)&p));
    bad+=check("CZ replaces Prophet in the identical slot",!native_put(ENGI_CZ,383,cz)&&native_used(ENGI_CZ,383)&&!native_used(ENGI_PROPHET,383));
    native_load(ENGI_CZ,383,0);bad+=check("highest slot retains its track identity",trk[0].user==384 && user_of(&trk[0])==383);
    fail_after=2;bad+=check("torn replacement reports failure",native_put(ENGI_PROPHET,383,(uint8_t *)&p)==2);fail_after=-1;up_boot();bad+=check("torn replacement keeps previous engine and bytes",native_used(ENGI_CZ,383)&&!memcmp(native_raw(ENGI_CZ,383),cz,sizeof cz));
    bad+=check("favorite can be set at slot 384",favorite_set(USER_GENERAL,383,1));up_boot();bad+=check("favorite survives reboot",favorite_has(USER_GENERAL,383));
    bad+=check("favorite can be cleared",favorite_set(USER_GENERAL,383,0));up_boot();bad+=check("favorite clear survives reboot",!favorite_has(USER_GENERAL,383));
    reset();up_boot();set_engine_of(TSEL,ENGI_808);drum_patch_of(0)->c[15][0]=-24;drum_patch_of(0)->c[15][3]=127;
    bad+=check("custom drum sound occupies any shared slot",!up_store(383,"DRUM MAX"));memset(drum_patch_of(0),0,sizeof(drum_patch_t));up_boot();up_load(383);
    bad+=check("drum offsets and attenuation survive compact storage",drum_patch_of(0)->c[15][0]==-24&&drum_patch_of(0)->c[15][3]==127);
    bad+=check("recent entries retain the ninth slot bit",(recent_push(USER_GENERAL,383),recent[0]==((uint32_t)USER_GENERAL<<16|383)));
    song.playing=1;uint32_t before=erases;bad+=check("favorite during playback is deferred without erasing",favorite_set(USER_GENERAL,383,1)&&favorite_has(USER_GENERAL,383)&&erases==before);
    song.playing=0;settings_poll();up_boot();bad+=check("deferred favorite commits when playback stops",favorite_has(USER_GENERAL,383));
    reset();up_boot();set_engine_of(TSEL,ENGI_CZ);up_store(383,"PROJECT SLOT");up_load(383);static project_t project;static project_store_t wire;
    project_capture(&project);proj_pack(&wire,&project);proj_import_same(&project,&wire,sizeof wire);native_put(ENGI_CZ,1,cz);TSEL->user=0;project_restore_runtime(&project);
    bad+=check("projects preserve U384's identity independently of embedded tone",TSEL->user==384&&!TSEL->user_native);
    reset();uint8_t unknown[3816];memset(unknown,0,sizeof unknown);up_rec_t future;memset(&future,0,sizeof future);future.used=UP_USED;future.ver=99;memcpy(future.name,"FUTURE",6);memset(future.note,0x6a,sizeof future.note);memset(future.flags,0x59,sizeof future.flags);
    uint32_t magic=UP_BANK_MAGIC;uint16_t record_size=238,records=16;memcpy(unknown,&magic,4);memcpy(unknown+4,&record_size,2);memcpy(unknown+6,&records,2);memcpy(unknown+8,&future,sizeof future);st_save(OBJ_UPRESET0,unknown,sizeof unknown);up_cache_reset();
    uint8_t original[3816];bad+=check("a bank 2.0 cannot read (1.x's, damaged) keeps all its bytes",up_old_bank(0)&&st_load(OBJ_UPRESET0,original,sizeof original)==sizeof original&&!memcmp(original,unknown,sizeof original));
    set_engine_of(TSEL,ENGI_CZ);preset_old_allow(1);fail_after=1;int failed=up_store(0,"REPLACE");fail_after=-1;preset_old_allow(0);
    bad+=check("failed confirmed whole-bank replacement retains opaque source",failed==2&&up_old_bank(0)&&st_load(OBJ_UPRESET0,original,sizeof original)==sizeof original&&!memcmp(original,unknown,sizeof original));
    puts(bad?"UNIFIED PRESET TEST FAILED":"unified preset test passed");return !!bad;
}
