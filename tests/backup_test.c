/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The editor's full backup (editor_backup.c: LIST / GET / PUT) against simulated NOR flash:
 * CRC before publication, stale runtime copies, USB resets and timeouts, malformed objects, older
 * project formats, settings values and user preset banks kept byte for byte. */
static unsigned char host_samples[3][0x14000];
#define SMP_USER_XIP(k) host_samples[k]
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#define MELODEE_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;
static int erase_error;
static uint32_t erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { if (erase_error) return -8; memset(nor + off, 0xFF, 4096); erases++; return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n) { memcpy(nor + off, src, n); return 0; }
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

#define ED_BK_FLASH_PTR(off) (nor + (off))
enum { ED_BACKUP_LIST = 65, ED_BACKUP_GET, ED_BACKUP_PUT };
static uint8_t rep[4096];
static uint32_t rep_n;
static void ed_b(uint32_t v) { if (rep_n < sizeof rep) rep[rep_n++] = (uint8_t)(v & 127u); }
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        uint32_t k = na > 7u ? 7u : na;
        if (!k || n + k > max || (m >> k))
            return 0;
        for (j = 0; j < k; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return na ? 0u : n;
}
#define ed_transfer_buf (main_scratch.transfer)
static void fm1_wdt_feed(void) {}
static int ed_flash_stop(void) { return transport_busy(); }
#include "../firmware/src/editor_backup.c"

