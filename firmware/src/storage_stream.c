/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Bounded sequential bank staging. An unpublished stage is scratch: readers
 * continue choosing the previous committed copy. Publication follows payload
 * readback and format validation by the caller. No raw addresses cross the API. */
typedef struct {
    st_hdr_t head;
    uint32_t obj, pos, crc, generation;
    uint32_t avail[ST_LW];              /* lazy: the linear sectors still free to take */
    uint32_t ready[ST_LW];              /* lazy: those erased ahead (st_stream_prepare): taken without an erase */
    int error;
    uint8_t lazy;                       /* st_stream_begin_lazy: the length is known only at st_stream_end */
    uint8_t nlist;                      /* lazy: its sectors so far, list[] their linear indices (the header holds 8) */
    uint8_t list[ST_REC_MAX];
} st_stream_t;
static int st_stream_begin(st_stream_t *w,uint32_t obj,uint32_t len)
{
    memset(w,0,sizeof *w);w->error=-1;
#if defined(MELODEE_AUDIO_PROFILE) && MELODEE_AUDIO_PROFILE
    if(AUDIO_LAB_GUARDED())return -9;
#endif
    if(!st_listed(obj)||len>st_capacity(obj))return -1;
    w->obj=obj; w->crc=0xffffffffu;
    w->generation=++st_stream_generation;st_stream_open=w->generation;st_stream_activity++;
    int rc=st_project_allocate(obj,len,&w->head);
    if(rc)return w->error=rc;
    w->error=0;return 0;
}
/* A project image whose length is known only at its end (project_stream.c): it takes the pool's free sectors, in
 * st_order, as its writes reach them (each erased then), so a small project erases and occupies little. Until
 * st_stream_end programs the staged header, its address map is the header in RAM. */
static int st_stream_begin_lazy(st_stream_t *w,uint32_t obj)
{
    _Static_assert(ST_REC_MAX < 256u, "st_stream_t.nlist");
    uint32_t used[ST_LW];
    memset(w,0,sizeof *w);w->error=-1;
#if defined(MELODEE_AUDIO_PROFILE) && MELODEE_AUDIO_PROFILE
    if(AUDIO_LAB_GUARDED())return -9;
#endif
    if(!st_banked(obj))return -1;
    w->obj=obj;w->crc=0xffffffffu;w->generation=++st_stream_generation;st_stream_open=w->generation;st_stream_activity++;
    memset(&w->head,0xFF,sizeof w->head);
    w->head.type=(uint16_t)obj;w->head.len=0;
    if(st_pool_used(used,obj,&w->head.seq))return w->error=-7;
    for(uint32_t i=0;i<ST_LN;i++)if(st_pool_free_at(used,i))w->avail[i/32u]|=1u<<(i%32u);
    st_list_init(&w->head);w->head.slot=0;
    w->lazy=1;w->error=0;return 0;
}
static uint32_t st_stream_sectors(const st_stream_t *w)
{
    uint8_t idx[ST_REC_MAX];
    return w->lazy?w->nlist:st_rec_sectors(&w->head,idx);
}
/* a lazy image's payload byte off: its flash address, *span the bytes up to the guard or the sector's end (its list in
 * RAM: the index blocks are written at st_stream_end) */
static uint32_t st_stream_address(const st_stream_t *w,uint32_t off,uint32_t *span)
{
    uint32_t k=off/ST_LIN_PAYLOAD_MAX,b=off%ST_LIN_PAYLOAD_MAX;
    if(k>=w->nlist){*span=0;return 0;}
    uint32_t at=st_lin_at(w->list[k]);
    if(b<ST_RUN0){*span=ST_RUN0-b;return at+ST_POOL_PAYLOAD_OFF+b;}
    *span=ST_LIN_PAYLOAD_MAX-b;
    return at+ST_GUARD_OFF+ST_GUARD_LEN+(b-ST_RUN0);
}
/* sectors taken for the image's first end bytes */
static int st_stream_room(st_stream_t *w,uint32_t end)
{
    uint32_t need=(end+ST_LIN_PAYLOAD_MAX-1u)/ST_LIN_PAYLOAD_MAX,have=w->nlist;
    if(need>st_max_sectors(w->obj))return w->error=-5;
    while(have<need){
        uint32_t m=0,i=0;
        for(;m<ST_LN;m++){i=st_order(w->obj,m);if((w->avail[i/32u]>>(i%32u))&1u)break;}
        if(m==ST_LN)return w->error=-5;                /* STORAGE FULL */
        w->avail[i/32u]&=~(1u<<(i%32u));
        if(have<8u)st_list_put(&w->head,have,i);
        w->list[have]=(uint8_t)i;
        if(!have)w->head.slot=(uint16_t)(ST_LIN_C0+i);
        if(!((w->ready[i/32u]>>(i%32u))&1u)){int rc=st_erase(st_lin_at(i));if(rc)return w->error=rc;}
        w->nlist=(uint8_t)++have;
    }
    return 0;
}
/* A lazy image's sectors made ready ahead, up to max of them (in the order it takes them): each free sector not blank
 * already erased now, so that its writes later erase nothing (a take recorded from USB audio: an erase holds the
 * interrupts ~45 ms, a page's program < 1 ms). Its room in bytes (sectors taken and ready); -n an error */
