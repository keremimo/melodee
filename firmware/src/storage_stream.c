/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Bounded sequential bank staging. An unpublished stage is scratch: readers
 * continue choosing the previous committed copy. Publication follows payload
 * readback and format validation by the caller. No raw addresses cross the API. */
typedef struct {
    st_hdr_t head;
    uint32_t obj, pos, crc, generation;
    uint32_t avail[ST_UW];              /* lazy: the pool sectors still free to take */
    int error;
    uint8_t lazy;                       /* st_stream_begin_lazy: the length is known only at st_stream_end */
} st_stream_t;
static int st_stream_begin(st_stream_t *w,uint32_t obj,uint32_t len)
{
    memset(w,0,sizeof *w);w->error=-1;
#if defined(MELODEE_AUDIO_PROFILE) && MELODEE_AUDIO_PROFILE
    if(AUDIO_LAB_GUARDED())return -9;
#endif
    if(!st_banked(obj)||len>st_capacity(obj))return -1;
    w->obj=obj; w->crc=0xffffffffu;
    w->generation=++st_stream_generation;
    int rc=st_project_allocate(obj,len,&w->head);
    if(rc)return w->error=rc;
    w->error=0;return 0;
}
/* A project image whose length is known only at its end (project_stream.c): it takes the pool's free sectors, the
 * lowest first, as its writes reach them (each erased then), so a small project erases and occupies little. Until
 * st_stream_end programs the staged header, its address map is the header in RAM. */
static int st_stream_begin_lazy(st_stream_t *w,uint32_t obj)
{
    uint32_t used[ST_UW];
    memset(w,0,sizeof *w);w->error=-1;
#if defined(MELODEE_AUDIO_PROFILE) && MELODEE_AUDIO_PROFILE
    if(AUDIO_LAB_GUARDED())return -9;
#endif
    if(!st_banked(obj))return -1;
    w->obj=obj;w->crc=0xffffffffu;w->generation=++st_stream_generation;
    memset(&w->head,0xFF,sizeof w->head);
    w->head.type=(uint16_t)obj;w->head.len=0;
    if(st_pool_used(used,obj,&w->head.seq,0))return w->error=-7;
    for(uint32_t i=0;i<ST_UW;i++)w->avail[i]=~used[i];
    st_list_init(&w->head);w->head.slot=0;
    w->lazy=1;w->error=0;return 0;
}
static uint32_t st_stream_sectors(const st_stream_t *w)
{
    uint8_t idx[ST_PROJECT_SECTORS];
    return st_rec_sectors(&w->head,idx);
}
/* sectors taken for the image's first end bytes */
static int st_stream_room(st_stream_t *w,uint32_t end)
{
    uint32_t need=(end+ST_POOL_PAYLOAD_MAX-1u)/ST_POOL_PAYLOAD_MAX,have=st_stream_sectors(w);
    if(need>ST_PROJECT_SECTORS)return w->error=-5;
    while(have<need){
        uint32_t i=0;
        while(i<ST_U_N && !((w->avail[i/32u]>>(i%32u))&1u))i++;
        if(i==ST_U_N)return w->error=-5;               /* STORAGE FULL */
        w->avail[i/32u]&=~(1u<<(i%32u));
        st_list_put(&w->head,have,i);
        if(!have)w->head.slot=(uint16_t)(i+2u);
        int rc=st_erase(st_pool_at(i));if(rc)return w->error=rc;
        have++;
    }
    return 0;
}
static int st_stream_read(st_stream_t *w,uint32_t off,void *dst,uint32_t n)
{
    uint8_t *d=dst;
    while(n){
        uint32_t span,at=st_pool_address(&w->head,off,&span),k=n<span?n:span;
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
    if(w->lazy){if(off>ST_PROJECT_MAX||n>ST_PROJECT_MAX-off||st_stream_room(w,off+n))return w->error?w->error:(w->error=-1);}
    else if(off>w->head.len||n>w->head.len-off)return w->error=-1;
    const uint8_t *p=data;
    while(n){
        uint32_t span,at=w->lazy?st_pool_address(&w->head,off,&span):st_address_at(w->obj,w->head.slot,off,&span,ST_PAYLOAD_OFF);
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
    if(st_stream_sectors(w)!=(len+ST_POOL_PAYLOAD_MAX-1u)/ST_POOL_PAYLOAD_MAX)return w->error=-1;
    int rc=st_prog(st_sector(w->obj,w->head.slot),&w->head,sizeof w->head);
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
    int rc=st_stream_check(w);if(rc)return rc;
    st_hdr_t current;int copy=st_current(w->obj,&current);
    if(copy>=0?(copy==w->head.slot||current.seq+1u!=w->head.seq):w->head.seq!=1u)return -6;
    w->head.magic=ST_MAGIC;
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
