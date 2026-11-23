/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Complete musical archive: no raw addresses are accepted. Objects are staged
 * in bounded RAM or the shared unpublished project workspace, then validated before publication.
 * Runtime and saved projects include all 32 pattern banks. Retired user sample
 * slots are absent from the inventory and cannot be restored over project data.
 */
#include "editor_wire.h"
#ifndef ED_BK_FLASH_PTR
#define ED_BK_FLASH_PTR(off) fm1_xip_ptr(off)
#endif
/* 2..5: projects 1..4; 64..91: projects 5..32 (project.c PROJ_SLOTS); 92..99: the data records (rec_store.c: a
 * pads synth's recorded kits) */
static const uint8_t ED_BK_IDS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91
#ifdef REC_STORE
    , 92, 93, 94, 95, 96, 97, 98, 99
#endif
};
#define ED_BK_KIT0 92u
#ifdef REC_STORE
static int ed_bk_kit(uint32_t id) { return id >= ED_BK_KIT0 && id < ED_BK_KIT0 + ST_NREC; }
#else
static int ed_bk_kit(uint32_t id) { (void)id; return 0; }
#endif
#define ED_BK_N ((uint32_t)sizeof ED_BK_IDS)
#define ED_BK_MAX (BANK_STORE_SIZE + 32u)   /* a raw record, or a packed one (project_stream.c PJ_HEAD) */
#ifdef REC_STORE                            /* a data record streams into its record (no staging): its own bound */
#define ED_BK_LEN_MAX(id) (ed_bk_kit(id) ? REC_BOX_HEAD + rec_max() : ED_BK_MAX)
#else
#define ED_BK_LEN_MAX(id) ED_BK_MAX
#endif
#define ED_BK_RAW (wire_take())             /* reuse the existing serialized main-loop scratch */
#define ed_bk_set set_rec    /* id 1: the settings record (+ the template), in the settings save's staging (project.c):
                             * a save while a backup runs rewrites it, and the archive's CRC then fails */
static uint32_t ed_bk_set_len;
static uint8_t ed_bk_valid, ed_bk_put, ed_bk_id, ed_bk_gen;
#ifdef REC_STORE
static uint32_t ed_bk_box_crc;                  /* a kit's PUT: its bytes' CRC so far (its pads are published as they come) */
#endif
static uint32_t ed_bk_len, ed_bk_crc, ed_bk_pos, ed_bk_ms, ed_bk_usb;
#if MELODEE_FLASH
static uint32_t ed_bk_source_obj, ed_bk_source_copy, ed_bk_source_crc;
/* Runtime LIST snapshots and PUT uploads have exclusive lifetimes. Neither
 * has a committed header until an explicitly validated saved-slot restore. */
static st_stream_t ed_bk_stage;
/* Profiling builds keep the original RAM-only archive workspace. Guarded
 * sessions must capture and restore live music without any flash mutation. */
