/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Engine-specific user preset slots. Command 78: engine, operation, slot/start,
 * then operation arguments. Replies begin engine, operation, slot/start, rc.
 * Native tones are VMEM (128 x 7-bit) or CZ-1 (144 low-first nibble pairs). */
#define ED_NATIVE 78u
static int ed_native_handle(uint32_t cmd,const uint8_t *a,uint32_t n)
{
    if(cmd!=ED_NATIVE)return 0;
    uint32_t e=ed_arg(a,n,0),op=ed_arg(a,n,1),k=ed_arg(a,n,2),rc=n<3u || !native_limit(e) || op>7u || k>=(op==7u?NTRK:native_limit(e));
    uint32_t limit=e==ENGI_FM6?64u:128u;
    if(op!=7u && k>=limit)rc=1;
    if(!rc && (op==2u || op==3u || op==5u) && up_used(k) && up_engine(k)!=e)rc=1;
    uint8_t raw[CZ_BYTES];
    if(!rc){
        if(op==0u){if(n!=4u || !a[3] || a[3]>16u)rc=1;}
        else if(op==1u){if(n!=3u)rc=1;else if(!native_used(e,k))rc=2;}
        else if(op==2u){
            if(n!=3u+(e==ENGI_FM6?FM6_PACKED:2u*native_size(e)))rc=1;
            if(!rc){if(e==ENGI_FM6)memcpy(raw,a+3,FM6_PACKED);else if(!ed_unnibble(raw,a+3,native_size(e)))rc=1;}
            if(!rc)rc=ed_flash_stop()?3u:(uint32_t)native_put(e,k,raw);
        }else if(op==3u || op==4u){
            if(n!=4u || a[3]>=NPART)rc=1;
            else if(op==4u)rc=(uint32_t)native_load(e,k,a[3]);
            else rc=ed_flash_stop()?3u:(uint32_t)native_store(e,k,a[3],0);
        }else if(op==5u){if(n!=3u)rc=1;else rc=ed_flash_stop()?3u:(uint32_t)native_put(e,k,0);}
        else if(op==7u){if(n!=3u)rc=1;}
        else {
            uint32_t len=e==ENGI_PROPHET?20u:e==ENGI_FM6?10u:16u;char name[21];
            if(n<4u || n>3u+len || !native_used(e,k) || !ed_text(name,a+3,n-3u))rc=1;
            else rc=ed_flash_stop()?3u:(uint32_t)up_rename(k,name);
        }
    }
    if(rc==3u && !transport_busy())rc=0; /* RAM-only writes use the normal editor success convention. */
    ed_b(e);ed_b(op);ed_b(k);ed_b(rc);
    if(!rc && op==7u){uint32_t u=trk[k].eng_req==e && !trk[k].user_native && user_of(&trk[k])<USER_NONE?trk[k].user:0u;ed_u14(u);}
    if(!rc && op==0u){uint32_t count=ed_page(k,a[3],limit);ed_b(count);ed_u14(limit);for(uint32_t j=k;j<k+count;j++){char name[13];ed_b(native_used(e,j));if(native_used(e,j))native_name(e,j,name);else name[0]=0;ed_str(name,12);}}
    if(!rc && op==1u){const uint8_t *r=native_raw(e,k);if(e!=ENGI_FM6)ed_nibbles(r,native_size(e));else ed_bytes(r,native_size(e));}
    return 1;
}
