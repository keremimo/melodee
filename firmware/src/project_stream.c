/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A decoded project and either its serialized active record or its inactive
 * bank tail fit the shared canvas. They are alternative views, never peers.
 * Freeze decoded state + tail, write that tail first, then reuse its RAM for
 * the active record. Only the final validated flash header publishes a save. */
_Static_assert(sizeof(project_t)+sizeof(project_store_t)<=CV_WORK_BYTES,"active project codec fits canvas work");
_Static_assert(sizeof(project_t)+BANK_TAIL_SIZE<=CV_WORK_BYTES,"decoded project and bank tail fit canvas work");
static uint8_t *proj_image_work(void){return (uint8_t *)cv_work()+sizeof(project_t);}
static uint8_t *proj_image_tail(void){return proj_image_work();}
static project_store_t *proj_image_store(void){return (project_store_t *)proj_image_work();}
static uint32_t proj_source_hash;

/* The same representable ranges as proj_pack, without allocating its output.
 * This preflights generated images and track transforms before flash/runtime
 * mutation. The existing full codec remains the independent host oracle. */
static int proj_image_valid(const project_t *q)
{
    int8_t oct[PROJ_TRK];uint8_t step[8];
    if(q->sel>=NTRK||q->parts>NTRK||q->phys>PROJ_PHYS)return 0;
    if(P_COUNT>127u||!chain_legacy_valid(&q->chain)||!motion_valid(&q->motion)||q->g[G_FXRETURN]<0||q->g[G_FXRETURN]>127||q->g[G_FXMODE]<0||q->g[G_FXMODE]>1)return 0;
    for(uint32_t i=0;i<FX_SLOTS*FX_PARAMS;i++)if(q->g[G_MFXTYPE+i]<GP[G_MFXTYPE+i].min||q->g[G_MFXTYPE+i]>GP[G_MFXTYPE+i].max)return 0;
    for(uint32_t i=0;i<FX_SLOTS;i++)if(q->g[G_MFXTONE+i]<-FX_TONE_MAX||q->g[G_MFXTONE+i]>FX_TONE_MAX)return 0;
    for(uint32_t k=0;k<PROJ_TRK;k++){
        oct[k]=q->t[k].mpc_octave;
        if(q->t[k].engine>=NENGINES||q->pattern[k]>=NPAT||!cz_patch_valid(q->cz[k].raw)||!p5_patch_valid(&q->p5[k])||!drum_patch_valid(&q->drum[k]))return 0;
        for(uint32_t i=0;i<FM6_PACKED;i++)if(q->fm6[k][i]>127u)return 0;
        for(uint32_t i=0;i<P_COUNT;i++)if(q->t[k].p[i]<-64||q->t[k].p[i]>127)return 0;
        for(uint32_t i=0;i<NSTEP;i++)if(!proj_step_pack(step,&q->t[k].step[i]))return 0;
    }
    if(!mpc_octaves_pack(oct))return 0;
    for(uint32_t i=0;i<RECORD_MAX;i++)if(!recording_valid(&q->recording[i])||(q->recording[i].vel&&(!proj_owner_ok(recorded_owner(&q->recording[i]))||recorded_step(&q->recording[i])>=64u)))return 0;
    return 68u+PROJ_TRK*(P_COUNT+2u+NSTEP*8u)+sizeof q->chain+sizeof q->motion+2u+2u*PROJ_TRK<=PROJ_MPC_OCT_WIRE;
}
static int proj_image_capture(int runtime)
{
    project_t *q=&proj_scratch;uint32_t f=motion_guard();
    project_capture(q);
    int ok=bank_pack_tail(proj_image_tail(),q,runtime);
    if(!ok&&bank_too_full&&runtime&&bank_v1_fits()){          /* a dense project of before 16 patterns: as FBKK */
        bank_force_v1=1;ok=bank_pack_tail(proj_image_tail(),q,runtime)&&!bank_wide;bank_force_v1=0;
        if(!ok)bank_too_full=1;
    }
    motion_unguard(f);
    /* The captured RAM is private until this synchronous operation returns;
     * validation can run with audio interrupts enabled. */
    return ok&&proj_image_valid(q)&&bank_tail_valid(proj_image_tail());
}
/* Hash-only probes can consume the serialized record before making the tail.
 * They never write flash. A concurrent active-bank/timing change is treated as
 * dirty; a subsequent save captures one guarded, consistent image. */
