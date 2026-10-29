/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define main storage_reference_main
#include "storage_test.c"
#undef main
#include <assert.h>
#define LEN ST_PROJECT_MAX
static uint8_t old[LEN],next[LEN],readback[LEN],saved[sizeof nor];
static int transfer(st_stream_t *w)
{
    int rc=st_stream_begin(w,OBJ_BANK0,LEN);if(rc)return rc;
    for(uint32_t pos=0;pos<LEN;){
        uint32_t n=1u+(pos*37u+113u)%256u;if(n>LEN-pos)n=LEN-pos;
        rc=st_stream_write(w,next+pos,n);if(rc)return rc;pos+=n;
    }
    return st_stream_publish(w);
}
static int generated(st_stream_t *w)
{
    int rc=st_stream_begin(w,OBJ_BANK0,LEN);if(rc)return rc;
    if((rc=st_stream_write_at(w,13220u,next+13220u,LEN-13220u-4u)) ||
       (rc=st_stream_write_at(w,0,next,13220u)) ||
       (rc=st_stream_write_at(w,LEN-4u,next+LEN-4u,4u)))return rc;
    st_stream_seal(w,st_crc32_update(0xffffffffu,next,LEN));return st_stream_publish(w);
}
static void original(void)
{
    fail_after=fail_bytes=-1;erase_error=write_protected=0;fail_erase_off=0xffffffffu;
    memcpy(nor,saved,sizeof nor);
}
int main(void)
{
    for(uint32_t i=0;i<LEN;i++){old[i]=(uint8_t)(i*61u);next[i]=(uint8_t)(i*113u+7u);}
    memset(nor,0xff,sizeof nor);assert(!st_save(OBJ_BANK0,old,LEN));memcpy(saved,nor,sizeof nor);
    st_stream_t w;assert(!st_stream_begin(&w,OBJ_BANK0,LEN));
    assert(!st_stream_write(&w,next,ST_BANK_BASE_MAX-1u));
    assert(!st_stream_write(&w,next+w.pos,LEN-w.pos));assert(!st_stream_check(&w));
    assert(st_load(OBJ_BANK0,readback,LEN)==LEN&&!memcmp(readback,old,LEN));
    assert(!st_read_range(w.obj,w.head.slot,0,readback,LEN)&&!memcmp(readback,next,LEN));
    assert(!st_stream_publish(&w));assert(st_load(OBJ_BANK0,readback,LEN)==LEN&&!memcmp(readback,next,LEN));
    original();program_calls=0;assert(!transfer(&w));uint32_t programs=program_calls;
    for(uint32_t n=0;n<=programs;n++){
        original();fail_after=(int)n;int rc=transfer(&w);fail_after=-1;
        assert(st_load(OBJ_BANK0,readback,LEN)==LEN);
        assert(!memcmp(readback,rc?old:next,LEN));
        assert(!rc == (n==programs));
    }
    uint32_t faults=0;
    for(uint32_t n=0;n<=LEN+2u*sizeof(st_hdr_t);n++){
        if(n>32u && n<LEN && n%256u>1u && n%256u<255u)continue;
        original();fail_bytes=(int)n;int rc=transfer(&w);fail_bytes=-1;
        assert(st_load(OBJ_BANK0,readback,LEN)==LEN);
        assert(!memcmp(readback,rc?old:next,LEN));faults++;
    }
    original();assert(!st_stream_begin(&w,OBJ_BANK0,LEN));
    assert(!st_stream_write_at(&w,13220u,next+13220u,LEN-13220u-4u));
    assert(!st_stream_write_at(&w,0,next,13220u));
    assert(!st_stream_write_at(&w,LEN-4u,next+LEN-4u,4u));
    st_stream_seal(&w,st_crc32_update(0xffffffffu,next,LEN));
    assert(!st_stream_publish(&w));
    assert(st_load(OBJ_BANK0,readback,LEN)==LEN&&!memcmp(readback,next,LEN));
    original();assert(!st_stream_begin(&w,OBJ_BANK0,LEN));
    assert(!st_stream_write_at(&w,0,next,LEN-1u));
    st_stream_seal(&w,st_crc32_update(0xffffffffu,next,LEN));assert(st_stream_publish(&w));
    assert(st_load(OBJ_BANK0,readback,LEN)==LEN&&!memcmp(readback,old,LEN));
    original();assert(!st_stream_begin(&w,OBJ_BANK0,LEN));
    assert(st_stream_write_at(&w,LEN-1u,next,2u)&&st_stream_publish(&w));
    original();program_calls=0;assert(!generated(&w));uint32_t generated_programs=program_calls;
    for(uint32_t n=0;n<=generated_programs;n++){
        original();fail_after=(int)n;int rc=generated(&w);fail_after=-1;
        assert(st_load(OBJ_BANK0,readback,LEN)==LEN&&!memcmp(readback,rc?old:next,LEN));assert(!rc == (n==generated_programs));
    }
    for(uint32_t n=0;n<=LEN+2u*sizeof(st_hdr_t);n++){
        if(n>32u && n<LEN && n%256u>1u && n%256u<255u)continue;
        original();fail_bytes=(int)n;int rc=generated(&w);fail_bytes=-1;
        assert(st_load(OBJ_BANK0,readback,LEN)==LEN&&!memcmp(readback,rc?old:next,LEN));
    }
    original();write_protected=1;assert(transfer(&w));write_protected=0;
    assert(st_load(OBJ_BANK0,readback,LEN)==LEN&&!memcmp(readback,old,LEN));
    original();assert(!st_stream_begin(&w,OBJ_BANK0,LEN));
    st_hdr_t target=w.head;
    uint8_t sectors[ST_PROJECT_SECTORS];
    for(uint32_t k=0,n=st_rec_sectors(&target,sectors);k<n;k++){
        original();fail_erase_off=st_pool_at(sectors[k]);
        assert(st_stream_begin(&w,OBJ_BANK0,LEN));
        assert(st_load(OBJ_BANK0,readback,LEN)==LEN&&!memcmp(readback,old,LEN));
    }
    original();assert(!st_stream_begin(&w,OBJ_BANK0,LEN));assert(!st_stream_write(&w,next,LEN));
    assert(!st_save(OBJ_BANK0,next,LEN));assert(st_stream_publish(&w));
    original();uint32_t calls=io_calls;assert(st_stream_begin(&w,OBJ_SETTINGS,LEN)&&io_calls==calls);
    assert(st_stream_begin(&w,OBJ_BANK0,LEN+1u)&&io_calls==calls);
    original();st_stream_t other;
    assert(!st_stream_begin(&w,OBJ_BANK0,LEN));
    assert(!st_stream_begin(&other,OBJ_BANK0+1,LEN));
    assert(st_stream_write(&w,next,1u)&&st_stream_publish(&w));
    assert(!st_stream_write(&other,next,LEN)&&!st_stream_publish(&other));
    assert(st_load(OBJ_BANK0,readback,LEN)==LEN&&!memcmp(readback,old,LEN));
    printf("Bank stream: %u program cutoffs, %u page/header byte cutoffs, unpublished snapshots, all allocated erase boundaries, write protection and stale publication passed\n",programs,faults);
    return 0;
}
