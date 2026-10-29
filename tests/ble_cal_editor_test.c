/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define MELODEE_BLE_TEST_NO_MAIN 1
#include "melodee_ble_test.c"
static uint8_t reply[32], disk_copy[100];
static uint32_t reply_n, saves;
static void ed_b(uint32_t v) { assert(reply_n < sizeof reply); reply[reply_n++]=(uint8_t)(v&127u); }
#include "../build/ble-tests/editor_decoder.h"
static uint32_t ed_ui_save(void) { memcpy(disk_copy,mb_saved.rf,100);mb_committed=mb_revision;saves++;return 0; }
#include "../firmware/src/editor_ble.c"
static void request(const uint8_t *p,uint32_t n,uint32_t rc)
{ reply_n=0;assert(ed_ble_handle(81,p,n));assert(reply_n==9&&reply[0]==rc&&reply[1]==1); }
static void pack(const uint8_t *p,uint8_t out[116])
{
    uint32_t k=1;out[0]=1;
    for(uint32_t i=0;i<100;i+=7){
        uint32_t n=100-i;if(n>7)n=7;
        uint32_t mask=k++;out[mask]=0;
        for(uint32_t j=0;j<n;j++){
            out[mask]|=(p[i+j]>>7)<<j;
            out[k++]=p[i+j]&127u;
        }
    }
    assert(k==116);
}
int main(void)
{
    reset_ble();mb.on=0;mb.cal=0;
    struct ble_rf_trims t={0};uint8_t copy[100],payload[116];
    ble_rf_copy_make(copy,&t,BLE_VM_HW);assert(ble_rf_copy_ok(copy));pack(copy,payload);
    request((uint8_t[]){0},1,0);assert(!reply[2]&&!reply[5]);
    uint32_t revision=mb_revision;
    payload[20]^=1;request(payload,116,1);assert(!mb.cal&&mb_revision==revision&&!saves);payload[20]^=1;
    request(payload,115,1);assert(!saves);
    song.playing=1;request(payload,116,4);assert(!saves&&!mb.cal);song.playing=0;
    mb.on=1;request(payload,116,4);assert(!saves&&!mb.cal);mb.on=0;
    request(payload,116,0);assert(mb.cal&&reply[5]&&saves==1&&!memcmp(copy,disk_copy,100));
    revision=mb_revision;request(payload,116,0);assert(mb_revision==revision);
    t.x106[0]=1;ble_rf_copy_make(copy,&t,BLE_VM_HW);pack(copy,payload);
    request(payload,116,2);assert(!memcmp(mb_saved.rf,disk_copy,100));
    assert(!ed_ble_handle(82,(uint8_t[]){0},1));
    puts("BLE calibration editor: checked import, quiet/radio guards, idempotence, persistence and replacement protection passed");
}