static int proj_image_hash(int runtime,uint32_t *result)
{
    project_t *q=&proj_scratch;
    bank_capture_kind(runtime);
    if(!proj_pack(proj_image_store(),q))return 0;
    uint32_t header[2]={bank_magic(),BANK_STORE_SIZE};
    uint32_t hash=fnv(fnv(2166136261u,header,sizeof header),proj_image_store(),sizeof(project_store_t));
    if(!bank_pack_tail(proj_image_tail(),q,runtime)||!bank_tail_valid(proj_image_tail()))return 0;
    hash=fnv(hash,proj_image_tail(),BANK_TAIL_SIZE-4u);
    *result=fnv(hash,&hash,sizeof hash);return 1;
}
static int proj_stream_read_hash(uint32_t obj,uint32_t copy,uint32_t off,uint32_t n,uint32_t *fn,uint32_t *crc)
{
    wire_take();
    while(n){
        uint32_t k=n>sizeof proj_wire_u.raw?sizeof proj_wire_u.raw:n;
        if(st_read_range(obj,copy,off,proj_wire_u.raw,k))return -1;
        if(fn)*fn=fnv(*fn,proj_wire_u.raw,k);
        if(crc)*crc=st_crc32_update(*crc,proj_wire_u.raw,k);
        off+=k;n-=k;
    }
    return 0;
}
/* Compact images (PJ1): the 27,904-byte record with its empty 256-byte pages left out, so a project occupies what
 * it holds. Two regions, each paged from its own start: the bank tail T [BANK_ACTIVE_OFF, end) (map bits 0..57)
 * and the header + active record S [0, BANK_ACTIVE_OFF) (bits 64..115). Stored: the 32-byte head below, T's present
 * pages, then S's, each region's last page short. A raw record (FBK magic first) is read as it is. */
#define PJ_MAGIC 0x314A5048u                     /* HPJ1 */
#define PJ_HEAD 32u
#define PJ_TPAGES ((BANK_TAIL_SIZE + 255u) / 256u)
#define PJ_SPAGES ((BANK_ACTIVE_OFF + 255u) / 256u)
_Static_assert(PJ_TPAGES <= 64u && PJ_SPAGES <= 64u, "one 64-bit map a region");
typedef struct { uint32_t magic, len, map[4], rsv[2]; } pj_head_t;
_Static_assert(sizeof(pj_head_t) == PJ_HEAD, "compact image head");
static struct { pj_head_t h; uint8_t on; } pj __attribute__((section(".pool")));   /* the image proj_stream_load reads */
static int pj_bit(const pj_head_t *h, uint32_t b) { return (int)((h->map[b / 32u] >> (b % 32u)) & 1u); }
static uint32_t pj_rank(const pj_head_t *h, uint32_t from, uint32_t to)
{
    uint32_t n = 0;
    for (uint32_t b = from; b < to; b++) n += (uint32_t)pj_bit(h, b);
    return n;
}
static uint32_t pj_tbytes(const pj_head_t *h)
{
    uint32_t n = 256u * pj_rank(h, 0, PJ_TPAGES);
    return pj_bit(h, PJ_TPAGES - 1u) ? n - (PJ_TPAGES * 256u - BANK_TAIL_SIZE) : n;
}
/* logical offset -> stored offset (1) or a hole (0, zeros); *span: the bytes to its page's end */
static int pj_where(const pj_head_t *h, uint32_t off, uint32_t *at, uint32_t *span)
{
    uint32_t tail = off >= BANK_ACTIVE_OFF, rel = tail ? off - BANK_ACTIVE_OFF : off, size = tail ? BANK_TAIL_SIZE : BANK_ACTIVE_OFF;
    uint32_t page = rel / 256u, bit = tail ? page : 64u + page, end = (page + 1u) * 256u;
    *span = (end < size ? end : size) - rel;
    if (!pj_bit(h, bit)) return 0;
    *at = (tail ? PJ_HEAD : PJ_HEAD + pj_tbytes(h)) + 256u * pj_rank(h, tail ? 0u : 64u, bit) + rel % 256u;
    return 1;
}
/* the record's logical bytes, of a compact image (pj.on) or a raw one */
static int pj_read(uint32_t obj, uint32_t copy, uint32_t off, void *dst, uint32_t n)
{
    if (!pj.on) return st_read_range(obj, copy, off, dst, n);
    uint8_t *d = dst;
    while (n) {
        uint32_t at, span, k;
        int here = pj_where(&pj.h, off, &at, &span);
        k = n < span ? n : span;
        if (here) { if (st_read_range(obj, copy, at, d, k)) return -1; }
        else memset(d, 0, k);
        d += k; off += k; n -= k;
    }
    return 0;
}
static int pj_zero(const uint8_t *p, uint32_t n) { while (n--) if (*p++) return 0; return 1; }
/* written bytes read back against RAM */
static int pj_same(st_stream_t *w, uint32_t at, const uint8_t *ram, uint32_t n)
{
    uint8_t b[32];
    for (uint32_t i = 0; i < n; i += sizeof b) {
        uint32_t k = n - i < sizeof b ? n - i : sizeof b;
        if (st_stream_read(w, at + i, b, k) || memcmp(b, ram + i, k)) return 0;
    }
    return 1;
}
/* S's page p (header + active record) into b; its length */
static uint32_t pj_spage(uint8_t *b, uint32_t p, const uint32_t header[2])
{
    uint32_t rel = p * 256u, k = BANK_ACTIVE_OFF - rel < 256u ? BANK_ACTIVE_OFF - rel : 256u;
    for (uint32_t i = 0; i < k; i++)
        b[i] = rel + i < 8u ? ((const uint8_t *)header)[rel + i] : ((const uint8_t *)proj_image_store())[rel + i - 8u];
    return k;
}
/* The decoded project (proj_scratch) and its bank tail (proj_image_tail) -> a compact image in w (staged, not
 * published). Writes T while its RAM holds it, then packs S into that RAM; the hash (the tail's last word) goes
 * last, over the header, the record and the tail as stored. */