static int check(const char *what, int ok)
{
    printf("backup: %-72s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}

static void reset(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(&song, 0, sizeof song);
    memset(trk, 0, sizeof trk);trk_voices_bind();
    memset(&chain, 0, sizeof chain);
    chain_defaults(&chain_config);
    pattern_init();
    memset(proj_slot, 0, sizeof proj_slot);
    up_cache_reset(); native_cache_reset();
    memset(&persist_saved, 0, sizeof persist_saved);
    memset(&settings, 0, sizeof settings);
    memset(&ui, 0, sizeof ui);
    panel = PANEL_DEFAULT;
    settings_init();
    host_tracks_init();
    fm1_ms = 0;
    transport_req = 0;
    usb.resets = 0;
    erase_error = 0;
    erases = 0;
    ed_bk_valid = ed_bk_put = 0;
    persist_old = tmpl_old = preset_old_write = 0;
}

static uint32_t call(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    rep_n = 0;
    ed_backup_handle(cmd, a, n);
    return rep_n;
}
static void put32(uint8_t *a, uint32_t v) { for (uint32_t i = 0; i < 5u; i++) a[i] = (uint8_t)((v >> (7u * i)) & 127u); }

/* PUT begin: rc */
static uint32_t put_begin(uint32_t id, uint32_t len, uint32_t crc)
{
    uint8_t a[12] = {0, (uint8_t)id};
    put32(a + 2, len);
    put32(a + 7, crc);
    call(ED_BACKUP_PUT, a, sizeof a);
    return rep[2];
}
/* PUT one chunk of data at off: rc */
static uint32_t put_chunk(uint32_t id, uint32_t off, const uint8_t *p, uint32_t n)
{
    uint8_t a[16 + 300];
    uint32_t k = 7;
    a[0] = 1;
    a[1] = (uint8_t)id;
    put32(a + 2, off);
    while (n) {
        uint32_t g = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < g; i++) m |= (uint32_t)(p[i] >> 7) << i;
        a[k++] = (uint8_t)m;
        for (i = 0; i < g; i++) a[k++] = p[i] & 127u;
        p += g;
        n -= g;
    }
    call(ED_BACKUP_PUT, a, k);
    return rep[2];
}
static uint32_t put_end(uint32_t id, uint32_t op)   /* op 2 commit, 3 abort */
{
    uint8_t a[2] = {(uint8_t)op, (uint8_t)id};
    call(ED_BACKUP_PUT, a, 2);
    return rep[2];
}
/* a whole object: begin, 256-byte chunks, commit; the first failing rc or the commit's */
static uint32_t put_all(uint32_t id, const void *v, uint32_t len, uint32_t crc)
{
    const uint8_t *p = v;
    uint32_t off, rc = put_begin(id, len, crc);
    for (off = 0; !rc && off < len; off += 256u)
        rc = put_chunk(id, off, p + off, len - off > 256u ? 256u : len - off);
    return rc ? rc : put_end(id, 2);
}
/* LIST: rc; the length and CRC of object id */
static uint32_t list(uint32_t id, uint32_t *len, uint32_t *crc)
{
    uint32_t i;
    call(ED_BACKUP_LIST, 0, 0);
    if (rep[1])
        return rep[1];
    for (i = 0; i < rep[2]; i++) {
        const uint8_t *o = rep + 3 + i * 11u;
        if (o[0] == id) {
            *len = ed_bk_r32(o + 1);
            *crc = ed_bk_r32(o + 6);
        }
    }
    return 0;
}
static uint32_t get(uint32_t id, uint32_t off, uint32_t count)
{
    uint8_t a[8] = {(uint8_t)id};
    put32(a + 1, off);
    a[6] = (uint8_t)(count & 127u);
    a[7] = (uint8_t)(count >> 7);
    call(ED_BACKUP_GET, a, sizeof a);
    return rep[1];
}

/* Runtime archive data now lives in an unpublished flash copy, independent
 * of the drawing canvas. Read through the same bounded source as GET: the compact image, its length. */
static uint32_t runtime_bytes(uint8_t *out)
{return st_read_range(ed_bk_stage.obj,ed_bk_stage.head.slot,0,out,ed_bk_stage.head.len)?0u:ed_bk_stage.head.len;}
/* a saved project's current record as the loader reads it (a compact image expanded) */
static int flash_image(uint32_t obj,uint8_t *dst)
{
    st_hdr_t h;int copy=st_current(obj,&h);
    if(copy<0)return -1;
    pj.on=!st_read_range(obj,(uint32_t)copy,0,&pj.h,PJ_HEAD)&&pj.h.magic==PJ_MAGIC;
    return pj_read(obj,(uint32_t)copy,0,dst,BANK_STORE_SIZE)?-1:(int)BANK_STORE_SIZE;
}

static int full_pattern_archive(void)
{
    int bad=0, ok=1; reset(); static uint8_t archive[ED_BK_MAX], saved[ED_BK_MAX];
    for (uint32_t k=0; k<NPART; k++) for (uint32_t b=0; b<NPAT; b++) {
        track_t *t=&trk[k]; pattern_request(t,b); t->p[P_SLEN]=(int16_t)(16+b);
        t->step[3]=(step_t){{(uint8_t)(40+k*8+b),60,64,67},4,ST_NOTE,SF_ACCENT,100,0x81,0x80,(uint8_t)(20+b)};
        t->step[4]=(step_t){{0},0,ST_TIE}; motion_set_event(t,3,P_PAN,(int16_t)(k*8+b));
    }
    chain_config.count=1; chain_config.row[0]=(chain_row_t){0,2};
    for (uint32_t k=0; k<NPART; k++) chain_patterns[0][k]=(uint8_t)(k+1);
    project_save_as(0,"CHORD BANKS");
    uint32_t len,crc; list(0,&len,&crc); uint32_t alen=runtime_bytes(archive);
    bad+=check("full bank archive contains all runtime banks with its verified CRC",alen && len==alen && crc==st_crc32(archive,alen));
    for (uint32_t off=0; off<alen; off+=256u) {
        cv_begin(240,124,RGB(off&255u,20,30));cv_blit(0,24);
        uint32_t n=alen-off>256u?256u:alen-off;
        ok &= get(2,off,n)==0;
        ok &= ed_unpack7(rep+9u,rep_n-9u,saved+off,n)==n;
    }
    /* GET emits pack7; the XIP payload itself remains byte-identical after other object reads. */
    ok &= !memcmp(saved,archive,alen) && (st_load(OBJ_BANK0,saved,sizeof saved)==(int)alen && !memcmp(saved,archive,alen)) && get(0,0,64)==0;
    bad+=check("saved-bank GET and interleaved screen draws preserve the frozen runtime snapshot",ok);
    bad+=check("full archive restores a saved slot through the shared workspace",put_all(3,archive,alen,st_crc32(archive,alen))==0);
    pattern_init(); memset(&motion,0,sizeof motion); memset(proj_meta,0,sizeof proj_meta); memset(proj_slot,0,sizeof proj_slot);
    proj_fetch(1); project_load(1); ok=1;
    for (uint32_t k=0; k<NPART; k++) for (uint32_t b=0; b<NPAT; b++) {
        track_t *t=&trk[k]; pattern_request(t,b);
        ok &= t->step[3].n==4 && t->step[3].note[0]==40+k*8+b && t->step[3].note[3]==67 &&
            t->step[4].time==ST_TIE && t->p[P_SLEN]==16+(int16_t)b && step_chance(&t->step[3])==20+b && motion_count(t)==1;
    }
    bad+=check("cold flash load restores all 64 chords, ties, timing, chance and motion banks",ok && chain_patterns[0][3]==4);
    bad+=check("full archive restores the runtime with its original active banks",put_all(0,archive,alen,st_crc32(archive,alen))==0 && trk[0].pattern==NPAT-1u && trk[3].pattern==NPAT-1u);
    return bad;
}
static int native_archive(void)
{
    reset();up_boot();int ok=1;static up_disk_bank_t saved[UP_BANKS];uint8_t fm[FM6_PACKED];fm6_factory(3,fm);
    for(uint32_t b=0;b<UP_BANKS;b++)ok &= !native_put(ENGI_FM6,b*UP_PER_BANK,fm);
    for(uint32_t id=6;id<ED_BK_N;id++){
        uint32_t b=ed_bk_preset_bank(id),len;if(b>=UP_BANKS)continue;
        const uint8_t *raw=ed_bk_object(id,&len);ok &= len==sizeof(up_disk_bank_t);memcpy(&saved[b],raw,sizeof(up_disk_bank_t));
        ok &= !put_all(id,0,0,0);ok &= !put_all(id,&saved[b],sizeof saved[b],st_crc32(&saved[b],sizeof saved[b]));
    }
    up_boot();for(uint32_t b=0;b<UP_BANKS;b++)ok &= native_used(ENGI_FM6,b*UP_PER_BANK)&&!memcmp(native_raw(ENGI_FM6,b*UP_PER_BANK),fm,sizeof fm);
    return check("all 22 shared bank objects archive and restore native voices exactly",ok);
}
static int prophet_archive(void)
{
    reset();up_boot();int bad=0,ok=1;static p5_bank_t saved[5],readback;
    for(uint32_t b=0;b<5;b++){
        p5_bank_defaults(&saved[b],b);saved[b].used=p5_bank_mask(b);saved[b].favorites=saved[b].used;
        for(uint32_t j=0;j<26u;j++){p5_patch_init(&saved[b].patch[j]);saved[b].patch[j].raw[97]=255;saved[b].patch[j].raw[65]=(uint8_t)('A'+b);}
        ok &= !put_all(23+b,&saved[b],sizeof saved[b],st_crc32(&saved[b],sizeof saved[b]));
    }
    up_boot();for(uint32_t k=0;k<128u;k++){p5_patch_t p;uint32_t dst=shared_p5_slot(k);ok &= !p5_user_get(dst,&p)&&p.raw[97]==255&&p.raw[65]=='A'+k/26u&&favorite_has(USER_GENERAL,dst);}
    bad+=check("preceding Prophet archives convert all patches and stars into shared slots",ok);
    uint32_t before=erases;readback=saved[4];readback.used|=1u<<24;
    bad+=check("out-of-range Prophet slot masks are rejected before an erase",put_all(27,&readback,sizeof readback,st_crc32(&readback,sizeof readback))==2&&erases==before);
    readback=saved[4];readback.patch[23].model=99;
    bad+=check("invalid Prophet wire metadata is rejected before an erase",put_all(27,&readback,sizeof readback,st_crc32(&readback,sizeof readback))==2&&erases==before);
    up_disk_bank_t disk;int n=st_load(OBJ_P5BANK0+4,&disk,sizeof disk);readback=saved[4];erase_error=1;
    bad+=check("failed legacy backup commit keeps the previous shared bank",put_all(27,&readback,sizeof readback,st_crc32(&readback,sizeof readback))==4&&n==sizeof disk);erase_error=0;
    up_disk_bank_t after;bad+=check("failed restore retains committed native bytes",st_load(OBJ_P5BANK0+4,&after,sizeof after)==sizeof after&&!memcmp(&after,&disk,sizeof after));
    return bad;
}
static int expanded_recording_archive(void)
{
    reset();
    for(uint32_t k=0;k<NPART;k++)for(uint32_t b=0;b<NPAT;b++) {
        pattern_request(&trk[k],b);
        trk[k].step[3]=(step_t){{60},1,ST_NOTE,SF_RECORDED,100,0,0,0};
    }
    for(uint32_t i=0;i<RECORD_MAX;i++){
        recording[i]=(recorded_note_t){.on=(uint16_t)(i*61u), .duration=1234, .vel=100, .step=3};
        recorded_set_note(&recording[i],60);recorded_set_owner(&recording[i],i%(NPART*NPAT));
    }
    recording_reindex();
    static recorded_note_t saved[RECORD_MAX];uint32_t at=0;for(uint32_t owner=0;owner<NPART*NPAT;owner++)for(uint32_t i=0;i<RECORD_MAX;i++)if(recorded_owner(&recording[i])==owner)saved[at++]=recording[i];
    int ok=!project_save_as(0,"FULL TAKE");
    uint32_t len,crc;ok &= !list(0,&len,&crc);
    static uint8_t archive[ED_BK_MAX], readback[ED_BK_MAX];uint32_t alen=runtime_bytes(archive);ok &= alen && len==alen;
    for(uint32_t off=0;off<alen;off+=256u){uint32_t n=alen-off>256u?256u:alen-off;ok &= !get(2,off,n) && ed_unpack7(rep+9u,rep_n-9u,readback+off,n)==n;}
    ok &= !memcmp(archive,readback,alen);
    ok &= !put_all(3,archive,alen,st_crc32(archive,alen));
    pattern_init();memset(proj_meta,0,sizeof proj_meta);project_load(1);
    ok &= !memcmp(saved,recording,sizeof saved);
    int bad=check("all 1024 timed notes across 64 banks survive flash, backup GET and saved-slot restore",ok);
    ok=!put_all(0,archive,alen,st_crc32(archive,alen)) && !memcmp(saved,recording,sizeof saved);
    bad+=check("expanded runtime archive restores every original timed note",ok);
    return bad;
}
#ifndef BACKUP_TEST_NO_MAIN
int main(void)
{
    int expanded_bad = expanded_recording_archive();

    int bad = 0;
    uint32_t len = 0, crc = 0, before;
    static project_store_t st;
    static project_store_t v6;
    static uint8_t bank_readback[BANK_STORE_SIZE];
    persist_t ps;

    reset();
    trk[0].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 96, 0, 0};
    bad += check("LIST captures runtime and all 57 archive objects with CRC",
                 list(0, &len, &crc) == 0 && rep[2] == ED_BK_N && len == ed_bk_stage.head.len &&
                 crc == ed_bk_stage.head.crc && !st_stream_check(&ed_bk_stage));
    bad += check("an empty project slot lists as length 0", list(2, &len, &crc) == 0 && len == 0);
    bad += check("GET of the runtime copy", get(0, 0, 64) == 0);
    project_save(0);
    bad += check("a project save after LIST reused the staging RAM: GET of the runtime is stale (5)",
                 get(0, 0, 64) == 5u);
    bad += check("a new LIST makes it readable again", list(0, &len, &crc) == 0 && get(0, 0, 64) == 0);
    usb.resets++;
    bad += check("a USB reset after LIST: GET is stale (5)", get(2, 0, 16) == 5u);

    /* settings */
    {
        static uint8_t old_settings[sizeof(persist_t)+sizeof(tmpl_fx_legacy_t)];
        persist_t defaults={0};settings_export(&defaults);memcpy(old_settings,&defaults,sizeof defaults);
        FILE *f=fopen("tests/fixtures/fx/tplf.bin","rb");
        uint32_t n=f?(uint32_t)fread(old_settings+sizeof defaults,1,sizeof(tmpl_fx_legacy_t),f):0;
        if(f)fclose(f);
        bad+=check("previous TPLF settings archive restores through PUT and remains dry",
            n==sizeof(tmpl_fx_legacy_t) && put_all(1,old_settings,sizeof old_settings,st_crc32(old_settings,sizeof old_settings))==0 &&
            template_used() && tmpl.t[0].p[P_E0]==17 && !tmpl.t[0].p[P_FXTYPE]);
        uint32_t before=erases;old_settings[sizeof defaults+__builtin_offsetof(tmpl_fx_legacy_t,cz)+2u]=255;
        bad+=check("damaged previous template is refused before any flash erase",
            put_all(1,old_settings,sizeof old_settings,st_crc32(old_settings,sizeof old_settings))==2 && erases==before);
    }
    reset();
    memset(&ps, 0, sizeof ps);
    settings_export(&ps);
    ps.lowcut = 2;                                       /* SPEAKER BASS+ */
    ps.palette = palette_to_stored(2);
    bad += check("settings with SPEAKER BASS+ restore", put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 &&
                 settings.lowcut == 0u && settings.palette == 2u);
    before = erases;
    ps.palette = 13;                                     /* an untagged id (before 1.0; 1.2 rewrote them) */
    bad += check("settings with an untagged palette id are refused, nothing written",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 2u && erases == before && settings.palette == 2u);
    ps.palette = palette_to_stored(2);
    settings_click = 1; settings_countin = 2;
    settings_export(&ps);                                /* (the record prefs, tagged in the zoom word) */
    settings_click = settings_countin = 0;
    bad += check("settings with a click and a count-in restore",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && settings_click == 1u && settings_countin == 2u);
    ps.zoom = 0x12340100u;
    bad += check("settings with an untagged zoom word are refused", put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 2u);
    ps.zoom = 0;
    before = erases;
    ps.lowcut = 3;
    bad += check("settings with an unknown SPEAKER value are refused, nothing written",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 2u && erases == before && settings.lowcut == 0u);
    ps.lowcut = 0;
    ps.bold = hold_to_stored(0, 3);                      /* HOLD 0.6 s */
    bad += check("settings with HOLD 0.6 s restore",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && HOLD_MS[settings_hold] == 600u);
    ps.bold = 1;                                         /* an older backup (its font weight): HOLD 0.4 s */
    bad += check("settings of an older backup restore with HOLD 0.4 s",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && HOLD_MS[settings_hold] == 400u);
    before = erases;
    ps.bold = HOLD_TAG + 7u;
    bad += check("settings with an unknown HOLD value are refused, nothing written",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 2u && erases == before);
    ps.bold = 0;
    bad += check("a wrong CRC is refused before any write",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps) ^ 1u) == 2u && erases == before);
    {   /* the settings record with the template after it (SLOT TMPL): both back; without one: no template */
        static struct { persist_t p; tmpl_t t; } rec;
        rec.p = ps;
        rec.p.ext.boot = 2;
        memset(&tmpl, 0, sizeof tmpl);
        trk[1].p[P_LEVEL] = 66;
        template_save();
        rec.t = tmpl;
        memset(&tmpl, 0, sizeof tmpl);
        settings_boot = 0;
        bad += check("settings + template restore: BOOT B, the template back",
                     put_all(1, &rec, sizeof rec, st_crc32(&rec, sizeof rec)) == 0 && settings_boot == 2u &&
                     template_used() && tmpl.t[1].p[P_LEVEL] == 66);
        bad += check("  settings without one: no template", put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 &&
                     !template_used() && settings_boot == 0u);
        rec.t.magic ^= 1u;
        before = erases;
        bad += check("  a broken template is refused, nothing written",
                     put_all(1, &rec, sizeof rec, st_crc32(&rec, sizeof rec)) == 2u && erases == before);
    }

    /* malformed requests */
    bad += check("an unknown object id is refused", put_begin(8, sizeof ps, 0) == 1u);
    bad += check("a project longer than any record is refused", put_begin(2, ED_BK_MAX + 1u, 0) == 1u);
    bad += check("settings of the wrong length are refused", put_begin(1, sizeof ps + 4u, 0) == 1u);
    bad += check("a chunk without a begin is refused (5)", (ed_bk_put = 0, put_chunk(1, 0, (const uint8_t *)&ps, 16)) == 5u);
    put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
    bad += check("a chunk at the wrong offset is refused", put_chunk(1, 16, (const uint8_t *)&ps, 16) == 1u);
    bad += check("a chunk for another object is refused (5)", put_chunk(2, 0, (const uint8_t *)&ps, 16) == 5u);
    {
        uint32_t off = 0, step = sizeof ps - 8u;
        put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
        for (; off + 256u <= step; off += 256u)
            put_chunk(1, off, (const uint8_t *)&ps + off, 256);
        if (off < step) put_chunk(1, off, (const uint8_t *)&ps + off, step - off);
        bad += check("a chunk past the length is refused", put_chunk(1, step, (const uint8_t *)&ps, 16) == 1u);
    }
    bad += check("commit before all bytes arrived is refused", put_end(1, 2) == 2u && erases == before);
    {   /* (a compact record has any length: one that is no record fails its commit) */
        static uint8_t junk[sizeof(project_store_t) - 4u];
        st_hdr_t h0;
        memset(junk, 0x55, sizeof junk);
        bad += check("a project of the wrong length is refused at commit, the slot left empty",
                     put_all(2, junk, sizeof junk, st_crc32(junk, sizeof junk)) == 2u && st_current(OBJ_BANK0, &h0) < 0);
    }
    put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
    put_chunk(1, 0, (const uint8_t *)&ps, 64);
    usb.resets++;
    bad += check("a USB reset during PUT ends it (5)", put_chunk(1, 64, (const uint8_t *)&ps + 64, 64) == 5u);
    put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
    fm1_ms += 16000u;
    bad += check("16 s without a chunk ends the PUT (5)", put_chunk(1, 0, (const uint8_t *)&ps, 64) == 5u);
    put_begin(2, sizeof st, 0);
    put_chunk(2, 0, (const uint8_t *)&ps, 64);
    project_save(1);
    bad += check("a project save during PUT (the staging RAM) ends it (5)",
                 put_chunk(2, 64, (const uint8_t *)&ps, 64) == 5u);
    transport_req = 1;
    bad += check("PUT while starting PLAY is refused (3)", put_begin(1, sizeof ps, 0) == 3u);
    transport_req = 0;

    /* projects */
    reset();
    trk[1].step[3] = (step_t){{64}, 1, ST_NOTE, 0, 96, 0, 0};
    project_capture(&proj_scratch);
    proj_pack(&st, &proj_scratch);
    bad += check("a bare FUN16 record restores into slot 3 (flash and RAM) as a bank",
                 put_all(4, &st, sizeof st, st_crc32(&st, sizeof st)) == 0 && project_used(2) &&
                 !memcmp(&proj_slot[2], &st, sizeof st) && flash_image(OBJ_BANK0 + 2, bank_readback) == BANK_STORE_SIZE &&
                 bank_valid(bank_readback, BANK_STORE_SIZE));
    {   /* a record of an older format: refused before anything is written (archives of before 1.0.0: 1.2) */
        static uint8_t old[3388];
        memset(old, 0, sizeof old);
        ((uint32_t *)old)[0] = 0x46554E36u; ((uint32_t *)old)[1] = sizeof old;
        bad += check("a FUN6 project archive is refused (2), the slot left empty",
                     put_all(5, old, sizeof old, st_crc32(old, sizeof old)) == 2u && !st_exists(OBJ_BANK0 + 3u));
    }
    {   /* Native voices now travel inside their shared record object. */
        static up_disk_bank_t got;static uint8_t owned[4240];uint8_t pk[FM6_PACKED];
        bad+=check("retired bank id 8 refuses a payload",put_begin(8,3612,0)==1u);
        bad+=check("retired bank id 8 is listed empty",list(8,&len,&crc)==0&&!len);
        bad+=check("obsolete oversized owned-voice archive is refused",put_begin(19,sizeof owned,st_crc32(owned,sizeof owned))==1u);
        fm6_factory(5,pk);native_put(ENGI_FM6,383,pk);
        const uint8_t *raw=ed_bk_object(28,&len);memcpy(&got,raw,sizeof got);
        bad+=check("U384's shared bank lists with its compact CRC",list(28,&len,&crc)==0&&len==sizeof got&&crc==st_crc32(&got,sizeof got));
        native_put(ENGI_FM6,383,0);
        bad+=check("highest shared voice restores through object 28",put_all(28,&got,sizeof got,st_crc32(&got,sizeof got))==0&&native_used(ENGI_FM6,383)&&!memcmp(native_raw(ENGI_FM6,383),pk,sizeof pk));
        got.magic^=1u;bad+=check("damaged shared bank magic is refused",put_all(28,&got,sizeof got,st_crc32(&got,sizeof got))==2u);
    }
    memcpy(&st, &proj_slot[2], sizeof st);
    erase_error = 1;
    proj_slot[2].raw[100] ^= 1;                          /* (marks the RAM copy, to see it is kept) */
    memcpy(&v6, &proj_slot[2], sizeof v6);
    bad += check("a flash error on commit (4) keeps the slot's RAM copy",
                 put_all(4, &st, sizeof st, st_crc32(&st, sizeof st)) == 4u && !memcmp(&proj_slot[2], &v6, sizeof v6));
    erase_error = 0;
    bad += check("an empty project object clears the slot", put_all(4, 0, 0, st_crc32(0, 0)) == 0 && !project_used(2));
    st.raw[200] ^= 1;                                    /* hash no longer matches */
    bad += check("a project whose own hash fails is refused", put_all(4, &st, sizeof st, st_crc32(&st, sizeof st)) == 2u);

    /* the runtime */
    reset();
    trk[2].step[5] = (step_t){{67}, 1, ST_NOTE, 0, 96, 0, 0};
    trk[2].p[P_LEVEL] = 77;
    project_capture(&proj_scratch);
    proj_pack(&st, &proj_scratch);
    host_tracks_init();
    bad += check("the runtime object restores the tracks",
                 put_all(0, &st, sizeof st, st_crc32(&st, sizeof st)) == 0 && trk[2].step[5].note[0] == 67u &&
                 trk[2].p[P_LEVEL] == 77);

    /* Unsupported banks and settings export their committed bytes, not empty cache defaults. */
    reset();
    {
        uint8_t old[3080]; memset(old, 0x5a, sizeof old);
        const uint32_t objects[] = {OBJ_SETTINGS, OBJ_UPRESET0, OBJ_NATIVEFM0, OBJ_CZBANK0};
        const uint32_t ids[] = {1u, 6u, 21u, 9u};
        int intact = 1;
        for (uint32_t k = 0; k < NELEM(objects); k++) intact &= st_save(objects[k], old, sizeof old) == 0;
        up_cache_reset(); native_cache_reset(); persist_old = 1;
        for (uint32_t k = 0; k < NELEM(ids); k++) {
            intact &= list(ids[k], &len, &crc) == 0 && len == sizeof old && crc == st_crc32(old, sizeof old);
            const uint8_t *raw = ed_bk_object(ids[k], &len);
            intact &= raw && len == sizeof old && !memcmp(raw, old, sizeof old);
        }
        bad += check("full backup retains unsupported settings and preset bank bytes", intact);
    }
    /* user preset banks: unknown record versions are kept as bytes */
    reset();
    {
        static up_disk_bank_t b;
        memset(&b, 0, sizeof b);
        b.magic = UP_DISK_MAGIC;
        b.rsize = sizeof(up_disk_rec_t);
        b.nslot = UP_PER_BANK;
        up_rec_t future;memset(&future,0,sizeof future);future.used=UP_USED;
        future.ver = 99;                                 /* a future record */
        memcpy(future.name, "Future", 7);
        future.packed[0] = 55;memcpy(b.r[4].head,&future,160u);
        bad += check("a bank with a future record version restores byte for byte",
                     put_all(6, &b, sizeof b, st_crc32(&b, sizeof b)) == 0 &&
                     st_load(OBJ_UPRESET0, &proj_wire_u, sizeof proj_wire_u) == (int)sizeof b &&
                     !memcmp(&proj_wire_u, &b, sizeof b));
        b.nslot = 3;
        bad += check("a bank with the wrong slot count is refused", put_all(7, &b, sizeof b, st_crc32(&b, sizeof b)) == 2u);
    }
    {   /* CZ-1 banks: a never-saved bank (BANK A..D: Casio's factory tones) is no object of the backup, so a
         * restore keeps it the default; a saved one is carried byte for byte */
        static cz_bank_t cb;
        reset();
        cz_bank_boot();
        bad += check("CZ-1: a never-saved BANK A (its factory default) lists as length 0",
                     list(9, &len, &crc) == 0 && len == 0 && cz_bank_load(0)->used == 0xFFFFu);
        cz_bank_empty(&cb, 0);
        memcpy(cb.tone[3].raw, cz_fac(7), CZ_BYTES);
        cb.used = 1u << 3;
        bad += check("CZ-1: a saved BANK A restores and lists with its 2332 B",
                     put_all(9, &cb, sizeof cb, st_crc32(&cb, sizeof cb)) == 0 && list(9, &len, &crc) == 0 &&
                     len == sizeof cb && cz_bank_load(0)->used == 1u << 3);
        bad += check("CZ-1: length 0 clears native presets while keeping factory tones separate",
                     put_all(9, &cb, 0, st_crc32(&cb, 0)) == 0 && !up_used(144) &&
                     list(9, &len, &crc) == 0 && len == 0);
    }
    bad += native_archive();
    bad += prophet_archive();
    bad += full_pattern_archive();