static int ed_bk_ram_only(void){return AUDIO_LAB_GUARDED();}
static void ed_bk_from(uint32_t obj, uint32_t copy, uint32_t crc)   /* GET reads the object from obj's copy */
{
    ed_bk_source_obj = obj; ed_bk_source_copy = copy; ed_bk_source_crc = crc;
}
/* a stored object: its length, and where GET reads it (none stored: the staging RAM, length 0) */
static const uint8_t *ed_bk_stored(uint32_t obj, uint32_t *len)
{
    st_hdr_t h;
    int copy = st_current(obj, &h);
    if (copy < 0)
        return ED_BK_RAW;
    *len = h.len;
    ed_bk_from(obj, (uint32_t)copy, h.crc);
    return ED_BK_FLASH_PTR(st_sector(obj, (uint32_t)copy) + st_payload_off_header(obj, &h));
}
#endif
static int ed_bk_project(uint32_t id){return id==0u || (id>=2u && id<=5u) || (id>=64u && id<60u+PROJ_SLOTS);}
static uint32_t ed_bk_slot(uint32_t id) { return id < 6u ? id - 2u : id - 60u; }   /* a saved project's id -> slot */
static uint32_t ed_bk_r32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 7 | (uint32_t)p[2] << 14 | (uint32_t)p[3] << 21 | (uint32_t)p[4] << 28;
}
static void ed_bk_pack(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, mask = 0;
        for (uint32_t i = 0; i < k; i++) mask |= (uint32_t)(p[i] >> 7) << i;
        ed_b(mask);
        for (uint32_t i = 0; i < k; i++) ed_b(p[i]);
        p += k; n -= k;
    }
}
static uint32_t ed_bk_preset_bank(uint32_t id)
{
    if(id==6u || id==7u)return id-6u;
    if(id==17u || id==18u)return id-15u;
    if(id>=19u && id<=22u)return id<21u?id-13u:id-17u;
    if(id>=9u && id<=16u)return id-1u;
    if(id>=23u && id<=28u)return id-7u;
    return UP_BANKS;
}
static const uint8_t *ed_bk_object(uint32_t id,uint32_t *len)
{
    *len=0;
#if MELODEE_FLASH
    ed_bk_source_obj=OBJ_COUNT;
    uint32_t bank=ed_bk_preset_bank(id),obj=bank<UP_BANKS?up_obj(bank):id==1u && template_old()?OBJ_SETTINGS:OBJ_COUNT;
    if(obj<OBJ_COUNT)return ed_bk_stored(obj,len);
#else
    uint32_t bank=ed_bk_preset_bank(id);
    if(bank<UP_BANKS){*len=sizeof(up_bank_t);return (const uint8_t *)up_cache_bank(bank);}
#endif
    if (id == 0u) {
        if(ed_bk_valid>1u)return ED_BK_RAW;   /* an inventory: no music captured (length 0) */
        *len=BANK_STORE_SIZE;
#if MELODEE_FLASH
        if(!ed_bk_ram_only()){*len=ed_bk_stage.head.len;ed_bk_from(ed_bk_stage.obj,ed_bk_stage.head.slot,ed_bk_stage.head.crc);}   /* (compact) */
#endif
        return ED_BK_RAW;
    }
    if (id == 1u) { *len = ed_bk_set_len; return (const uint8_t *)&ed_bk_set; }
#ifdef REC_STORE
    if (ed_bk_kit(id)) {                    /* a kit: GET reads its blob range by range (rec_store.c rec_box_*) */
        *len = rec_box_open(id - ED_BK_KIT0);
        return ED_BK_RAW;
    }
#endif
    if (id && ed_bk_project(id)) {
        uint32_t slot = ed_bk_slot(id);
#if MELODEE_FLASH
        return ed_bk_stored(st_proj_obj(slot), len);   /* empty: a valid pointer, zero length */
#else
        if (slot >= 4u) return (const uint8_t *)&proj_slot[0];   /* (host fixtures: four slots) */
        if (bank_valid(proj_bank_slot[slot], BANK_STORE_SIZE) &&
            !memcmp(proj_bank_slot[slot] + 8u, &proj_slot[slot], sizeof proj_slot[slot])) {
            *len = BANK_STORE_SIZE; return proj_bank_slot[slot];
        }
        if (project_used(slot)) *len = sizeof proj_slot[slot];
        return (const uint8_t *)&proj_slot[slot];
#endif
    }
    if(id==8u)return ED_BK_RAW;
    return 0;
}
/* inv: an inventory (LIST 1): the stored objects and the settings, the music not captured: no stop, no flash write */
static uint32_t ed_bk_capture(uint32_t inv)
{
    if(!inv){
    if (ed_flash_stop()) return 3;
    up_favorite_poll();
    }
    ed_bk_valid=ed_bk_put=0;
    if(!inv){
    ed_bk_gen=++proj_wire_gen;
#if MELODEE_FLASH
#if MELODEE_AUDIO_PROFILE
    if(ed_bk_ram_only()){
        project_capture(&proj_scratch);
        if(!bank_pack(ED_BK_RAW,&proj_scratch,1))return 2;
    }else
#endif
    {
    if(!flash_ok || !proj_image_capture(1))return 2;
    if(proj_stream_generate(&ed_bk_stage,st_proj_obj(proj_cur<PROJ_SLOTS?proj_cur:0u)))return 4;
    }
#else
    project_capture(&proj_scratch);
    if(!bank_pack(ED_BK_RAW,&proj_scratch,1))return 2;
#endif
    }
#if MELODEE_FLASH
    ed_bk_set.p = persist_saved;                    /* fields absent from this build survive */
#else
    memset(&ed_bk_set.p, 0, sizeof ed_bk_set.p);
#endif
    settings_export(&ed_bk_set.p);                  /* the record as it is stored: the template (tmpl) after the settings */
    ed_bk_set_len = sizeof ed_bk_set.p + (template_used() ? sizeof ed_bk_set.t : 0u);
    ed_bk_valid = (uint8_t)(1u + inv); ed_bk_usb = usb.resets;
    return 0;
}
static int ed_bk_panel_valid(const panel_t *p)
{
    uint32_t b = 0, e = 0;
    if (p->magic != PANEL_MAGIC) return 0;
    for (uint32_t i = 0; i < NB; i++) {
        if (p->btn[i] >= NB || (b & (1u << p->btn[i]))) return 0;
        b |= 1u << p->btn[i];
    }
    for (uint32_t i = 0; i < NE; i++) {
        if (p->enc[i] >= NE || (e & (1u << p->enc[i])) || (p->dir[i] != 1 && p->dir[i] != -1)) return 0;
        e |= 1u << p->enc[i];
    }
    return 1;
}
#if MELODEE_FLASH
static int ed_bk_preset_valid(uint32_t bank,const uint8_t *raw,uint32_t len)
{
    const up_bank_t *h=(const up_bank_t *)raw;
    const uint8_t *m=raw+sizeof(up_disk_bank_t);         /* (its synths' IDs after its records: upreset.c) */
    if(len>sizeof(up_disk_bank_t) && h->magic==UP_DISK_MAGIC && (len<sizeof(up_disk_bank_t)+8u ||
       mod_le32(m)!=UP_IDS_MAGIC || m[4]>UP_IDS_MAX || len!=sizeof(up_disk_bank_t)+8u+8u*m[4]))return 0;
    if(len>=sizeof(up_disk_bank_t) && h->magic==UP_DISK_MAGIC && h->rsize==sizeof(up_disk_rec_t) && h->nslot==UP_PER_BANK){
        const up_disk_bank_t *d=(const up_disk_bank_t *)raw;
        for(uint32_t j=0;j<UP_PER_BANK;j++){up_rec_t r;memset(&r,0,sizeof r);memcpy(&r,d->r[j].head,160u);memcpy(r.tone_extra,d->r[j].extra,46u);
            if(r.used && (r.ver==UP_VER || r.ver==UP_VER_GRID || r.ver==UP_VER_KIT || r.ver==UP_VER_KIT_GRID || r.ver==UP_VER_FM || r.ver==UP_VER_P5 || r.ver==UP_VER_NATIVE_CZ) && !up_valid(&r))return 0;
        }
        return 1;
    }
    return 0;
}
#endif
/* a backup's runtime object brings CLK back with it; a project load alone keeps the device's */
static void ed_bk_clock(void){song.g[G_CLOCK]=(int16_t)clamp(proj_scratch.g[G_CLOCK],GP[G_CLOCK].min,GP[G_CLOCK].max);}
static uint32_t ed_bk_commit_inner(void)
{
    uint8_t *raw = ED_BK_RAW;
    uint32_t obj;
#ifdef REC_STORE
    if(ed_bk_kit(ed_bk_id)){                         /* a kit: none in the archive empties it */
        uint32_t u=ed_bk_id-ED_BK_KIT0;
        if(!ed_bk_len)return rec_delete(u)?4u:0u;
        if(ed_bk_pos!=ed_bk_len || ~ed_bk_box_crc!=ed_bk_crc)return 2;
        int rc=rec_box_put_end();
        return rc==-2?2u:rc?4u:0u;
    }
#endif
#if MELODEE_FLASH
    if(ed_bk_project(ed_bk_id) && ed_bk_len && !ed_bk_ram_only()){
        if(ed_bk_pos!=ed_bk_len || ~ed_bk_stage.crc!=ed_bk_crc || st_stream_check(&ed_bk_stage) ||
           !proj_stream_load(ed_bk_stage.obj,ed_bk_stage.head.slot,ed_bk_len))return 2;
        int full=bank_full(ed_bk_len)||pj.on;
        if(!ed_bk_id){
            up_boot();if(up_migration_error)return 4;
            if(project_restore_runtime(&proj_scratch))return 2;
            ed_bk_clock();
            if(full)bank_restore_tail(proj_image_tail());
            return 0;
        }
        if(proj_publish_slot(ed_bk_slot(ed_bk_id),&ed_bk_stage))return 4;
        if(proj_cur==ed_bk_slot(ed_bk_id))proj_cur=PROJ_NO_SLOT;
        sync_reload=1;ui.force=1;return 0;
    }
#endif
    if (ed_bk_pos != ed_bk_len || st_crc32(raw, ed_bk_len) != ed_bk_crc) return 2;
    if (ed_bk_project(ed_bk_id)) {
#if !MELODEE_FLASH || MELODEE_AUDIO_PROFILE
        int full = bank_full(ed_bk_len);
        if (ed_bk_len && !(full && bank_valid(raw, ed_bk_len))) return 2;
        if (ed_bk_id == 0u) {
            if (!ed_bk_len) return 2;
            up_boot();
#if MELODEE_FLASH
            if(up_migration_error)return 4;
#endif
            if (project_restore_runtime(&proj_scratch)) return 2;
            ed_bk_clock();
            if (full) bank_restore(raw);
            return 0;
        }
#endif
        if (proj_write_slot(ed_bk_slot(ed_bk_id), raw, ed_bk_len)) return 4;
        if (proj_cur == ed_bk_slot(ed_bk_id)) proj_cur = PROJ_NO_SLOT;
        sync_reload = 1; ui.force = 1;
        return 0;
    } else if (ed_bk_id == 1u) {
#if MELODEE_AUDIO_PROFILE
        /* Guarded diagnostics restore preferences in RAM only. No template
         * replacement, persistence bookkeeping or flash commit is allowed. */
        if(AUDIO_LAB_GUARDED()){
            if(ed_bk_len!=sizeof(persist_t) || ((const persist_t *)raw)->magic!=PERSIST_MAGIC ||
               !ed_bk_panel_valid(&((const persist_t *)raw)->panel))return 2;
            settings_import((persist_t *)raw,sizeof(persist_t));
            panel_init();settings_init();palette_set(settings.palette);
            return 0;
        }
#endif
        const persist_t *p = (const persist_t *)raw;
        const uint8_t *t = raw + sizeof *p;                  /* a template after the settings */
        uint32_t tl = ed_bk_len - sizeof *p, tm = 0, ts = 0;
        if (ed_bk_len >= sizeof *p + 8u) {
            memcpy(&ts, t + tl - 8u, 4);
            memcpy(&tm, t + tl - 4u, 4);
        }
        if (!(ed_bk_len>=sizeof *p && ed_bk_len<=sizeof ed_bk_set && p->magic==PERSIST_MAGIC &&
              (p->ext.usb_off<=3u || (p->ext.usb_off&0xFFFF00FCu)==WORK_BUDGET_TAG) && p->ext.boot<=PROJ_SLOTS &&   /* (the
                                                   * voice budget's tag: settings_export writes it, settings_import reads it) */
              (ed_bk_len==sizeof *p || tmpl_blob_valid(t,tl))) || !palette_stored_ok(p->palette) ||
            p->lowcut > 2u || (p->zoom & 0xFFu) > 1u || (p->zoom > 0xFFu && (p->zoom & 0xFFFF0000u) != RECORD_PREF_TAG) || !hold_stored_ok(p->bold) || p->favorites.filter > 1u || !ed_bk_panel_valid(&p->panel)) return 2;
        if(tl && ed_bk_len>sizeof *p && !tmpl_blob_valid(t,tl))return 2;
        obj = OBJ_SETTINGS;
    } else if(ed_bk_preset_bank(ed_bk_id)<UP_BANKS){
        uint32_t bank=ed_bk_preset_bank(ed_bk_id);
#if MELODEE_FLASH
        if(ed_bk_len && !ed_bk_preset_valid(bank,raw,ed_bk_len))return 2;
        /* (an empty bank: its record deleted. Saved empty, a pool record took a sector of its own for nothing: a
         * backup's restore puts every bank, and 23 empty ones left no room for its kits) */
        if(!flash_ok || (!ed_bk_len && st_listed(up_obj(bank)) ? st_project_delete(up_obj(bank)) : st_save(up_obj(bank),raw,ed_bk_len)))return 4;
        up_cache_reset();up_gen++;sync_reload=1;ui.force=1;
#else
        if(ed_bk_len && (ed_bk_len!=sizeof(up_bank_t) || !up_bank_shape(ed_bk_len,((const up_bank_t *)raw)->rsize)))return 2;
        memset(up_cache_bank(bank),0,sizeof(up_bank_t));if(ed_bk_len)memcpy(up_cache_bank(bank),raw,ed_bk_len);
#endif
        return 0;
    } else if(ed_bk_id==8u){return ed_bk_len?2u:0u;
    } else return 1;
#if MELODEE_FLASH
    if (!flash_ok || st_save(obj, ed_bk_id == 17u || ed_bk_id == 18u ? raw + 8u : raw,
                            (ed_bk_id == 17u || ed_bk_id == 18u) && ed_bk_len ? ed_bk_len - 8u : ed_bk_len)) return 4;
#else
    (void)obj;
#endif
    if (ed_bk_id == 1u) {
        uint32_t ns = ed_bk_len > sizeof ed_bk_set.p ? sizeof ed_bk_set.p : ed_bk_len;
        memset(&ed_bk_set, 0, sizeof ed_bk_set);
        memcpy(&ed_bk_set.p, raw, ns);
        settings_import(&ed_bk_set.p, (int)ns);
        tmpl_take(raw + ns, ed_bk_len - ns);            /* (none in the backup: none now) */
        tmpl_dirty = 0;
#if MELODEE_FLASH
        if (st_exists(ST_TPLX))
            (void)st_project_delete(ST_TPLX);           /* (the template here before: its end block goes with it) */
#endif
        settings_old_accept();
        panel_init(); settings_init(); palette_set(settings.palette);
#if MELODEE_FLASH
        persist_saved = ed_bk_set.p; persist_pending = persist_repair = 0;
#if MELODEE_BLE
        /* The musical archive contains preferences/template, not device keys.
         * Reattach this device's BLE trailer on the next quiet A/B save. */
        persist_pending = 1; mb_revision++;
#endif
#endif
    }
    sync_reload = 1; ui.force = 1;
    return 0;
}
static uint32_t ed_bk_commit(void) { return ed_bk_commit_inner(); }
static uint32_t ed_bk_write(const uint8_t *a, uint32_t n)
{
    if (n < 2u || a[0] > 3u || (a[1] > 28u && !ed_bk_project(a[1]) && !ed_bk_kit(a[1]))) return 1;
    if (ed_flash_stop()) return 3;
    if (a[0] == 0u) {
        if (n != 12u || a[6] > 15u || a[11] > 15u) return 1;
        uint32_t len = ed_bk_r32(a + 2);
        if (len > ED_BK_LEN_MAX(a[1]) || (a[1] == 0u && !len) ||
            (a[1] == 1u && len != sizeof(persist_t) && len != sizeof(persist_t) + sizeof(tmpl_t)) ||
            (!ed_bk_project(a[1]) && !ed_bk_kit(a[1]) && len > BANK_STORE_SIZE) ||   /* (a record: rec_max, above) */
            (ed_bk_preset_bank(a[1])<UP_BANKS && len>3840u) ||
            (a[1]==8u && len)) return 1;
        ed_bk_valid=ed_bk_put=0;
        ed_bk_gen=++proj_wire_gen;
#if MELODEE_FLASH
#ifdef REC_STORE
        if(ed_bk_kit(a[1])){                         /* a kit: each pad record streamed in as its bytes come */
            if(ed_bk_ram_only())return 4;
            if(len && (!flash_ok || rec_box_put_begin(&ed_bk_stage,a[1]-ED_BK_KIT0,len)))return len<REC_BOX_HEAD?1:4;
            ed_bk_box_crc=0xFFFFFFFFu;
        }else
#endif
        if(ed_bk_project(a[1]) && len && !ed_bk_ram_only()){
            uint32_t slot=a[1]?ed_bk_slot(a[1]):proj_cur<PROJ_SLOTS?proj_cur:0u;
            if(!flash_ok || st_stream_begin(&ed_bk_stage,st_proj_obj(slot),len))return 4;
        }else if(len>sizeof proj_wire_u.raw)return 1;
#endif
        ed_bk_put = 1; ed_bk_id = a[1]; ed_bk_len = len;
        ed_bk_crc = ed_bk_r32(a + 7); ed_bk_pos = 0;
        ed_bk_usb = usb.resets; ed_bk_ms = fm1_ms;
        return 0;
    }
    if (!ed_bk_put || ed_bk_id != a[1] || ed_bk_usb != usb.resets || fm1_ms - ed_bk_ms > 15000u ||
        ed_bk_gen != proj_wire_gen) {                 /* (a project save / load reused the staging RAM) */
        ed_bk_put = 0; return 5;
    }
    ed_bk_ms = fm1_ms;
    if (a[0] == 3u) { ed_bk_put = 0; return n == 2u ? 0u : 1u; }
    if (a[0] == 2u) {
        if (n != 2u) return 1;
        uint32_t rc = ed_bk_commit(); ed_bk_put = 0; return rc;
    }
    if (n < 9u || a[6] > 15u || ed_bk_r32(a + 2) != ed_bk_pos) return 1;
    uint32_t count = ed_unpack7(a + 7, n - 7u, ed_transfer_buf, 256u);
    if (!count || count > ed_bk_len - ed_bk_pos) return 1;
#if MELODEE_FLASH
#ifdef REC_STORE
    if(ed_bk_kit(ed_bk_id) && !ed_bk_ram_only()){
        uint8_t chunk[256];                          /* (a pad's commit reads the flash through st_buf: the transfer
                                                      * buffer's own RAM, main_scratch) */
        memcpy(chunk,ed_transfer_buf,count);
        ed_bk_box_crc=st_crc32_update(ed_bk_box_crc,chunk,count);
        int rc=rec_box_put_write(chunk,count);
        if(rc){ed_bk_put=0;return rc==-2?2:rc==-1?1:4;}
    }else
#endif
    if(ed_bk_project(ed_bk_id) && !ed_bk_ram_only()){
        if(st_stream_write(&ed_bk_stage,ed_transfer_buf,count)){ed_bk_put=0;return 4;}
    }else
#endif
        memcpy(ED_BK_RAW+ed_bk_pos,ed_transfer_buf,count);
    ed_bk_pos+=count;
    return 0;
}
static int ed_backup_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (cmd == ED_BACKUP_LIST) {
        uint32_t rc = n > 1u || (n && a[0] != 1u) ? 1u : ed_bk_capture(n);   /* (1: an inventory) */
        ed_b(1); ed_b(rc); ed_b(rc ? 0u : ED_BK_N);
        if (!rc) for (uint32_t i = 0; i < ED_BK_N; i++) {
            uint32_t len;
            const uint8_t *p = ed_bk_object(ED_BK_IDS[i], &len);
            uint32_t crc;
#if MELODEE_FLASH
            if (st_obj_ok(ed_bk_source_obj)) crc = ed_bk_source_crc;
            else
#endif
#ifdef REC_STORE
            if (ed_bk_kit(ED_BK_IDS[i])) crc = len ? rec_box_crc() : st_crc32(p, 0);
            else
#endif
                crc = st_crc32(p, len);
            ed_b(ED_BK_IDS[i]); ed_u35(len); ed_u35(crc);
            fm1_wdt_feed();
        }
        return 1;
    }
    if (cmd == ED_BACKUP_GET) {
        uint32_t len = 0, off = n >= 6u ? ed_bk_r32(a + 1) : 0u;
        uint32_t count = n == 8u ? (uint32_t)a[6] | (uint32_t)a[7] << 7 : 0u;
        const uint8_t *p = n ? ed_bk_object(a[0], &len) : 0;
        uint32_t rc = (!ed_bk_valid || ed_bk_usb != usb.resets || (n && !a[0] && (ed_bk_gen != proj_wire_gen || ed_bk_valid > 1u))) ? 5u :   /* (playing: read too) */
            n != 8u || a[5] > 15u || !p || !count || count > 256u || off > len || count > len - off ? 1u : 0u;
        uint32_t data_off = off;
#if MELODEE_FLASH
        if (!rc && !a[0] && !ed_bk_ram_only() && ed_bk_stage.generation != st_stream_generation) rc = 5;
        if (!rc && st_obj_ok(ed_bk_source_obj)) {
            if (st_read_range(ed_bk_source_obj, ed_bk_source_copy, off, ed_transfer_buf, count)) rc = 4;
            p = ed_transfer_buf; data_off = 0;
        }
#ifdef REC_STORE
        else if (!rc && ed_bk_kit(a[0])) {
            if (rec_box_read(off, ed_transfer_buf, count)) rc = 4;
            p = ed_transfer_buf; data_off = 0;
        }
#endif
#endif
        ed_echo(a, n, 1u); ed_b(rc); ed_u35(off); ed_u14(rc ? 0u : count);
        if (!rc) ed_bk_pack(p + data_off, count);
        return 1;
    }
    if (cmd == ED_BACKUP_PUT) {
        uint32_t rc = ed_bk_write(a, n);
        ed_echo(a, n, 2u); ed_b(rc);
        return 1;
    }
    return 0;
}
