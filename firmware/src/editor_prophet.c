/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* 95: operation, track, payload. 0 exact GET, 1 native-frame PUT (without
 * F0/F7), 2 field edit (offset,value), 3 printable name, 4 native dump send. */
static void p5_accept_patch(uint32_t tr,const p5_patch_t *patch)
{
    track_t *t=&trk[tr];load_begin(t,UNDO_SOUND);panic_req|=(uint8_t)(1u<<tr);
    fm1_irq_off();t->eng_req=ENGI_PROPHET;patch_set(tr,ENGI_PROPHET,(const uint8_t *)patch,sizeof *patch,MOD3_PATCH_SET);
    t->preset=t->user=t->user_native=0;
    for(uint32_t i=0;i<8u;i++)t->p[P_E0+i]=ENGINES[ENGI_PROPHET]->edit[i].def;
    p5_track_accept(t);fm1_irq_on();load_end(t);sync_reload=1;ui.force=1;
}
/* TEMPORARY (phase 4: the editor's Prophet command) */
/* the Prophet's panel byte id as the editor's op 95 2 sets it (the raw byte Melodee 1.x's panel stored: its schema's
 * range, the bytes its pages compute from as they are stored) */
static int p4_p5_raw_range(uint32_t id, int32_t *lo, int32_t *hi)
{
    sch_param_t p;
    if (id >= 88u || sch_param(ENGI_PROPHET, id, &p))
        return 0;
    *lo = p.kind == MOD3_SK_FIELD ? p.min : id == P5_UNISON_COUNT ? 1 : 0;
    *hi = p.kind == MOD3_SK_FIELD ? p.max : id == P5_UNISON_COUNT ? 5 : id == P5_BEND ? 11 : 127;
    return 1;
}
static int ed_prophet_handle(uint32_t cmd,const uint8_t *a,uint32_t n)
{
    if(cmd!=95u)return 0;
    uint32_t op=ed_arg(a,n,0),tr=ed_arg(a,n,1),rc=n<2u||tr>=NPART||op>4u;
    p5_patch_t patch;uint8_t frame[P5_FRAME_MAX];uint32_t size=0;
    if(!rc){
        if(op==0u){if(n!=2u)rc=1;else size=p5_patch_encode(p5_prog(tr),frame,sizeof frame);}
        else if(op==1u){
            if(n>sizeof frame || n<6u)rc=1;
            else {frame[0]=0xF0;memcpy(frame+1,a+2,n-2u);frame[n-1u]=0xF7;if(!p5_patch_decode(&patch,frame,n))rc=1;else p5_accept_patch(tr,&patch);}
        }else if(op==2u){
            int32_t lo,hi;
            if(n!=4u || !p4_p5_raw_range(a[2],&lo,&hi) || a[3]<lo || a[3]>hi || trk[tr].eng_req!=ENGI_PROPHET)rc=1;
            else {fm1_irq_off();patch_of(tr)[a[2]]=a[3];patch_note(tr,MOD3_PATCH_EDIT,a[2]);fm1_irq_on();ui.force=1;}
        }else if(op==3u){
            char name[P5_NAME_LEN+1];
            if(n>2u+P5_NAME_LEN || n<3u || trk[tr].eng_req!=ENGI_PROPHET || !ed_text(name,a+2,n-2u))rc=1;
            if(!rc){patch=*p5_prog(tr);memset(patch.raw+P5_NAME,' ',P5_NAME_LEN);memcpy(patch.raw+P5_NAME,a+2,n-2u);fm1_irq_off();*p5_prog(tr)=patch;fm1_irq_on();ui.force=1;}
        }else if(n!=2u)rc=1;else p5_send(tr);
    }
    ed_b(op==0u||op==1u?1u:op);ed_b(rc);ed_b(tr);
    if(!rc && op==0u)for(uint32_t j=1u;j+1u<size;j++)ed_b(frame[j]);
    return 1;
}
/* Native single/edit dumps load into RAM. Library storage is an explicit
 * editor/panel operation. Requests use Sequential framing, not editor framing. */
static int p5_native_frame(const uint8_t *body,uint32_t n)
{
    if(n<3u || body[0]!=1u || (body[1]!=0x31u && body[1]!=0x32u))return 0;
    uint32_t tr=song.sel%NTRK;uint8_t frame[P5_FRAME_MAX];p5_patch_t patch;
    if(body[2]==2u || body[2]==3u){
        if(n+2u>sizeof frame)return 1;
        frame[0]=0xF0;memcpy(frame+1,body,n);frame[n+1u]=0xF7;
        if(p5_patch_decode(&patch,frame,n+2u)){sysex_stage(ENGI_PROPHET,(const uint8_t *)&patch,1,sizeof patch);}
        else ui_message("INVALID PROPHET");
    }else if(body[2]==6u && n==3u){p5_send(tr);}
    else if(body[2]==5u && n==5u){
        uint32_t slot=body[3]*40u+body[4];
        if(body[3]<=9u && body[4]<=39u && slot<P5_USER_SLOTS && !p5_user_get(slot,&patch)){
            patch.model=body[1];patch.command=2;patch.group=body[3];patch.program=body[4];
            uint32_t size=p5_patch_encode(&patch,frame,sizeof frame);if(size)ota_wire_send(frame,size);
        }
    }
    return 1;
}
