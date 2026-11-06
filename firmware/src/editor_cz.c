/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Full native Casio tone over the editor transport, nibble-packed, low first.
 * 75 GET / 76 PUT. target 0: track; target 1: ordinary user preset slot.
 * Native slots use UP_VER_CZ, so their existing A/B store is atomic. */
enum { ED_CZ_GET = 75, ED_CZ_PUT = 76, ED_CZ_BANK = 77 };
static int ed_cz_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if(cmd==ED_CZ_BANK){
        uint32_t k=ed_arg(a,n,0);cz_bank_t *b=n==1?cz_bank_load(k):0;ed_b(k);ed_b(b?0:1);
        if(b){ed_str(b->name,16);for(uint32_t i=0;i<16;i++){ed_b((b->used>>i)&1u);char name[17]={0};if((b->used>>i)&1u)ed_shown(name,b->tone[i].raw+128,16u);ed_str(name,16);}}
        return 1;
    }
    if (cmd != ED_CZ_GET && cmd != ED_CZ_PUT) return 0;
    uint32_t target = ed_arg(a, n, 0), index = ed_arg(a, n, 1), rc = 0;
    uint8_t raw[CZ_BYTES];
    if (target > 2u || index >= (target == 2u ? 128u : target ? UP_SLOTS : NPART) || (target==2u && cmd==ED_CZ_PUT)) rc = 1;
    if (!rc && cmd == ED_CZ_GET) {
        if (n != 2u) rc = 1;
        else if (target == 2u) rc = cz_bank_get(index/16u,index%16u,raw) ? 2u : 0u;
        else if (!target) memcpy(raw, cz_patch_of(index)->raw, CZ_BYTES);
        else if (up_used(index) && up_cz_raw(up_rec(index),raw)) { /* complete native tone */ }
        else rc = 2;
    }
    if (!rc && cmd == ED_CZ_PUT) {
        if (n != 2u + 2u*CZ_BYTES && n != 2u + 2u*CZ_BYTES + 32u) rc = 1;
        if (!rc && (!ed_unnibble(raw, a + 2, CZ_BYTES) || !cz_patch_valid(raw))) rc = 1;
        if (!rc && target) {
            up_rec_t r; memset(&r,0,sizeof r);
            r.used=UP_USED; r.ver=UP_VER_CZ; r.engine=ENGI_CZ; r.np=P_COUNT;
            memcpy(r.packed,raw,CZ_BYTES);
            char name[13];
            ed_shown(name,raw+128u,12u); up_set_name(&r,index,name);
            if (n > 2u+2u*CZ_BYTES) for (uint32_t i=0;i<16u;i++) {
                r.note[i]=a[2u+2u*CZ_BYTES+2u*i]; r.flags[i]=a[3u+2u*CZ_BYTES+2u*i];
                up_pat_norm(&r.note[i],&r.flags[i]);
            }
            int result=ed_flash_stop() ? 2 : up_put(index,&r);
            rc=result==1 ? 1u : result==2 ? 2u : 0u;
        } else if (!rc) {
            track_t *t=&trk[index];
            load_begin(t,UNDO_SOUND);
            panic_req |= (uint8_t)(1u<<index);
            fm1_irq_off();
            memcpy(cz_patch_of(index)->raw,raw,CZ_BYTES); t->eng_req=ENGI_CZ;
            for (uint32_t i=P_E0;i<P_COUNT;i++) t->p[i]=param_desc_of(ENGI_CZ,i)->def;
            t->p[P_E7]=CZ_NATIVE; t->preset=0; t->user=0; t->user_native=0; cz_track_accept(t);
            fm1_irq_on();
            load_end(t); sync_reload=1; ui.force=1;
        }
    }
    ed_b(target); ed_b(index); ed_b(rc);
    if (cmd == ED_CZ_GET && !rc) ed_nibbles(raw, CZ_BYTES);
    return 1;
}
