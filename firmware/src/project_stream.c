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
        if(q->t[k].engine>=NENGINES||q->pattern[k]>=NPAT||!p4_store_valid(MOD_SCH_STORE_TONE,q->cz[k])||
           !p4_store_valid(MOD_SCH_STORE_PROGRAM,q->p5[k])||!p4_store_valid(MOD_SCH_STORE_KIT,q->drum[k]))return 0;
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
/* Packed images (HPJ2; every save writes one): the same record in two blocks, each as lz_unpack reads it (dsp.c: c <
 * 0x80: c + 1 bytes follow; else (c & 0x7F) + 3 bytes copied from d + 1 back, d in the next two bytes), or as it is
 * when packing does not make it smaller. Stored: the 32-byte head (pj_head_t: map[0] the record's first word, its FBK
 * magic; map[1] its last, the sum; map[2] and map[3] the blocks' stored bytes, | PZ_RAW: as it is), the bank tail
 * without its sum, then the FUN record. Each block is whole on its own: there is RAM for one at a time.
 * Compact images stay readable (pj_read); nothing writes them any more. */
#define PZ_MAGIC 0x324A5048u                     /* HPJ2 */
#define PZ_RAW 0x80000000u
#define PZ_TAIL (BANK_TAIL_SIZE - 4u)            /* the tail block's bytes */
#define PZ_HBITS 9u                              /* the packer's table: for each three bytes (hashed), the last */
#define PZ_WAYS 4u                               /* PZ_WAYS places where they began, the newest first */
_Static_assert((2u * PZ_WAYS << PZ_HBITS) <= sizeof proj_wire_u.raw && BANK_TAIL_SIZE < 0xFFFFu && sizeof(project_store_t) < 0xFFFFu,
               "the packer's table in the main loop's scratch, a place there in 16 bits");
/* a block's stored bytes on their way into a stage (w 0: only counted) */
typedef struct { st_stream_t *w; uint32_t at, k; int rc; uint8_t b[256]; } pz_out_t;
static void pz_flush(pz_out_t *o)
{
    if (o->k && o->w && !o->rc) o->rc = st_stream_write_at(o->w, o->at, o->b, o->k);
    o->at += o->k; o->k = 0;
}
static void pz_put(pz_out_t *o, uint32_t v)
{
    o->b[o->k++] = (uint8_t)v;
    if (o->k == sizeof o->b) pz_flush(o);
}
static void pz_lits(pz_out_t *o, const uint8_t *end, uint32_t lit)   /* the lit bytes before end, as they are */
{
    if (lit) pz_put(o, lit - 1u);
    for (; lit; lit--) pz_put(o, end[-(int32_t)lit]);
}
static uint16_t *pz_places(const uint8_t *s, uint16_t *head)   /* the places where these three bytes began */
{
    return head + PZ_WAYS * (((s[0] | (uint32_t)s[1] << 8 | (uint32_t)s[2] << 16) * 2654435761u) >> (32u - PZ_HBITS));
}
/* the longest run (130 at most) that s[i..n) shares with one begun at a place of its three bytes; *from: that place */
static uint32_t pz_find(const uint8_t *s, uint32_t n, uint32_t i, uint16_t *head, uint32_t *from)
{
    uint32_t best = 0;
    if (i + 3u > n) return 0;
    const uint16_t *b = pz_places(s + i, head);
    for (uint32_t w = 0; w < PZ_WAYS; w++) {
        uint32_t c = b[w], len = 0;
        if (c >= i) continue;                    /* (0xFFFF: none) */
        while (len < 130u && i + len < n && s[c + len] == s[i + len]) len++;
        if (len > best) { best = len; *from = c; }
    }
    return best;
}
static void pz_note(const uint8_t *s, uint32_t n, uint32_t i, uint16_t *head)   /* i: its three bytes' newest place */
{
    if (i + 3u > n) return;
    uint16_t *b = pz_places(s + i, head);
    for (uint32_t w = PZ_WAYS - 1u; w; w--) b[w] = b[w - 1u];
    b[0] = (uint16_t)i;
}
/* s[0..n) packed into o: at each place the longest run found there, unless a longer one begins at the next byte */
static void pz_pack(pz_out_t *o, const uint8_t *s, uint32_t n, uint16_t *head)
{
    uint32_t i = 0, lit = 0;                     /* lit: the bytes before i still to go out as they are */
    memset(head, 0xFF, 2u * PZ_WAYS << PZ_HBITS);
    while (i < n) {
        uint32_t c = 0, next, len = pz_find(s, n, i, head, &c);
        pz_note(s, n, i, head);
        if (len >= 3u && pz_find(s, n, i + 1u, head, &next) > len) len = 0;
        if (len >= 3u) {
            pz_lits(o, s + i, lit); lit = 0;
            pz_put(o, 0x80u | (len - 3u)); pz_put(o, i - c - 1u); pz_put(o, (i - c - 1u) >> 8);
            for (len--, i++; len; len--, i++) pz_note(s, n, i, head);
        } else {
            i++;
            if (++lit == 128u) { pz_lits(o, s + i, lit); lit = 0; }
        }
    }
    pz_lits(o, s + i, lit);
    pz_flush(o);
}
/* a block's stored bytes [at, end) of a stage (w) or of a copy, read a few at a time */
typedef struct { st_stream_t *w; uint32_t obj, copy, at, end, k, n; uint8_t b[32]; } pz_in_t;
static int pz_get(pz_in_t *in)                   /* the next one; -1: none */
{
    if (in->k == in->n) {
        uint32_t n = in->end - in->at < sizeof in->b ? in->end - in->at : sizeof in->b;
        if (!n || (in->w ? st_stream_read(in->w, in->at, in->b, n) : st_read_range(in->obj, in->copy, in->at, in->b, n))) return -1;
        in->at += n; in->n = n; in->k = 0;
    }
    return in->b[in->k++];
}
/* The block at in->at (field: its head word) -> its n bytes in o, or (same) checked against o: every length and
 * distance inside what is there, the stored bytes used to their last. 0: so, and in->at is the block after it */
