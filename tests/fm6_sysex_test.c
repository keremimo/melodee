/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* DX7 librarian transfers through the real USB-MIDI parser and native pools. */
#define EDITOR_TEST_NO_MAIN
#include "editor_test.c"
#include "../firmware/src/fm6_store.c"

static uint8_t cartridge[FM6_RX], voice[163];
static void fresh_sysex(void)
{
    reset(); fm6_init();
    fm6_rx_ready = fm6_rx_on = 0; fm6_rx_n = 0; fm6_bslot = 0;
    usb.sx_on = 0;
    set_engine_of(&trk[0], ENGI_FM6);
    up_cache_reset();
}
/* Interleave clock and emulate macOS splitting a long message into CIN F
 * single-byte packets in addition to normal CIN 4/5/6/7 packets. */
static void usb_dump(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n;) {
        if (i % 41u == 0) {
            midi_in_event(0x0000f80fu);
            midi_in_event(0xfu | (uint32_t)p[i++] << 8);
        } else {
            uint32_t k = n - i > 3u ? 3u : n - i;
            uint32_t pkt = i + k == n ? 4u + k : 4u;
            for (uint32_t j = 0; j < k; j++) pkt |= (uint32_t)p[i++] << (8u * (j + 1u));
            midi_in_event(pkt);
        }
        /* A host normally consumes these clocks in its audio callback. */
        mi_r = mi_w;
    }
    fm6_service(); ed_service(); host_drain();
}
static void make_bank(uint32_t ch)
{
    memcpy(cartridge, (uint8_t[]){0xf0,0x43,(uint8_t)ch,9,0x20,0}, 6);
    for (uint32_t k = 0; k < 32; k++) {
        fm6_factory(k % FM6_NFAC, cartridge + 6 + k * 128u);
        cartridge[6 + k * 128u + 118] = (uint8_t)('A' + k % 26);
    }
    cartridge[4102] = fm6_chk(cartridge + 6, 4096); cartridge[4103] = 0xf7;
}
static void dump_request(uint32_t ch, uint32_t format)
{
    uint8_t req[] = {0xf0,0x43,(uint8_t)(0x20u | ch),(uint8_t)format,0xf7};
    host_wire_n = 0; usb_dump(req, sizeof req);
}
static int dump_backup_lifetime(uint32_t format)
{
    uint8_t get[] = {0,0,0,0,0,0,64,0};
    fresh_sysex();
    request(ED_BACKUP_LIST,0,0);
    int ok=host_wire[6]==0;
    request(ED_BACKUP_GET,get,sizeof get);ok &= host_wire[6]==0;
    dump_request(0,format);ok &= host_wire_n==(format==9u?FM6_RX:163u);
    request(ED_BACKUP_GET,get,sizeof get);ok &= host_wire[6]==5u;
    request(ED_BACKUP_LIST,0,0);ok &= host_wire[6]==0;
    request(ED_BACKUP_GET,get,sizeof get);ok &= host_wire[6]==0;
    return check(format==9u?"bank dump invalidates the staged backup until a new LIST":
                            "voice dump invalidates the staged backup until a new LIST",ok);
}
int main(void)
{
    int bad = 0, ok;
    uint8_t unpacked[FP_SIZE + 1u], previous[FP_SIZE], pk[128];
    fresh_sysex(); make_bank(7);
    native_put(ENGI_FM6, 63, FM6_INIT);
    usb_dump(cartridge, sizeof cartridge);
    bad += check("valid bank stages all names and bytes before any preset is overwritten",sx_import.count==32u && fm6_rx_ready==2u && up_count()==1u && !memcmp(sx_import.data,cartridge+6,4096));
    ui.uslot=0;sysex_choose();
    ok = up_count() == 33;
    for (uint32_t k = 0; k < 32; k++) ok &= !memcmp(native_raw(ENGI_FM6,k),cartridge+6+k*128u,128);
    bad += check("bank receive stores all 32 packed patches without the editor", ok && native_used(ENGI_FM6,63));
    native_load(ENGI_FM6,0,0);
    fm6_unpack(cartridge + 6, unpacked);
    bad += check("chosen user preset can be loaded explicitly after importing", !memcmp(fm6_patch[0],unpacked,FP_SIZE) && !trk[0].user_native && trk[0].user == 1);
    dump_request(7,9);
    bad += check("bank dump reply round trips every byte and request channel", host_wire_n == FM6_RX && !memcmp(host_wire,cartridge,FM6_RX));
    fm6_bslot = 32; make_bank(15); usb_dump(cartridge, sizeof cartridge);ui.uslot=32;sysex_choose();ui.confirm=0;sysex_commit();native_load(ENGI_FM6,32,0);
    bad += check("STORE slot F033 selects the upper half for bank receive", up_count() == 64 && !memcmp(native_raw(ENGI_FM6,32),cartridge+6,128) && trk[0].user == 33);
    dump_request(15,9);
    bad += check("upper bank dump returns its 32 voices on channel 16", host_wire_n == FM6_RX && !memcmp(host_wire,cartridge,FM6_RX));
    fm6_factory(3,pk); fm6_unpack(pk,unpacked);
    memcpy(voice,(uint8_t[]){0xf0,0x43,3,0,1,0x1b},6); memcpy(voice+6,unpacked,FP_SIZE);
    voice[161]=fm6_chk(voice+6,FP_SIZE);voice[162]=0xf7;
    usb_dump(voice,sizeof voice);
    bad+=check("single voice stages its native name without detaching the current saved sound",sx_import.count==1 && trk[0].user==33 && library_engine()==ENGI_FM6);
    ui.uslot=31;sysex_choose();bad+=check("occupied destination requires overwrite confirmation",ui.confirm==CF_IMPORT);ui.confirm=0;sysex_commit();native_load(ENGI_FM6,31,0);dump_request(3,0);
    bad += check("single voice receive and reply preserve patch and channel", host_wire_n == sizeof voice && !memcmp(host_wire,voice,sizeof voice));
    bad += check("explicitly loaded import records its chosen user slot", trk[0].user==32 && !trk[0].user_native);
    song.sel=2;set_engine_of(&trk[2],ENGI_FM6);fm6_put_patch(2,unpacked,1);
    host_wire_n=0;fm6_send();host_drain();
    bad += check("device SEND uses the selected track MIDI channel", host_wire_n==163 && host_wire[2]==2 && !memcmp(host_wire+6,unpacked,FP_SIZE) && !strcmp(ui.msg,"VOICE SENT"));
    memcpy(previous,fm6_patch[2],FP_SIZE);
    voice[6]^=1;usb_dump(voice,sizeof voice);
    bad += check("bad voice checksum cannot change the track", !memcmp(previous,fm6_patch[2],FP_SIZE));voice[6]^=1;
    uint32_t gen=up_gen;cartridge[4102]^=1;usb_dump(cartridge,sizeof cartridge);
    bad += check("bad bank checksum cannot mutate presets", up_gen==gen);cartridge[4102]^=1;
    song.playing=1;usb_dump(cartridge,sizeof cartridge);
    sysex_choose();bad += check("bank can be reviewed during playback but cannot write presets",up_gen==gen && sx_import.count==32 && !strcmp(ui.msg,"STOP TO IMPORT"));song.playing=0;sysex_cancel();
    usb_dump(cartridge,FM6_RX-1u);sysex_byte(0);sysex_byte(0xf7);fm6_service();
    bad += check("oversized dump is discarded", up_gen==gen && !fm6_rx_ready);
    for(uint32_t i=0;i<162;i++)sysex_byte(voice[i]);
    sysex_byte(0x90);sysex_byte(0xf7);fm6_service();
    bad += check("status inside SysEx aborts DX7 assembly", !fm6_rx_ready && !memcmp(previous,fm6_patch[2],FP_SIZE));
    for(uint32_t i=0;i<162;i++)sysex_byte(voice[i]);
    midi_in_event(0x643c9009u);sysex_byte(0xf7);fm6_service();
    bad += check("channel event in another USB packet aborts DX7 assembly", !fm6_rx_ready && !memcmp(previous,fm6_patch[2],FP_SIZE));
    usb_dump((uint8_t[]){0xf0,0x43,0xf7},3);dump_request(3,0);
    bad += check("short frames recover for subsequent dump requests", host_wire_n==163 && host_wire[2]==3);
    /* Exact packed bank bytes (including unused bits) must survive export. */
    fresh_sysex();make_bank(0);cartridge[6+11]|=0x70;cartridge[4102]=fm6_chk(cartridge+6,4096);
    usb_dump(cartridge,FM6_RX);ui.uslot=0;sysex_choose();dump_request(0,9);
    bad += check("bank round trip retains original packed bytes", host_wire_n==FM6_RX && !memcmp(host_wire,cartridge,FM6_RX));
    fresh_sysex();dump_request(4,9);
    ok=host_wire_n==FM6_RX && host_wire[2]==4 && host_wire[4102]==fm6_chk(host_wire+6,4096);
    for(uint32_t k=0;k<32;k++)ok &= !memcmp(host_wire+6+k*128u,FM6_INIT,128);
    bad += check("empty bank exports 32 valid INIT voices", ok);
    usb.config=0;host_wire_n=0;dump_request(0,9);
    bad += check("disconnected send reports an error instead of success", !strcmp(ui.msg,"SYSEX SEND ERROR") && !sx_busy);
    fm6_send();
    bad += check("disconnected single-voice SEND also reports failure", !strcmp(ui.msg,"SYSEX SEND ERROR") && so_w==so_r);
    bad += dump_backup_lifetime(0)+dump_backup_lifetime(9);
    printf("DX7 SysEx test %s\n",bad?"FAILED":"passed");return !!bad;
}
