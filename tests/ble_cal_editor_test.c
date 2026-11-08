/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define MELODEE_BLE_TEST_NO_MAIN 1
#include "melodee_ble_test.c"
static uint8_t reply[1024], disk_copy[100];
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
/* BLE (52): the reply's state, read back */
static struct { uint32_t op, rc, flags, links, link[2], code, bond, last, found; const char *status, *name, *first; } st;
static void bt(const uint8_t *p, uint32_t n)
{
    uint32_t i = 2;
    reply_n = 0; assert(ed_ble_handle(52, p, n));
    st.op = reply[0]; st.rc = reply[1]; st.flags = reply[i++]; st.links = reply[i++];
    for (uint32_t k = 0; k < st.links; k++) st.link[k] = reply[i++];
    st.code = reply[i] | reply[i + 1] << 7 | reply[i + 2] << 14; i += 3;
    st.status = (const char *)reply + i; i += strlen(st.status) + 1u;
    st.bond = reply[i++]; st.last = reply[i++];
    st.name = (const char *)reply + i; i += strlen(st.name) + 1u;
    st.found = reply[i++]; st.first = (const char *)reply + i;
    for (uint32_t k = 0; k < st.found; k++) i += strlen((const char *)reply + i) + 1u;
    assert(i == reply_n);
}
static void bluetooth(void)
{
    reset_ble(); mb.cal = 1; mb.on = 0;
    bt(0, 0);
    assert(st.op == 127 && !st.rc && st.flags == 1 && st.links == BLE_LINKS && st.code == 0x1FFFFF && !strcmp(st.status, "BLE OFF") &&
           !st.bond && !st.last && !st.name[0] && !st.found);
    bt((uint8_t[]){1, 1}, 2);
    assert(!st.rc && mb.on && (st.flags & 2) && (mb_saved.flags & MB_RADIO));            /* the radio, kept for power-on */
    bt((uint8_t[]){1, 0}, 2); assert(!st.rc && !mb.on && !(st.flags & 2));
    mb.started = 0; song.playing = 1;
    bt((uint8_t[]){1, 1}, 2); assert(st.rc == 3 && !mb.on);                             /* STOP AUDIO TO START BLE */
    song.playing = 0; mb.started = 1; mb.cal = 0;
    bt((uint8_t[]){1, 1}, 2); assert(st.rc == 2 && !mb.on);                             /* RADIO CALIBRATION MISSING */
    mb.cal = 1; bt((uint8_t[]){1, 1}, 2); assert(!st.rc && mb.on);
    bt((uint8_t[]){2, 1}, 2); assert(!st.rc && (st.flags & 4) && (mb_saved.flags & MB_VISIBLE));   /* Mode VISIBLE */
    bt((uint8_t[]){6, 1}, 2); assert(st.rc == 2 && !mb.scanning);                       /* VISIBLE never scans */
    bt((uint8_t[]){2, 0}, 2); assert(!st.rc && !(st.flags & 4));                        /* CONNECT OUT */
    bt((uint8_t[]){6, 1}, 2); assert(!st.rc && mb.scanning && ed_bt_scan && (st.flags & 8));
    memset(&mb.found, 0, sizeof mb.found);
    mb.found.e[0].used = 1; strcpy(mb.found.e[0].name, "WIDI BUD");
    mb.found.e[1].used = 1; memcpy(mb.found.e[1].addr, (uint8_t[]){0xA1, 0xB2, 3, 4, 5, 0xC4}, 6);
    mb.idx[0] = 0; mb.idx[1] = 1; mb.count = 2;
    bt(0, 0); assert(st.found == 2 && !strcmp(st.first, "WIDI BUD") && !strcmp(st.first + 9, "C4:05:..:B2:A1"));
    bt((uint8_t[]){7, 5}, 2); assert(st.rc == 2);                                       /* not a row the scan found */
    bt((uint8_t[]){4}, 1); assert(st.rc == 2);                                          /* no last device */
    ble_store_set_last(&mb.store, (uint8_t[]){1, 2, 3, 4, 5, 6}, 0, "KEYS 49", BLE_KIND_MIDI);
    mb_saved.bond[0] = 0xB5u; mb_saved.bond[1] = 1;
    bt(0, 0); assert(st.bond == 1 && st.last == 1 && !strcmp(st.name, "KEYS 49"));
    bt((uint8_t[]){6, 0}, 2); assert(!st.rc && !mb.scanning && !ed_bt_scan);
    bt((uint8_t[]){3}, 1); assert(!st.rc);                                              /* Disconnect */
    bt((uint8_t[]){5}, 1); assert(!st.rc && !st.bond && !st.last && !st.name[0] && !mb_saved.bond[0]);   /* Forget pairings */
    bt((uint8_t[]){0}, 1); assert(st.rc == 1);
    bt((uint8_t[]){8}, 1); assert(st.rc == 1);
    bt((uint8_t[]){1, 2}, 2); assert(st.rc == 1 && mb.on);
    bt((uint8_t[]){3, 0}, 2); assert(st.rc == 1);
    bt((uint8_t[]){6}, 1); assert(st.rc == 1 && st.op == 6);
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
    bluetooth();
    puts("BLE calibration editor: checked import, quiet/radio guards, idempotence, persistence and replacement protection passed");
    puts("BLE (52): radio, its quiet and calibration refusals, Mode, scan and its list, last device, pairings, forget, bad requests passed");
}