static int pz_block(pz_in_t *in, uint32_t field, uint8_t *o, uint32_t n, int same)
{
    uint32_t k = 0, raw = field & PZ_RAW;
    in->end = in->at + (field & ~PZ_RAW); in->k = in->n = 0;
    while (k < n) {
        int c = raw ? (int)(n - k) : pz_get(in), d = 0;
        if (c < 0) return -1;
        if (!raw) {
            if (c < 0x80) c++;
            else {
                int lo = pz_get(in), hi = pz_get(in);
                if ((lo | hi) < 0) return -1;
                d = (lo | hi << 8) + 1; c = (c & 0x7F) + 3;
            }
        }
        if ((uint32_t)d > k || (uint32_t)c > n - k) return -1;
        for (; c; c--, k++) {
            int v = d ? o[k - (uint32_t)d] : pz_get(in);
            if (v < 0 || (same && o[k] != v)) return -1;
            o[k] = (uint8_t)v;
        }
    }
    return in->k == in->n && in->at == in->end ? 0 : -1;
}
/* a block's head word: its stored bytes packed, or (| PZ_RAW) as it is when packing does not make it smaller */
static uint32_t pz_size(const uint8_t *s, uint32_t n)
{
    pz_out_t o;
    memset(&o, 0, sizeof o);
    pz_pack(&o, s, n, (uint16_t *)(void *)proj_wire_u.raw);   /* (counted) */
    return o.at < n ? o.at : n | PZ_RAW;
}
/* A block (field: pz_size's word for it) into the stage at *at, then read from the stage as the loader will read
 * it, against the RAM it came from. 0 */
static int pz_store(st_stream_t *w, uint32_t *at, uint8_t *s, uint32_t n, uint32_t field)
{
    pz_out_t o;
    pz_in_t in;
    memset(&o, 0, sizeof o); memset(&in, 0, sizeof in);
    o.w = w; o.at = *at;
    if (field & PZ_RAW) o.rc = st_stream_write_at(w, *at, s, n);
    else pz_pack(&o, s, n, (uint16_t *)(void *)proj_wire_u.raw);
    if (o.rc) return o.rc;
    in.w = w; in.at = *at;
    if (pz_block(&in, field, s, n, 1)) return -7;
    *at = in.at;
    return 0;
}
/* the head, the image's length and its CRC, read back: the stage ready to publish. 0 */
static int pz_close(st_stream_t *w, uint32_t obj, const pj_head_t *h, uint32_t at)
{
    uint8_t b[64];
    uint32_t crc = 0xffffffffu;
    int rc;
    if ((rc = st_stream_write_at(w, 0, h, sizeof *h)) || (rc = st_stream_end(w, at))) return rc;
    for (uint32_t off = 0; off < at; off += sizeof b) {
        uint32_t k = at - off < sizeof b ? at - off : sizeof b;
        if (st_read_range(obj, w->head.slot, off, b, k)) return -7;
        crc = st_crc32_update(crc, b, k);
    }
    st_stream_seal(w, crc);
    return st_stream_check(w);
}
/* The decoded project (proj_scratch) and its bank tail (proj_image_tail) -> a packed image in w (staged, not
 * published). The tail goes while its RAM holds it, then the FUN record is packed into that RAM and goes; the
 * record's sum covers the header, the FUN record and the tail in that order, so the tail comes back from the stage
 * for it. The packer's table is the main loop's scratch (wire_take: a DX7 frame there is dropped, as any job's). */
