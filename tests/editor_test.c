/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The firmware editor handler, USB framing and UART recovery against RAM flash.
 * Build with the generated tables and the same flags as hostsim.c. */
static unsigned char host_samples[3][0x14000];
#define SMP_USER_XIP(k) host_samples[k]
#define MELODEE_OTA 1
#define MELODEE_FLASH 0
#define MELODEE_VERSION "TEST"
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint32_t host_progress = 1, host_erases, host_writes;
static uint8_t host_wire[8192];
static uint32_t host_wire_n;
static void host_drain(void)
{
    while (so_r != so_w) {
        uint32_t p = sx_out_q[so_r++ % SXQ], cin = p & 15u, i;
        uint32_t n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (i = 0; i < n && host_wire_n < sizeof host_wire; i++)
            host_wire[host_wire_n++] = (uint8_t)(p >> (8u * (i + 1u)));
    }
}
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { host_drain(); fm1_ms++; }
static void fm1_wdt_feed(void)
{
    fm1_ms++;
    if (host_progress && transport_req == 2u) { seq_stop(); transport_req = 0; }
}
/* Target cli/sti do not nest: an inner pair reopens the outer section. */
static uint32_t host_irq_masked, host_irq_nested;
static void fm1_irq_off(void) { host_irq_nested += host_irq_masked; host_irq_masked = 1; }
static void fm1_irq_on(void) { host_irq_masked = 0; }
static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
static void panel_setup(void) {}
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

