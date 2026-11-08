/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Incoming dumps retain their native names and bytes until the player chooses
 * a destination. The receive ISR never writes flash or changes a track. */
static struct {
    uint8_t engine, count, general;
    uint16_t size, first;
    const uint8_t *data;
    uint8_t raw[238];
} sx_import;
static struct { uint16_t anchor, end; uint8_t on, engine, track, pattern; uint32_t generation; uint8_t notes[RECORD_MAX/8u]; } bulk;
static uint8_t sx_checksum(const uint8_t *p,uint32_t n)
{
    uint32_t sum=0;while(n--)sum+=*p++;return (uint8_t)(-sum&127u);
}
static uint32_t library_engine(void)
{
    return sx_import.count && !ui.home && cur_page()->graph == GR_USER ? sx_import.engine : TSEL->eng_req;
}
static void bulk_clear(void) { bulk.on = 0; ui.note_copy = ui.note_move = 0; ui.force = 1; }
static int bulk_has(uint32_t k)
{
    uint32_t a = bulk.anchor < bulk.end ? bulk.anchor : bulk.end;
    uint32_t b = bulk.anchor > bulk.end ? bulk.anchor : bulk.end;
    return bulk.on && k >= a && k <= b;
}
/* the first patch's name (the synth's schema says where its patch keeps it; a general record's: its own), 12 at most */
static void sysex_import_name(char *nm)
{
    uint32_t off=4u,n=12u;
    if(!sx_import.general){n=patch_name_place(sx_import.engine,&off);if(n>12u)n=12u;}
    for(uint32_t i=0;i<n;i++){uint8_t c=sx_import.data[off+i];nm[i]=c>=32u&&c<=126u?(char)c:' ';}nm[n]=0;
}
/* count patches of synth e (size bytes each) offered for user slots: more than fit here stay where they are, in the
 * frame being served (a synth's bank: sysex_route.c holds the scratch until they are chosen); 1 staged */
static int sysex_stage(uint32_t e, const uint8_t *raw, uint32_t count, uint32_t size)
{
    int held=count*size>sizeof sx_import.raw;
    if(!count || (held && !(msx_serving && raw>=proj_wire_u.raw && raw+count*size<=proj_wire_u.raw+sizeof proj_wire_u.raw)))return 0;
    if (sx_import.count || ui.confirm || ui.name_active || ui.new_active || ui.layer || ui.ly || designer_on()) { ui_message("FINISH CURRENT IMPORT"); return 0; }
    if(held){sx_import.data=raw;msx.ready=2;}
    else {memcpy(sx_import.raw,raw,count*size);sx_import.data=sx_import.raw;}
    sx_import.general=size==238u;
    sx_import.engine = (uint8_t)e; sx_import.count = (uint8_t)count; sx_import.size = (uint16_t)size;
    sx_import.first = 0;
    bulk_clear();
    ui.menu = ui.layer = ui.ly = 0;
    for(uint32_t p=0;p<NPAGES;p++)if(PG(p).graph==GR_USER){ui.page=(uint8_t)p;break;}
    ui.import_refresh=1;ui.home=0;ui.fam_last[FAM_SAVE]=ui.page;ui.uslot=0;song.seq_mode=0;ui.entry_open=0;ui.note_pick=0;ui.act=0;seq_midi_reset();
    char nm[16];sysex_import_name(nm);
    ui_say("IMPORT ", nm); str_cpy(ui.msg2, "CHOOSE USER SLOTS", sizeof ui.msg2); ui.force = 1;
    return 1;
}
static void sysex_cancel(void) { if(msx.ready==2u)msx.ready=0;sx_import.count = 0; bulk_clear(); ui_message("IMPORT CANCELLED"); }
/* project.c wire_take: a job needs the scratch a staged bank lives in */
static int sysex_cancel_staged(void) { if (msx.ready != 2u) return 0; sysex_cancel(); return 1; }
static void sysex_commit(void)
{
    if (!sx_import.count) return;
    if (transport_busy()) { ui_message("STOP TO IMPORT"); return; }
    uint32_t saved = 0;
    int rc=0;
    if(!sx_import.general)rc=shared_import(sx_import.engine,sx_import.first,sx_import.count,sx_import.data,sx_import.size,&saved);
    else while(saved<sx_import.count){rc=general_sysex_save(sx_import.first+saved,sx_import.data+saved*sx_import.size);if(rc==1 || rc==2)break;saved++;}
    if(rc==1 || rc==2){
        if(saved){sx_import.data+=saved*sx_import.size;sx_import.count-=saved;sx_import.first+=saved;ui.uslot=sx_import.first;}
        ui_message("IMPORT SAVE ERROR");return;
    }
    sx_import.count=0;if(msx.ready==2u)msx.ready=0;bulk_clear();ui_message("PATCHES IMPORTED");
}
static void sysex_choose(void)
{
    if (transport_busy()) { ui_message("STOP TO IMPORT"); return; }
    uint32_t first = ui.uslot;
    if (first + sx_import.count > UP_SLOTS) { ui_message("NOT ENOUGH SLOTS"); return; }
    sx_import.first = (uint16_t)first;
    for(uint32_t k=first;k<first+sx_import.count;k++)if((up_used(k)||up_old(k))){confirm_open(CF_IMPORT,0);return;}
    sysex_commit();
}
/* A synth's own patch goes as its synth's SysEx (its module's sysex_dump: the slot's name kept), any other sound as
 * Melodee's general record */
static uint32_t msx_dump(uint32_t e, const uint8_t *patch, uint32_t what, uint32_t ch, uint8_t *out, uint32_t max);
static int library_send(uint32_t e, uint32_t k)
{
#if MELODEE_OTA
    if(!up_used(k))return 1;
    e=up_engine(k);
    if(!native_limit(e) || !native_used(e,k))return general_sysex_send(k);
    if (!usb.config || !native_used(e,k)) return 1;
    static uint8_t raw[MOD3_PATCH_MAX], frame[320] __attribute__((section(".pool"))); uint32_t n;
    memset(raw,0,sizeof raw);memcpy(raw,native_raw(e,k),native_size(e));
    n=msx_dump(e,raw,MOD3_DUMP_STORED,0,frame,sizeof frame);
    return !n || ota_wire_send(frame,n);
#else
    (void)e; (void)k; return 1;
#endif
}
static void bulk_erase(void)
{
    if(transport_busy()){ui_message("STOP TO ERASE");return;}
    for(uint32_t k=0;k<UP_SLOTS;k++)if(bulk_has(k) && up_old(k)){ui_message("ERASE OLD SLOT FIRST");return;}
    for(uint32_t k=0;k<UP_SLOTS;k++)if(bulk_has(k) && up_used(k)){up_ui_named(1,k,0);if(up_used(k))return;}
    bulk_clear();ui_message("PRESETS ERASED");
}
static void bulk_erase_ask(void) { if(transport_busy())ui_message("STOP TO ERASE");else confirm_open(CF_BULK_ERASE,0); }
static void bulk_send(void)
{
    for(uint32_t k=0;k<UP_SLOTS;k++)if(bulk_has(k)&&up_used(k))if(library_send(bulk.engine,k)){ui_message("SYSEX SEND ERROR");return;}
    ui_message("PRESETS SENT");
}
static void library_send_current(void) { ui_message(library_send(library_engine(),ui.uslot)?"SYSEX SEND ERROR":"PRESET SENT"); }
