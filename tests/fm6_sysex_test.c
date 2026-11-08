/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* DX7 librarian transfers through the real USB-MIDI parser and native pools. */
#define EDITOR_TEST_NO_MAIN
#include "editor_test.c"

static uint8_t cartridge[MOD3_SX_MAX], voice[163];
static uint8_t sx_chk7(const uint8_t *p, uint32_t n) { uint32_t s = 0; while (n--) s += *p++; return (uint8_t)(-s & 127u); }
static void fresh_sysex(void)
{
    reset(); fm6_init();
    wire_release();
    msx_reset(); sch_store_slot[ENGI_FM6] = 0;
    usb.sx_on = 0;
    set_engine_of(&trk[0], ENGI_FM6);
    up_cache_reset();
}
/* Interleave clock and emulate macOS splitting a long message into CIN F
 * single-byte packets in addition to normal CIN 4/5/6/7 packets. */
static void usb_dump(const uint8_t *p, uint32_t n)
{
    wire_release();                                      /* next main-loop turn, before USB receives a frame */
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
    msx_service(); ed_service(); host_drain();
}
static void make_bank(uint32_t ch)
{
    memcpy(cartridge, (uint8_t[]){0xf0,0x43,(uint8_t)ch,9,0x20,0}, 6);
    for (uint32_t k = 0; k < 32; k++) {
        fm6_factory(k % FM6_NFAC, cartridge + 6 + k * 128u);
        cartridge[6 + k * 128u + 118] = (uint8_t)('A' + k % 26);
    }
    cartridge[4102] = sx_chk7(cartridge + 6, 4096); cartridge[4103] = 0xf7;
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
    dump_request(0,format);ok &= host_wire_n==(format==9u?MOD3_SX_MAX:163u);
    request(ED_BACKUP_GET,get,sizeof get);ok &= host_wire[6]==5u;
    request(ED_BACKUP_LIST,0,0);ok &= host_wire[6]==0;
    request(ED_BACKUP_GET,get,sizeof get);ok &= host_wire[6]==0;
    return check(format==9u?"bank dump invalidates the staged backup until a new LIST":
                            "voice dump invalidates the staged backup until a new LIST",ok);
}
static int extended_voice_import(void)
{
    int bad = 0;
    uint8_t wrapped[231] = {0xf0,0x43,15,0x7d,4,8}, pk[FM6_PACKED], v[FP_SIZE + 1u];
    fresh_sysex();
    fm6_factory(3, pk); fm6_unpack(pk, v);
    memcpy(wrapped + 6, v, FP_SIZE); wrapped[230] = 0xf7;
    usb_dump(wrapped, sizeof wrapped);
    bad += check("extended wrapper stages exactly its DX7 voice without changing the track",
                 sx_import.count == 1u && !memcmp(sx_import.data, pk, sizeof pk) && !trk[0].user);
    ui.uslot = 0; sysex_choose(); native_load(ENGI_FM6, 0, 0); dump_request(15, 0);
    bad += check("extended voice saves, loads and sends as a checksummed standard DX7 voice",
                 host_wire_n == 163u && host_wire[2] == 15u && host_wire[3] == 0 &&
                 !memcmp(host_wire + 6, v, FP_SIZE) && host_wire[161] == sx_chk7(v, FP_SIZE));
    static const uint8_t mutations[][2] = {{1,0x44},{2,0x10},{3,0x7c},{4,3},{5,9},
                                         {6,100},{151,31},{162,0x80},{230,0}};
    int rejected = 1;
    for (uint32_t i = 0; i < NELEM(mutations); i++) {
        uint8_t invalid[232]; memcpy(invalid, wrapped, sizeof wrapped);
        invalid[mutations[i][0]] = mutations[i][1];
        fresh_sysex(); usb_dump(invalid, sizeof wrapped);
        rejected &= !sx_import.count && !up_count();
    }
    fresh_sysex(); usb_dump(wrapped, sizeof wrapped - 1u);
    rejected &= !sx_import.count && !up_count();
    uint8_t oversized[232]; memcpy(oversized, wrapped, sizeof wrapped);
    oversized[230] = 0; oversized[231] = 0xf7;
    fresh_sysex(); usb_dump(oversized, sizeof oversized);
    rejected &= !sx_import.count && !up_count();
    bad += check("extended wrapper rejects changed headers, bad ranges, status, missing end and wrong lengths", rejected);
    return bad;
}
int main(void)
{
    int bad = extended_voice_import(), ok;
    uint8_t unpacked[FP_SIZE + 1u], previous[FP_SIZE], pk[128];
    fresh_sysex(); make_bank(7);
    native_put(ENGI_FM6, 63, FM6_INIT);
    usb_dump(cartridge, sizeof cartridge);
    bad += check("valid bank stages all names and bytes before any preset is overwritten",sx_import.count==32u && msx.ready==2u && up_count()==1u && !memcmp(sx_import.data,cartridge+6,4096));
    ui.uslot=0;sysex_choose();
    ok = up_count() == 33;
    for (uint32_t k = 0; k < 32; k++) ok &= !memcmp(native_raw(ENGI_FM6,k),cartridge+6+k*128u,128);
    bad += check("bank receive stores all 32 packed patches without the editor", ok && native_used(ENGI_FM6,63));
    native_load(ENGI_FM6,0,0);
    fm6_unpack(cartridge + 6, unpacked);
    bad += check("chosen user preset can be loaded explicitly after importing", !memcmp(fm6_patch_of(0),unpacked,FP_SIZE) && !trk[0].user_native && trk[0].user == 1);
    dump_request(7,9);
    bad += check("bank dump reply round trips every byte and request channel", host_wire_n == MOD3_SX_MAX && !memcmp(host_wire,cartridge,MOD3_SX_MAX));
    sch_store_slot[ENGI_FM6] = 32; make_bank(15); usb_dump(cartridge, sizeof cartridge);ui.uslot=32;sysex_choose();ui.confirm=0;sysex_commit();native_load(ENGI_FM6,32,0);
    bad += check("STORE slot F033 selects the upper half for bank receive", up_count() == 64 && !memcmp(native_raw(ENGI_FM6,32),cartridge+6,128) && trk[0].user == 33);
    dump_request(15,9);
    bad += check("upper bank dump returns its 32 voices on channel 16", host_wire_n == MOD3_SX_MAX && !memcmp(host_wire,cartridge,MOD3_SX_MAX));
    fm6_factory(3,pk); fm6_unpack(pk,unpacked);
    memcpy(voice,(uint8_t[]){0xf0,0x43,3,0,1,0x1b},6); memcpy(voice+6,unpacked,FP_SIZE);
    voice[161]=sx_chk7(voice+6,FP_SIZE);voice[162]=0xf7;
    usb_dump(voice,sizeof voice);
    bad+=check("single voice stages its native name without detaching the current saved sound",sx_import.count==1 && trk[0].user==33 && library_engine()==ENGI_FM6);
    ui.uslot=31;sysex_choose();bad+=check("occupied destination requires overwrite confirmation",ui.confirm==CF_IMPORT);ui.confirm=0;sysex_commit();native_load(ENGI_FM6,31,0);dump_request(3,0);
    bad += check("single voice receive and reply preserve patch and channel", host_wire_n == sizeof voice && !memcmp(host_wire,voice,sizeof voice));
    bad += check("explicitly loaded import records its chosen user slot", trk[0].user==32 && !trk[0].user_native);
    song.sel=2;set_engine_of(&trk[2],ENGI_FM6);fm6_put_patch(2,unpacked,1);
    host_wire_n=0;(void)store_send(song.sel, 1);host_drain();
    bad += check("device SEND uses the selected track MIDI channel", host_wire_n==163 && host_wire[2]==2 && !memcmp(host_wire+6,unpacked,FP_SIZE) && !strcmp(ui.msg,"VOICE SENT"));
    memcpy(previous,fm6_patch_of(2),FP_SIZE);
    voice[6]^=1;usb_dump(voice,sizeof voice);
    bad += check("bad voice checksum cannot change the track", !memcmp(previous,fm6_patch_of(2),FP_SIZE));voice[6]^=1;
    uint32_t gen=up_gen;cartridge[4102]^=1;usb_dump(cartridge,sizeof cartridge);
    bad += check("bad bank checksum cannot mutate presets", up_gen==gen);cartridge[4102]^=1;
    song.playing=1;usb_dump(cartridge,sizeof cartridge);
    sysex_choose();bad += check("bank can be reviewed during playback but cannot write presets",up_gen==gen && sx_import.count==32 && !strcmp(ui.msg,"STOP TO IMPORT"));song.playing=0;sysex_cancel();
    usb_dump(cartridge,MOD3_SX_MAX-1u);sysex_byte(0);sysex_byte(0xf7);msx_service();
    bad += check("oversized dump is discarded", up_gen==gen && !msx.ready);
    for(uint32_t i=0;i<162;i++)sysex_byte(voice[i]);
    sysex_byte(0x90);sysex_byte(0xf7);msx_service();
    bad += check("status inside SysEx aborts DX7 assembly", !msx.ready && !memcmp(previous,fm6_patch_of(2),FP_SIZE));
    for(uint32_t i=0;i<162;i++)sysex_byte(voice[i]);
    midi_in_event(0x643c9009u);sysex_byte(0xf7);msx_service();
    bad += check("channel event in another USB packet aborts DX7 assembly", !msx.ready && !memcmp(previous,fm6_patch_of(2),FP_SIZE));
    usb_dump((uint8_t[]){0xf0,0x43,0xf7},3);dump_request(3,0);
    bad += check("short frames recover for subsequent dump requests", host_wire_n==163 && host_wire[2]==3);
    /* Exact packed bank bytes (including unused bits) must survive export. */
    fresh_sysex();make_bank(0);cartridge[6+11]|=0x70;cartridge[4102]=sx_chk7(cartridge+6,4096);
    usb_dump(cartridge,MOD3_SX_MAX);ui.uslot=0;sysex_choose();dump_request(0,9);
    bad += check("bank round trip retains original packed bytes", host_wire_n==MOD3_SX_MAX && !memcmp(host_wire,cartridge,MOD3_SX_MAX));
    fresh_sysex();dump_request(4,9);
    ok=host_wire_n==MOD3_SX_MAX && host_wire[2]==4 && host_wire[4102]==sx_chk7(host_wire+6,4096);
    for(uint32_t k=0;k<32;k++)ok &= !memcmp(host_wire+6+k*128u,FM6_INIT,128);
    bad += check("empty bank exports 32 valid INIT voices", ok);
    usb.config=0;host_wire_n=0;dump_request(0,9);
    bad += check("disconnected send reports an error instead of success", !strcmp(ui.msg,"SYSEX SEND ERROR") && !sx_busy);
    (void)store_send(song.sel, 1);
    bad += check("disconnected single-voice SEND also reports failure", !strcmp(ui.msg,"SYSEX SEND ERROR") && so_w==so_r);
    bad += dump_backup_lifetime(0)+dump_backup_lifetime(9);
    printf("DX7 SysEx test %s\n",bad?"FAILED":"passed");return !!bad;
}
