/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The synths' SysEx, in and out, against what 88f4eb02's firmware did (tests/fixtures/sysex-88f4eb02.txt, written by this
 * same test built on that tree: `sysex_parity emit`): DX7 32-voice banks, single voices (VCED and the 231-byte wrapper),
 * voice and function parameters, dump requests; CZ-1 tones in (direct, both sizes, the handshake) and out (direct, the
 * handshake, the 128-byte form, a timeout); Prophet programs in (both commands, both sizes) and out (a part's, a user
 * slot's); the user pages' sends and STORE's SEND; the editor's DX7 bank export (88f4eb02's was its client's). Everything
 * goes in as USB-MIDI packets (macOS's CIN 0xF bytes and clocks between them) and through the main loop's services; what
 * is compared: every user slot, every part's patch and its engine, the parts' voice settings, the staged import, the
 * screen's lines and every byte sent, at each step. */
#define EDITOR_TEST_NO_MAIN
#include "editor_test.c"
#if __has_include("../firmware/src/fm6_store.c")         /* (88f4eb02: the synths' SysEx in the firmware) */
#define SX_OLD 1
#include "../firmware/src/fm6_store.c"
#define SX_SERVICE() do { cz_service(); fm6_service(); ed_service(); if (sx_ready) ota_frame_done(); host_drain(); } while (0)
#define SX_STORE_SEND(e, tr) p4_send(e, tr)
#define SX_FRESH() (fm6_rx_ready = fm6_rx_on = 0, fm6_rx_n = 0, cz_rx_on = cz_rx_ready = cz_rx_req = cz_rx_go = 0, cz_handshake = 0)
#else
#define SX_OLD 0
#define SX_SERVICE() do { msx_service(); ed_service(); if (sx_ready) ota_frame_done(); host_drain(); } while (0)
#define SX_STORE_SEND(e, tr) ((void)(e), (void)store_send(tr, 1))
#define SX_FRESH() msx_reset()
#endif

static FILE *sx_emit;
static const char *sx_want;                        /* the fixture's text */
static int sx_bad, sx_n;
static uint32_t fnv1(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
/* an engine as 88f4eb02 numbered it (the fixture's hashes hold those numbers; 2.0's handles are this device's) */
static uint8_t sx_num(uint32_t e)
{
#if SX_OLD
    return (uint8_t)e;
#else
    return e == ENGI_PROPHET ? 19u : e == ENGI_FM6 ? 12u : e == ENGI_CZ ? 15u : e == ENGI_SID ? 3u : e >= ENGI_808 && e <= ENGI_SAMPLER ? 10u :
           e == ENGI_MIDI ? 21u : (uint8_t)e;
#endif
}
/* the state after a step: its hashes, emitted or compared with the fixture's line */
static void sx_state(const char *step)
{
    uint32_t h[6] = {2166136261u, 2166136261u, 2166136261u, 2166136261u, 2166136261u, 2166136261u}, k;
    char line[200], *at;
    host_drain();
    for (k = 0; k < UP_SLOTS; k++) {
        uint8_t used = (uint8_t)up_used(k);
        h[0] = fnv1(h[0], &used, 1);
        if (used) {
            up_rec_t r = *up_rec(k);
            r.engine = sx_num(r.engine);
            h[0] = fnv1(h[0], &r, sizeof r);
        }
    }
    for (k = 0; k <= NPART; k++) {
        uint8_t e = sx_num(patch_eng[k]);
        h[1] = fnv1(fnv1(h[1], &e, 1), part_patch[k], MOD3_PATCH_MAX);
    }
    for (k = 0; k < NTRK; k++)
        h[2] = fnv1(h[2], trk[k].p, sizeof trk[k].p);
    h[3] = fnv1(h[3], &sx_import.count, 1);
    if (sx_import.count) {
        uint8_t ie = sx_num(sx_import.engine);
        h[3] = fnv1(h[3], &ie, 1);
        h[3] = fnv1(h[3], &sx_import.size, 2);
        h[3] = fnv1(h[3], &sx_import.general, 1);
        h[3] = fnv1(h[3], sx_import.data, (uint32_t)sx_import.count * sx_import.size);
    }
    h[4] = fnv1(fnv1(h[4], ui.msg, (uint32_t)strlen(ui.msg)), ui.msg2, (uint32_t)strlen(ui.msg2));
    h[5] = fnv1(h[5], host_wire, host_wire_n);
    snprintf(line, sizeof line, "%s slots %08x patches %08x params %08x import %08x screen %08x out %u %08x\n", step,
             h[0], h[1], h[2], h[3], h[4], host_wire_n, h[5]);
    sx_n++;
    if (sx_emit) {
        fputs(line, sx_emit);
        return;
    }
    at = sx_want ? strstr(sx_want, step) : 0;
    if (!at || (at != sx_want && at[-1] != '\n') || strncmp(at, line, strlen(line))) {
        printf("sysex: %-60s FAIL\n  now  %s", step, line);
        if (at) {
            const char *e = strchr(at, '\n');
            printf("  then %.*s\n", (int)(e ? e - at : (long)strlen(at)), at);
        }
        sx_bad++;
    }
}
/* (SX_SERVICE: a frame the editor leaves is ota.c's, which drops one it does not know: the main loop's next turn) */
/* bytes in as a computer sends them over USB: CIN 4..7 packets, every 41st byte a CIN 0xF one after a clock */
static void sx_usb(const uint8_t *p, uint32_t n)
{
    wire_release();
    for (uint32_t i = 0; i < n;) {
        if (i % 41u == 0) {
            midi_in_event(0x0000f80fu);
            midi_in_event(0xfu | (uint32_t)p[i++] << 8);
        } else {
            uint32_t k = n - i > 3u ? 3u : n - i, pkt = i + k == n && p[n - 1] == 0xF7 ? 4u + k : 4u;
            for (uint32_t j = 0; j < k; j++)
                pkt |= (uint32_t)p[i++] << (8u * (j + 1u));
            midi_in_event(pkt);
        }
        mi_r = mi_w;
    }
}
static void sx_in(const uint8_t *p, uint32_t n) { host_wire_n = 0; sx_usb(p, n); SX_SERVICE(); }
static uint8_t sx_chk(const uint8_t *p, uint32_t n) { uint32_t s = 0; while (n--) s += *p++; return (uint8_t)(-s & 127u); }
static uint32_t sx_seed = 12345u;
static uint8_t sx_rand(void) { sx_seed = sx_seed * 1103515245u + 12345u; return (uint8_t)(sx_seed >> 16); }
static void sx_fresh(void)
{
    reset(); fm6_init(); cz_init(); p5_parts_init();
    wire_release();
    SX_FRESH();
    usb.sx_on = 0;
    up_cache_reset();
    sch_store_slot[ENGI_FM6] = 0;
}
static void sx_choose(uint32_t slot)
{
    ui.uslot = (uint16_t)slot; sysex_choose();
    if (ui.confirm == CF_IMPORT) { ui.confirm = 0; sysex_commit(); }
}
static void sx_engine(uint32_t tr, uint32_t e, uint32_t preset)
{
    set_engine_of(&trk[tr], e); apply_preset_to(&trk[tr], preset); trk[tr].engine = trk[tr].eng_req;
}

/* ------------------------------------------------------------------ DX7 */
static uint8_t dx_frame[4200];
/* the editor's bank export of user slots s (m of them) into host_wire, as the client saves it */
static void sx_bank_export(const uint16_t *s, uint32_t m)
{
#if SX_OLD   /* 88f4eb02's: the client read each slot's voice (FM6_GET: none where empty), FM6.unpack, FM6.bankSysex */
    uint8_t v[FP_SIZE + 1u];
    memcpy(host_wire, (uint8_t[]){0xf0, 0x43, 0, 9, 0x20, 0}, 6);
    for (uint32_t k = 0; k < 32u; k++) {
        uint8_t *pk = host_wire + 6 + k * 128u;
        if (k < m && native_used(ENGI_FM6, s[k])) { fm6_unpack(native_raw(ENGI_FM6, s[k]), v); fm6_pack(v, pk); }
        else memcpy(pk, FM6_INIT, 128);
    }
    host_wire[4102] = sx_chk(host_wire + 6, 4096); host_wire[4103] = 0xf7; host_wire_n = 4104;
#else        /* PATCH BANK piece by piece: the frame between F0 and F7 */
    static uint8_t f[4200], a[200];
    uint32_t off = 0, total = 0, cnt, i;
    do {
        a[0] = 4; a[1] = 1; a[2] = ENGI_FM6; a[3] = off & 127u; a[4] = (uint8_t)(off >> 7);
        for (i = 0; i < m; i++) { a[5 + 2 * i] = s[i] & 127u; a[6 + 2 * i] = (uint8_t)(s[i] >> 7); }
        request(ED_PATCH, a, 5u + 2u * m);
        total = host_wire[11] | (uint32_t)host_wire[12] << 7;
        cnt = host_wire[13] | (uint32_t)host_wire[14] << 7;
        if (host_wire[10] || !cnt || total > sizeof f - 2u || cnt > total - off) { host_wire_n = 0; return; }
        memcpy(f + 1 + off, host_wire + 15, cnt);
        off += cnt;
    } while (off < total);
    f[0] = 0xf0; f[total + 1] = 0xf7;
    memcpy(host_wire, f, total + 2u); host_wire_n = total + 2u;
#endif
}
static void dx7(void)
{
    uint32_t k, n;
    sx_fresh(); sx_engine(0, ENGI_FM6, 3); sx_engine(1, ENGI_FM6, 7); song.sel = 1;
    sx_state("dx7 start");
    /* a 32-voice bank (VMEM): the factory voices with letters for names, then random bytes */
    memcpy(dx_frame, (uint8_t[]){0xf0, 0x43, 2, 9, 0x20, 0}, 6);
    for (k = 0; k < 32u; k++) {
        if (k < 16u) {
            (void)preset_patch(ENGI_FM6, k, dx_frame + 6 + k * 128u, 128u);
            dx_frame[6 + k * 128u + 118] = (uint8_t)('A' + k);
        } else
            for (n = 0; n < 128u; n++) dx_frame[6 + k * 128u + n] = (uint8_t)(sx_rand() & 127u);
    }
    dx_frame[4102] = sx_chk(dx_frame + 6, 4096); dx_frame[4103] = 0xf7;
    sx_in(dx_frame, 4104); sx_state("dx7 bank staged");
    sx_choose(32); sx_state("dx7 bank into U033..U064");
    dx_frame[4102] ^= 1; sx_in(dx_frame, 4104); sx_state("dx7 bank bad checksum");
    dx_frame[4102] ^= 1;
    sx_in(dx_frame, 4000); dx_frame[0] = 0x90; sx_in(dx_frame, 3); dx_frame[0] = 0xf0; sx_state("dx7 bank cut by a note");
    /* dump requests: the selected FM6 part's voice, the STORE page's bank */
    { uint8_t r[] = {0xf0, 0x43, 0x22, 0, 0xf7}; sx_in(r, 5); sx_state("dx7 voice request ch 3"); }
    sch_store_slot[ENGI_FM6] = 40;
    { uint8_t r[] = {0xf0, 0x43, 0x2f, 9, 0xf7}; sx_in(r, 5); sx_state("dx7 bank request U033"); }
    sch_store_slot[ENGI_FM6] = 0;
    { uint8_t r[] = {0xf0, 0x43, 0x20, 9, 0xf7}; sx_in(r, 5); sx_state("dx7 bank request U001 empty"); }
    /* one voice (VCED): the part's own, sent back with a new name, then random values */
    { uint8_t r[] = {0xf0, 0x43, 0x20, 0, 0xf7}; sx_in(r, 5); }
    memcpy(dx_frame, host_wire, 163); memcpy(dx_frame + 6 + 145, "SENT BACK!", 10);
    dx_frame[161] = sx_chk(dx_frame + 6, 155); sx_in(dx_frame, 163); sx_state("dx7 voice staged");
    sx_choose(5); sx_state("dx7 voice into U006");
    for (n = 0; n < 155u; n++) dx_frame[6 + n] = (uint8_t)(sx_rand() & 127u);
    dx_frame[161] = sx_chk(dx_frame + 6, 155); sx_in(dx_frame, 163); sx_state("dx7 random voice staged");
    sysex_cancel(); sx_state("dx7 import cancelled");
    /* the 231-byte wrapper (a MainStage file's) around the part's voice; then one out of range */
    { uint8_t r[] = {0xf0, 0x43, 0x20, 0, 0xf7}; sx_in(r, 5); }
    memcpy(dx_frame + 6, host_wire + 6, 155);
    memcpy(dx_frame, (uint8_t[]){0xf0, 0x43, 0, 0x7d, 4, 8}, 6);
    for (n = 0; n < 69u; n++) dx_frame[161 + n] = (uint8_t)(n & 127u);
    dx_frame[230] = 0xf7; sx_in(dx_frame, 231); sx_state("dx7 wrapped voice staged");
    sx_choose(6); sx_state("dx7 wrapped voice into U007");
    dx_frame[6 + 134] = 40; sx_in(dx_frame, 231); sx_state("dx7 wrapped voice out of range");
    /* voice parameters (the selected FM6 part), the operator switches, function parameters */
    {
        static const uint16_t P[][2] = {{0, 50}, {17, 3}, {64, 99}, {105, 20}, {134, 21}, {135, 7}, {144, 1}, {145, 'Z'},
                                        {154, 4}, {155, 0x15}, {156, 1}};
        for (k = 0; k < NELEM(P); k++) {
            uint8_t r[] = {0xf0, 0x43, 0x10, (uint8_t)(P[k][0] >> 7), (uint8_t)(P[k][0] & 127u), (uint8_t)P[k][1], 0xf7};
            sx_in(r, 7);
        }
        sx_state("dx7 voice parameters");
        static const uint8_t F[][2] = {{64, 1}, {65, 7}, {66, 3}, {68, 1}, {69, 120}, {70, 50}, {71, 2}, {72, 99},
                                       {73, 5}, {74, 60}, {75, 1}, {76, 99}, {77, 7}, {67, 5}, {64, 0}};
        for (k = 0; k < NELEM(F); k++) {
            uint8_t r[] = {0xf0, 0x43, 0x10, 8, F[k][0], F[k][1], 0xf7};
            sx_in(r, 7);
        }
        sx_state("dx7 function parameters");
        song.sel = 2;                                    /* (a SID part: channel 1's part, track 2, is FM6) */
        { uint8_t r[] = {0xf0, 0x43, 0x11, 0, 4, 31, 0xf7}; sx_in(r, 7); }
        sx_state("dx7 parameter on channel 2");
        sx_engine(0, ENGI_SID, 0); sx_engine(1, ENGI_SID, 0);
        { uint8_t r[] = {0xf0, 0x43, 0x10, 0, 4, 31, 0xf7}; sx_in(r, 7); }
        { uint8_t r[] = {0xf0, 0x43, 0x20, 0, 0xf7}; sx_in(r, 5); }
        sx_state("dx7 with no FM6 part");
    }
    /* the user pages' send (current, selected), STORE's SEND */
    sx_engine(0, ENGI_FM6, 9); song.sel = 0;
    host_wire_n = 0; (void)library_send(ENGI_FM6, 33); sx_state("dx7 library send U034");
    bulk = (typeof(bulk)){.on = 1, .engine = ENGI_FM6, .anchor = 4, .end = 7}; host_wire_n = 0; bulk_send();
    sx_state("dx7 bulk send U005..U008");
    host_wire_n = 0; SX_STORE_SEND(ENGI_FM6, 0); sx_state("dx7 STORE SEND");
    /* the editor's bank export (the old web editor's and Studio's FM6.bankSysex): the imported bank's 32 (its random
     * voices among them, packed again), a few with an empty slot (the init voice there and after them), more than 32
     * (the first 32) */
    {
        uint16_t s[40];
        for (k = 0; k < 32u; k++) s[k] = (uint16_t)(32u + k);
        sx_bank_export(s, 32); sx_state("dx7 bank export U033..U064");
        s[0] = 5; s[1] = 0; s[2] = 6; s[3] = 47;
        sx_bank_export(s, 4); sx_state("dx7 bank export U006 U001 U007 U048");
        for (k = 0; k < 40u; k++) s[k] = (uint16_t)(k < 8u ? 63u - k : k);
        sx_bank_export(s, 40); sx_state("dx7 bank export 40 slots");
    }
}

/* ------------------------------------------------------------------ CZ-1 */
static uint32_t cz_frame(uint8_t *f, uint32_t cmd, uint32_t ch, const uint8_t *raw, uint32_t n, int head7)
{
    uint32_t o = 0, j;
    f[o++] = 0xf0; f[o++] = 0x44; f[o++] = 0; f[o++] = 0; f[o++] = (uint8_t)(0x70u | ch); f[o++] = (uint8_t)cmd;
    if (head7) f[o++] = 0x60;
    for (j = 0; j < n; j++) { f[o++] = raw[j] & 15u; f[o++] = raw[j] >> 4; }
    f[o++] = 0xf7;
    return o;
}
static void cz(void)
{
    uint8_t raw[144], f[300];
    uint32_t n, k;
    sx_fresh(); sx_engine(0, ENGI_CZ, 5); sx_engine(2, ENGI_CZ, 20); song.sel = 0;
    sx_state("cz start");
    (void)preset_patch(ENGI_CZ, 12, raw, 144);
    memcpy(raw + 128, "PARITY TONE 144!", 16);
    n = cz_frame(f, 0x30, 0, raw, 144, 0); sx_in(f, n); sx_state("cz direct 144 staged");
    sx_choose(7); sx_state("cz direct 144 into U008");
    (void)preset_patch(ENGI_CZ, 30, raw, 144);
    for (k = 0; k < 2u; k++) { raw[18 + 57 * k] = (uint8_t)(3 + k); raw[16 + 57 * k] &= 15u; }
    n = cz_frame(f, 0x30, 1, raw, 128, 0);
    for (k = 0; k < 2u; k++) f[6 + 2 * (18 + 57 * k)] = (uint8_t)(3 + k), f[7 + 2 * (18 + 57 * k)] = 0;
    sx_in(f, n); sx_state("cz direct 128 staged");
    sx_choose(8); sx_state("cz direct 128 into U009");
    raw[16] = 0xAF; n = cz_frame(f, 0x30, 0, raw, 144, 0); sx_in(f, n); sx_state("cz direct invalid");
    /* the handshake: the computer sends to the CZ (0x21: 144 bytes), the CZ answers the header, then F7 */
    (void)preset_patch(ENGI_CZ, 40, raw, 144);
    n = cz_frame(f, 0x21, 0, raw, 144, 1);
    sx_in(f, 7); sx_state("cz send header answered");
    host_wire_n = 0; sx_usb(f + 7, n - 7); SX_SERVICE(); sx_state("cz send tone staged");
    sysex_cancel();
    n = cz_frame(f, 0x20, 2, raw, 128, 1);
    sx_in(f, 7); host_wire_n = 0; sx_usb(f + 7, n - 7); SX_SERVICE(); sx_state("cz send 128 staged");
    sx_choose(9); sx_state("cz send 128 into U010");
    /* requests: direct (the part's tone), 0x10 (the CZ-101's 128 bytes), the handshake, a timeout */
    { uint8_t r[] = {0xf0, 0x44, 0, 0, 0x70, 0x11, 0x60, 0xf7}; sx_in(r, 8); sx_state("cz request 144"); }
    { uint8_t r[] = {0xf0, 0x44, 0, 0, 0x72, 0x10, 0x60, 0xf7}; sx_in(r, 8); sx_state("cz request 128 ch 3"); }
    {
        uint8_t r[] = {0xf0, 0x44, 0, 0, 0x70, 0x11, 0x60, 0x70, 0x31, 0xf7};
        sx_in(r, 7); sx_state("cz handshake header");
        host_wire_n = 0; sx_usb(r + 7, 2); SX_SERVICE(); sx_state("cz handshake data");
        host_wire_n = 0; sx_usb(r + 9, 1); SX_SERVICE(); sx_state("cz handshake end");
        sx_in(r, 7); fm1_ms += 3001u; host_wire_n = 0; SX_SERVICE(); SX_SERVICE(); sx_state("cz handshake timeout");
        sx_in(r, 7); host_wire_n = 0; { uint8_t s[] = {0x90, 60, 100}; sx_usb(s, 3); } SX_SERVICE(); SX_SERVICE();
        sx_state("cz handshake cut by a note");
    }
    sx_engine(0, ENGI_SID, 0); sx_engine(2, ENGI_SID, 0);
    { uint8_t r[] = {0xf0, 0x44, 0, 0, 0x70, 0x11, 0x60, 0xf7}; sx_in(r, 8); sx_state("cz request with no CZ part"); }
    n = cz_frame(f, 0x30, 0, raw, 144, 0); sx_in(f, n); sx_state("cz direct with no CZ part");
    sysex_cancel();
    sx_engine(1, ENGI_CZ, 3); song.sel = 1;
    host_wire_n = 0; (void)library_send(ENGI_CZ, 7); sx_state("cz library send U008");
    bulk = (typeof(bulk)){.on = 1, .engine = ENGI_CZ, .anchor = 6, .end = 9}; host_wire_n = 0; bulk_send();
    sx_state("cz bulk send U007..U010");
}

/* ------------------------------------------------------------------ Prophet */
static uint32_t p5_frame(uint8_t *f, const uint8_t *raw, uint32_t size, uint32_t model, uint32_t cmd, uint32_t g, uint32_t p)
{
    uint32_t n = 0, pos = 0;
    f[n++] = 0xf0; f[n++] = 1; f[n++] = (uint8_t)model; f[n++] = (uint8_t)cmd;
    if (cmd == 2u) { f[n++] = (uint8_t)g; f[n++] = (uint8_t)p; }
    while (pos < size) {
        uint32_t k = size - pos < 7u ? size - pos : 7u, m = n++;
        f[m] = 0;
        for (uint32_t j = 0; j < k; j++, pos++) { f[m] |= (uint8_t)((raw[pos] >> 7) << j); f[n++] = raw[pos] & 127u; }
    }
    f[n++] = 0xf7;
    return n;
}
static void prophet(void)
{
    uint8_t raw[138], f[200];
    uint32_t n, k;
    sx_fresh(); sx_engine(0, ENGI_PROPHET, 12); sx_engine(3, ENGI_PROPHET, 77); song.sel = 3;
    sx_state("p5 start");
    (void)preset_patch(ENGI_PROPHET, 50, raw, 138);
    memcpy(raw + 65, "PARITY PROGRAM 5", 16);
    n = p5_frame(f, raw, 133, 0x32, 2, 4, 17); sx_in(f, n); sx_state("p5 program 2 staged");
    sx_choose(127); sx_state("p5 program into U128");
    for (k = 0; k < 128u; k++) raw[k] = sx_rand();
    n = p5_frame(f, raw, 128, 0x31, 3, 0, 0); sx_in(f, n); sx_state("p5 edit buffer 128 staged");
    sx_choose(126); sx_state("p5 edit buffer into U127");
    n = p5_frame(f, raw, 128, 0x32, 3, 0, 0); f[n - 4] |= 4u; sx_in(f, n); sx_state("p5 bad mask");
    n = p5_frame(f, raw, 133, 0x33, 3, 0, 0); sx_in(f, n); sx_state("p5 another model");
    { uint8_t r[] = {0xf0, 1, 0x32, 6, 0xf7}; sx_in(r, 5); sx_state("p5 request the edit buffer"); }
    { uint8_t r[] = {0xf0, 1, 0x32, 5, 3, 7, 0xf7}; sx_in(r, 7); sx_state("p5 request U128"); }
    { uint8_t r[] = {0xf0, 1, 0x31, 5, 3, 6, 0xf7}; sx_in(r, 7); sx_state("p5 request U127 as Rev 3"); }
    { uint8_t r[] = {0xf0, 1, 0x32, 5, 0, 1, 0xf7}; sx_in(r, 7); sx_state("p5 request an empty slot"); }
    host_wire_n = 0; (void)library_send(ENGI_PROPHET, 127); sx_state("p5 library send U128");
    host_wire_n = 0; SX_STORE_SEND(ENGI_PROPHET, 3); sx_state("p5 STORE SEND");
}

int main(int argc, char **argv)
{
    static char want[65536];
    if (argc > 2 && !strcmp(argv[1], "emit")) {
        sx_emit = fopen(argv[2], "w");
        if (!sx_emit) return 2;
    } else {
        FILE *fp = fopen(argc > 1 ? argv[1] : "tests/fixtures/sysex-88f4eb02.txt", "r");
        size_t got = fp ? fread(want, 1, sizeof want - 1u, fp) : 0;
        if (fp) fclose(fp);
        want[got] = 0;
        sx_want = want;
        if (!got) { printf("sysex: no fixture\n"); return 1; }
    }
    dx7(); cz(); prophet();
    if (sx_emit) { fclose(sx_emit); printf("sysex: %d steps written\n", sx_n); return 0; }
    printf("sysex: %d steps as 88f4eb02's: %s\n", sx_n, sx_bad ? "FAILED" : "passed");
    return sx_bad != 0;
}