static int proj_stream_generate(st_stream_t *w,uint32_t obj)
{
    project_t *q=&proj_scratch;uint8_t *tail=proj_image_tail();
    if(!proj_image_valid(q)||!bank_tail_valid(tail))return -1;
    pj_head_t h;memset(&h,0,sizeof h);h.magic=PZ_MAGIC;h.len=BANK_STORE_SIZE;
    wire_take();
    int rc=st_stream_begin_lazy(w,obj);if(rc)return rc;
    uint32_t at=PJ_HEAD;
    h.map[2]=pz_size(tail,PZ_TAIL);
    if((rc=pz_store(w,&at,tail,PZ_TAIL,h.map[2])))return rc;
    if(!proj_pack(proj_image_store(),q))return -1;
    uint32_t header[2]={bank_magic(),BANK_STORE_SIZE};
    uint32_t hash=fnv(fnv(2166136261u,header,sizeof header),proj_image_store(),sizeof(project_store_t));
    h.map[3]=pz_size((const uint8_t *)proj_image_store(),sizeof(project_store_t));
    if((rc=pz_store(w,&at,(uint8_t *)proj_image_store(),sizeof(project_store_t),h.map[3])))return rc;
    pz_in_t in;memset(&in,0,sizeof in);in.w=w;in.at=PJ_HEAD;
    if(pz_block(&in,h.map[2],tail,PZ_TAIL,0))return -7;
    hash=fnv(hash,tail,PZ_TAIL);
    memcpy(tail+PZ_TAIL,&hash,4u);
    h.map[0]=header[0];h.map[1]=hash;
    proj_source_hash=fnv(hash,&hash,4u);
    return pz_close(w,obj,&h,at);
}
/* The record of a copy stored compact or raw (the one proj_stream_load read last: pj tells where its bytes are)
 * -> the same record packed, staged in w: not a byte of the record itself changes, so its sum, its name and the
 * music are what they were. Both blocks are sized before anything is written: -5 when the pool has no room for the
 * new copy beside the old one (nothing erased then). */