static uint32_t flash_ok = 1;
static void audio_silence(void) {}
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static uint8_t *host_flash_ptr(uint32_t off) { return &host_samples[0][0] + off - 0xA0000u; }
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    memset(host_flash_ptr(off), 0xFF, 4096); *took = 0; host_erases++; return 0;
}
static int fl_write(uint32_t off, const void *p, uint32_t n)
{
    memcpy(host_flash_ptr(off), p, n); host_writes++; return 0;
}
static int st_read(uint32_t off, void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int st_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int st_erase(uint32_t off) { (void)off; return -1; }
#include "../firmware/src/storage.c"
#include "../firmware/src/editor.c"
#include "../firmware/src/cz_store.c"

static int check(const char *what, int ok)
{
    printf("editor: %-70s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}
static void reset(void)
{
    uint32_t t, i;
    memset(&song, 0, sizeof song); memset(trk, 0, sizeof trk);
    memset(&chain, 0, sizeof chain); chain_defaults(&chain_config);
    pattern_init();
    memset(&sx_import,0,sizeof sx_import);memset(&bulk,0,sizeof bulk);resource_release(RES_NOTE_HISTORY);note_transaction_ptr=0;fm6_rx_ready=0;
    memset(&ed_w, 0, sizeof ed_w); memset(&ui, 0, sizeof ui);
    memset(&favorites, 0, sizeof favorites); memset(&settings, 0, sizeof settings); settings_init();
    memset(proj_slot, 0, sizeof proj_slot); up_cache_reset(); native_cache_reset();
    memset(&um, 0, sizeof um);
    host_progress = 1; host_erases = host_writes = host_wire_n = 0;
    transport_req = panic_req = 0; sx_ready = sx_collect = sx_busy = 0;
    so_r = so_w = mi_r = mi_w = 0; midi_in_overflow = 0; usb.config = 1;
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    for (t = 0; t < NTRK; t++) {
        track_defaults(&trk[t]); set_engine_of(&trk[t], 3); apply_preset_to(&trk[t], 0);   /* (SID: values on p[]) */
        trk[t].engine = trk[t].eng_req; track_defaults_steps(&trk[t]);
    }
}
static uint32_t request(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t i;
    host_wire_n = 0; ota_frame_done();
    sysex_byte(0xF0); sysex_byte(ED_HDR0); sysex_byte(ED_HDR1); sysex_byte(ED_HDR2); sysex_byte((uint8_t)cmd);
    for (i = 0; i < n; i++) sysex_byte(a[i]);
    sysex_byte(0xF7); ed_service(); host_drain();
    return host_wire_n;
}
static uint32_t pack7(const uint8_t *p, uint32_t n, uint8_t *a)
{
    uint32_t o = 0, i;
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = o++;
        a[m] = 0;
        for (i = 0; i < k; i++, n--) { a[m] |= (*p >> 7) << i; a[o++] = *p++ & 127u; }
    }
    return o;
}

static int preferences(void)
{
    int bad = 0;
    uint8_t a[4] = {0, 2, 0, 0};
    reset();
    uint32_t n = request(ED_INFO, a, 0);
    const uint8_t caps[]={0,0x55,1,57,0x4d,2,MOTION_MAX,1,0x42,1,3,0x50,1,NPAT,CHAIN_ROWS,0x46,1,FM6_NFAC,0,0x43,1,16,1,0,0,0x35,1,19,16,17,18,20,0,1,0x4e,1,64,0,0,1,0x55,1,0,3};
    bad+=check("INFO preserves existing IDs and advertises the shared 384-slot library",ED_SONG==33&&ED_FAV_SET==38&&n>sizeof caps&&!memcmp(host_wire+n-sizeof caps-1,caps,sizeof caps));
    request(ED_UI_SET, a, 2);
    bad += check("UI_SET updates the actual palette and reports RAM-only saving",
        host_wire[5] == 3 && settings.palette == 2 && T_BG == UI_PALETTES[2].bg);
    a[1] = NPALETTES; request(ED_UI_SET, a, 2);
    bad += check("out-of-range palette leaves the display unchanged", host_wire[5] == 1 && settings.palette == 2);
    a[0] = 1; a[1] = 1; request(ED_UI_SET, a, 2);
    bad += check("the retired font weight is not supported (rc 2), UI_STATE says 127",
        host_wire[5] == 2 && host_wire[8] == 57 && host_wire[10] == 127);
    a[0] = 9; request(ED_UI_SET, a, 2);
    bad += check("retired FX latch cannot be enabled", host_wire[5] == 2);
    a[0] = 2; request(ED_UI_SET, a, 2);
    bad += check("unsupported preference is reported without applying it", host_wire[5] == 2);
    a[0] = ENGI_DRUM; a[1] = 0; a[2] = 64; a[3] = 1;
    request(ED_FAV_SET, a, 4);
    bad += check("FAV_SET marks DRUM as a normal engine", host_wire[5] == 3 && favorite_has(ENGI_DRUM, 0));
    request(ED_FAV_GET, a, 4);
    bad += check("FAV_GET reads the bounded favorite range", host_wire[5] == 0 && host_wire[10] == 1);
    a[0] = USER_GENERAL; a[1] = 31; request(ED_FAV_SET, a, 4);
    bad += check("empty user slot cannot be starred", host_wire[5] == 1 && !favorite_has(USER_GENERAL, 31));
    up_store(31, "Saved"); request(ED_FAV_SET, a, 4);
    bad += check("saved user slot can be starred without changing its sound", host_wire[5] == 3 && favorite_has(USER_GENERAL, 31));
    a[1]=(UP_SLOTS-1u+8192u)&127u;a[2]=(UP_SLOTS-1u+8192u)>>7;a[3]=32; request(ED_FAV_GET, a, 4);
    bad += check("favorite range cannot cross the end of user slots", host_wire[5] == 1);
    request(ED_FAV_SET, a, 3);
    bad += check("short favorite writes return an error without reading absent bytes", host_wire[5] == 1);
    bad += check("UI_STATE rejects unexpected request bytes", request(ED_UI_STATE, a, 1) == 0);
    a[0] = 0; request(ED_SONG, a, 1);
    bad += check("SONG still responds through its original command", host_wire_n > 6 && host_wire[4] == 33 && host_wire[5] == 0);
    {
        uint8_t before_state[32];
        up_store(63, "UPPER FAVORITE");
        uint32_t nstate = request(ED_UI_STATE, a, 0);
        memcpy(before_state, host_wire, nstate);
        favorite_set(USER_GENERAL, 63, !favorite_has(USER_GENERAL, 63));
        request(ED_UI_STATE, a, 0);
        bad += check("upper-slot favorites change the editor synchronization signature", memcmp(before_state, host_wire, nstate) != 0);
    }
    return bad;
}

static int framing(void)
{
    uint32_t i, bad = 0;
    static const uint8_t rt[] = {0xF0, 0x7D, 0xF8, 0x46, 0xFE, 0x4C, ED_PING, 0xFF, 0xF7};
    static const uint8_t aborted[] = {0xF0, 0x7D, 0x46, 0x4C, ED_SET, 0, P_LEVEL, 0x90, 0, 64, 0xF7};
    reset();
    for (i = 0; i < sizeof rt; i++) sysex_byte(rt[i]);
    ed_service(); host_drain();
    bad += check("realtime bytes interleaved in SysEx do not enter the editor frame",
                 host_wire_n == 7u && host_wire[4] == ED_PING && host_wire[5] == 0);
    ota_frame_done(); host_wire_n = 0;
    for (i = 0; i < sizeof aborted; i++) sysex_byte(aborted[i]);
    ed_service(); host_drain();
    bad += check("a non-realtime status aborts SysEx without changing parameters",
                 !sx_ready && !host_wire_n && TSEL->p[P_LEVEL] == TP[P_LEVEL].def);
    bad += check("a valid request works after an aborted frame", request(ED_PING, 0, 0) == 7u);
    ota_frame_done();
    midi_in_event(0x467DF004u);                          /* F0 7D 46 */
    midi_in_event(0x00004C04u);                          /* unfinished SysEx */
    midi_in_event(0x643C9009u);
    sysex_byte(0xF7);
    bad += check("a channel event in another USB packet aborts an unfinished SysEx",
                 !sx_ready && mi_w == 1u && midi_in_q[0] == 0x643C9009u);
    midi_in_event(0x643C8009u);                          /* wrong CIN */
    midi_in_event(0x643CFF09u);                          /* wrong status */
    midi_in_event(0xFF3C9009u);                          /* not a seven-bit velocity */
    midi_in_event(0x64FF9009u);                          /* not a seven-bit note */
    bad += check("USB channel packets reject mismatched CIN and high data bits", mi_w == 1u);
    midi_in_event(0xFF05C00Cu);                          /* one-byte status: padding is ignored */
    midi_in_event(0xFF07D00Du);
    bad += check("USB program and channel pressure keep their one-byte payload", mi_w == 3u);
    mi_w = UINT32_MAX - 5u; mi_r = mi_w;
    for (i = 0; i < MQ + 8u; i++) midi_in_event(0x643C9009u);
    bad += check("USB MIDI ring stops at capacity across counter wrap", mi_w - mi_r == MQ && midi_in_overflow);
    mi_r = mi_w; midi_in_event(0x643C9009u);
    bad += check("USB rejects new messages until the audio consumer clears overflow", mi_w == mi_r);
    midi_in_overflow = 0; midi_in_event(0x643C9009u);
    bad += check("USB resumes after the audio consumer clears overflow", mi_w - mi_r == 1u);
    return bad;
}

static int uart_recovery(void)
{
    uint32_t i;
    int bad = 0;
    reset();
    um_byte(0x90); um_byte(50);
    for (i = 0; i < UM_RING; i++) um_ring[i] = 0;
    um_ring[126] = 0x90; um_ring[127] = 72; um_ring[0] = 99;
    uart_midi_take(UM_RING + 1u);
    bad += check("UART DMA overrun never combines a stale partial note with new data",
                 !mi_w && midi_in_overflow && um.drops == 2u && um.bytes == UM_RING);
    midi_in_overflow = 0; um_byte(0x90); um_byte(72); um_byte(99);
    bad += check("UART receives a complete fresh note after DMA recovery",
                 mi_w == 1u && midi_in_q[0] == 0x63489009u);
    reset();
    um_byte(0x90); um_byte(50);
    for (i = 0; i < UM_RING; i++) um_ring[i] = 60;
    uart_midi_take(UM_RING + 2u);
    bad += check("UART discards running-status data after lost bytes until a new status",
                 !mi_w && midi_in_overflow && um.drops == 2u && !um.st && !um.got);
    midi_in_overflow = 0;
    um_byte(0x91); um_byte(70); um_byte(100);
    bad += check("UART recovers immediately on a complete channel message", mi_w == 1u && midi_in_q[0] == 0x64469109u);
    reset(); mi_w = MQ;
    um_byte(0x90); um_byte(60); um_byte(100);
    bad += check("UART queue overflow latches the same recovery flag as USB", midi_in_overflow && um.drops == 1u && mi_w == MQ);
    mi_r = mi_w; um_byte(61); um_byte(101);
    bad += check("UART rejects a new stream until audio clears overflow", mi_w == mi_r && um.drops == 2u);
    midi_in_overflow = 0; um_byte(62); um_byte(102);
    bad += check("UART resumes valid running status after audio clears overflow",
                 mi_w - mi_r == 1u && midi_in_q[mi_r % MQ] == 0x663E9009u);
    return bad;
}

static int steps(void)
{
    uint8_t a[80] = {0, 1, 60, 0, 0, 0, ST_NOTE, SF_ACCENT, 100, 0x12, 0x02, 1};
    step_t before;
    uint32_t n, ok = 1;
    int bad = 0;
    reset();
    TSEL->step[0].hit = TSEL->step[0].acc = 0x80;
    bad += check("legacy 8-byte step writes preserve lane data",
                 request(ED_STEP_SET, a, 9) == 19u && TSEL->step[0].note[0] == 60 &&
                 TSEL->step[0].hit == 0x80 && TSEL->step[0].acc == 0x80);
    bad += check("full grid step writes preserve high lane bits and constrain accents",
                 request(ED_STEP_SET, a, 12) == 19u && TSEL->step[0].hit == 0x92 && TSEL->step[0].acc == 2);
    before = TSEL->step[0]; a[2] = 71;
    for (n = 2; n <= sizeof a; n++) {
        if (n == 9u || n == 12u || n == 13u) continue;
        ok &= !request(ED_STEP_SET, a, n) && !memcmp(&before, &TSEL->step[0], sizeof before);
    }
    bad += check("partial or oversized step payloads never mutate a valid step", ok);
    a[0] = 1; a[1] = 0; memcpy(a + 2, (const uint8_t[]){1,64,0,0,0,ST_NOTE,0,99}, 8);
    bad += check("TRACK_STEP accepts its legacy payload on an unselected track",
                 request(ED_TRACK_STEP, a, 10) == 20u && trk[1].step[0].note[0] == 64 && song.sel == 0);
    before = trk[1].step[0]; a[3] = 65;
    bad += check("TRACK_STEP rejects an incomplete grid extension",
                 !request(ED_TRACK_STEP, a, 11) && !memcmp(&before, &trk[1].step[0], sizeof before));
    return bad;
}


static int song_protocol(void)
{
    uint8_t a[] = {1, 1, 0, 2};
    chain_config_t before;
    int bad = 0;
    reset();
    bad += check("SONG sets and queries the complete chain without changing selection",
                 request(ED_SONG, a, sizeof a) == 14u && !host_wire[6] && chain_config.count == 1 && song.sel == 0);
    before = chain_config; a[3] = 0;
    bad += check("SONG invalid repeats leave the chain unchanged",
                 request(ED_SONG, a, sizeof a) == 14u && host_wire[6] == 1 && !memcmp(&before, &chain_config, sizeof before));
    a[3] = 2; chain.armed = 1;
    bad += check("SONG cannot replace a chain while its start is pending",
                 request(ED_SONG, a, sizeof a) == 14u && host_wire[6] == 2 && !memcmp(&before, &chain_config, sizeof before));
    chain.armed = 0; a[0] = 2;
    bad += check("SONG PLAY refuses extra argument bytes", !request(ED_SONG, a, 2) && !transport_req);
    return bad;
}

static int malformed_saves(void)
{
    static const uint8_t slot[] = {1, 4}, name[] = {0, 'X', 0, 'Y'};
    const uint8_t save[] = {1, 0};
    project_store_t old;
    int bad = 0;
    reset();
    bad += check("PROJECT refuses an invalid slot instead of wrapping to slot 0",
                 !request(ED_PROJECT, slot, sizeof slot) && !project_used(0));
    bad += check("UP_STORE rejects bytes after its name terminator without saving",
                 request(ED_UP_STORE, name, sizeof name) == 8u && host_wire[6] == 1 && !up_used(0));
    bad += check("a successful PROJECT save confirms its slot", request(ED_PROJECT, save, sizeof save) == 9u && project_used(0));
    old = proj_slot[0];
    TSEL->p[P_LEVEL] = 21; song.playing = 1; host_progress = 0;
    bad += check("PROJECT never confirms an old used slot when STOP timed out",
                 !request(ED_PROJECT, save, sizeof save) && !memcmp(&old, &proj_slot[0], sizeof old));
    return bad;
}

/* FM6 patches (cmds 68..71): a track's own patch, the bank (RAM here), the factory patches, malformed frames */
static int fm6_patches(void)
{
    int bad = 0;
    uint8_t a[2 + FM6_PACKED], pk[FM6_PACKED];
    uint32_t n, i;
    reset();
    a[0] = ED_FM6_FACTORY; a[1] = 3;
    n = request(ED_FM6_GET, a, 2);
    bad += check("FM6_GET factory 4: the packed record", n == 5u + 3u + FM6_PACKED + 1u && host_wire[7] == 0 &&
                 !memcmp(host_wire + 8, FM6_FACTORY[3], FM6_PACKED));
    memcpy(pk, FM6_FACTORY[3], FM6_PACKED);
    memcpy(pk + 118, "MY PATCH  ", 10);
    a[0] = ED_FM6_TRACK; a[1] = 2; memcpy(a + 2, pk, FM6_PACKED);
    n = request(ED_FM6_PUT, a, sizeof a);
    bad += check("FM6_PUT track 3: rc 0, the track's patch", n == 9u && host_wire[7] == 0 &&
                 !memcmp(fm6_patch[2] + FP_NAME, "MY PATCH  ", 10));
    a[1] = 2;
    n = request(ED_FM6_GET, a, 2);
    bad += check("FM6_GET track 3: as sent", host_wire[7] == 0 && !memcmp(host_wire + 8, pk, FM6_PACKED));
    a[0] = ED_FM6_BANK; a[1] = 4;
    n = request(ED_FM6_GET, a, 2);
    bad += check("retired FM6 bank GET returns rc 3", n == 9u && host_wire[7] == 3);
    a[0] = ED_FM6_BANK; a[1] = 4; memcpy(a + 2, pk, FM6_PACKED);
    request(ED_FM6_PUT, a, sizeof a);
    bad += check("retired FM6 bank PUT returns rc 3", host_wire[7] == 3);
    set_engine_of(TSEL, ENGI_FM6);
    (void)up_store(63, "LAST PRESET");
    a[0] = ED_FM6_USER; a[1] = 63; memcpy(a + 2, pk, FM6_PACKED);
    a[2 + 14] = 120;
    request(ED_FM6_PUT, a, sizeof a);
    request(ED_FM6_GET, a, 2);
    bad += check("user U64 owns its sanitized FM6 voice", host_wire[7] == 0 && host_wire[8 + 14] == 99 &&
                 !memcmp(host_wire + 8 + 118, "MY PATCH  ", 10));
    n = request(ED_FM6_LIST, a, 0);
    bad += check("FM6_LIST advertises 24 factory voices and no bank", host_wire[5] == FM6_NFAC && host_wire[6] == 0);
    a[0] = 4;
    request(ED_FM6_ERASE, a, 1);
    bad += check("retired FM6 bank ERASE returns rc 3", host_wire[6] == 3);
    a[0] = ED_FM6_TRACK; a[1] = 4;
    request(ED_FM6_PUT, a, sizeof a);
    i = host_wire[7];
    request(ED_FM6_PUT, a, 20);
    bad += check("FM6_PUT: a fifth track or a short record: rc 1", i == 1u && host_wire[7] == 1u);
    a[0] = ED_FM6_BANK; a[1] = 31;
    request(ED_FM6_GET, a, 2);
    bad += check("FM6_GET retired bank: rc 3", host_wire[7] == 3u);
    return bad;
}

/* UP_PUT -> UP_GET: every value round-trips through the v4 record (a byte each), negative ones too */
static int user_preset_roundtrip(void)
{
    static uint8_t a[16 + 2u * P_COUNT + 32u];
    int16_t want[P_COUNT], got[P_COUNT];
    uint32_t k = 0, i, p, ok;
    int bad = 0;
    reset();
    a[k++] = 3; a[k++] = 3;                                /* slot U04, SID */
    memcpy(a + k, "ROUND", 5); k += 5; a[k++] = 0;
    for (i = 0; i < P_COUNT; i++) {
        const param_desc_t *d = param_desc_of(0, i);
        int16_t v = d->def;
        uint32_t u;
        if (i == P_LEVEL) v = 90;
        if (i == P_PAN) v = -20;
        if (i == P_ED_FLT) v = -40;
        if (i == P_LD_FLT) v = d->min;
        if (i == P_E1) v = d->max;
        want[i] = v; u = (uint32_t)(v + 8192);
        a[k++] = u & 127u; a[k++] = (u >> 7) & 127u;
    }
    for (i = 0; i < 16u; i++) { a[k++] = i & 1u ? 0u : (uint8_t)(48u + i); a[k++] = 0; }
    request(ED_UP_PUT, a, k);
    bad += check("UP_PUT stores a v4 record (no flash here: kept in RAM)",
                 host_wire[6] != 1u && up_used(3) && up_rec(3)->ver == UP_VER);
    up_values(up_rec(3), got);
    for (i = 0, ok = 1; i < P_COUNT; i++) ok &= got[i] == want[i];
    bad += check("UP_PUT keeps every value (PAN -20, FLT -40, min and max)", ok);
    a[0] = 3;
    request(ED_UP_GET, a, 1);
    ok = host_wire[5] == 3 && host_wire[6] == 1 && host_wire[7] == 3 && !memcmp(host_wire + 8, "ROUND", 6);   /* (slot, used, SID) */
    for (i = 0, p = 14; i < P_COUNT; i++, p += 2u)
        ok &= (int32_t)(host_wire[p] | host_wire[p + 1] << 7) - 8192 == want[i];
    for (i = 0; i < 16u; i++, p += 2u)
        ok &= host_wire[p] == (i & 1u ? 0u : 48u + i);
    bad += check("UP_GET returns the values and the pattern UP_PUT sent", ok);
    return bad;
}

static int bank_protocol(void)
{
    int bad=0; reset(); uint8_t a[12] = {2,1,7};
    bad += check("PATTERN selects a track's bank without changing track selection", request(ED_PATTERN,a,3)==12 && !host_wire[7] && trk[2].pattern==7 && !song.sel);
    a[2]=8;
    bad += check("PATTERN rejects out-of-range bank IDs", request(ED_PATTERN,a,3)==12 && host_wire[7] && trk[2].pattern==7);
    uint8_t row[] = {1,1,0,2,4,7,3};
    bad += check("bank SONG independently assigns four track banks", request(ED_BANK_SONG,row,sizeof row)==17 && !host_wire[6] && chain_patterns[0][3]==7 && chain_config.row[0].repeat==3);
    row[4]=8;
    bad += check("malformed bank SONG leaves the arrangement unchanged", request(ED_BANK_SONG,row,sizeof row)==17 && host_wire[6]==1 && chain_patterns[0][2]==4);
    a[0]=0; uint32_t writes=host_writes;
    bad += check("sample upload and erase commands cannot write into pattern storage", request(ED_SMP_BEGIN,a,1)==0 && request(ED_SMP_ERASE,a,1)==0 && host_writes==writes && !host_erases);
    request(ED_SMP_INFO,a,0);
    bad += check("sample inventory advertises zero user slots", host_wire[5]==0);
    return bad;
}
static int cz_native_protocol(void)
{
    int bad=0; uint8_t raw[CZ_BYTES],a[322]; reset();cz_init();
    cz_patch_init(raw);raw[20]=0x37;raw[16]=0x42;raw[17]=17;raw[14]=0xE0;raw[15]=0xDE;
    for(uint32_t k=0;k<8;k++){raw[21+2*k]=(uint8_t)(k*15);raw[22+2*k]=(uint8_t)(k*17);}raw[28]|=128;
    a[0]=0;a[1]=2;for(uint32_t i=0;i<CZ_BYTES;i++){a[2+2*i]=raw[i]&15;a[3+2*i]=raw[i]>>4;}
    request(ED_CZ_PUT,a,290);
    bad+=check("CZ PUT preserves all eight DCA points, velocity and hidden waves",host_wire[7]==0 && trk[2].eng_req==ENGI_CZ && trk[2].p[P_E7]==CZ_NATIVE && !memcmp(cz_patch[2].raw,raw,CZ_BYTES));
    request(ED_CZ_GET,a,2);int same=host_wire_n==297 && host_wire[7]==0;
    for(uint32_t i=0;i<CZ_BYTES;i++)same &= host_wire[8+2*i]==(raw[i]&15) && host_wire[9+2*i]==(raw[i]>>4);
    bad+=check("CZ GET returns exact low-first tone nibbles",same);
    a[40]=16;request(ED_CZ_PUT,a,290);bad+=check("invalid CZ upload leaves live patch intact",host_wire[7]==1 && !memcmp(cz_patch[2].raw,raw,CZ_BYTES));a[40]=raw[19]&15;
    a[0]=1;a[1]=8;request(ED_CZ_PUT,a,290);bad+=check("CZ tone uses atomic ordinary preset storage",host_wire[7]==0 && up_used(8) && up_rec(8)->ver==UP_VER_CZ && !memcmp(up_rec(8)->packed,raw,CZ_BYTES));
    song.sel=1;up_load(8);bad+=check("loading user CZ preset restores all 144 native bytes",TSEL->p[P_E7]==CZ_NATIVE && !memcmp(cz_patch[1].raw,raw,CZ_BYTES));
    project_t q,r;project_store_t packed;project_capture(&q);bad+=check("native tones survive project serialization",proj_pack(&packed,&q) && proj_unpack(&r,packed.raw) && !memcmp(q.cz,r.cz,sizeof q.cz));
    static uint8_t banked[BANK_STORE_SIZE];
    bad+=check("native tones survive full pattern-bank serialization",bank_pack(banked,&q,1) && bank_valid(banked,sizeof banked) && !memcmp(q.cz,proj_scratch.cz,sizeof q.cz));
    template_save(); static tmpl_t saved; saved=tmpl;
    memset(cz_patch,0,sizeof cz_patch); memset(&tmpl,0,sizeof tmpl);
    int restored=tmpl_take((const uint8_t *)&saved,sizeof saved);template_load();
    bad+=check("native tone bytes and mode survive a saved template",restored && trk[1].p[P_E7]==CZ_NATIVE && !memcmp(cz_patch[1].raw,raw,CZ_BYTES));
    const uint32_t sizes[]={sizeof(tmpl_fx_legacy_t),sizeof(tmpl_fx_single_t),sizeof(tmpl_t)-NTRK*2u*(P_COUNT-116u)-20u,sizeof(tmpl_t)-NTRK*2u*(P_COUNT-117u)-4u,sizeof(tmpl_t)-NTRK*2u*(P_COUNT-119u)-4u,sizeof(tmpl_t)+NTRK*2u*(125u-P_COUNT),sizeof(tmpl_t)-NTRK*2u*(P_COUNT-122u),sizeof(tmpl_t)};
    for(uint32_t k=0;k<NELEM(sizes);k++){
        uint8_t begin[12]={0,1},abort[2]={3,1};uint32_t len=sizeof(persist_t)+sizes[k];
        for(uint32_t j=0;j<5;j++)begin[2+j]=(uint8_t)((len>>(7*j))&127u);
        bad+=check("settings transfer accepts every supported template layout",!ed_bk_write(begin,sizeof begin) && !ed_bk_write(abort,sizeof abort));
    }
    {uint8_t malformed[12]={0,1,1};bad+=check("settings transfer rejects undersized template payload",ed_bk_write(malformed,sizeof malformed)==1);}
    return bad;
}
static int cz_casio_sysex(void)
{
    int bad=0;reset();cz_init();trk[0].eng_req=ENGI_CZ;uint8_t raw[CZ_BYTES],frame[296],got[CZ_BYTES];cz_patch_init(raw);raw[16]=0x42;raw[17]=17;raw[18]=5;raw[19]=83;raw[128]='S';
    const uint8_t head[]={0xf0,0x44,0,0,0x70,0x21,0x60};memcpy(frame,head,7);for(uint32_t j=0;j<CZ_BYTES;j++){frame[7+2*j]=raw[j]&15;frame[8+2*j]=raw[j]>>4;}frame[295]=0xf7;
    for(uint32_t j=0;j<sizeof frame;j++)sysex_byte(frame[j]);cz_service();host_drain();
    bad+=check("direct Casio MIDI SysEx stages exact named tone in the CZ user list",sx_import.count==1 && library_engine()==ENGI_CZ && !memcmp(sx_import.data,raw,CZ_BYTES) && cur_page()->graph==GR_USER);
    ui.uslot=7;sysex_choose();native_load(ENGI_CZ,7,0);memcpy(got,cz_patch[0].raw,CZ_BYTES);
    frame[10]=16;for(uint32_t j=0;j<sizeof frame;j++)sysex_byte(frame[j]);cz_service();
    bad+=check("invalid direct Casio SysEx leaves the native tone intact",!memcmp(cz_patch[0].raw,got,CZ_BYTES));
    const uint8_t request[]={0xf0,0x44,0,0,0x70,0x11,0x60,0xf7};host_wire_n=0;for(uint32_t j=0;j<sizeof request;j++)sysex_byte(request[j]);cz_service();host_drain();
    int same=host_wire_n==295 && host_wire[5]==0x30;for(uint32_t j=0;same && j<CZ_BYTES;j++)same=host_wire[6+2*j]==(raw[j]&15) && host_wire[7+2*j]==(raw[j]>>4);
    bad+=check("direct Casio tone request exports exact native bytes",same);return bad;
}

static int cz_dedicated_banks(void)
{
    int bad=0;reset();cz_init();cz_bank_boot();uint8_t raw[CZ_BYTES];cz_patch_init(raw);
    static up_bank_t bank;
    for(uint32_t k=0;k<8u;k++){
        memset(&bank,0,sizeof bank);bank.magic=UP_BANK_MAGIC;bank.rsize=sizeof(up_rec_t);bank.nslot=UP_PER_BANK;
        raw[128]=(uint8_t)('A'+k);shared_native_record(&bank.r[0],ENGI_CZ,raw);
        raw[128]=(uint8_t)('a'+k);shared_native_record(&bank.r[17],ENGI_CZ,raw);
        memcpy(ED_BK_RAW,&bank,sizeof bank);ed_bk_id=(uint8_t)(9+k);ed_bk_len=ed_bk_pos=sizeof bank;ed_bk_crc=st_crc32(&bank,sizeof bank);
        bad+=check("shared host bank commits through bounded archive staging",!ed_bk_commit());
    }
    for(uint32_t k=0;k<8u;k++)bad+=check("shared CZ records preserve complete voices across bank evictions",native_used(ENGI_CZ,(8u+k)*18u+17u)&&native_raw(ENGI_CZ,(8u+k)*18u+17u)[128]=='a'+k);
    bad+=check("full backups include all 22 shared preset objects",ED_BK_N==29u);
    return bad;
}
static int native_protocol(void)
{
    int bad=0;reset();cz_init();uint8_t a[3+2*CZ_BYTES],fm[FM6_PACKED],cz[CZ_BYTES];
    fm6_factory(5,fm);cz_patch_init(cz);memcpy(cz+128,"SIXTEEN CHAR CZ! ",16);
    a[0]=ENGI_FM6;a[1]=2;a[2]=63;memcpy(a+3,fm,sizeof fm);request(ED_NATIVE,a,3+sizeof fm);
    bad+=check("NATIVE saves F064 independently of general user records",!host_wire[8] && native_used(ENGI_FM6,63) && up_used(63));
    a[1]=1;uint32_t n=request(ED_NATIVE,a,3);
    bad+=check("NATIVE GET returns exact DX7 VMEM bytes",n==138 && !host_wire[8] && !memcmp(host_wire+9,fm,sizeof fm));
    a[0]=ENGI_CZ;a[1]=2;a[2]=127;
    for(uint32_t j=0;j<CZ_BYTES;j++){a[3+2*j]=cz[j]&15;a[4+2*j]=cz[j]>>4;}
    request(ED_NATIVE,a,sizeof a);
    bad+=check("NATIVE saves Z128 without using general or FM6 slots",!host_wire[8] && native_used(ENGI_CZ,127) && !memcmp(native_raw(ENGI_CZ,127),cz,sizeof cz));
    a[3]=16;request(ED_NATIVE,a,sizeof a);
    bad+=check("malformed native nibbles cannot replace a committed tone",host_wire[8]==1 && !memcmp(native_raw(ENGI_CZ,127),cz,sizeof cz));
    a[1]=0;a[2]=120;a[3]=16;request(ED_NATIVE,a,4);
    bad+=check("last native page clamps to Z128 and reports 128 slots",!host_wire[8] && host_wire[9]==8 && host_wire[10]==0 && host_wire[11]==1);
    a[1]=4;a[2]=127;a[3]=2;trk[2].p[P_PAN]=37;trk[2].p[P_LEVEL]=61;trk[2].step[0].note[0]=67;request(ED_NATIVE,a,4);
    bad+=check("native load targets track 3 and preserves pan, level and pattern",!host_wire[8] && trk[2].user==128 && !trk[2].user_native && trk[2].p[P_PAN]==37 && trk[2].p[P_LEVEL]==61 && trk[2].step[0].note[0]==67 && !memcmp(cz_patch[2].raw,cz,sizeof cz));
    trk[2].p[P_E2]=23;request(ED_NATIVE,a,4);
    bad+=check("same-engine native load also keeps track macros",trk[2].p[P_E2]==param_desc_of(ENGI_CZ,P_E2)->def);
    a[1]=7;a[2]=2;request(ED_NATIVE,a,3);
    bad+=check("native status encodes Z128 without a 7-bit overflow",!host_wire[8] && host_wire[9]==0 && host_wire[10]==1);
    uint8_t fav[4]={USER_NATIVE_CZ,127,64,1};request(ED_FAV_SET,fav,4);
    bad+=check("highest native preset can be favorited through editor",host_wire[5]==3 && favorite_has(USER_NATIVE_CZ,127));
    a[1]=3;a[2]=126;a[3]=2;request(ED_NATIVE,a,4);
    bad+=check("native STORE captures just the complete tone",!host_wire[8] && !memcmp(native_raw(ENGI_CZ,126),cz,sizeof cz) && !up_used(62));
    a[1]=6;memcpy(a+3,"FULL NAME 16 XYZ!",16);request(ED_NATIVE,a,19);
    bad+=check("native rename retains all 16 CZ name bytes",!host_wire[8] && !memcmp(native_raw(ENGI_CZ,126)+128,"FULL NAME 16 XYZ!",16));
    a[1]=5;a[2]=127;request(ED_NATIVE,a,3);
    bad+=check("native erase clears favorite and current preset origin",!host_wire[8] && !native_used(ENGI_CZ,127) && !favorite_has(USER_NATIVE_CZ,127) && !trk[2].user);
    a[0]=ENGI_FM6;a[1]=3;a[2]=0;a[3]=2;request(ED_NATIVE,a,4);
    bad+=check("wrong-engine store cannot create a native preset",host_wire[8]==1 && !native_used(ENGI_FM6,0));
    return bad;
}
static int shared_protocol(void)
{
    int bad=0;reset();cz_init();set_engine_of(TSEL,ENGI_CZ);TSEL->p[P_M1AMT]=37;
    uint8_t a[700]={3,127,2,0};memcpy(a+4,"A SHARED CZ TONE",16);request(ED_LIBRARY,a,20);
    bad+=check("shared STORE addresses U384 and captures sound parameters",!host_wire[8]&&native_used(ENGI_CZ,383)&&up_rec(383)->np==sizeof UP_NATIVE_PARAMS);
    a[0]=0;a[1]=120;a[2]=2;a[3]=16;request(ED_LIBRARY,a,4);
    bad+=check("shared LIST clamps the final page and reports 384 slots",!host_wire[8]&&host_wire[9]==8&&host_wire[10]==0&&host_wire[11]==3);
    a[0]=4;a[1]=127;a[2]=2;a[3]=2;trk[2].p[P_LEVEL]=64;trk[2].step[0].note[0]=69;request(ED_LIBRARY,a,4);
    bad+=check("shared LOAD preserves wide identity, mix and sequence on track 3",!host_wire[8]&&trk[2].user==384&&!trk[2].user_native&&trk[2].p[P_LEVEL]==64&&trk[2].p[P_M1AMT]==37&&trk[2].step[0].note[0]==69);
    a[0]=1;request(ED_LIBRARY,a,3);bad+=check("shared GET returns the actual engine of U384",!host_wire[8]&&host_wire[9]==1&&host_wire[10]==ENGI_CZ);
    a[0]=4;a[1]=0;a[2]=3;request(ED_LIBRARY,a,4);bad+=check("shared command rejects U385",host_wire[8]==1);
    p5_patch_t p;p5_patch_init(&p);sysex_stage(ENGI_PROPHET,(uint8_t *)&p,1,sizeof p);ui.uslot=383;sysex_choose();
    bad+=check("incoming Prophet asks before overwriting a CZ shared slot",ui.confirm==CF_IMPORT&&native_used(ENGI_CZ,383));ui.confirm=0;sysex_cancel();
    uint8_t raw[CZ_BYTES];cz_patch_init(raw);raw[128]=0;raw[143]=255;uint32_t pos=0;
    a[pos++]=2;a[pos++]=126;a[pos++]=2;a[pos++]=ENGI_CZ;memcpy(a+pos,"OPAQUE NAME",11);pos+=11;a[pos++]=0;
    for(uint32_t j=0;j<P_COUNT;j++){a[pos++]=0;a[pos++]=64;}memset(a+pos,0,32);pos+=32;a[pos++]=2u|32u|64u;a[pos++]=CZ_BYTES&127u;a[pos++]=CZ_BYTES>>7;pos+=pack7(raw,CZ_BYTES,a+pos);request(ED_LIBRARY,a,pos);
    bad+=check("shared PUT preserves every native name byte when requested",!host_wire[8]&&native_used(ENGI_CZ,382)&&!memcmp(native_raw(ENGI_CZ,382),raw,sizeof raw));
    a[0]=5;a[1]=127;a[2]=2;request(ED_LIBRARY,a,3);bad+=check("shared ERASE clears U384 and the loaded origin",!host_wire[8]&&!up_used(383)&&!trk[2].user);
    return bad;
}
static int names_whole(void)
{
    int bad = 0;
    reset();
    for (uint32_t e = 0; e < NENGINES; e++) {   /* 0.11 cut CZ-1's 65 names at a 600-byte reply */
        uint32_t got = 0, pages = 0, ok = 1;     /* PROPHET's 201 come in pages: ask from the next first */
        while (ok && pages++ < 8u) {
            uint8_t a[3] = {(uint8_t)e, (uint8_t)(got & 127u), (uint8_t)(got >> 7)};
            uint32_t n = request(ED_NAMES, a, got ? 3u : 1u), i = 7, k, count = host_wire[6];
            ok = n >= 12u && n <= sizeof ed_out && host_wire[5] == e && count <= 127u;
            for (k = 0; ok && k < count; k++) {
                char want[21];
                str_cpy(want, eng_preset(ENGINES[e], got + k)->name, 21);
                ok = got + k < ENGINES[e]->npresets && !strcmp((const char *)host_wire + i, want);
                while (i < n && host_wire[i++]) ;
            }
            for (k = 0; ok && k < 2u; k++)       /* both titles */
                while (i < n && host_wire[i++]) ;
            ok = ok && i + 5u == n && (host_wire[i] | host_wire[i + 1] << 7) == ENGINES[e]->npresets &&
                 (host_wire[i + 2] | host_wire[i + 3] << 7) == got && host_wire[n - 1u] == 0xF7 && (count || !ENGINES[e]->npresets);
            got += count;
            if (got >= ENGINES[e]->npresets) break;
        }
        if (!ok || got != ENGINES[e]->npresets) {
            printf("editor: NAMES of %s: %u of %u names\n", ENGINES[e]->name, got, ENGINES[e]->npresets);
            bad = 1;
        }
    }
    return check("NAMES of every engine carries all its presets and both titles", !bad);
}
static int concert_pitch(void)
{
    int bad = 0;
    uint8_t a[4] = {1, G_A4, (432u + 8192u) & 127u, (432u + 8192u) >> 7};
    reset(); tuning_a4 = 440;
    int16_t saved = song.g[G_A4];
    request(ED_SET, a, sizeof a);
    bad += check("A4 SET updates device tuning without writing project globals", tuning_a4 == 432 && song.g[G_A4] == saved);
    request(ED_GET, a, 2);
    bad += check("A4 GET returns the device reference", ed_rv(host_wire + 7) == 432);
    request(ED_DUMP, a, 0);
    bad += check("DUMP reports device A4 instead of the unused project field", ed_rv(host_wire + 7 + 2 * (P_COUNT + G_A4)) == 432);
    a[2] = 0; a[3] = 0; request(ED_SET, a, sizeof a);
    bad += check("A4 SET clamps to its supported range", tuning_a4 == A4_MIN);
    tuning_a4 = 440;
    int16_t old_budget=settings_budget,old_global=song.g[G_BUDGET];
    a[1]=G_BUDGET;a[2]=(32+8192)&127;a[3]=(32+8192)>>7;request(ED_SET,a,sizeof a);
    bad+=check("BUDGET SET changes the device pool without changing project data",settings_budget==32 && song.g[G_BUDGET]==old_global);
    request(ED_GET,a,2);bad+=check("BUDGET GET reports the live allowance",ed_rv(host_wire+7)==32);
    settings_budget=old_budget;
    return bad;
}
static int scale_catalogue(void)
{
    reset(); uint8_t a[2] = {0, P_SCALE};
    uint32_t n = request(ED_DESC, a, 2), at = 14;
    while (at < n && host_wire[at++]); /* label */
    while (at < n && host_wire[at++]); /* unit */
    int good = ed_rv(host_wire + 10) == SCALE_TOTAL - 1;
    for (uint32_t i = 0; i < SCALE_TOTAL; i++) {
        good &= at < n && strcmp((const char *)host_wire + at, N_SCALE[i]) == 0;
        while (at < n && host_wire[at++]);
    }
    return check("SCL DESC carries every legacy and microtonal name in one complete frame", good && at == n - 1);
}
static int prophet_protocol(void)
{
    reset();int bad=0;p5_patch_t patch,copy;uint8_t frame[P5_FRAME_MAX],args[P5_FRAME_MAX];
    p5_patch_init(&patch);patch.raw[97]=patch.raw[98]=255;memcpy(patch.raw+P5_NAME,"FULL PROPHET NAME XY!",20);
    uint32_t size=p5_patch_encode(&patch,frame,sizeof frame);args[0]=1;args[1]=2;memcpy(args+2,frame+1,size-2);
    request(95,args,size);
    bad+=check("Prophet PUT loads a selected track with every opaque byte intact",host_wire[6]==0&&trk[2].eng_req==19&&!memcmp(p5_patch_of(&trk[2]),&patch,sizeof patch));
    args[0]=0;request(95,args,2);frame[0]=240;memcpy(frame+1,host_wire+8,host_wire_n-9);frame[host_wire_n-8]=247;
    bad+=check("Prophet GET re-encodes a complete reusable native SysEx frame",p5_patch_decode(&copy,frame,host_wire_n-7)&&!memcmp(&copy,&patch,sizeof patch));
    args[0]=2;args[2]=P5_CUTOFF;args[3]=120;request(95,args,4);
    bad+=check("native field edit touches only its documented offset",host_wire[6]==0&&(patch.raw[P5_CUTOFF]=120,!memcmp(p5_patch_of(&trk[2]),&patch,sizeof patch)));
    args[2]=P5_KEY_FILTER;args[3]=3;request(95,args,4);bad+=check("out-of-range native field edit leaves the full patch unchanged",host_wire[6]==1&&!memcmp(p5_patch_of(&trk[2]),&patch,sizeof patch));
    args[0]=3;memcpy(args+2,"TWENTY CHARACTERS XYZ",20);request(95,args,22);memcpy(patch.raw+P5_NAME,args+2,20);
    bad+=check("native rename keeps all twenty name bytes",!host_wire[6]&&!memcmp(p5_patch_of(&trk[2]),&patch,sizeof patch));
    args[0]=1;memcpy(args+2,frame+1,size-2);args[2]=2;request(95,args,size);
    bad+=check("foreign native SysEx is rejected without changing the track",host_wire[6]==1&&!memcmp(p5_patch_of(&trk[2]),&patch,sizeof patch));
    return bad;
}
static int native_motion_protocol(void)
{
    int bad = 0; reset(); set_engine_of(&trk[0], ENGI_FM6); apply_preset_to(&trk[0], 0);
    uint32_t id = MO_NATIVE + FP_ALG; int16_t value = (int16_t)(MO_TAG_FM6 << 8 | 23);
    uint16_t encoded = (uint16_t)(value + 8192);
    uint8_t a[6] = {0,3,63u | 64u,id & 127u,encoded & 127u,encoded >> 7};
    request(ED_MOTION, a, 6);
    bad += check("native motion write retains high parameter bit and tagged value", !host_wire[6] && motion.count == 1u &&
        motion.event[0].param == id && motion.event[0].place == 63u && motion.event[0].value == value);
    bad += check("native motion query encodes the high id in the spare step bit", host_wire[10] == 127u && host_wire[11] == (id & 127u));
    a[1] = 4; request(ED_MOTION, a, 4);
    bad += check("native motion delete removes the same high parameter id", !host_wire[6] && !motion.count);
    a[1] = 3; a[2] = 0; a[3] = P_LEVEL; a[4] = 64; a[5] = 64; request(ED_MOTION, a, 6);
    bad += check("legacy motion wire writes retain the original format", !host_wire[6] && motion.event[0].param == P_LEVEL && motion.event[0].value == 64);
    motion_clear(&trk[0]);a[2]=0;a[3]=P_E1;a[4]=7;a[5]=64;request(ED_MOTION,a,6);
    a[3]=P_E1;a[4]=80;a[5]=70;request(ED_MOTION,a,6); /* FM6 tag 3, byte 80 */
    bad+=check("overlapping native/common IDs coexist through the protocol",!host_wire[6] && motion.count==2);
    a[1]=4;request(ED_MOTION,a,6);
    bad+=check("typed native delete retains the common engine event",!host_wire[6] && motion.count==1 && motion.event[0].param==P_E1 && motion.event[0].value==7);
    a[1]=3;request(ED_MOTION,a,6);a[1]=4;a[4]=7;a[5]=64;request(ED_MOTION,a,6);
    bad+=check("typed common delete retains the native event",!host_wire[6] && motion.count==1 && motion.event[0].value==(MO_TAG_FM6<<8|80));
    request(ED_MOTION,a,4);bad+=check("legacy delete clears the remaining raw ID",!host_wire[6] && !motion.count);
    return bad;
}
#ifndef EDITOR_TEST_NO_MAIN
#include "../firmware/src/fm6_store.c"
static void receive_library(const uint8_t *frame,uint32_t n)
{
    for(uint32_t i=0;i<n;i++)midi_in_event(15u|(uint32_t)frame[i]<<8);
    fm6_service();cz_service();ed_service();host_drain();ota_frame_done();
}
static int library_transfers(void)
{
    int bad=0;uint8_t frame[320],raw[CZ_BYTES];uint32_t n;
    reset();up_store(2,"NAMED GENERAL");up_rec_t saved=*up_rec(2);int16_t level=TSEL->p[P_LEVEL];
    bad+=check("general slot exports a complete 7-bit USB frame",!library_send(TSEL->eng_req,2));host_drain();
    n=host_wire_n;memcpy(frame,host_wire,n);
    bad+=check("general export has a valid checksum and exact bounded length",n==279u&&frame[0]==240&&frame[n-1]==247&&!sx_checksum(frame+1,n-2));
    frame[15]^=1;receive_library(frame,n);
    bad+=check("bad general checksum cannot stage or mutate a saved preset",!sx_import.count&&!memcmp(up_rec(2),&saved,sizeof saved));
    frame[15]^=1;receive_library(frame,n);
    bad+=check("named general import opens its pool without loading the track",sx_import.count==1&&sx_import.general&&cur_page()->graph==GR_USER&&TSEL->p[P_LEVEL]==level&&!memcmp(sx_import.data,&saved,sizeof saved));
    ui.uslot=5;sysex_choose();bad+=check("chosen general slot retains the entire record and name",!sx_import.count&&up_used(5)&&!memcmp(up_rec(5),&saved,sizeof saved));
    bulk=(typeof(bulk)){.on=1,.engine=TSEL->eng_req,.anchor=2,.end=5};host_wire_n=0;bulk_send();host_drain();
    bad+=check("general bulk export sends used selected slots and skips empty slots",host_wire_n==2*n&&!memcmp(host_wire,frame,n)&&!memcmp(host_wire+n,frame,n));
    reset();p5_patch_t p;p5_patch_init(&p);memcpy(p.raw+P5_NAME,"TWENTY NAME BYTES XYZ",20);p.raw[97]=255;
    n=p5_patch_encode(&p,frame,sizeof frame);receive_library(frame,n);
    bad+=check("Prophet import keeps all twenty name bytes and opens P slots from another engine",sx_import.count==1&&library_engine()==ENGI_PROPHET&&UP_SLOTS==384&&TSEL->eng_req!=ENGI_PROPHET&&!memcmp(sx_import.data,&p,sizeof p));
    ui.uslot=127;sysex_choose();host_wire_n=0;library_send(ENGI_PROPHET,127);host_drain();
    bad+=check("Prophet export reproduces the received frame and opaque bytes",!sx_import.count&&host_wire_n==n&&!memcmp(host_wire,frame,n));
    reset();cz_patch_init(raw);memcpy(raw+128,"FULL CZ NAME XYZ!",16);native_put(ENGI_CZ,7,raw);host_wire_n=0;
    library_send(ENGI_CZ,7);host_drain();n=host_wire_n;memcpy(frame,host_wire,n);receive_library(frame,n);
    bad+=check("CZ USB export/import retains all sixteen name bytes",n==295&&sx_import.count==1&&library_engine()==ENGI_CZ&&!memcmp(sx_import.data,raw,CZ_BYTES));sysex_cancel();
    reset();memcpy(raw,FM6_INIT,FM6_PACKED);memcpy(raw+118,"FULL DX7!!",10);native_put(ENGI_FM6,3,raw);native_put(ENGI_FM6,4,raw);
    bulk=(typeof(bulk)){.on=1,.engine=ENGI_FM6,.anchor=3,.end=4};bulk_send();host_drain();n=163;memcpy(frame,host_wire,n);
    bad+=check("FM6 bulk export sends two independently valid named voice frames",host_wire_n==326&&!memcmp(host_wire,host_wire+n,n)&&!sx_checksum(frame+6,156));
    receive_library(frame,n);bad+=check("DX7 voice import retains packed bytes without loading or overwriting",sx_import.count==1&&library_engine()==ENGI_FM6&&!memcmp(sx_import.data,raw,FM6_PACKED)&&TSEL->eng_req!=ENGI_FM6);
    sysex_cancel();bad+=check("cancel releases a pending import without changing its destination",!sx_import.count&&native_used(ENGI_FM6,3));
    return bad;
}
int main(void)
{
    int bad = library_transfers() + native_motion_protocol() + prophet_protocol() + bank_protocol() + preferences() + framing() + uart_recovery() + steps() + song_protocol() + malformed_saves() + names_whole() +
              native_protocol() + shared_protocol() + fm6_patches() + user_preset_roundtrip() + cz_native_protocol() + cz_dedicated_banks() + cz_casio_sysex() + concert_pitch() + scale_catalogue();
    printf("%s\n", bad ? "EDITOR TEST FAILED" : "editor test passed");
    return bad != 0;
}
#endif