static int proj_stream_generate(st_stream_t *w,uint32_t obj)
{
    project_t *q=&proj_scratch;uint8_t *tail=proj_image_tail(),b[256];
    if(!proj_image_valid(q)||!bank_tail_valid(tail))return -1;
    pj_head_t h;memset(&h,0,sizeof h);h.magic=PJ_MAGIC;h.len=BANK_STORE_SIZE;
    int rc=st_stream_begin_lazy(w,obj);if(rc)return rc;
    uint32_t at=PJ_HEAD,hash_at=0;
    for(uint32_t p=0;p<PJ_TPAGES;p++){
        uint32_t rel=p*256u,k=BANK_TAIL_SIZE-rel<256u?BANK_TAIL_SIZE-rel:256u,last=p+1u==PJ_TPAGES,body=last?k-4u:k;
        if(!last&&pj_zero(tail+rel,k))continue;
        h.map[p/32u]|=1u<<(p%32u);
        if((rc=st_stream_write_at(w,at,tail+rel,body)))return rc;
        if(!pj_same(w,at,tail+rel,body))return -7;
        if(last)hash_at=at+body;
        at+=k;
    }
    if(!proj_pack(proj_image_store(),q))return -1;
    uint32_t header[2]={bank_magic(),BANK_STORE_SIZE};
    uint32_t hash=fnv(fnv(2166136261u,header,sizeof header),proj_image_store(),sizeof(project_store_t));
    for(uint32_t p=0;p<PJ_SPAGES;p++){
        uint32_t k=pj_spage(b,p,header);
        if(pj_zero(b,k))continue;
        h.map[2u+p/32u]|=1u<<(p%32u);
        if((rc=st_stream_write_at(w,at,b,k)))return rc;
        if(!pj_same(w,at,b,k))return -7;
        at+=k;
    }
    for(uint32_t off=0;off<BANK_TAIL_SIZE-4u;){              /* the tail as stored, its holes zeros */
        uint32_t where,span,k;
        int here=pj_where(&h,BANK_ACTIVE_OFF+off,&where,&span);
        k=BANK_TAIL_SIZE-4u-off<span?BANK_TAIL_SIZE-4u-off:span;if(k>sizeof b)k=sizeof b;
        if(here){if(st_stream_read(w,where,b,k))return -7;}else memset(b,0,k);
        hash=fnv(hash,b,k);off+=k;
    }
    if((rc=st_stream_write_at(w,hash_at,&hash,4u))||(rc=st_stream_write_at(w,0,&h,sizeof h))||(rc=st_stream_end(w,at)))return rc;
    uint32_t crc=0xffffffffu;
    for(uint32_t off=0;off<at;off+=sizeof b){
        uint32_t k=at-off<sizeof b?at-off:sizeof b;
        if(st_read_range(obj,w->head.slot,off,b,k))return -7;
        crc=st_crc32_update(crc,b,k);
    }
    st_stream_seal(w,crc);
    proj_source_hash=fnv(hash,&hash,4u);
    return st_stream_check(w);
}
static int proj_stream_load(uint32_t obj,uint32_t copy,uint32_t len)
{
    project_t *q=&proj_scratch;uint32_t hash=2166136261u;
    proj_source_hash=0;bank_wide=0;bank_p16=0;bank_patterns=0;bank_song64=0;bank_songfx=0;bank_sounds=0;bank_sound_bytes=0;
    pj.on=0;
    if(len>PJ_HEAD&&len<=BANK_STORE_SIZE+PJ_HEAD&&!st_read_range(obj,copy,0,&pj.h,PJ_HEAD)&&pj.h.magic==PJ_MAGIC&&
       pj.h.len==BANK_STORE_SIZE){pj.on=1;len=BANK_STORE_SIZE;}
    if(bank_full(len)){
        uint32_t header[2];
        if(pj_read(obj,copy,0,header,sizeof header)||(header[0]!=BANK_MAGIC&&header[0]!=BANKL_MAGIC&&header[0]!=BANKM_MAGIC&&header[0]!=BANKN_MAGIC&&header[0]!=BANKO_MAGIC&&header[0]!=BANKP_MAGIC&&header[0]!=BANKQ_MAGIC)||header[1]!=len)return 0;
        bank_patterns=(uint8_t)(header[0]==BANKQ_MAGIC);
        bank_p16=(uint8_t)(header[0]==BANKP_MAGIC || bank_patterns);
        bank_sounds=(uint8_t)(header[0]==BANKO_MAGIC);
        bank_songfx=(uint8_t)(header[0]==BANKN_MAGIC || bank_sounds || bank_p16);
        bank_song64=(uint8_t)(header[0]==BANKM_MAGIC || bank_songfx);
        bank_wide=(uint8_t)(header[0]!=BANK_MAGIC);
        if(bank_wide&&pj_read(obj,copy,BL.rec,proj_rec_ext,BL.recn))return 0;
        if(pj_read(obj,copy,8u,proj_image_store(),sizeof(project_store_t)))return 0;
        hash=fnv(fnv(hash,header,sizeof header),proj_image_store(),sizeof(project_store_t));
        if(!proj_import(q,proj_image_store(),sizeof(project_store_t)))return 0;
        if(pj_read(obj,copy,BANK_ACTIVE_OFF,proj_image_tail(),BANK_TAIL_SIZE))return 0;
        hash=fnv(hash,proj_image_tail(),BANK_TAIL_SIZE-4u);uint32_t sum;
        memcpy(&sum,proj_image_tail()+BANK_TAIL_SIZE-4u,4);
        if(sum!=hash||!bank_tail_valid(proj_image_tail()))return 0;
        proj_source_hash=fnv(hash,&sum,4u);return 1;
    }
    if(len==sizeof(project_store_t)){
        if(st_read_range(obj,copy,0,proj_image_store(),len))return 0;
        return proj_import(q,proj_image_store(),(int)len);
    }
    if(len==sizeof *q){
        if(st_read_range(obj,copy,0,q,len))return 0;
        return proj_ok(q);
    }
    /* Prior raw snapshots: expand/convert each ten-byte note without ever
     * holding the complete old and new layouts at once. */
    if(len==sizeof *q+RECORD_MAX){
        uint32_t sum,head[2];
        if(st_read_range(obj,copy,0,head,sizeof head)||head[0]!=PROJ_MAGIC||head[1]!=len||
           st_read_range(obj,copy,len-4u,&sum,4u)||proj_stream_read_hash(obj,copy,0,len-4u,&hash,0)||hash!=sum)return 0;
        uint32_t at=__builtin_offsetof(project_t,recording),end=__builtin_offsetof(project_t,pattern);
        if(st_read_range(obj,copy,0,q,at))return 0;
        for(uint32_t i=0;i<RECORD_MAX;i++){
            uint8_t r[10];if(st_read_range(obj,copy,at+i*10u,r,sizeof r))return 0;
            recorded_note_t v={0};
            if(r[5]){
                if(r[4]>127u||r[5]>127u||(int8_t)r[8]<-24||(int8_t)r[8]>24||r[9]>=8u)return 0;
                v=(recorded_note_t){.on=(uint16_t)(r[0]|r[1]<<8),.duration=(uint16_t)(r[2]|r[3]<<8),.note7=r[4],.vel=r[5],.owner=r[6],.step=r[7],.pitch=(int8_t)r[8],.length=r[9]&1u,.extended=r[9]>>1};
            }
            q->recording[i]=v;
        }
        if(st_read_range(obj,copy,at+RECORD_MAX*10u,(uint8_t *)q+end,sizeof *q-end))return 0;
        q->size=sizeof *q;q->sum=proj_sum(q);return proj_ok(q);
    }
    return 0;
}