static int proj_stream_repack(st_stream_t *w,uint32_t obj,uint32_t copy)
{
    uint8_t *ram=proj_image_work();
    pj_head_t h;memset(&h,0,sizeof h);h.magic=PZ_MAGIC;h.len=BANK_STORE_SIZE;
    uint32_t at=PJ_HEAD,room=0;int rc;
    wire_take();
    if(pj_read(obj,copy,0,&h.map[0],4u)||pj_read(obj,copy,BANK_STORE_SIZE-4u,&h.map[1],4u)||
       pj_read(obj,copy,BANK_ACTIVE_OFF,ram,PZ_TAIL))return -7;
    h.map[2]=pz_size(ram,PZ_TAIL);
    if(pj_read(obj,copy,8u,ram,sizeof(project_store_t)))return -7;
    h.map[3]=pz_size(ram,sizeof(project_store_t));
    if((rc=st_stream_begin_lazy(w,obj)))return rc;
    for(uint32_t i=0;i<ST_LN;i++)room+=(w->avail[i/32u]>>(i%32u))&1u;
    at+=h.map[2]&~PZ_RAW;                          /* (the FUN record is in RAM: it goes first, to its place) */
    if(room*ST_LIN_PAYLOAD_MAX<at+(h.map[3]&~PZ_RAW))return -5;
    if((rc=pz_store(w,&at,ram,sizeof(project_store_t),h.map[3])))return rc;
    room=PJ_HEAD;
    if(pj_read(obj,copy,BANK_ACTIVE_OFF,ram,PZ_TAIL))return -7;
    if((rc=pz_store(w,&room,ram,PZ_TAIL,h.map[2])))return rc;
    return pz_close(w,obj,&h,at);
}
#if !defined(FM1_IRQ_TARGET)
/* (host: a stored copy's record whole, whichever way it is stored, into dst[BANK_STORE_SIZE]; 0) */
static int pj_image(uint32_t obj,uint32_t copy,uint8_t *dst)
{
    pj_head_t h,keep=pj.h;uint8_t on=pj.on;int rc;
    if(st_read_range(obj,copy,0,&h,PJ_HEAD))return -1;
    if(h.magic==PZ_MAGIC){
        pz_in_t in;memset(&in,0,sizeof in);in.obj=obj;in.copy=copy;in.at=PJ_HEAD;
        memcpy(dst,&h.map[0],4u);memcpy(dst+4u,&h.len,4u);memcpy(dst+BANK_STORE_SIZE-4u,&h.map[1],4u);
        return pz_block(&in,h.map[2],dst+BANK_ACTIVE_OFF,PZ_TAIL,0)||pz_block(&in,h.map[3],dst+8u,sizeof(project_store_t),0)?-1:0;
    }
    pj.h=h;pj.on=h.magic==PJ_MAGIC;
    rc=pj_read(obj,copy,0,dst,BANK_STORE_SIZE);
    pj.h=keep;pj.on=on;
    return rc;
}
#endif
static int proj_stream_load(uint32_t obj,uint32_t copy,uint32_t len)
{
    project_t *q=&proj_scratch;uint32_t hash=2166136261u;
    proj_source_hash=0;bank_wide=0;bank_p16=0;bank_patterns=0;bank_song64=0;bank_songfx=0;bank_sounds=0;bank_sound_bytes=0;
    pj.on=0;
    if(len>PJ_HEAD&&len<=BANK_STORE_SIZE+PJ_HEAD&&!st_read_range(obj,copy,0,&pj.h,PJ_HEAD)&&pj.h.len==BANK_STORE_SIZE){
        if(pj.h.magic==PJ_MAGIC)pj.on=1;
        else if(pj.h.magic==PZ_MAGIC&&PJ_HEAD+(pj.h.map[2]&~PZ_RAW)+(pj.h.map[3]&~PZ_RAW)==len)pj.on=2;   /* (packed) */
        if(pj.on)len=BANK_STORE_SIZE;
    }
    if(bank_full(len)){
        uint32_t header[2]={pj.h.map[0],len};uint8_t *tail=proj_image_tail();
        pz_in_t in;memset(&in,0,sizeof in);in.obj=obj;in.copy=copy;in.at=PJ_HEAD;
        if((pj.on!=2u&&pj_read(obj,copy,0,header,sizeof header))||(header[0]!=BANK_MAGIC&&header[0]!=BANKL_MAGIC&&header[0]!=BANKM_MAGIC&&header[0]!=BANKN_MAGIC&&header[0]!=BANKO_MAGIC&&header[0]!=BANKP_MAGIC&&header[0]!=BANKQ_MAGIC)||header[1]!=len)return 0;
        bank_patterns=(uint8_t)(header[0]==BANKQ_MAGIC);
        bank_p16=(uint8_t)(header[0]==BANKP_MAGIC || bank_patterns);
        bank_sounds=(uint8_t)(header[0]==BANKO_MAGIC);
        bank_songfx=(uint8_t)(header[0]==BANKN_MAGIC || bank_sounds || bank_p16);
        bank_song64=(uint8_t)(header[0]==BANKM_MAGIC || bank_songfx);
        bank_wide=(uint8_t)(header[0]!=BANK_MAGIC);
        if(pj.on==2u){                                  /* the tail first: its directory of timed notes, then again */
            if(pz_block(&in,pj.h.map[2],tail,PZ_TAIL,0))return 0;
            if(bank_wide)memcpy(proj_rec_ext,tail+BT(BL.rec),BL.recn);
            if(pz_block(&in,pj.h.map[3],(uint8_t *)proj_image_store(),sizeof(project_store_t),0))return 0;
        }else{
            if(bank_wide&&pj_read(obj,copy,BL.rec,proj_rec_ext,BL.recn))return 0;
            if(pj_read(obj,copy,8u,proj_image_store(),sizeof(project_store_t)))return 0;
        }
        hash=fnv(fnv(hash,header,sizeof header),proj_image_store(),sizeof(project_store_t));
        if(!proj_import(q,proj_image_store(),sizeof(project_store_t)))return 0;
        if(pj.on==2u){
            in.at=PJ_HEAD;
            if(pz_block(&in,pj.h.map[2],tail,PZ_TAIL,0))return 0;
            memcpy(tail+PZ_TAIL,&pj.h.map[1],4u);
        }else if(pj_read(obj,copy,BANK_ACTIVE_OFF,tail,BANK_TAIL_SIZE))return 0;
        hash=fnv(hash,tail,BANK_TAIL_SIZE-4u);uint32_t sum;
        memcpy(&sum,tail+BANK_TAIL_SIZE-4u,4);
        if(sum!=hash||!bank_tail_valid(tail))return 0;
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