static int st_stream_prepare(st_stream_t *w,uint32_t max)
{
    uint32_t n=w->nlist;
    if(w->generation!=st_stream_generation)return w->error=-6;
    if(w->error||!w->lazy)return w->error?w->error:-1;
    if(max>st_max_sectors(w->obj))max=st_max_sectors(w->obj);
    for(uint32_t m=0;m<ST_LN&&n<max;m++){
        uint32_t i=st_order(w->obj,m),blank=1;
        if(!((w->avail[i/32u]>>(i%32u))&1u))continue;
        if(!((w->ready[i/32u]>>(i%32u))&1u)){
            for(uint32_t off=0;blank&&off<ST_SECTOR;off+=sizeof st_buf){
                if(st_fetch(st_lin_at(i)+off,st_buf,sizeof st_buf))return w->error=-7;
                for(uint32_t j=0;j<sizeof st_buf;j++)if(st_buf[j]!=0xFFu){blank=0;break;}
            }
            if(!blank){int rc=st_erase(st_lin_at(i));if(rc)return w->error=rc;}
            w->ready[i/32u]|=1u<<(i%32u);
            ST_IDLE();
        }
        n++;
    }
    return (int)(n*ST_LIN_PAYLOAD_MAX);
}
static int st_stream_read(st_stream_t *w,uint32_t off,void *dst,uint32_t n)
{
    uint8_t *d=dst;
    while(n){
        uint32_t span,at=w->lazy?st_stream_address(w,off,&span):st_pool_address(&w->head,off,&span),k=n<span?n:span;
        if(!k||st_fetch(at,d,k))return -7;
        d+=k;off+=k;n-=k;
    }
    return 0;
}
/* Input must be RAM: the flash-program window cannot fetch from XIP. Calls
 * may use arbitrary packet lengths; split at physical pages and extents. */
static int st_stream_write_at(st_stream_t *w,uint32_t off,const void *data,uint32_t n)
{
    if(w->generation!=st_stream_generation)return w->error=-6;
    if(w->error)return w->error;
    st_stream_activity++;
    if(w->lazy){uint32_t cap=st_capacity(w->obj);if(off>cap||n>cap-off||st_stream_room(w,off+n))return w->error?w->error:(w->error=-1);}
    else if(off>w->head.len||n>w->head.len-off)return w->error=-1;
    const uint8_t *p=data;
    while(n){
        uint32_t span,at=w->lazy?st_stream_address(w,off,&span):st_address_at(w->obj,w->head.slot,off,&span,ST_PAYLOAD_OFF);
        if(!span)return w->error=-1;
        uint32_t k=256u-(at&255u);if(k>span)k=span;if(k>n)k=n;
        int rc=st_prog(at,p,k);if(rc)return w->error=rc;
        off+=k;p+=k;n-=k;
    }
    return 0;
}
static int st_stream_write(st_stream_t *w,const void *data,uint32_t n)
{
    int rc=st_stream_write_at(w,w->pos,data,n);if(rc)return rc;
    w->crc=st_crc32_update(w->crc,data,n);w->pos+=n;return 0;
}
/* a lazy image's length: the staged header (no magic) programmed, so range reads and st_stream_check see it; the
 * CRC is the caller's seal */
static int st_stream_end(st_stream_t *w,uint32_t len)
{
    if(w->generation!=st_stream_generation)return w->error=-6;
    if(w->error)return w->error;
    if(!w->lazy||!len||st_stream_room(w,len))return w->error?w->error:(w->error=-1);
    w->head.len=len;
    if(st_stream_sectors(w)!=(len+ST_LIN_PAYLOAD_MAX-1u)/ST_LIN_PAYLOAD_MAX)return w->error=-1;
    int rc=st_lix_write(w->list,w->nlist);
    if(!rc)rc=st_prog(st_sector(w->obj,w->head.slot),&w->head,sizeof w->head);
    return rc?(w->error=rc):0;
}
/* Generated images may write disjoint pieces out of order. The caller seals
 * their exact coverage and expected CRC; readback must match before publish. */
static void st_stream_seal(st_stream_t *w,uint32_t crc)
{w->pos=w->head.len;w->crc=crc;}
static int st_stream_check(st_stream_t *w)
{
    if(w->generation!=st_stream_generation)return w->error=-6;
    if(w->error)return w->error;
    if(w->pos!=w->head.len)return -1;
    w->head.crc=~w->crc;
    return st_body(w->obj,w->head.slot,&w->head)?-7:0;
}
/* Optional commit: runtime backup/restore stages stay unpublished. A caller
 * can stream/read their payload with st_read_range using this exact copy. */
static int st_stream_publish(st_stream_t *w)
{
    if(st_stream_open==w->generation)st_stream_open=0;   /* (done, published or not) */
    int rc=st_stream_check(w);if(rc)return rc;
    st_hdr_t current;int copy=st_current(w->obj,&current);
    if(copy>=0?(copy==w->head.slot||current.seq+1u!=w->head.seq):w->head.seq!=1u)return -6;
    w->head.magic=ST_MAGIC;
    st_pool_gen++;
    w->head.hcrc=st_crc32(&w->head,sizeof w->head-4u);
    if((rc=st_prog(st_sector(w->obj,w->head.slot),&w->head,sizeof w->head)))return rc;
    st_hdr_t check;
    if(st_head(w->obj,w->head.slot,&check)||memcmp(&check,&w->head,sizeof check)||st_body(w->obj,w->head.slot,&check))return -7;
    return 0;
}

/* Buffered and streamed saves share allocation, verification and publication. */
static int st_project_save(uint32_t obj, const void *src, uint32_t len)
{
    st_stream_t w; int rc=st_stream_begin(&w,obj,len);
    if(rc)return rc;
    if((rc=st_stream_write(&w,src,len)))return rc;
    return st_stream_publish(&w);
}