#if MELODEE_AUDIO_PROFILE
    reset();
    {
        persist_t original;settings_export(&original);
        persist_t saved=persist_saved;
        uint32_t writes=erases;
        audio_lab_guard=1;
        settings_budget=24;settings_save();
        bad+=check("lab guard blocks settings and direct persistent writes",
            persist_pending && erases==writes && st_save(OBJ_SETTINGS,&original,sizeof original)==-9);
        song.g[G_CLOCK]^=1;glo_poll();
        bad+=check("lab guard keeps saved global preferences untouched",!memcmp(settings_glo,original.ext.glo,sizeof settings_glo));
        bad+=check("lab preferences restore RAM without updating flash bookkeeping",
            !put_all(1,&original,sizeof original,st_crc32(&original,sizeof original)) &&
            settings_budget==VOICE_BUDGET_DEFAULT && erases==writes && !memcmp(&persist_saved,&saved,sizeof saved));
        memset(&motion,0,sizeof motion);memset(motion_pattern,0,sizeof motion_pattern);
        static uint8_t guarded[BANK_STORE_SIZE];
        bad+=check("guarded runtime LIST captures without erasing flash",!list(0,&len,&crc)&&erases==writes);
        int captured=1;
        for(uint32_t off=0;off<BANK_STORE_SIZE;off+=256u){
            uint32_t n=BANK_STORE_SIZE-off>256u?256u:BANK_STORE_SIZE-off;
            captured &= !get(0,off,n)&&ed_unpack7(rep+9u,rep_n-9u,guarded+off,n)==n;
        }
        uint32_t guard_crc=st_crc32(guarded,sizeof guarded),guard_rc=put_all(0,guarded,sizeof guarded,crc);
        bad+=check("guarded runtime archive restores and verifies without flash writes",
            captured&&crc==guard_crc&&!guard_rc&&erases==writes);
        original.magic=0;
        bad+=check("lab preferences reject invalid data",put_all(1,&original,sizeof original,st_crc32(&original,sizeof original))==2);
        audio_lab_guard=0;
    }
#endif
    printf("backup test %s\n", bad ? "FAILED" : "passed");
    return (bad + expanded_bad) != 0;
}

#endif
