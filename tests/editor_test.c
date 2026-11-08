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
    else if (host_progress && transport_req == 1u) events_block(1);   /* (TRANSPORT play waits for its start) */
}
/* Target cli/sti do not nest: an inner pair reopens the outer section. */
#if !MELODEE_BLE                                      /* (with Bluetooth: hostsim.c's) */
static uint32_t host_irq_masked, host_irq_nested;
static void fm1_irq_off(void) { host_irq_nested += host_irq_masked; host_irq_masked = 1; }
static void fm1_irq_on(void) { host_irq_masked = 0; }
#endif
static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
static void panel_setup(void) {}
static void sound_name(const track_t *t, char *b)        /* (ui_draw.c's, not built here: the preset's name) */
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    str_cpy(b, e->npresets ? eng_preset(e, t->preset % e->npresets)->name : e->name, 16);
}
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#include "../firmware/src/sysex_route.c"
static int layer_allowed(void) { return !ui.menu && !ui.confirm; }   /* (ui_layer.c's, not built here) */

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
#if MELODEE_BLE                                       /* (ui_ble.c's, not built here: BLE 52 asks them) */
static uint32_t ble_found_n(void) { return mb_saved.flags & MB_VISIBLE ? 0u : mb.count; }
static int ble_start_quiet(void) { return !song.playing; }
#endif
#include "../firmware/src/editor.c"

static int check(const char *what, int ok)
{
    printf("editor: %-70s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}
static void reset(void)
{
    uint32_t t, i;
    memset(&song, 0, sizeof song); memset(trk, 0, sizeof trk);trk_voices_bind();
    memset(&chain, 0, sizeof chain); chain_defaults(&chain_config);
    pattern_init();
    memset(&sx_import,0,sizeof sx_import);memset(&bulk,0,sizeof bulk);resource_release(RES_NOTE_HISTORY);note_transaction_ptr=0;msx_reset();
    memset(&ed_w, 0, sizeof ed_w); memset(&ui, 0, sizeof ui);
    memset(&favorites, 0, sizeof favorites); memset(&settings, 0, sizeof settings); settings_init();
    memset(proj_slot, 0, sizeof proj_slot); up_cache_reset(); native_cache_reset();
    memset(&um, 0, sizeof um);
    host_progress = 1; host_erases = host_writes = host_wire_n = 0;
    transport_req = panic_req = 0; sx_ready = sx_collect = sx_busy = 0;
    so_r = so_w = mi_r = mi_w = 0; midi_in_overflow = 0; usb.config = 1;
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    for (t = 0; t < NPART; t++) {
        track_defaults(&trk[t]); set_engine_of(&trk[t], 3); apply_preset_to(&trk[t], 0);   /* (SID: values on p[]) */
        trk[t].engine = trk[t].eng_req; track_defaults_steps(&trk[t]);
    }
    for (t = NPART; t < NTRK; t++) {                    /* the MIDI-only tracks, as melodee_init (FBKP carries them) */
        for (i = 0; i < P_E0; i++) trk[t].p[i] = TP[i].def;
        for (i = 0; i < NSTEP; i++) trk[t].step[i].time = ST_REST;
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
    const uint8_t caps[]={0,0x55,1,121,0x4d,2,MOTION_MAX,1,0x42,1,3,0x50,4,NPAT,CHAIN_ROWS,0x55,1,0,3,0x53,1,39,0x59,1,85};
    bad+=check("INFO preserves existing IDs, the shared 384-slot library, Studio's tag, then the synths' (85..88)",ED_SONG==33&&ED_FAV_SET==38&&n>sizeof caps&&!memcmp(host_wire+n-sizeof caps-1,caps,sizeof caps));
    request(ED_UI_SET, a, 2);
    bad += check("UI_SET updates the actual palette and reports RAM-only saving",
        host_wire[5] == 3 && settings.palette == 2 && T_BG == UI_PALETTES[2].bg);
    a[1] = NPALETTES; request(ED_UI_SET, a, 2);
    bad += check("out-of-range palette leaves the display unchanged", host_wire[5] == 1 && settings.palette == 2);
    a[0] = 1; a[1] = 1; request(ED_UI_SET, a, 2);
    bad += check("the retired font weight is not supported (rc 2), UI_STATE says 127",
        host_wire[5] == 2 && host_wire[8] == 121 && host_wire[10] == 127);
    a[0] = 5; a[1] = 100; request(ED_UI_SET, a, 2);
    bad += check("UI_SET click level takes a percentage up to 100", host_wire[5] == 3 && settings_click_level == 100u);
    a[1] = 101; request(ED_UI_SET, a, 2);
    bad += check("click level above 100 % is refused", host_wire[5] == 1 && settings_click_level == 100u);
    settings_click_level = CLICK_LEVEL_DEFAULT;
    a[0] = 9; a[1] = 1; request(ED_UI_SET, a, 2);
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
    bad += check("SONG PLAY rejects a start row beyond the arrangement", request(ED_SONG, a, 2) == 14u && host_wire[6] == 1 && !transport_req);
    bad += check("SONG PLAY refuses malformed extra argument bytes", !request(ED_SONG, a, 3) && !transport_req);
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
    a[2]=NPAT;
    bad += check("PATTERN rejects out-of-range bank IDs", request(ED_PATTERN,a,3)==12 && host_wire[7] && trk[2].pattern==7);
    a[2]=NPAT-1u;
    bad += check("PATTERN selects a part's bank 16 and reports its 16 banks", request(ED_PATTERN,a,3)==12 && !host_wire[7] && trk[2].pattern==NPAT-1u && host_wire[10]==NPAT);
    uint8_t m[3] = {NPART,1,NPAT_MIDI};
    bad += check("PATTERN rejects a MIDI-only track's bank 9 and reports its 8 banks", request(ED_PATTERN,m,3)==12 && host_wire[7] && !trk[NPART].pattern && host_wire[10]==NPAT_MIDI);
    a[2]=7; request(ED_PATTERN,a,3);
    uint8_t row[] = {1,1,0,2,4,7,3};
    bad += check("bank SONG independently assigns four track banks", request(ED_BANK_SONG,row,sizeof row)==18 && !host_wire[6] && chain_patterns[0][3]==7 && chain_config.row[0].repeat==3);
    row[4]=CHAIN_HOLD+1u;
    bad += check("malformed bank SONG leaves the arrangement unchanged", request(ED_BANK_SONG,row,sizeof row)==18 && host_wire[6]==1 && chain_patterns[0][2]==4);
    uint8_t long_song[2u + CHAIN_ROWS * 5u] = {1, CHAIN_ROWS};
    for (uint32_t r = 0; r < CHAIN_ROWS; r++) {
        long_song[2u + r * 5u] = 0; long_song[3u + r * 5u] = CHAIN_REST;
        long_song[4u + r * 5u] = 2; long_song[5u + r * 5u] = 7; long_song[6u + r * 5u] = 16;
    }
    bad += check("bank SONG transfers 64 sections with explicit resting tracks", request(ED_BANK_SONG,long_song,sizeof long_song) == 13u + 5u * CHAIN_ROWS && !host_wire[6] && chain_config.count == CHAIN_ROWS && chain_patterns[63][1] == CHAIN_REST);
    const uint8_t start[] = {2, 63};
    bad += check("bank SONG starts from an optional selected section", request(ED_BANK_SONG,start,sizeof start) == 13u + 5u * CHAIN_ROWS && !host_wire[6] && chain.armed && chain.start == 63);
    transport_req = 2; events_block(1);
    const uint8_t loop_on[]={4,1},loop_bad[]={4,2};
    bad+=check("bank SONG exposes the saved loop option",request(ED_BANK_SONG,loop_on,sizeof loop_on)==13u+5u*CHAIN_ROWS&&!host_wire[6]&&host_wire[host_wire_n-2u]==1&&chain_config.rsv[CHAIN_LOOP]);
    bad+=check("bank SONG rejects invalid loop values",request(ED_BANK_SONG,loop_bad,sizeof loop_bad)==13u+5u*CHAIN_ROWS&&host_wire[6]==1&&chain_config.rsv[CHAIN_LOOP]);
    a[0]=0; uint32_t writes=host_writes;
    bad += check("sample upload and erase commands cannot write into pattern storage", request(ED_SMP_BEGIN,a,1)==0 && request(ED_SMP_ERASE,a,1)==0 && host_writes==writes && !host_erases);
    request(ED_SMP_INFO,a,0);
    bad += check("sample inventory advertises zero user slots", host_wire[5]==0);
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
/* DESC with a count (CAPS R): from id on, length-prefixed bodies, each the same bytes as that id's own DESC after
 * scope, id; whole ones only, the next request goes on where a reply stopped */
static int desc_bulk(void)
{
    static uint8_t one[1024];
    uint8_t a[3];
    uint32_t n, m, at, len, id, scope, reads, cuts = 0, ok = 1;
    int bad;
    reset();
    n = request(ED_CAPS, a, 0);
    for (at = 6; at + 1u < n && host_wire[at] != 'R'; at += 2u + host_wire[at + 1]) ;
    bad = check("CAPS: tag R (DESC takes a count, v1)", at + 2u < n && host_wire[at + 1] == 1 && host_wire[at + 2] == 1);
    for (scope = 0; scope < 2u; scope++) {
        uint32_t count = scope ? G_COUNT : P_COUNT;
        for (id = 0, reads = 0; id < count && reads < 200u; reads++) {
            a[0] = (uint8_t)scope; a[1] = (uint8_t)id; a[2] = 127;
            n = request(ED_DESC, a, 3);
            if (n < 8u || host_wire[5] != scope || host_wire[6] != id) { ok = 0; break; }
            static uint8_t got[1024];
            memcpy(got, host_wire, n);
            for (at = 7; at + 1u < n; id++) {
                len = got[at] | got[at + 1] << 7;
                a[1] = (uint8_t)id;
                m = request(ED_DESC, a, 2);
                memcpy(one, host_wire, m);
                ok &= at + 2u + len < n && m == 8u + len && !memcmp(one + 7, got + at + 2, len);
                at += 2u + len;
            }
            ok &= at == n - 1u;
            cuts += id < count;
        }
        ok &= id == count;
    }
    a[0] = 0; a[1] = P_COUNT; a[2] = 5;
    ok &= !request(ED_DESC, a, 3);                       /* past the last: no reply, as one id */
    a[1] = 0; a[2] = 0;
    n = request(ED_DESC, a, 3);
    ok &= n > 9u && (host_wire[7] | host_wire[8] << 7) == n - 10u;   /* count 0: one */
    return bad + check("DESC with a count: every id's own bytes, whole ones a reply, paged on", ok && cuts > 0);
}
static int project_scale_protocol(void)
{
    reset();
    int ok = 1;
    const uint8_t ids[] = {P_ROOT, P_SCALE, P_QUANT, P_RECQ};
    const int16_t values[] = {9, 2, Q_WHITE, 6};
    for (uint32_t i = 0; i < NELEM(ids); i++) {
        uint16_t v = (uint16_t)(values[i] + 8192);
        uint8_t a[4] = {0, ids[i], v & 127u, v >> 7};
        request(ED_SET, a, sizeof a);
        for (uint32_t k = 0; k < NTRK; k++) ok &= trk[k].p[ids[i]] == values[i];
        a[0] = 2;
        v = (uint16_t)(TP[ids[i]].def + 8192);
        a[2] = v & 127u; a[3] = v >> 7;
        request(ED_TRACK_PARAM, a, sizeof a);
        for (uint32_t k = 0; k < NTRK; k++) ok &= trk[k].p[ids[i]] == TP[ids[i]].def;
    }
    ok &= check("SET and TRACK_PARAM share project ROOT, scale, note and recording quantization across all eight tracks", ok) == 0;
    {                                                   /* a tuning: the same, and a maqam set brings its own */
        uint16_t v = (uint16_t)(7 + 8192);
        uint8_t a[4] = {0, P_TUNING, v & 127u, v >> 7};
        int tuned;
        request(ED_SET, a, sizeof a);
        tuned = TSEL->p[P_TUNING] == 7;
        for (uint32_t k = 0; k < NTRK; k++) tuned &= trk[k].p[P_TUNING] == 7;
        v = (uint16_t)(SCALE_MICRO_END + 8192);
        a[1] = P_SCALE; a[2] = v & 127u; a[3] = v >> 7;
        request(ED_SET, a, sizeof a);
        for (uint32_t k = 0; k < NTRK; k++) tuned &= trk[k].p[P_SCALE] == (int16_t)SCALE_MICRO_END && trk[k].p[P_TUNING] == SCALE_LINK[0];
        v = (uint16_t)(1 + 8192);
        a[0] = 2; a[2] = v & 127u; a[3] = v >> 7;
        request(ED_TRACK_PARAM, a, sizeof a);
        for (uint32_t k = 0; k < NTRK; k++) tuned &= trk[k].p[P_SCALE] == 1 && !trk[k].p[P_TUNING];
        return (ok ? 0 : 1) + check("SET and TRACK_PARAM share the tuning, a maqam's scale brings its tuning", tuned);
    }
}
static uint32_t cz_frame_of(const uint8_t *raw, uint8_t *f)   /* a CZ-1 tone straight (F0 44 00 00 70 30) */
{
    memcpy(f, (uint8_t[]){0xf0, 0x44, 0, 0, 0x70, 0x30}, 6);
    for (uint32_t j = 0; j < CZ_BYTES; j++) { f[6 + 2 * j] = raw[j] & 15u; f[7 + 2 * j] = raw[j] >> 4; }
    f[6 + 2 * CZ_BYTES] = 0xf7;
    return 7u + 2u * CZ_BYTES;
}
/* the synths' commands (editor_synth.c): nothing in them names a synth, so these checks go through each shipped one */
static uint32_t synth_req(uint32_t cmd, const uint8_t *a, uint32_t n) { return request(cmd, a, n); }
static int synth_protocol(void)
{
    int bad = 0;
    static uint8_t a[700], got[16384], want[16384];
    uint8_t raw[MOD3_PATCH_MAX], p[MOD3_PATCH_MAX];
    uint32_t n, len, off, i, key;
    reset(); fm6_init(); cz_init(); p5_parts_init();
    /* SCHEMA: FM6's whole schema in pages, as the device reads it; no PRST in a host build */
    len = 0; off = 0;
    do {
        a[0] = ENGI_FM6; a[1] = 0; a[2] = off & 127u; a[3] = off >> 7 & 127u; a[4] = off >> 14; a[5] = 840u & 127u; a[6] = 840u >> 7;
        n = synth_req(ED_SCHEMA, a, 7);
        if (host_wire[7] || n < 26u) break;
        len = host_wire[8] | (uint32_t)host_wire[9] << 7 | (uint32_t)host_wire[10] << 14;
        key = host_wire[11] | (uint32_t)host_wire[12] << 7 | (uint32_t)host_wire[13] << 14 | (uint32_t)host_wire[14] << 21 | (uint32_t)host_wire[15] << 28;
        uint32_t cnt = host_wire[19] | (uint32_t)host_wire[20] << 7;
        if (ed_unpack7(host_wire + 21, n - 22u, got + off, sizeof got - off) != cnt) break;
        off += cnt;
    } while (off < len);
    {
        mod_sch_head h;
        bad += check("SCHEMA reads FM6's schema whole, in pages, byte for byte, its CRC as the key", len && off == len &&
                     !sch_rd(ENGI_FM6, 0, want, len) && !memcmp(got, want, len) && !sch_head(ENGI_FM6, &h) && key == h.crc);
    }
    a[1] = 1; synth_req(ED_SCHEMA, a, 7);
    bad += check("SCHEMA: no PRST chunk where the presets are built in (rc 2)", host_wire[7] == 2);
    a[0] = 0; a[1] = 0; synth_req(ED_SCHEMA, a, 7);
    bad += check("SCHEMA: an engine without a schema (rc 2)", host_wire[7] == 2);
    /* PATCH: a part's, a factory preset's, a user slot's; put, sent, dumped */
    set_engine_of(&trk[1], ENGI_FM6); apply_preset_to(&trk[1], 5);
    a[0] = 0; a[1] = 0; a[2] = 127; a[3] = 1; a[4] = 0; n = synth_req(ED_PATCH, a, 5);
    len = host_wire[12] | (uint32_t)host_wire[13] << 7;
    bad += check("PATCH GET a part: its synth's number, its patch's bytes", !host_wire[10] && host_wire[11] == ENGI_FM6 &&
                 len == patch_len(ENGI_FM6) && ed_unpack7(host_wire + 14, n - 15u, raw, sizeof raw) == len &&
                 !memcmp(raw, patch_of(1), len));
    a[2] = ENGI_CZ; synth_req(ED_PATCH, a, 5);
    bad += check("PATCH GET a part of another synth: rc 2", host_wire[10] == 2);
    a[1] = 2; a[2] = ENGI_PROPHET; a[3] = 4; n = synth_req(ED_PATCH, a, 5);
    len = host_wire[12] | (uint32_t)host_wire[13] << 7;
    bad += check("PATCH GET a factory preset: its patch as the preset carries it", !host_wire[10] && len == patch_sound_len(ENGI_PROPHET) &&
                 ed_unpack7(host_wire + 14, n - 15u, raw, sizeof raw) == len && preset_patch(ENGI_PROPHET, 4, p, len) && !memcmp(raw, p, len));
    raw[70] = 'Z';                                       /* (a name byte: the program's own) */
    a[0] = 1; a[1] = 0; a[2] = ENGI_PROPHET; a[3] = 2; a[4] = 0; n = 5u + pack7(raw, len, a + 5);
    undo_seal(); synth_req(ED_PATCH, a, n);
    bad += check("PATCH PUT a Prophet program on a SID part: loaded as its sound, every byte", !host_wire[10] &&
                 trk[2].eng_req == ENGI_PROPHET && patch_eng_of(2) == ENGI_PROPHET && !memcmp(patch_of(2), raw, len));
    a[0] = 3; a[1] = 0; a[2] = 127; n = synth_req(ED_PATCH, a, 5);
    {
        p5_patch_t q; uint8_t f[P5_FRAME_MAX]; memcpy(&q, raw, sizeof q); q.command = 3;
        uint32_t m = p5_patch_encode(&q, f, sizeof f), k = host_wire[11] | (uint32_t)host_wire[12] << 7;
        bad += check("PATCH DUMP a part: its synth's SysEx (the Prophet's edit buffer), F0 and F7 off", !host_wire[10] &&
                     k == m - 2u && !memcmp(host_wire + 13, f + 1, k));
    }
    a[0] = 2; host_wire_n = 0; n = synth_req(ED_PATCH, a, 5);
    bad += check("PATCH SEND a part: the frame over USB, then the reply", n > 150u && host_wire[0] == 0xF0 && host_wire[1] == 1);
    (void)preset_patch(ENGI_CZ, 9, raw, CZ_BYTES); memcpy(raw + 128, "PATCH PUT  TONE!", 16);
    a[0] = 1; a[1] = 1; a[2] = ENGI_CZ; a[3] = 9; a[4] = 0; n = 5u + pack7(raw, CZ_BYTES, a + 5);
    synth_req(ED_PATCH, a, n);
    bad += check("PATCH PUT a user slot: the synth's stored patch, named by its own name", !host_wire[10] && native_used(ENGI_CZ, 9) &&
                 !memcmp(native_raw(ENGI_CZ, 9), raw, CZ_BYTES) && !memcmp(up_rec(9)->name, "PATCH PUT  T", 12));
    raw[0] = 0xF0; n = 5u + pack7(raw, CZ_BYTES, a + 5); synth_req(ED_PATCH, a, n);
    bad += check("PATCH PUT a tone its synth's checks refuse: rc 1, the slot as it was", host_wire[10] == 1 &&
                 native_raw(ENGI_CZ, 9)[0] != 0xF0);
    a[0] = 3; a[1] = 1; a[2] = 127; n = synth_req(ED_PATCH, a, 5);
    bad += check("PATCH DUMP a user slot: Casio's tone frame (F0 44 00 00 70 30), 288 nibbles", !host_wire[10] &&
                 (host_wire[11] | host_wire[12] << 7) == 293u && host_wire[13] == 0x44 && host_wire[17] == 0x30 &&
                 host_wire[18] == (native_raw(ENGI_CZ, 9)[0] & 15u));
    a[0] = 0; a[1] = 1; a[3] = 10; synth_req(ED_PATCH, a, 5);
    bad += check("PATCH GET an empty slot: rc 2", host_wire[10] == 2);
    /* PATCH BANK: user slots as their synth's bank dump, a piece at an offset (a DX7's 32 voices: the init voice where a
     * slot holds no FM6 voice); a synth without one answers rc 2 (the client sends them one by one) */
    a[0] = 4; a[1] = 1; a[2] = ENGI_FM6; a[3] = 0; a[4] = 0; a[5] = 9; a[6] = 0; a[7] = 10; a[8] = 0;
    n = synth_req(ED_PATCH, a, 9);
    {
        uint32_t total = host_wire[11] | (uint32_t)host_wire[12] << 7, cnt = host_wire[13] | (uint32_t)host_wire[14] << 7;
        bad += check("PATCH BANK: a DX7 32-voice bank (F0 43 00 09 20 00), the first piece filling the reply",
                     !host_wire[10] && total == 4102u && cnt == n - 16u && n == 1024u && host_wire[15] == 0x43 &&
                     !memcmp(host_wire + 16, (uint8_t[]){0, 9, 0x20, 0}, 4) && !memcmp(host_wire + 20, FM6_INIT, 128));
        a[3] = 4100u & 127u; a[4] = 4100u >> 7; n = synth_req(ED_PATCH, a, 9);
        uint32_t sum = 0;
        for (i = 0; i < 4096u; i++) sum += FM6_INIT[i % 128u];
        bad += check("PATCH BANK: the last piece from an offset, the checksum at its end", !host_wire[10] &&
                     (host_wire[13] | host_wire[14] << 7) == 2u && host_wire[16] == (uint8_t)(-sum & 127u) && n == 18u);
    }
    a[3] = 0; a[4] = 0; a[2] = ENGI_CZ; synth_req(ED_PATCH, a, 9);
    bad += check("PATCH BANK of a synth with no bank dump (the CZ-1): rc 2", host_wire[10] == 2);
    a[2] = ENGI_FM6; a[6] = UP_SLOTS >> 7; synth_req(ED_PATCH, a, 9);
    bad += check("PATCH BANK: a slot past the user slots, rc 1", host_wire[10] == 1);
    a[6] = 0; a[1] = 0; synth_req(ED_PATCH, a, 9);
    bad += check("PATCH BANK of a part, not user slots: rc 1", host_wire[10] == 1);
    a[1] = 1; a[3] = 4102u & 127u; a[4] = 4102u >> 7; synth_req(ED_PATCH, a, 9);
    bad += check("PATCH BANK: an offset at the end, rc 1", host_wire[10] == 1);
    /* SYSEX: a DX7 bank in parts, kept from U041 on, as the device's import keeps it */
    {
        static uint8_t f[4104];
        memcpy(f, (uint8_t[]){0xf0, 0x43, 0, 9, 0x20, 0}, 6);
        for (i = 0; i < 32u; i++) (void)preset_patch(ENGI_FM6, i % 24u, f + 6 + i * 128u, 128u);
        uint32_t s = 0;
        for (i = 6; i < 4102u; i++) s += f[i];
        f[4102] = (uint8_t)(-s & 127u); f[4103] = 0xf7;
        a[0] = 0; a[1] = 4104u & 127u; a[2] = 4104u >> 7 & 127u; a[3] = 0; synth_req(ED_SYSEX, a, 4);
        int ok = !host_wire[6];
        for (off = 0; ok && off < 4104u; off += 448u) {
            uint32_t m = 4104u - off < 448u ? 4104u - off : 448u;
            a[0] = 1; a[1] = off & 127u; a[2] = off >> 7 & 127u; a[3] = off >> 14;
            n = 4u + pack7(f + off, m, a + 4); synth_req(ED_SYSEX, a, n); ok = !host_wire[6];
        }
        a[0] = 2; a[1] = 40; a[2] = 0; synth_req(ED_SYSEX, a, 3);
        ok = ok && !host_wire[6] && host_wire[7] == ENGI_FM6 && host_wire[8] == 32 && !sx_import.count;
        for (i = 0; ok && i < 32u; i++) ok = native_used(ENGI_FM6, 40 + i) && !memcmp(native_raw(ENGI_FM6, 40 + i), f + 6 + i * 128u, 128u);
        bad += check("SYSEX passes a DX7 bank to FM6 in parts: its 32 voices kept from the slot given", ok);
        a[0] = 0; a[1] = 9; a[2] = 0; a[3] = 0; synth_req(ED_SYSEX, a, 4);
        a[0] = 1; a[1] = 0; a[2] = 0; a[3] = 0; n = 4u + pack7((const uint8_t[]){0xf0, 0x7d, 0x46, 0x4c, 1, 0, 0, 0, 0xf7}, 9, a + 4);
        synth_req(ED_SYSEX, a, n); a[0] = 2; a[1] = UP_SLOTS & 127u; a[2] = UP_SLOTS >> 7; synth_req(ED_SYSEX, a, 3);
        bad += check("SYSEX of no synth's pattern: rc 3, nothing staged", host_wire[6] == 3 && !sx_import.count);
        memcpy(raw, native_raw(ENGI_CZ, 9), CZ_BYTES);   /* (the tone kept above: valid) */
        n = cz_frame_of(raw, f);
        a[0] = 0; a[1] = n & 127u; a[2] = n >> 7; a[3] = 0; synth_req(ED_SYSEX, a, 4);
        a[0] = 1; a[1] = 0; a[2] = 0; a[3] = 0; synth_req(ED_SYSEX, a, 4u + pack7(f, n, a + 4));
        a[0] = 2; a[1] = UP_SLOTS & 127u; a[2] = UP_SLOTS >> 7; synth_req(ED_SYSEX, a, 3);
        bad += check("SYSEX with no slot: the synth's tone offered on the device (its import asks where)", !host_wire[6] &&
                     host_wire[7] == ENGI_CZ && host_wire[8] == 1 && sx_import.count == 1 && cur_page()->graph == GR_USER);
        sysex_cancel();
    }
    /* PARAM: by schema id, as the pages read and set them */
    {
        uint32_t id = 134u;                              /* (FM6's ALG) */
        a[0] = 0; a[1] = 1; a[2] = id & 127u; a[3] = id >> 7; synth_req(ED_PARAM, a, 4);
        bad += check("PARAM GET: FM6's ALG as its pages show it", !host_wire[7] && host_wire[8] == ENGI_FM6 &&
                     ed_rv(host_wire + 9) == sch_get_t(1, id));
        a[0] = 1; a[4] = 17u + 8192u & 127u; a[5] = (17u + 8192u) >> 7; synth_req(ED_PARAM, a, 6);
        bad += check("PARAM SET: the value written, FM6 told", !host_wire[7] && sch_get_t(1, id) == 17 && ed_rv(host_wire + 8) == 17);
        a[0] = 2; a[2] = (105u + 18u) & 127u; a[3] = (105u + 18u) >> 7; n = synth_req(ED_PARAM, a, 4);
        bad += check("PARAM GET with texts: a frequency as FM6 writes it", !host_wire[7] && n > 13u && host_wire[11] != 0);
        a[1] = 4; synth_req(ED_PARAM, a, 4);
        bad += check("PARAM on a MIDI-only place: rc 1", host_wire[7] == 1);
    }
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
static void receive_library(const uint8_t *frame,uint32_t n)
{
    wire_release();                                      /* (a main-loop turn's top: main.c) */
    for(uint32_t i=0;i<n;i++)midi_in_event(15u|(uint32_t)frame[i]<<8);
    msx_service();ed_service();host_drain();ota_frame_done();
}
static int library_transfers(void)
{
    int bad=0;uint8_t frame[320],raw[CZ_BYTES];uint32_t n;
    reset();up_store(2,"NAMED GENERAL");up_rec_t saved=*up_rec(2);int16_t level=TSEL->p[P_LEVEL];
    bad+=check("general slot exports a complete 7-bit USB frame",!library_send(TSEL->eng_req,2));host_drain();
    n=host_wire_n;memcpy(frame,host_wire,n);
    bad+=check("general export has a valid checksum and exact bounded length (02: the record and its synth's entry)",n==311u&&frame[4]==2u&&frame[0]==240&&frame[n-1]==247&&!sx_checksum(frame+1,n-2));
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
static int midi_track_protocol(void)
{
    reset(); int bad = 0;
    song.sel = 2; set_engine_of(TSEL, ENGI_MIDI);
    int16_t level = TSEL->p[P_LEVEL], pan = TSEL->p[P_PAN];
    uint8_t a[4] = {0, P_E0, 16, 64};
    request(ED_SET, a, 4);
    a[1] = P_E1; a[2] = 1; request(ED_SET, a, 4);
    bad += check("MIDI Track channel and TRS controls use ordinary track parameters",
                 TSEL->eng_req == ENGI_MIDI && TSEL->p[P_E0] == 16 && TSEL->p[P_E1] == 1);
    a[1] = P_LEVEL; a[2] = 0; request(ED_SET, a, 4);
    a[0] = 2; a[1] = P_PAN; a[2] = 30; request(ED_TRACK_PARAM, a, 4);
    a[1] = 0; a[2] = 64; a[3] = 1; request(ED_TRACK_MIX, a, 4);
    bad += check("MIDI Track editor level and pan are inert while mute remains available",
                 TSEL->p[P_LEVEL] == level && TSEL->p[P_PAN] == pan && TSEL->p[P_MUTE]);
    a[0] = 0; a[1] = P_FXTYPE; a[2] = FX_REVERB; a[3] = 64; request(ED_SET, a, 4);
    a[0] = 2; a[1] = P_FXSEND; a[2] = 127; request(ED_TRACK_PARAM, a, 4);
    bad += check("MIDI Track editor cannot enable insert FX or shared sends",
                 TSEL->p[P_FXTYPE] == FX_OFF && !TSEL->p[P_FXSEND]);
    return bad;
}
/* the pushes one ed_sync pass sends (fm1_ms moved past its 5 ms pace): the offset of the first frame of cmd, or 0 */
static uint32_t pushed(uint32_t cmd, uint32_t ms)
{
    uint32_t i;
    fm1_ms += ms; ed_w.last_ms = fm1_ms;
    host_wire_n = 0; ed_sync(); host_drain();
    for (i = 0; i + 4u < host_wire_n; i++)
        if (host_wire[i] == 0xF0 && host_wire[i + 1] == ED_HDR0 && host_wire[i + 4] == cmd)
            return i + 5u;
    return 0;
}
/* Melodee Studio's session (F1): CAPS, TRACKS, TRANSPORT, PROJECTS, STORAGE, WATCH bits, METERS, backup inventory */
static int studio_session(void)
{
    int bad = 0;
    uint8_t a[16] = {0};
    uint32_t n, i;
    reset();
    n = request(ED_CAPS, a, 0);
    bad += check("CAPS: revision 1, then TLVs (TRACKS v1, 8 places, 4 parts)", n > 8u && host_wire[5] == 1 &&
                 host_wire[6] == 'B' && host_wire[9] == 'L' && host_wire[15] == 'T' && host_wire[17] == 1 && host_wire[18] == NTRK && host_wire[19] == NPART);
    bad += check("CAPS with an argument: no reply", !request(ED_CAPS, a, 1));
    n = request(ED_TRACKS, a, 0);
    bad += check("TRACKS: 127, rc 0, eight places, four in use, the names", host_wire[5] == 127 && !host_wire[6] && host_wire[7] == NTRK &&
                 host_wire[8] == 4 && !host_wire[9] && n > 5u + 2u + 7u + NTRK * 8u + NTRK + 1u);
    for (i = 0; i < 4u; i++) { a[0] = 4; request(ED_TRACKS, a, 1); }
    bad += check("TRACKS add x4: eight tracks, the last one selected", trk_n() == NTRK && song.sel == NTRK - 1u && host_wire[8] == NTRK && host_wire[9] == NTRK - 1u);
    bad += check("TRACKS add past eight: rc 1", request(ED_TRACKS, a, 1) && host_wire[6] == 1 && trk_n() == NTRK);
    a[0] = 1; a[1] = 6; request(ED_TRACKS, a, 2);
    bad += check("TRACKS selects track 7 (a MIDI-only one)", !host_wire[6] && song.sel == 6 && host_wire[9] == 6);
    request(ED_TRACK, a, 0);
    bad += check("TRACK (27) still lists four, track 7 in the last place", host_wire[5] == ED_TRK - 1u && host_wire[6] == ED_TRK);
    a[0] = 7; request(ED_WATCH, a, 1);
    bad += check("WATCH 7: the reply says bits 0..2 are known", host_wire[5] == 7);
    request(ED_TRACK, a, 0);
    bad += check("WATCH bit 2: TRACK reports the real selection", host_wire[5] == 6);
    a[0] = 3; request(ED_WATCH, a, 1);
    bad += check("WATCH 3 as the old editor: 3 back, selection clamped again", host_wire[5] == 3 && (request(ED_TRACK, a, 0), host_wire[5] == ED_TRK - 1u));
    a[0] = 2; a[1] = 0x45; a[2] = 1; request(ED_TRACKS, a, 3);
    bad += check("TRACKS solo mask: tracks 1, 3, 7 and 8 (14 bits)", !host_wire[6] && track_solo == 0xC5 && track_solo_latch == 0xC5 &&
                 host_wire[10] == 0x45 && host_wire[11] == 1);
    a[0] = 3; a[1] = 1; a[2] = 1; request(ED_TRACKS, a, 3);
    bad += check("TRACKS arms track 2 (latched, as REC)", !host_wire[6] && song.rec == 2 && (song.rec_latch & 2));
    a[1] = 2; request(ED_TRACKS, a, 3);
    bad += check("TRACKS arms track 3: one armed at a time", !host_wire[6] && song.rec == 4 && host_wire[12] == 4);
    a[2] = 0; request(ED_TRACKS, a, 3);
    bad += check("TRACKS disarms it", !host_wire[6] && !song.rec);
    chain.armed = 1; a[2] = 1; request(ED_TRACKS, a, 3);
    bad += check("TRACKS arm while the song plays: rc 3", host_wire[6] == 3 && !song.rec);
    chain.armed = 0;
    a[0] = 5; a[1] = 3; song.playing = 1; request(ED_TRACKS, a, 2);
    bad += check("TRACKS remove while playing: rc 3, nothing removed", host_wire[6] == 3 && trk_n() == NTRK);
    song.playing = 0; request(ED_TRACKS, a, 2);
    bad += check("TRACKS removes track 4: seven left, the solo bits follow", !host_wire[6] && trk_n() == NTRK - 1u && track_solo_latch == 0x65);
    a[0] = 1; request(ED_TRACKS, a, 3);
    bad += check("TRACKS select with a stray byte: rc 1", host_wire[6] == 1);
    a[0] = 9; request(ED_TRACKS, a, 1);
    bad += check("TRACKS unknown op: rc 1", host_wire[6] == 1);

    reset();
    n = request(ED_TRANSPORT, a, 0);
    bad += check("TRANSPORT: stopped, CLK INT, 13 bytes", n == 15u && host_wire[5] == 127 && !host_wire[6] && !host_wire[7] && !host_wire[8]);
    a[0] = 1; request(ED_TRANSPORT, a, 1);
    bad += check("TRANSPORT play: playing in the reply", !host_wire[6] && (host_wire[7] & 1) && song.playing && !transport_req);
    a[0] = 3; request(ED_TRANSPORT, a, 1);
    bad += check("TRANSPORT stop", !host_wire[6] && !(host_wire[7] & 1) && !song.playing);
    song.g[G_CLOCK] = 1; a[0] = 1; request(ED_TRANSPORT, a, 1);
    bad += check("TRANSPORT play on CLK USB: rc 2, nothing starts", host_wire[6] == 2 && host_wire[8] == 1 && !transport_req && !song.playing);
    a[0] = 4; a[1] = 1; request(ED_TRANSPORT, a, 2);
    bad += check("TRANSPORT song loop on (allowed on an external clock)", !host_wire[6] && host_wire[9] == 1 && chain_config.rsv[CHAIN_LOOP]);
    song.g[G_CLOCK] = 0;
    const uint8_t chain1[] = {1, 1, 0, 2};
    request(ED_SONG, chain1, sizeof chain1);
    a[0] = 2; a[1] = 1; request(ED_TRANSPORT, a, 2);
    bad += check("TRANSPORT song from a row past its end: rc 1", host_wire[6] == 1 && !chain_busy());
    a[1] = 0; request(ED_TRANSPORT, a, 2);
    bad += check("TRANSPORT plays the song from row 1", !host_wire[6] && (host_wire[7] & 2) && host_wire[11] == 1);
    a[0] = 4; a[1] = 0; request(ED_TRANSPORT, a, 2);
    bad += check("TRANSPORT loop while the song plays: rc 3", host_wire[6] == 3);
    a[1] = 2; request(ED_TRANSPORT, a, 2);
    bad += check("TRANSPORT loop 2: rc 1", host_wire[6] == 1);
    a[0] = 3; request(ED_TRANSPORT, a, 1);

    reset();
    a[0] = 9; request(ED_WATCH, a, 1);                      /* on + TRACKS / TRANSPORT pushes */
    bad += check("WATCH 9: known", host_wire[5] == 9);
    bad += check("nothing changed: no TRACKS_CHANGED", !pushed(ED_TRACKS_CHANGED, 10));
    track_select(2);
    bad += check("a selection on the device: RELOAD first", pushed(ED_RELOAD, 10));
    i = pushed(ED_TRACKS_CHANGED, 10);                  /* (the next pass) */
    bad += check("a selection on the device: TRACKS_CHANGED (no names)", i && host_wire[i] == NTRK && host_wire[i + 2] == 2 &&
                 host_wire[i + 7u + NTRK * 8u] == 0xF7);
    a[0] = 1; a[1] = 1; request(ED_TRACKS, a, 2);
    bad += check("the editor's own TRACKS select: no TRACKS_CHANGED", !pushed(ED_TRACKS_CHANGED, 10));
    transport_req = 1; events_block(1);
    i = pushed(ED_TRANSPORT_CHANGED, 10);
    bad += check("PLAY on the device: TRANSPORT_CHANGED, playing", i && (host_wire[i] & 1));
    transport_req = 2; events_block(1);
    a[0] = 25; request(ED_WATCH, a, 1);                     /* .. and METERS */
    bad += check("WATCH 25 while watching: no push lost (the change still goes out)", host_wire[5] == 25 && pushed(ED_TRANSPORT_CHANGED, 10));
    trk[0].peak = 32767; trk[1].peak = 64;
    i = pushed(ED_METERS, 70);
    bad += check("METERS: 8 log2 peaks (32767: 119, 64: 48), every track's step", i && host_wire[i + 2] == NPART && host_wire[i + 3] == 119 &&
                 host_wire[i + 4] == 48 && !host_wire[i + 5] && host_wire[i + 7] == NTRK && host_wire[i + 8u + NTRK] == 0xF7);
    bad += check("the screen's meter still gets the peak the editor took", trk_peak_take(0, 0) == 32767 && !trk_peak_take(0, 1));
    bad += check("METERS paced (~15 Hz): none 10 ms later", !pushed(ED_METERS, 10));
    a[0] = 0; request(ED_WATCH, a, 1);
    bad += check("WATCH 0: no METERS", !host_wire[5] && !pushed(ED_METERS, 100));

    reset();
    for (i = NPART; i < NTRK; i++) {                    /* the MIDI-only tracks as melodee_init sets them (a channel) */
        track_defaults(&trk[i]); set_engine_of(&trk[i], trk_def_engine(i)); apply_preset_to(&trk[i], trk_def_preset(i));
        trk[i].engine = trk[i].eng_req;
    }
    proj_cur = PROJ_NO_SLOT; proj_name[0] = 0;          /* (no project loaded: an earlier check's save is not this one's) */
    n = request(ED_PROJECTS, a, 0);
    bad += check("PROJECTS status: no slot, unchanged, no name", n == 11u && host_wire[5] == 127 && !host_wire[6] && host_wire[7] == 127 && !host_wire[8] && !host_wire[9]);
    a[0] = 3; a[1] = 1; memcpy(a + 2, "MY SONG", 7);
    n = request(ED_PROJECTS, a, 9);
    bad += check("PROJECTS save as slot 2 \"MY SONG\": its slot, used, the size, the name", !host_wire[6] && host_wire[7] == 1 && host_wire[8] == 1 &&
                 host_wire[9] == 1 && (host_wire[10] | host_wire[11] << 7 | host_wire[12] << 14) == BANK_STORE_SIZE && !memcmp(host_wire + 13, "MY SONG", 8));
    a[0] = 0; a[1] = 0; a[2] = 32;
    n = request(ED_PROJECTS, a, 3);
    bad += check("PROJECTS list: first 0, four of four here (no flash), slot 2 named", !host_wire[6] && !host_wire[8] && host_wire[10] == 4 &&
                 host_wire[12] == 4 && host_wire[14] == 0 && host_wire[15] == 0 && host_wire[20] == 1 && host_wire[21] == 1);
    a[0] = 4; a[1] = 1; memcpy(a + 2, "RENAMED", 7); request(ED_PROJECTS, a, 9);
    bad += check("PROJECTS rename slot 2: the music's name too", !host_wire[6] && host_wire[7] == 1 && !memcmp(proj_name, "RENAMED", 8));
    a[0] = 5; a[1] = 1; a[2] = 3; request(ED_PROJECTS, a, 3);
    bad += check("PROJECTS duplicate 2 -> 4: a copy, the music still slot 2's", !host_wire[6] && host_wire[7] == 1 && host_wire[8] == 3 &&
                 host_wire[9] == 1 && project_used(3) && !memcmp(host_wire + 13, "RENAMED", 8));
    a[1] = 3; a[2] = 1; request(ED_PROJECTS, a, 3);
    bad += check("PROJECTS duplicate over the music's slot: no slot now", !host_wire[6] && host_wire[7] == 127);
    a[0] = 6; a[1] = 3; request(ED_PROJECTS, a, 2);
    bad += check("PROJECTS delete slot 4", !host_wire[6] && !host_wire[9] && !project_used(3));
    a[0] = 1; a[1] = 1; request(ED_PROJECTS, a, 2);
    bad += check("PROJECTS load slot 2: the music's slot", !host_wire[6] && host_wire[7] == 1 && proj_cur == 1);
    a[1] = 2; song.playing = 1; request(ED_PROJECTS, a, 2);
    bad += check("PROJECTS load an empty slot: rc 1, the music's slot kept, still playing", host_wire[6] == 1 && proj_cur == 1 && song.playing);
    song.playing = 0;
    request(ED_PROJECTS, a, 0);
    bad += check("PROJECTS status after the load: slot 2, unchanged, its name", host_wire[7] == 1 && !host_wire[8] && !memcmp(host_wire + 9, "RENAMED", 8));
    TSEL->step[0].n = 1; TSEL->step[0].note[0] = 60; TSEL->step[0].time = 0; trk[0].p[P_LEVEL] = 17;
    request(ED_PROJECTS, a, 0);
    bad += check("PROJECTS status after an edit: changed", host_wire[8] == 1);
    a[0] = 3; a[1] = 0; a[2] = 'A'; a[3] = 10; request(ED_PROJECTS, a, 4);
    bad += check("PROJECTS save as with a control character: rc 1", host_wire[6] == 1 && !project_used(0));
    memset(a + 2, 'A', 13); request(ED_PROJECTS, a, 15);
    bad += check("PROJECTS save as with 13 characters: rc 1", host_wire[6] == 1);
    a[0] = 2; a[1] = 4; request(ED_PROJECTS, a, 2);
    bad += check("PROJECTS slot 5 without flash: rc 1", host_wire[6] == 1);
    a[1] = 0; song.playing = 1; host_progress = 0; request(ED_PROJECTS, a, 2);
    bad += check("PROJECTS save while playing, STOP times out: rc 3, not saved", host_wire[6] == 3 && !project_used(0));
    host_progress = 1; request(ED_PROJECTS, a, 2);
    bad += check("PROJECTS save while playing: stopped, then saved; stays stopped", !host_wire[6] && project_used(0) && !song.playing);
    n = request(ED_STORAGE, a, 0);
    bad += check("STORAGE without flash: rc 4, no map", n == 13u && host_wire[5] == 4);

    reset();
    song.playing = 1; a[0] = 1;
    n = request(ED_BACKUP_LIST, a, 1);
    bad += check("BACKUP_LIST 1 (inventory) while playing: rc 0, still playing, id 0 empty", !host_wire[6] && song.playing &&
                 host_wire[8] == 0 && !host_wire[9] && !host_wire[13]);
    a[0] = 1; memset(a + 1, 0, 5); a[6] = 16; a[7] = 0;
    request(ED_BACKUP_GET, a, 8);
    bad += check("BACKUP_GET of the settings while playing: rc 0", host_wire[5] == 1 && !host_wire[6]);
    a[0] = 0; request(ED_BACKUP_GET, a, 8);
    bad += check("BACKUP_GET of the music after an inventory: rc 5 (none captured)", host_wire[6] == 5);
    a[0] = 2; request(ED_BACKUP_LIST, a, 1);
    bad += check("BACKUP_LIST 2: rc 1", host_wire[6] == 1);
    wire_release(); proj_cur = PROJ_NO_SLOT; track_solo = track_solo_latch = 0;   /* (a main-loop turn) */
    return bad;
}
/* Melodee Studio's song (F3): SONG2 (whole song, timing, write keeping the punch events and block sounds, section
 * operations), SONG_SOUND (the song's sounds, a block's sound from a preset / user slot / id), PUNCH (live, events),
 * the SONG_CHANGED push */
static uint32_t u35_at(uint32_t i) { return host_wire[i] | host_wire[i + 1] << 7 | host_wire[i + 2] << 14 | (uint32_t)host_wire[i + 3] << 21; }
static int studio_song(void)
{
    int bad = 0;
    uint8_t a[600] = {0}, cell[2][NTRK + 1u] = {{2, 0, 1, CHAIN_REST, 3, CHAIN_REST, CHAIN_REST, 7, CHAIN_REST},
                                               {1, CHAIN_HOLD, 2, CHAIN_REST, CHAIN_HOLD, CHAIN_REST, CHAIN_REST, CHAIN_HOLD, CHAIN_REST}};
    uint32_t n, i;
    reset(); song_sound_reset(); song_edit_sync();
    n = request(ED_SONG2, a, 0);
    bad += check("SONG2 query: no sections, no loop, 120 BPM, no song sounds", n == 13u && host_wire[5] == 127 && !host_wire[6] &&
                 !host_wire[7] && !host_wire[8] && host_wire[9] == 120 && !host_wire[10] && !host_wire[11]);
    a[0] = 2; a[1] = 2; a[2] = 1; memcpy(a + 3, cell, sizeof cell);
    request(ED_SONG2, a, 3u + sizeof cell);
    bad += check("SONG2 write: two sections of eight tracks, the loop", !host_wire[6] && host_wire[7] == 2 && host_wire[8] == 1 &&
                 chain_config.count == 2 && chain_config.rsv[CHAIN_LOOP] && chain_config.row[0].repeat == 2 &&
                 chain_patterns[0][6] == 7 && chain_patterns[1][6] == CHAIN_HOLD && chain_config.row[1].slot == CHAIN_HOLD);
    chain_config_t before = chain_config;
    a[3u + (NTRK + 1u) + 1u + 2u] = CHAIN_HOLD;             /* section 2, track 3: goes on after a rest */
    request(ED_SONG2, a, 3u + sizeof cell);
    bad += check("SONG2 write: goes on after a rest: rc 1, unchanged", host_wire[6] == 1 && !memcmp(&before, &chain_config, sizeof before) &&
                 chain_patterns[1][2] == CHAIN_REST);
    memcpy(a + 3, cell, sizeof cell); a[3 + 5] = 8;         /* track 5 (MIDI-only, 8 patterns): pattern 9 */
    request(ED_SONG2, a, 3u + sizeof cell);
    bad += check("SONG2 write: pattern 9 on a MIDI-only track: rc 1", host_wire[6] == 1 && chain_patterns[0][4] == CHAIN_REST);
    a[3 + 5] = CHAIN_REST; a[3] = 0; request(ED_SONG2, a, 3u + sizeof cell);
    bad += check("SONG2 write: 0 repeats: rc 1; a byte short: rc 1", host_wire[6] == 1 && (request(ED_SONG2, a, 2u + sizeof cell), host_wire[6] == 1));
    a[3] = 2;
    chain_config.fx[0][1] = (chain_fx_t){5, 2, 4}; chain_config.fx[1][0] = (chain_fx_t){1, 0, 16};
    chain_config.sound[0][1] = 3; chain_config.sound[0][2] = 4; song_edit_sync();
    a[3 + 3] = CHAIN_REST; request(ED_SONG2, a, 3u + sizeof cell);   /* track 2 rests in section 1 */
    bad += check("SONG2 write keeps the punch events and the sounds of the blocks it keeps (FBKN / FBKO)", !host_wire[6] &&
                 chain_config.fx[0][1].fx == 5 && chain_config.fx[1][0].length == 16 && chain_config.sound[0][1] == 3 && !chain_config.sound[0][2]);
    a[1] = 1; request(ED_SONG2, a, 3u + NTRK + 1u);
    bad += check("SONG2 write of one section: section 2's events and sounds go (as a delete)", !host_wire[6] && chain_config.count == 1 &&
                 !chain_config.fx[1][0].fx && chain_valid(&chain_config));
    song_edit_history(0);
    bad += check("the device's EDIT undo takes the editor's write back", chain_config.count == 2 && chain_config.fx[1][0].fx == 1);
    a[0] = 0; a[1] = 0; a[2] = 64; n = request(ED_SONG2, a, 3);
    bad += check("SONG2 sections: first 0, two of two, 24 bytes each", !host_wire[6] && !host_wire[12] && host_wire[14] == 2 && host_wire[16] == 2 &&
                 n == 19u + 2u * ED_SONG_ROW);
    bad += check("SONG2 section 1: 2 repeats, 16 steps of 1/16, 2 x 16 x 6144 units, cells and sounds", host_wire[18] == 2 && host_wire[19] == 16 &&
                 host_wire[20] == 2 && u35_at(21) == 2u * 16u * 6144u && !host_wire[25] && host_wire[26] == 0 && !host_wire[27] &&
                 host_wire[28] == 1 && host_wire[29] == 3 && host_wire[38] == 7);
    bad += check("SONG2 section 2 (track 1 goes on): one bar of 1/16 (the conductor's four beats)", host_wire[42] == 1 && host_wire[43] == 16 &&
                 u35_at(45) == 16u * 6144u && host_wire[50] == CHAIN_HOLD && !host_wire[51] && host_wire[52] == 2);
    trk[0].p[P_SLEN] = 12; trk[0].p[P_SDIV] = 1; trk[0].p[P_SSWING] = 50;
    request(ED_SONG2, a, 3);
    bad += check("SONG2 timing follows track 1's pattern: 12 eighths swung, x 2", host_wire[19] == 12 && host_wire[20] == 1 &&
                 u35_at(21) == 2u * 12u * 12288u);
    a[0] = 1; n = request(ED_SONG2, a, 1);
    bad += check("SONG2 timing table: swing, 16 banks a part, 8 a MIDI-only track, (steps, division, swing) each", !host_wire[6] &&
                 n == 14u + NTRK + 3u * (NPART * NPAT + (NTRK - NPART) * NPAT_MIDI) && host_wire[13] == NPAT && host_wire[14] == 12 &&
                 host_wire[15] == 1 && host_wire[16] == 50 && host_wire[14u + 1u + 3u * NPAT + 3u * NPAT + 1u] == 16);
    a[0] = 3; a[1] = 1; request(ED_SONG2, a, 2);
    bad += check("SONG2 insert at section 2: an empty section, the rest moves on", !host_wire[6] && chain_config.count == 3 &&
                 chain_patterns[1][0] == CHAIN_REST && chain_config.fx[2][0].fx == 1 && chain_patterns[2][0] == 0);
    a[0] = 4; a[1] = 0; a[2] = 0; request(ED_SONG2, a, 3);
    bad += check("SONG2 duplicate section 1: its punch events and sounds come along", !host_wire[6] && chain_config.count == 4 &&
                 chain_config.fx[1][1].fx == 5 && chain_config.sound[1][1] == 3 && chain_patterns[1][6] == 7);
    a[0] = 6; a[1] = 0; a[2] = 1; a[3] = 4; request(ED_SONG2, a, 4);
    bad += check("SONG2 move sections 1..2 to the end", !host_wire[6] && chain_config.count == 4 && chain_patterns[0][0] == CHAIN_REST &&
                 chain_config.fx[3][1].fx == 5 && !se.range && !se.pending);
    a[3] = 1; request(ED_SONG2, a, 4);
    bad += check("SONG2 move onto itself: rc 2 (the page refuses), unchanged", host_wire[6] == 2 && chain_patterns[0][0] == CHAIN_REST && !se.pending);
    a[0] = 5; a[1] = 0; a[2] = 1; request(ED_SONG2, a, 3);
    bad += check("SONG2 delete sections 1..2", !host_wire[6] && chain_config.count == 2 && chain_valid(&chain_config));
    a[2] = 2; request(ED_SONG2, a, 3);
    bad += check("SONG2 delete past the end: rc 1", host_wire[6] == 1 && chain_config.count == 2);
    chain.armed = 1; a[0] = 3; a[1] = 0; request(ED_SONG2, a, 2);
    bad += check("SONG2 insert while the song plays: rc 3", host_wire[6] == 3 && chain_config.count == 2);
    chain.armed = 0;

    reset(); song_sound_reset(); song_edit_sync();
    a[0] = 2; a[1] = 2; a[2] = 0; memcpy(a + 3, cell, sizeof cell); request(ED_SONG2, a, 3u + sizeof cell);
    uint8_t eng = trk[0].eng_req, pre = trk[0].preset;
    a[0] = 0; a[1] = 0; a[2] = 64; n = request(ED_SONG_SOUND, a, 3);
    bad += check("SONG_SOUND list: no sounds", !host_wire[8] && !host_wire[9] && n == 17u && !host_wire[14]);
    a[0] = 1; a[1] = 1; a[2] = 0; a[3] = 3; a[4] = 2; a[5] = 0;
    request(ED_SONG_SOUND, a, 6);
    bad += check("SONG_SOUND preset SID 3 on track 1's block (sections 1..2): sound 1, the track's own sound back", !host_wire[8] &&
                 host_wire[9] == 1 && host_wire[6] == 1 && !host_wire[7] && chain_config.sound[0][0] == 1 &&
                 chain_config.sound[1][0] == 1 && trk[0].eng_req == eng && trk[0].preset == pre && !song_picker.active);
    a[0] = 0; a[1] = 0; a[2] = 64; request(ED_SONG_SOUND, a, 3);
    bad += check("SONG_SOUND list: sound 1 = SID preset 3, named", !host_wire[8] && host_wire[14] == 1 && host_wire[16] == 1 &&
                 host_wire[17] == 3 && host_wire[18] == 2 && !host_wire[19] && host_wire[20] >= 'A');
    a[0] = 3; a[1] = 0; a[2] = 3; a[3] = 1; request(ED_SONG_SOUND, a, 4);
    bad += check("SONG_SOUND sound 1 on track 4's block too", !host_wire[8] && chain_config.sound[0][3] == 1 && chain_config.sound[1][3] == 1);
    a[3] = 0; request(ED_SONG_SOUND, a, 4);
    bad += check("SONG_SOUND 0 removes it (the track's own)", !host_wire[8] && !chain_config.sound[0][3] && !chain_config.sound[1][3]);
    a[2] = 6; a[3] = 1; request(ED_SONG_SOUND, a, 4);
    bad += check("SONG_SOUND a synth sound on a MIDI-only track: rc 1", host_wire[8] == 1 && !chain_config.sound[0][6]);
    a[2] = 1; a[3] = 2; request(ED_SONG_SOUND, a, 4);
    bad += check("SONG_SOUND an id past the song's sounds: rc 1", host_wire[8] == 1);
    a[0] = 2; a[1] = 0; a[2] = 2; a[3] = 100; a[4] = 2; request(ED_SONG_SOUND, a, 5);
    bad += check("SONG_SOUND an empty user slot: rc 1, nothing kept", host_wire[8] == 1 && !chain_config.sound[0][2] && !song_picker.active);
    up_store(5, "MY BASS");
    a[3] = 5; a[4] = 0; request(ED_SONG_SOUND, a, 5);
    bad += check("SONG_SOUND user slot 6 on track 3's cell: sound 2 (the slot's sound, no slot kept)", !host_wire[8] && host_wire[9] == 2 &&
                 chain_config.sound[0][2] == 2 && song_sound_get(2)->engine == up_rec(5)->engine && !song_sound_get(2)->user[0]);
    a[0] = 1; a[1] = 0; a[2] = 1; a[3] = 3; a[4] = 127; a[5] = 127; request(ED_SONG_SOUND, a, 6);
    bad += check("SONG_SOUND a preset past the engine's: rc 1", host_wire[8] == 1);
    a[4] = 2; a[5] = 0; song.playing = 1; request(ED_SONG_SOUND, a, 6);
    bad += check("SONG_SOUND a preset while playing: rc 3 (as the sheet: STOP TO EDIT)", host_wire[8] == 3);
    song.playing = 0;

    reset();
    n = request(ED_PUNCH, a, 0);
    bad += check("PUNCH query: none held, none from the song, 27 effects", n == 12u && host_wire[5] == 127 && !host_wire[6] &&
                 host_wire[7] == 127 && !host_wire[8] && host_wire[9] == 127 && host_wire[10] == PUNCH_NFX);
    a[0] = 0; request(ED_PUNCH, a, 1);
    bad += check("PUNCH names: LOOP 1/4 first", !memcmp(host_wire + 11, "LOOP 1/4", 9));
    a[0] = 1; a[1] = PX_REV; request(ED_PUNCH, a, 2);
    bad += check("PUNCH hold REVERSE: held, the editor's", !host_wire[6] && host_wire[7] == PX_REV && host_wire[8] == 1 && punch.req == PX_REV);
    a[0] = 2; request(ED_PUNCH, a, 1);
    bad += check("PUNCH release", !host_wire[6] && host_wire[7] == 127 && punch.req < 0 && !punch.keybit);
    a[0] = 1; a[1] = PUNCH_NFX; request(ED_PUNCH, a, 2);
    bad += check("PUNCH effect 28: rc 1", host_wire[6] == 1 && punch.req < 0);
    a[1] = 0; ui.menu = 1; request(ED_PUNCH, a, 2);
    bad += check("PUNCH hold while the device's menu is open: rc 3", host_wire[6] == 3 && punch.req < 0);
    ui.menu = 0; request(ED_PUNCH, a, 2);
    fm1_ms += 3001u; host_wire_n = 0; ed_service();
    bad += check("PUNCH: 3 s without a request, the editor's effect stops", punch.req < 0 && !punch.keybit);
    punch.req = 3; punch.keybit = 1u << 3; a[0] = 2; request(ED_PUNCH, a, 1);
    bad += check("PUNCH release leaves a key's effect alone", punch.req == 3 && host_wire[7] == 3 && !host_wire[8]);
    punch.req = -1; punch.keybit = 0;
    const uint8_t two[] = {1, 2, 0, 1, 0, 1};
    request(ED_SONG, two, sizeof two);
    uint8_t ev[3u + 24u] = {4, 0, 2, PX_HPF + 1u, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0,  PX_ECHO + 1u, 12, 4};
    n = request(ED_PUNCH, ev, sizeof ev);
    bad += check("PUNCH writes sections 1..2's events; the events back", !host_wire[6] && n == 13u + 24u && host_wire[11] == 2 &&
                 host_wire[12] == PX_HPF + 1u && host_wire[14] == 8 && host_wire[24] == PX_ECHO + 1u && chain_config.fx[1][0].start == 12);
    ev[4] = 60; request(ED_PUNCH, ev, sizeof ev);
    bad += check("PUNCH an event past step 64: rc 1, unchanged", host_wire[6] == 1 && chain_config.fx[0][0].length == 8);
    ev[4] = 0; ev[1] = 1; request(ED_PUNCH, ev, sizeof ev);
    bad += check("PUNCH events past the song's end: rc 1", host_wire[6] == 1);
    a[0] = 3; n = request(ED_PUNCH, a, 1);
    bad += check("PUNCH reads the song's events", !host_wire[6] && n == 13u + 24u && host_wire[12] == PX_HPF + 1u);

    reset(); song_sound_reset(); song_edit_sync();
    a[0] = 33; request(ED_WATCH, a, 1);
    bad += check("WATCH 33: bit 5 known", host_wire[5] == 33);
    song_edit_sync();
    bad += check("nothing changed: no SONG_CHANGED", !pushed(ED_SONG_CHANGED, 10));
    a[0] = 3; a[1] = 0; request(ED_SONG2, a, 2);
    song_edit_sync();
    bad += check("the editor's own section insert: no SONG_CHANGED", !pushed(ED_SONG_CHANGED, 10));
    ui.song_row = 0; song_loop_set(1); song_edit_sync();
    i = pushed(ED_SONG_CHANGED, 10);
    bad += check("SONG loop on the device: SONG_CHANGED with the head", i && host_wire[i] == 1 && host_wire[i + 1] == 1 && host_wire[i + 5] == 0xF7);
    bad += check("SONG_CHANGED once", !pushed(ED_SONG_CHANGED, 10));
    a[0] = 0; request(ED_WATCH, a, 1);
    for (i = 0; i < CHAIN_ROWS; i++) {                      /* the largest SONG2 write: 64 sections, 579 bytes */
        a[3u + i * (NTRK + 1u)] = 16;
        for (n = 0; n < NTRK; n++) a[4u + i * (NTRK + 1u) + n] = (uint8_t)(i ? CHAIN_HOLD : n % NPAT_MIDI);
    }
    a[0] = 2; a[1] = CHAIN_ROWS; a[2] = 0; request(ED_SONG2, a, 3u + CHAIN_ROWS * (NTRK + 1u));
    a[0] = 0; a[1] = 41; a[2] = 64; n = request(ED_SONG2, a, 3);
    bad += check("SONG2 64 sections: written in one request, read in two pages (41, then 23)", chain_config.count == CHAIN_ROWS &&
                 host_wire[14] == 23 && n == 19u + 23u * ED_SONG_ROW && (request(ED_SONG2, (uint8_t[]){0, 0, 64}, 3), host_wire[14] == 41) &&
                 host_wire_n <= sizeof ed_out);
    a[0] = 3; a[1] = 0; request(ED_SONG2, a, 2);
    bad += check("SONG2 insert into 64 sections: rc 2 (SONG FULL), unchanged", host_wire[6] == 2 && chain_config.count == CHAIN_ROWS);
    a[0] = 4; a[1] = 0; a[2] = 0; request(ED_SONG2, a, 3);
    bad += check("SONG2 duplicate into 64 sections: rc 2, no range left on the page", host_wire[6] == 2 && chain_config.count == CHAIN_ROWS &&
                 !se.range);
    return bad;
}

/* F2: PATTERN_DATA (44), NOTES (45), MOTION on a bank (64), PATTERN 3..5 (73), WATCH bit 6 -> PATTERN_CHANGED (59) */
static uint32_t pd_half(uint32_t k, uint32_t b, uint32_t h) { uint8_t q[3] = {(uint8_t)k, (uint8_t)b, (uint8_t)h}; return request(ED_PATTERN_DATA, q, 3); }
static uint32_t nt_note(uint8_t *q, uint32_t id, uint32_t step, uint32_t on, uint32_t du, uint32_t note, uint32_t vel)
{
    uint32_t i;
    q[0] = id & 127u; q[1] = id >> 7; q[2] = (uint8_t)step;
    for (i = 0; i < 5u; i++) { q[3 + i] = (on >> 7 * i) & 127u; q[8 + i] = (du >> 7 * i) & 127u; }
    q[13] = (uint8_t)note; q[14] = (uint8_t)vel; q[15] = 64; q[16] = 0;
    return ED_NOTE_N;
}
static uint32_t wire14(uint32_t i) { return host_wire[i] | host_wire[i + 1] << 7; }
static int studio_patterns(void)
{
    int bad = 0;
    uint8_t a[640] = {0}, *q;
    uint32_t n, i, id0, id1;
    reset();
    n = request(ED_CAPS, a, 0);
    for (i = 6; i + 1u < n && host_wire[i] != 'D'; i += 2u + host_wire[i + 1]) ;
    bad += check("CAPS: tag D (patterns v2, 64 steps, 1,024 timed notes), W 127", host_wire[i + 1] == 4 && host_wire[i + 2] == 2 &&
                 host_wire[i + 3] == NSTEP && host_wire[i + 4] + (host_wire[i + 5] << 7) == RECORD_MAX);
    n = pd_half(1, 3, 1);
    bad += check("PATTERN_DATA track 2 bank 4, steps 33..64: rc 0, LEN 16, DIV 1/16, 32 rests of 12 bytes", n == 13u + 32u * ED_STEP_N + 1u &&
                 !host_wire[8] && host_wire[9] == 16 && host_wire[10] == 2 && host_wire[13 + 5] == ST_REST);
    a[0] = 1; a[1] = 3; a[2] = 1; a[3] = 48; a[4] = 1; a[5] = 20; a[6] = 90;
    for (i = 0; i < 32u; i++) { q = a + 7 + i * ED_STEP_N; q[5] = ST_REST; }
    q = a + 7;                                                  /* step 33: two notes, accent, 100, hits 1 + 8 accented on 8, 41 % */
    q[0] = 60; q[1] = 64; q[4] = 2; q[5] = ST_NOTE; q[6] = SF_ACCENT; q[7] = 100; q[8] = 1; q[9] = 0; q[10] = 41; q[11] = 3;
    request(ED_PATTERN_DATA, a, 7u + 32u * ED_STEP_N);
    step_t s = pattern_step(1, 3, 32);
    bad += check("PATTERN_DATA writes bank 4 (not playing): the step, its hits past bit 7, LEN 48 / DIV 1/8 / SWING 20 / GATE 90",
                 !host_wire[8] && s.note[1] == 64 && s.n == 2 && s.hit == 0x81 && s.acc == 0x80 && s.probability == 41 &&
                 pattern_timing(1, 3, 0) == 48 && pattern_timing(1, 3, 3) == 90 && trk[1].pattern == 0 && trk[1].p[P_SLEN] == 16 &&
                 host_wire[9] == 48 && host_wire[13] == 60 && host_wire[13 + 8] == 1 && host_wire[13 + 11] == 3);
    q[4] = 5; request(ED_PATTERN_DATA, a, 7u + 32u * ED_STEP_N);
    bad += check("PATTERN_DATA five notes in a step: rc 1, unchanged", host_wire[8] == 1 && pattern_step(1, 3, 32).n == 2);
    q[4] = 2; a[1] = 0; a[2] = 0; chain.armed = 1; request(ED_PATTERN_DATA, a, 7u + 32u * ED_STEP_N);
    bad += check("PATTERN_DATA write while the song plays: rc 3", host_wire[8] == 3 && trk[1].step[0].n == 0);
    chain.armed = 0; request(ED_PATTERN_DATA, a, 7u + 32u * ED_STEP_N);
    bad += check("PATTERN_DATA writes the bank playing: the track's own steps and LEN", !host_wire[8] && trk[1].step[0].note[1] == 64 &&
                 trk[1].p[P_SLEN] == 48);
    n = pd_half(4, 8, 0);
    bad += check("PATTERN_DATA bank 9 of a MIDI-only track (8 banks): rc 1, no data", n == 10u && host_wire[8] == 1);
    n = request(ED_PATTERN_DATA, a, 5);
    bad += check("PATTERN_DATA of a wrong length: rc 1, the half still read", n == 13u + 32u * ED_STEP_N + 1u && host_wire[8] == 1);

    /* PATTERN 73: 3 clears, 4 halves, 5 doubles (SEQ's pattern dialog) */
    a[0] = 1; a[1] = 4; a[2] = 3; request(ED_PATTERN, a, 3);
    bad += check("PATTERN halve track 2 bank 4: LEN 24, the steps past it kept hidden", !host_wire[7] && pattern_timing(1, 3, 0) == 24 &&
                 pattern_step(1, 3, 32).n == 2);
    a[1] = 5; request(ED_PATTERN, a, 3);
    bad += check("PATTERN double it: LEN 48, steps 1..24 copied after themselves", !host_wire[7] && pattern_timing(1, 3, 0) == 48 &&
                 pattern_step(1, 3, 32).time == ST_REST);
    request(ED_PATTERN, a, 3);
    bad += check("PATTERN double 48 steps: rc 3 (64 STEPS MAX)", host_wire[7] == 3 && pattern_timing(1, 3, 0) == 48);
    {   /* op 2 with a fifth byte: to another track, as PATTERNS' Copy to (v2) */
        pattern_t ps, pd;
        uint8_t c[5] = {1, 2, 3, 6, 2};
        n = request(ED_PATTERN, c, 5);
        pattern_read(1, 3, &ps); pattern_read(2, 6, &pd);
        bad += check("PATTERN copy track 2 bank 4 to track 3 bank 7: rc 0, steps and LEN 48 there", n == 12u && !host_wire[7] &&
                     host_wire[5] == 1 && !memcmp(&ps, &pd, sizeof ps) && pattern_timing(2, 6, 0) == 48 && !pattern_sound[2][6]);
        c[4] = NTRK; bad += check("PATTERN copy to track 9: no reply", !request(ED_PATTERN, c, 5));
        c[1] = 1; c[4] = 2; bad += check("PATTERN select with a fifth byte: no reply", !request(ED_PATTERN, c, 5));
    }
    a[0] = 1; a[2] = 0; a[1] = 4; request(ED_PATTERN, a, 3);
    bad += check("PATTERN halve the bank playing (48 -> 24)", !host_wire[7] && trk[1].p[P_SLEN] == 24);
    undo_swap();
    bad += check("  the device's EDIT held undoes it", trk[1].p[P_SLEN] == 48);
    a[1] = 3; a[2] = 3; request(ED_PATTERN, a, 3);
    bad += check("PATTERN clear bank 4 (stopped: at once)", !host_wire[7] && pattern_empty(1, 3));
    song.playing = 1; a[2] = 0; request(ED_PATTERN, a, 3);
    bad += check("PATTERN clear the bank playing while playing: rc 5, waits for the loop end", host_wire[7] == 5 && dq_n == 1 &&
                 trk[1].step[0].n == 2);
    song.playing = 0; dq_poll();
    bad += check("  stopped: done", !dq_n && !trk[1].step[0].n);
    a[2] = NPAT; request(ED_PATTERN, a, 3);
    bad += check("PATTERN clear bank 17: rc 1", host_wire[7] == 1);
    a[1] = 6; bad += check("PATTERN op 6: no reply", !request(ED_PATTERN, a, 3));

    /* MOTION 64 on another bank: track, 5, bank, then as before */
    reset();
    a[0] = 0; a[1] = 5; a[2] = 3; a[3] = 3; a[4] = 4; a[5] = P_LEVEL; a[6] = (90 + 8192) & 127; a[7] = (90 + 8192) >> 7;
    request(ED_MOTION, a, 8);
    bad += check("MOTION bank 4: an event set there, none on the bank playing", !host_wire[6] && host_wire[8] == 1 && motion.count == 1 &&
                 motion_pattern[0] == 3 && motion_count(&trk[0]) == 0);
    n = request(ED_MOTION, a, 3);
    bad += check("MOTION bank 4 query: step 5, LEVEL, 90", n == 15u && host_wire[8] == 1 && host_wire[10] == 4 && host_wire[11] == P_LEVEL &&
                 ed_rv(host_wire + 12) == 90);
    n = request(ED_MOTION, a, 1);
    bad += check("MOTION query (the bank playing): none, as before", n == 11u && !host_wire[8]);
    a[3] = 4; request(ED_MOTION, a, 6);
    bad += check("MOTION bank 4 delete", !host_wire[6] && !host_wire[8] && !motion.count);
    a[2] = NPAT; bad += check("MOTION bank 17: no reply", !request(ED_MOTION, a, 3));

    /* NOTES 45 */
    reset();
    a[0] = 1; a[1] = 2; a[2] = 1; a[3] = 2;
    n = 4u + nt_note(a + 4, ED_NOTE_NEW, 3, 16384, 65536, 60, 100);
    n += nt_note(a + n, ED_NOTE_NEW, 3, 40000, 32768, 67, 120);
    n = request(ED_NOTES, a, n);
    id0 = wire14(9); id1 = wire14(11);
    s = pattern_step(1, 2, 3);
    step_t s4 = pattern_step(1, 2, 4);
    bad += check("NOTES adds two timed notes to track 2 bank 3: ids back, the views on steps 4 and 5 (rounded)", !host_wire[8] &&
                 n == 14u && id0 != id1 && recorded_owner(&recording[id0]) == 1u * NPAT + 2u && (s.flags & SF_RECORDED) &&
                 s.n == 1 && s.note[0] == 60 && s.vel == 100 && (s4.flags & SF_RECORDED) && s4.note[0] == 67 && (s4.flags & SF_ACCENT));
    a[2] = 0; a[3] = 0; a[4] = 0; a[5] = 100; n = request(ED_NOTES, a, 6);
    bad += check("NOTES lists them: two of two, onset, length, note, velocity, the rounded flag", !host_wire[8] && wire14(11) == 2 &&
                 wire14(13) == 2 && n == 16u + 2u * ED_NOTE_N);
    for (i = 15; i < n - 1u && wire14(i) != id1; i += ED_NOTE_N) ;
    bad += check("  the second: step 4, onset 40000, length 32768, 67, 120, flags 2", host_wire[i + 2] == 3 &&
                 u35_at(i + 3) == 40000u && u35_at(i + 8) == 32768u &&
                 host_wire[i + 13] == 67 && host_wire[i + 14] == 120 && host_wire[i + 15] == 64 && host_wire[i + 16] == 2);
    a[2] = 1; a[3] = 1; nt_note(a + 4, id1, 5, 0, 65536, 67, 50);
    request(ED_NOTES, a, 4u + ED_NOTE_N);
    bad += check("NOTES moves the second to step 6 at 50: step 5 empty again, step 6 its view", !host_wire[8] &&
                 pattern_step(1, 2, 4).time == ST_REST && pattern_step(1, 2, 5).note[0] == 67 && !(pattern_step(1, 2, 5).flags & SF_ACCENT) &&
                 pattern_step(1, 2, 3).note[0] == 60);
    nt_note(a + 4, id0, 3, 0, 0, 60, 0); request(ED_NOTES, a, 4u + ED_NOTE_N);
    bad += check("NOTES velocity 0 removes the first: step 4 empty", !host_wire[8] && !recording[id0].vel && pattern_step(1, 2, 3).time == ST_REST);
    nt_note(a + 4, id1, 5, 0, 65536, 67, 50); a[1] = 3; request(ED_NOTES, a, 4u + ED_NOTE_N);
    bad += check("NOTES an id of another bank: rc 1", host_wire[8] == 1);
    a[1] = 2; a[4 + 15] = 100; request(ED_NOTES, a, 4u + ED_NOTE_N);
    bad += check("NOTES a pitch past +24: rc 1", host_wire[8] == 1);
    trk[0].step[7] = (step_t){{48}, 1, ST_NOTE, 0, 96};
    a[0] = 0; a[1] = 0; nt_note(a + 4, ED_NOTE_NEW, 7, 0, 65536, 50, 90); request(ED_NOTES, a, 4u + ED_NOTE_N);
    bad += check("NOTES on a step holding its own notes: rc 1", host_wire[8] == 1 && trk[0].step[7].note[0] == 48);
    nt_note(a + 4, ED_NOTE_NEW, 9, 0, 65536, 50, 90); request(ED_NOTES, a, 4u + ED_NOTE_N);
    bad += check("NOTES on the bank playing: its view in the track's steps", !host_wire[8] && (trk[0].step[9].flags & SF_RECORDED) &&
                 trk[0].step[9].note[0] == 50);
    undo_swap();
    bad += check("  the device's EDIT held undoes it", !(trk[0].step[9].flags & SF_RECORDED) && !trk[0].step[9].n);
    a[0] = 1; a[1] = 2; a[3] = 37;                              /* 75 notes: two pages */
    for (i = 0; i < 37u; i++) nt_note(a + 4 + i * ED_NOTE_N, ED_NOTE_NEW, 20u + i % 40u, i * 1000u, 4096, 40u + i, 64);
    request(ED_NOTES, a, 4u + 37u * ED_NOTE_N); i = host_wire[8];
    request(ED_NOTES, a, 4u + 37u * ED_NOTE_N);
    a[2] = 0; a[3] = 0; a[4] = 0; a[5] = 127; n = request(ED_NOTES, a, 6);
    uint32_t c = wire14(11);
    bad += check("NOTES 75 notes (a 639-byte write twice): read in two pages", !i && !host_wire[8] && wire14(13) == 75 && c < 75 && c > 50 &&
                 n <= sizeof ed_out && (a[3] = (uint8_t)(c & 127u), a[4] = (uint8_t)(c >> 7), request(ED_NOTES, a, 6), wire14(11) == 75 - c));

    /* WATCH bit 6: PATTERN_CHANGED */
    reset();
    a[0] = 65; request(ED_WATCH, a, 1);
    bad += check("WATCH 65: bit 6 known", host_wire[5] == 65);
    bad += check("nothing changed: no PATTERN_CHANGED", !pushed(ED_PATTERN_CHANGED, 10));
    memset(a, 0, sizeof a); a[0] = 2; a[1] = 5; a[2] = 0; for (i = 0; i < 32u; i++) a[7 + i * ED_STEP_N + 5] = ST_REST;
    a[3] = 16; a[4] = 2; a[5] = 0; a[6] = 64; a[7] = 72; a[11] = 1; request(ED_PATTERN_DATA, a, 7u + 32u * ED_STEP_N);
    bad += check("the editor's own write: no PATTERN_CHANGED", !host_wire[8] && !pushed(ED_PATTERN_CHANGED, 10));
    trk[3].step[2].n = 1; trk[3].step[2].note[0] = 62; trk[3].step[2].time = ST_NOTE;
    i = pushed(ED_PATTERN_CHANGED, 10);
    bad += check("a step changed on the device: PATTERN_CHANGED with the banks", i && host_wire[i] == NTRK && !host_wire[i + 1] &&
                 host_wire[i + 2] == 127 && host_wire[i + 1u + 2u * NTRK] == 0xF7);
    bad += check("PATTERN_CHANGED once", !pushed(ED_PATTERN_CHANGED, 10));
    pattern_request(&trk[2], 4);
    i = pushed(ED_PATTERN_CHANGED, 10);
    bad += check("track 3 switched to bank 5 on the device: PATTERN_CHANGED says so", i && host_wire[i + 5] == 4);
    pattern_copy(&trk[2], 4, 6);
    bad += check("a bank copied on the device: PATTERN_CHANGED", pushed(ED_PATTERN_CHANGED, 10) != 0);
    return bad;
}
/* Melodee Studio's FM-1 page (F4): SETTINGS, MLEARN, DRUM, WATCH bit 7 and DEVICE_CHANGED */
static uint32_t sv(uint32_t row) { return wire14(8u + 2u * row); }   /* SETTINGS: a row's value in the reply */
static uint32_t set_row(uint32_t row, uint32_t v)
{
    uint8_t a[4] = {1, (uint8_t)row, (uint8_t)(v & 127u), (uint8_t)(v >> 7)};
    request(ED_SETTINGS, a, 4);
    return host_wire[6];
}
/* MLEARN v2: the parameters' part of the list (after the controls' names): count, the one waiting, per one 4 bytes */
static uint32_t mparams_at(void)
{
    uint32_t at = 10u + 3u * MLEARN_N, i;
    for (i = 0; i < MLEARN_N; i++)
        at += (uint32_t)strlen((const char *)host_wire + at) + 1u;
    return at;
}
static uint32_t plearn(uint32_t tr, uint32_t id, int any)  /* MLEARN 5: learn track tr's parameter id; -> rc */
{
    uint8_t a[4] = {5, (uint8_t)tr, (uint8_t)id, 1};
    request(ED_MLEARN, a, any ? 4u : 3u);
    return host_wire[6];
}
#define MP(k) ((const uint8_t *)&mparam_row[k])     /* parameter k's bytes */
static int studio_mlearn_params(uint8_t *a)
{
    int bad = 0;
    uint32_t n, at, i, ok = 1;
    uint8_t keep[sizeof mparam_row];
    const param_desc_t *dec = ed_tdesc(&trk[1], P_DEC);
    memset(mparam_row, 0, sizeof mparam_row); memset(mlearn_row, 0, sizeof mlearn_row);
    settings_midi_knob_mode = MK_ABSOLUTE; song.sel = 0;
    n = request(ED_MLEARN, a, 0); at = mparams_at();
    for (i = 0; i < MPARAM_N; i++) ok &= !host_wire[at + 3u + 4u * i];
    bad += check("MLEARN v2 list: then 23 parameters, none waiting, none learned", host_wire[at] == MPARAM_N && host_wire[at + 1] == 127 &&
                 host_wire[at + 2] == 127 && ok && n == at + 3u + 4u * MPARAM_N + 1u && host_wire[n - 1u] == 0xF7);
    bad += check("MLEARN 5 learns DECAY of track 2: it waits (the list says which), as the page", !plearn(1, P_DEC, 0) &&
                 !host_wire[7] && host_wire[mparams_at() + 1] == 1 && host_wire[mparams_at() + 2] == P_DEC && mlearn_on && ed_plearn);
    mlearn_receive(0x90u, 2, 60, 100); ed_service();
    bad += check("  a note: still waiting", ed_plearn && !mlearn_count());
    trk[1].p[P_DEC] = dec->min;
    mlearn_receive(0xB0u, 2, 74, 127); ed_service();
    bad += check("  CC 74 on channel 3: DECAY of track 2's now, on its channel, the wait over (nothing moved)",
                 !ed_plearn && !mlearn_on && mlearn_count() == 1 && mparam_valid() && MP(0)[0] == 74 &&
                 MP(0)[1] == 2 && MP(0)[2] == 1 && MP(0)[3] == P_DEC && trk[1].p[P_DEC] == dec->min);
    n = request(ED_MLEARN, a, 0); at = mparams_at();
    bad += check("MLEARN list: CC 74, channel 3, track 2, DECAY", host_wire[at + 3] == 74 && host_wire[at + 4] == 2 &&
                 host_wire[at + 5] == 1 && host_wire[at + 6] == P_DEC && !host_wire[at + 7]);
    bad += check("CC 74 on channel 3 sets track 2's DECAY over its range, the panel on track 1",
                 mlearn_receive(0xB0u, 2, 74, 127) && trk[1].p[P_DEC] == dec->max && (mlearn_receive(0xB0u, 2, 74, 0), trk[1].p[P_DEC] == dec->min));
    bad += check("... not on another channel", !mlearn_receive(0xB0u, 3, 74, 127) && trk[1].p[P_DEC] == dec->min);
    settings_midi_knob_mode = MK_REL1;
    mlearn_receive(0xB0u, 2, 74, 66);
    bad += check("MIDI MODE relative: +2 steps", trk[1].p[P_DEC] == dec->min + 2);
    settings_midi_knob_mode = MK_ABSOLUTE;
    bad += check("learnt again from CC 75: one CC per parameter", !plearn(1, P_DEC, 0) && (mlearn_receive(0xB0u, 2, 75, 64), ed_service(), 1) &&
                 mlearn_count() == 1 && MP(0)[0] == 75 && !mlearn_receive(0xB0u, 2, 74, 0));
    bad += check("LEVEL of track 3 on any channel (op 5's fourth byte)", !plearn(2, P_LEVEL, 1) &&
                 (mlearn_receive(0xB0u, 9, 20, 64), ed_service(), 1) && MP(1)[0] == 20 && MP(1)[1] == MPARAM_ANY &&
                 mlearn_receive(0xB0u, 15, 20, 0) && trk[2].p[P_LEVEL] == ed_tdesc(&trk[2], P_LEVEL)->min);
    a[0] = 3; a[1] = EN_K1; request(ED_MLEARN, a, 2); mlearn_receive(0xB0u, 0, 20, 64); ed_service();
    bad += check("one job per CC: KNOB 1 learns CC 20, LEVEL of track 3 lets it go", mlearn_row[EN_K1] == 20 && !MP(1)[0] &&
                 mlearn_count() == 2);
    plearn(3, P_REL, 0); mlearn_receive(0xB0u, 0, 20, 64); ed_service();
    bad += check("... and back: RELEASE of track 4 takes CC 20 from KNOB 1", !mlearn_row[EN_K1] && MP(1)[0] == 20 &&
                 MP(1)[1] == 0 && MP(1)[2] == 3 && MP(1)[3] == P_REL);
    mlearn_row[EN_K2] = 20; mlearn_row[28] = 'M'; mlearn_row[29] = 'L'; mlearn_row[30] = 1;   /* (a hand-made record: both) */
    i = midi_knob_w;
    bad += check("a CC with both a parameter and a control: the parameter wins", mlearn_receive(0xB0u, 0, 20, 127) &&
                 trk[3].p[P_REL] == ed_tdesc(&trk[3], P_REL)->max && midi_knob_w == i && mlearn_receive(0xB0u, 1, 20, 127) && midi_knob_w == i + 1u);
    mlearn_row[EN_K2] = 0; midi_knob_r = midi_knob_w;
    a[0] = 6; a[1] = 1; request(ED_MLEARN, a, 2);
    bad += check("MLEARN 6 forgets parameter 2 (RELEASE), DECAY stays", host_wire[6] == 3 && !MP(1)[0] && MP(0)[0] == 75 &&
                 mlearn_count() == 1 && !mlearn_receive(0xB0u, 0, 20, 0));
    bad += check("MLEARN 5 checks: past the tracks, past the parameters, a reserved one, wrong length: rc 1",
                 plearn(NTRK, P_DEC, 0) == 1 && plearn(1, P_COUNT, 0) == 1 && plearn(1, P_RESERVED33, 0) == 1 &&
                 (a[0] = 5, a[1] = 1, request(ED_MLEARN, a, 2), host_wire[6] == 1) && (a[0] = 6, request(ED_MLEARN, a, 1), host_wire[6] == 1) &&
                 (a[0] = 6, a[1] = MPARAM_N, request(ED_MLEARN, a, 2), host_wire[6] == 1) && !ed_plearn);
    ui.menu = 4;
    bad += check("MLEARN 5 while the device's MIDI LEARN page is open: rc 2", plearn(1, P_ATK, 0) == 2 && !ed_plearn);
    ui.menu = 0;
    for (i = 0; i < MPARAM_N; i++) mparam_bind(i % NPART, P_LEVEL + i / NPART, 30u + i, 0);
    memcpy(keep, mparam_row, sizeof mparam_row);
    bad += check("23 learned; a 24th waits but learns nothing (Studio asks for a place first)", mlearn_count() == MPARAM_N &&
                 !plearn(5, P_PAN, 0) && (mlearn_receive(0xB0u, 0, 90, 64), ed_service(), !ed_plearn) && !memcmp(keep, mparam_row, sizeof mparam_row));
    bad += check("... one of the 23 learnt again from CC 90: its place", !plearn(MP(22)[2], MP(22)[3], 0) &&
                 (mlearn_receive(0xB0u, 0, 90, 64), ed_service(), MP(22)[0] == 90) && mlearn_count() == MPARAM_N && (plearn(1, P_ATK, 0), 1));
    a[0] = 4; request(ED_MLEARN, a, 1);
    bad += check("MLEARN 4 ends a parameter's wait", !ed_plearn && !mlearn_on);
    plearn(1, P_ATK, 0); fm1_ms += 3100u; ed_device_poll();
    bad += check("a parameter's wait ends 3 s after the editor's last request", !ed_plearn && !mlearn_on);
    a[0] = 2; request(ED_MLEARN, a, 1);
    bad += check("MLEARN 2 forgets the parameters too", !mlearn_count() && !mparam_valid() && !mparam_row[0] && !mparam_row[MPARAM_N]);
    memset(mparam_row, 0x15, sizeof mparam_row);           /* (old stars in the rows: no table) */
    request(ED_MLEARN, a, 1);
    bad += check("... old stars in the rows are no table, and FORGET LEARNED leaves them", !mlearn_count() && mparam_row[MPARAM_N] == 0x15151515u);
    memset(mparam_row, 0, sizeof mparam_row);
    return bad;
}
static int studio_device(void)
{
    int bad = 0;
    uint8_t a[64] = {0};
    uint32_t n, i, j, p, seen = 0, rows = 0, ok = 1;
    reset();
    memset(mlearn_row, 0, sizeof mlearn_row); memset(part_patch, 0, sizeof part_patch);
    settings_lights = LIGHTS_MID; settings_click_level = 40; HOME_SELECT = HS_BPM; mlearn_on = 0; ed_learn = 0;
    n = request(ED_CAPS, a, 0);
    for (i = 6; i + 1u < n && host_wire[i] != 'C'; i += 2u + host_wire[i + 1]) ;
    for (j = 6; j + 1u < n && host_wire[j] != 'W'; j += 2u + host_wire[j + 1]) ;
    bad += check("CAPS: tag C (SETTINGS v1, MLEARN v2, BLE v1 with Bluetooth, else 0, PADS v2), W 2 bytes (bit 7 known)",
                 host_wire[i + 1] == 4 && host_wire[i + 2] == 1 && host_wire[i + 3] == 2 && host_wire[i + 4] == ED_BT && host_wire[i + 5] == 2 &&
                 host_wire[j + 1] == 2 && host_wire[j + 2] == 127 && host_wire[j + 3] == 1);

    /* SETTINGS */
    n = request(ED_SETTINGS, a, 0);
    bad += check("SETTINGS: 127, rc 0, the rows' values (LIGHTS, CLICK LEVEL, COLOR as the menu has them)", n == 9u + 2u * MI_COUNT &&
                 host_wire[5] == 127 && !host_wire[6] && host_wire[7] == MI_COUNT && sv(MI_LIGHTS) == LIGHTS_MID &&
                 sv(MI_CLICK_LEVEL) == 40 && sv(MI_COLOR) == settings.palette);
    for (p = 0; ok && p < 9u && (p == 1u || rows < MI_COUNT); p++) {           /* the list, a page at a time: the first of what fits a
                                                                   * reply (all here), then 7 rows a page */
        if (p == 1u) rows = 0;
        a[0] = 0; a[1] = (uint8_t)rows; a[2] = p ? 7 : 127; n = request(ED_SETTINGS, a, 3);
        uint32_t at = 8u + 2u * MI_COUNT, first = wire14(at), c = wire14(at + 2u);
        ok &= !host_wire[6] && first == rows && wire14(at + 4u) == MI_COUNT && c && n <= sizeof ed_out;
        at += 6u;
        for (i = 0; ok && i < c; i++, rows++) {
            uint32_t k = host_wire[at], m = wire14(at + 1u), st = host_wire[at + 3u];
            const char *name = (const char *)host_wire + at + 4u, *v;
            ok &= k == menu_kind(rows) && m == MR_N[rows] && st == (k == MR_PERCENT ? 5u : k <= MR_CYCLE) && !strcmp(name, MI_NAME[rows]);
            at += 5u + (uint32_t)strlen(name);
            for (j = 0; k <= MR_CYCLE && j < m; j++) {
                char b[24], w[24];
                v = (const char *)host_wire + at;
                menu_words(w, menu_value(rows, j, b), sizeof w);
                ok &= !strcmp(v, w);
                at += 1u + (uint32_t)strlen(v);
            }
            seen |= (rows == MI_COLOR && m == NPALETTES && k == MR_CYCLE) | (rows == MI_CLICK_LEVEL && k == MR_PERCENT && m == 100) << 1 |
                    (rows == MI_MIDI_LEARN && k == MR_PAGE) << 2 | (rows == MI_BACK && k == MR_ACTION) << 3 |
                    (rows == MI_HOME_SEL && !strcmp(name, "HOME SELECT") && m == HS_N) << 4;
        }
        ok &= host_wire[at] == 0xF7;
    }
    bad += check("SETTINGS list: every row in pages (kind, values, step, name, the values' words as the menu shows them)",
                 ok && rows == MI_COUNT && p >= 2u && seen == 31u);
    bad += check("SETTINGS sets LIGHTS FULL: the menu's value (rc 3: no flash here)", set_row(MI_LIGHTS, LIGHTS_N - 1u) == 3 &&
                 settings_lights == LIGHTS_N - 1u && sv(MI_LIGHTS) == LIGHTS_N - 1u);
    bad += check("SETTINGS past the row's values, on a page, past the rows: rc 1, unchanged", set_row(MI_LIGHTS, LIGHTS_N) == 1 &&
                 set_row(MI_ABOUT, 0) == 1 && set_row(MI_COUNT, 0) == 1 && settings_lights == LIGHTS_N - 1u);
    bad += check("SETTINGS CLICK LEVEL: 35 %, not 37 (KNOB 1 steps 5)", set_row(MI_CLICK_LEVEL, 37) == 1 && settings_click_level == 40 &&
                 set_row(MI_CLICK_LEVEL, 35) == 3 && settings_click_level == 35 && set_row(MI_CLICK_LEVEL, 105) == 1);
    bad += check("SETTINGS HOME SELECT in the menu's order: OCTAVE beside TRANSPOSE", set_row(MI_HOME_SEL, 5) == 3 &&
                 HOME_SELECT == HS_OCT && sv(MI_HOME_SEL) == 5);
    i = (settings.palette + 3u) % NPALETTES;
    bad += check("SETTINGS COLOR: the palette, as the menu sets it", set_row(MI_COLOR, i) == 3 && settings.palette == i && ui.force);
    midi_panel_delta[EN_K1] = 3; midi_panel_mask = 1;
    bad += check("SETTINGS MIDI KNOBS GENERAL: the old mapping's turns dropped (as the menu)", set_row(MI_MIDI_KNOBS, MK_GENERAL) == 3 &&
                 settings_midi_knobs == MK_GENERAL && !midi_panel_delta[EN_K1] && !midi_panel_mask);
    a[0] = 1; a[1] = MI_LIGHTS; request(ED_SETTINGS, a, 3);
    bad += check("SETTINGS set of the wrong length: rc 1 (and the values)", host_wire[5] == 1 && host_wire[6] == 1 && host_wire[7] == MI_COUNT);
    a[0] = 0; a[1] = MI_COUNT - 2u; a[2] = 5; n = request(ED_SETTINGS, a, 3);
    bad += check("SETTINGS list from the last two rows: two, ABOUT .. BACK", wire14(8u + 2u * MI_COUNT + 2u) == 2);
    settings_midi_knobs = MK_OFF;

    /* MLEARN */
    n = request(ED_MLEARN, a, 0);
    bad += check("MLEARN: nothing learned, 21 controls (7 knobs), their names", !host_wire[6] && !host_wire[7] && host_wire[8] == MLEARN_N &&
                 host_wire[9] == NE && !host_wire[10] && host_wire[12] == 127 &&
                 !strcmp((const char *)host_wire + 10 + 3 * MLEARN_N, E_NAME[0]));
    a[0] = 3; a[1] = EN_K1; request(ED_MLEARN, a, 2);
    bad += check("MLEARN learns KNOB 1: it waits, as the page does (nothing learned acts)", !host_wire[6] && host_wire[7] == EN_K1 + 1 && mlearn_on);
    mlearn_receive(0x90u, 0, 60, 100); ed_service();
    bad += check("  a note for a knob: still waiting", ed_learn == EN_K1 + 1 && !mlearn_row[EN_K1]);
    mlearn_receive(0xB0u, 2, 21, 64); ed_service();
    bad += check("  CC 21: KNOB 1's now, the wait over", mlearn_row[EN_K1] == 21 && !ed_learn && !mlearn_on && mlearn_count() == 1);
    a[0] = 3; a[1] = NE + B_SAVE; request(ED_MLEARN, a, 2);
    mlearn_receive(0x90u, 9, 60, 100); ed_service();
    n = request(ED_MLEARN, a, 0);
    j = 10u + 3u * (NE + B_SAVE);
    bad += check("MLEARN learns a button from a note on channel 10; the list says so", mlearn_row[NE + B_SAVE] == (MLEARN_NOTE | 60) &&
                 host_wire[10 + 3 * EN_K1] == 1 && host_wire[11 + 3 * EN_K1] == 21 && host_wire[12 + 3 * EN_K1] == 127 &&
                 host_wire[j] == 2 && host_wire[j + 1] == 60 && host_wire[j + 2] == 9);
    a[0] = 1; a[1] = EN_K1; request(ED_MLEARN, a, 2);
    bad += check("MLEARN forgets KNOB 1 (the button's stays)", !mlearn_row[EN_K1] && mlearn_count() == 1 && host_wire[6] == 3);
    a[0] = 3; a[1] = 0; request(ED_MLEARN, a, 2); fm1_ms += 3100u; ed_device_poll();
    bad += check("MLEARN's wait ends 3 s after the editor's last request", !ed_learn && !mlearn_on);
    ui.menu = 4; a[0] = 3; request(ED_MLEARN, a, 2);
    bad += check("MLEARN while the device's MIDI LEARN page is open: rc 2", host_wire[6] == 2 && !ed_learn);
    ui.menu = 0; a[0] = 3; request(ED_MLEARN, a, 2); a[0] = 4; request(ED_MLEARN, a, 1);
    bad += check("MLEARN stop: no wait", !host_wire[6] && !ed_learn && !mlearn_on);
    a[0] = 2; request(ED_MLEARN, a, 1);
    bad += check("MLEARN forget all (FORGET LEARNED)", !mlearn_count() && host_wire[6] == 3);
    a[0] = 1; a[1] = MLEARN_N; request(ED_MLEARN, a, 2);
    bad += check("MLEARN past the controls, op 0, wrong length: rc 1", host_wire[6] == 1 && (a[0] = 0, request(ED_MLEARN, a, 1), host_wire[6] == 1) &&
                 (a[0] = 2, request(ED_MLEARN, a, 2), host_wire[6] == 1));
    bad += studio_mlearn_params(a);

    /* PADS (53): a kit's pads as its synth's pads block has them (DRUM: TUNE, DECAY, the third, LEVEL) */
    a[0] = 0; request(ED_PADS, a, 1);
    bad += check("PADS on a track that is not a kit: rc 1, no pads", host_wire[6] == 1 && !host_wire[7] && !host_wire[8]);
    set_engine_of(&trk[1], ENGI_DRUM); apply_preset_to(&trk[1], 0); trk[1].p[P_E0] = DK_808;
    a[0] = 1; n = request(ED_PADS, a, 1);
    {
        const uint8_t *q = host_wire + 9, *o = q + 12, *v = o + 16;  /* (the parameters, the order, the values) */
        const char *w = (const char *)v + 64;
        int names = !strcmp(w, "KICK") && !strcmp(w + 5, "TUNE") && !strcmp(w + 10, "DECAY") && !strcmp(w + 16, "TONE") &&
                    !strcmp(w + 21, "LEVEL") && !strcmp(w + 27, "SNARE") && !strcmp(w + 27 + 6 + 5 + 6, "SNAPPY");
        bad += check("PADS 808 kit: 16 pads x 4 parameters (TUNE from -24, 48 steps; DECAY, the third, LEVEL from 0, 127; the kit's own: TUNE 0, 64, 64, LEVEL 127) "
                     "in the knob's order (KICK SNARE CLAP HAT C ..), at their own values (TUNE 24 = 0, DECAY 64, TONE 64, "
                     "LEVEL 127), names and each pad's labels", !host_wire[6] && host_wire[7] == 16 && host_wire[8] == 4 &&
                     q[0] == 40 && q[1] == 48 && q[2] == 24 && q[3] == 64 && q[4] == 127 && q[5] == 64 && q[9] == 64 &&
                     q[10] == 127 && q[11] == 127 &&
                     o[0] == 0 && o[1] == 1 && o[2] == 6 && o[3] == 7 && v[0] == 24 && v[1] == 64 && v[2] == 64 && v[3] == 127 && names);
    }
    a[1] = 1; a[2] = 2; a[3] = 36; a[4] = 10; a[5] = 100; a[6] = 90; a[7] = 0; a[8] = 127; a[9] = 0; a[10] = 0;
    request(ED_PADS, a, 11);
    bad += check("PADS writes SNARE and TOM L as the page would", !host_wire[6] && (*(drum_patch_t *)(void *)patch_of(1)).c[1][0] == 12 && (*(drum_patch_t *)(void *)patch_of(1)).c[1][1] == -54 &&
                 (*(drum_patch_t *)(void *)patch_of(1)).c[1][2] == 36 && (*(drum_patch_t *)(void *)patch_of(1)).c[1][3] == 37 && (*(drum_patch_t *)(void *)patch_of(1)).c[2][0] == -24 && (*(drum_patch_t *)(void *)patch_of(1)).c[2][1] == 63 &&
                 (*(drum_patch_t *)(void *)patch_of(1)).c[2][2] == -64 && (*(drum_patch_t *)(void *)patch_of(1)).c[2][3] == 127 && host_wire[9 + 12 + 16 + 4] == 36 && host_wire[9 + 12 + 16 + 7] == 90);
    a[3] = 49; request(ED_PADS, a, 11);
    bad += check("PADS TUNE past +24, past the pads, the wrong length: rc 1, unchanged", host_wire[6] == 1 && (*(drum_patch_t *)(void *)patch_of(1)).c[1][0] == 12 &&
                 (a[3] = 36, a[1] = 15, request(ED_PADS, a, 11), host_wire[6] == 1) && (a[1] = 1, request(ED_PADS, a, 10), host_wire[6] == 1));
    trk[1].p[P_E0] = DK_909; request(ED_PADS, a, 1);
    bad += check("PADS 909 kit: eleven pads, the 909's own third labels (ATTACK on its kick)", !host_wire[6] && host_wire[7] == D9_NUM &&
                 !strcmp((const char *)host_wire + 9 + 12 + D9_NUM + 4 * D9_NUM + 5 + 5 + 6, "ATTACK"));

    /* WATCH bit 7: DEVICE_CHANGED */
    a[0] = 1; a[1] = 1; request(ED_WATCH, a, 2);
    bad += check("WATCH 1, 1: bit 7 known (a second byte back)", host_wire[5] == 1 && host_wire[6] == 1 && host_wire[7] == 0xF7);
    bad += check("nothing changed: no DEVICE_CHANGED", !pushed(ED_DEVICE_CHANGED, 10));
    set_row(MI_LIGHTS, LIGHTS_LOW);
    bad += check("the editor's own SETTINGS: no DEVICE_CHANGED", !pushed(ED_DEVICE_CHANGED, 10));
    settings_lights = LIGHTS_HIGH;
    i = pushed(ED_DEVICE_CHANGED, 10);
    bad += check("LIGHTS changed on the device: DEVICE_CHANGED (bit 0) with the values", i && host_wire[i] == 1 && host_wire[i + 1] == MI_COUNT &&
                 host_wire[i + 2 + 2 * MI_LIGHTS] == LIGHTS_HIGH && !pushed(ED_DEVICE_CHANGED, 10));
    mlearn_bind(NE + 1u, 30, 0); (*(drum_patch_t *)(void *)patch_of(1)).c[0][0] = 5;
    i = pushed(ED_DEVICE_CHANGED, 10);
    bad += check("a control learned and a drum tuned on the device: bits 0, 1, 3", i && host_wire[i] == 11);
    a[0] = 127; request(ED_WATCH, a, 1);
    settings_lights = LIGHTS_LOW;
    bad += check("WATCH 127 (one byte): 127 back, no DEVICE_CHANGED", host_wire[5] == 127 && host_wire[6] == 0xF7 && !pushed(ED_DEVICE_CHANGED, 10));
    a[0] = 1; a[1] = 2; request(ED_WATCH, a, 2);
    bad += check("WATCH's second byte: only bit 7 is known", host_wire[5] == 1 && !host_wire[6]);
    memset(mlearn_row, 0, sizeof mlearn_row); memset(part_patch, 0, sizeof part_patch); settings_lights = LIGHTS_MID;
    return bad;
}

#if MELODEE_BLE_EDITOR
/* Melodee Studio over Bluetooth (MELODEE_BLE_EDITOR): requests go in through the stack's own BLE-MIDI encoder and its
 * MIDI I/O entry (ble_att_midi_packet: the decoder, then midi_ble.c), replies come off the computer's link as ble_att.c
 * sends them (peek, encode, pop) and are decoded as the computer would. cap: a packet's bytes (ATT MTU - 3) */
static uint8_t bt_wire[2048];                         /* what the computer got: SysEx bytes in order */
static uint32_t bt_wire_n, bt_packets, bt_seen[16], bt_seen_at[16], bt_nseen;   /* (other messages, and where) */
static struct ble_midi_dec bt_dec;
static struct ble_midi_enc bt_enc;
static void bt_sink(void *ctx, uint32_t pkt, uint16_t ts)
{
    uint32_t cin = pkt & 15u, k = cin == 4u ? 3u : cin - 4u, i;
    (void)ctx; (void)ts;
    if (cin < 4u || cin > 7u) {
        if (bt_nseen < 16u) { bt_seen[bt_nseen] = pkt; bt_seen_at[bt_nseen++] = bt_wire_n; }
        return;
    }
    for (i = 0; i < k && bt_wire_n < sizeof bt_wire; i++)
        bt_wire[bt_wire_n++] = (uint8_t)(pkt >> (8u + 8u * i));
}
static void bt_reset(void)
{
    ble_init((uint8_t[]){1, 2, 3, 4, 5, 0xC6}, 1);
    memset(&mbe, 0, sizeof mbe); memset(mb.ln, 0, sizeof mb.ln);
    mb.on = mb.started = 1; mb.ln[BLE_LINK_PERIPH].ready = 1; mbe.epoch = 1;
    ble_midi_dec_reset(&bt_dec); ble_midi_enc_reset(&bt_enc);
    bt_wire_n = bt_packets = bt_nseen = 0;
}
static void bt_send(const uint8_t *f, uint32_t n, uint32_t cap)   /* a frame (F0..F7) from the computer */
{
    struct ble_midi_enc e;
    uint8_t buf[256];
    uint32_t i = 0;
    ble_midi_enc_reset(&e); ble_midi_enc_begin(&e, buf, (uint16_t)cap);
    while (i < n) {
        uint32_t k = n - i >= 3u ? 3u : n - i;
        uint32_t pkt = (i + k < n ? 4u : 4u + k) | (uint32_t)f[i] << 8 | (k > 1u ? (uint32_t)f[i + 1] << 16 : 0u) |
                       (k > 2u ? (uint32_t)f[i + 2] << 24 : 0u);
        if (ble_midi_enc_add(&e, pkt, fm1_ms) > 0) { i += k; continue; }
        ble_att_midi_packet(buf, ble_midi_enc_len(&e));   /* full: this packet goes, the next begins */
        ble_midi_enc_begin(&e, buf, (uint16_t)cap);
    }
    if (ble_midi_enc_len(&e)) ble_att_midi_packet(buf, ble_midi_enc_len(&e));
}
static uint32_t bt_take(uint32_t cap)                 /* the link's notifications until nothing waits */
{
    uint8_t buf[256];
    uint32_t pkt, t, before = bt_packets;
    for (;;) {
        ble_midi_enc_begin(&bt_enc, buf, (uint16_t)cap);
        while (ble_app_midi_peek(&pkt, &t)) {
            if (!ble_midi_enc_add(&bt_enc, pkt, t)) break;
            ble_app_midi_pop();
        }
        if (!ble_midi_enc_len(&bt_enc)) return bt_packets - before;
        bt_packets++;
        ble_midi_dec(&bt_dec, buf, ble_midi_enc_len(&bt_enc), bt_sink, 0);
    }
}
static uint32_t bt_frame(uint32_t cmd, const uint8_t *a, uint32_t n, uint8_t *f)
{
    uint32_t k = 0, i;
    f[k++] = 0xF0; f[k++] = ED_HDR0; f[k++] = ED_HDR1; f[k++] = ED_HDR2; f[k++] = (uint8_t)cmd;
    for (i = 0; i < n; i++) f[k++] = a[i];
    f[k++] = 0xF7;
    return k;
}
static uint32_t bt_request(uint32_t cmd, const uint8_t *a, uint32_t n, uint32_t cap)   /* -> the reply's length (0 none) */
{
    uint8_t f[700];
    bt_wire_n = bt_nseen = 0;
    bt_send(f, bt_frame(cmd, a, n, f), cap);
    ed_service(); bt_take(cap);
    return bt_wire_n;
}
static int studio_bluetooth(void)
{
    int bad = 0;
    uint8_t a[8] = {0}, usb_reply[1100], f[32];
    (void)usb_reply;
    uint32_t n, m, i, ok, before;
    reset(); bt_reset();
    n = request(ED_INFO, a, 0); memcpy(usb_reply, host_wire, n);
    uint32_t info_n = n;
    host_wire_n = 0; m = bt_request(ED_INFO, a, 0, 20); host_drain();
    bad += check("Bluetooth: INFO as over USB byte for byte, in 20-byte packets, nothing on USB",
                 m == n && !memcmp(bt_wire, usb_reply, n) && bt_packets > 3u && !host_wire_n);
    n = request(ED_CAPS, a, 0);
    bad += check("CAPS over USB ends with E 2: link 0, Bluetooth's list v1", host_wire[n - 5] == 'E' && host_wire[n - 4] == 2 &&
                 host_wire[n - 3] == 0 && host_wire[n - 2] == 1);
    m = bt_request(ED_CAPS, a, 0, 20);
    bad += check("CAPS over Bluetooth: link 1, the rest as over USB", m == n && bt_wire[m - 3] == 1 && !memcmp(bt_wire, host_wire, n - 3));
    host_wire_n = 0; bt_request(ED_PING, a, 0, 20); host_drain();
    bad += check("PING over Bluetooth: answered there only", bt_wire_n == 7 && bt_wire[4] == ED_PING && !host_wire_n);

    static const uint8_t usb_only[] = {11, 12, 13, 14, 15, 23, 24, 26, 32, 46, 54, 55, 60, 65, 66, 67, 72, 79, 81, 82, 96, 100, 101, 127};
    for (i = 0, ok = 1; i < sizeof usb_only; i++) {
        a[0] = 0; a[1] = 0;
        ok &= !bt_request(usb_only[i], a, usb_only[i] == 66 ? 0 : 1, 20) && !mbe.ready && !ed_ble_busy();
    }
    static const uint8_t other[] = {0xF0, 0x7D, 0x4D, 0x4C, 0x01, 0x00, 0xF7}, dx7[] = {0xF0, 0x43, 0x20, 0x00, 0xF7};
    bt_send(other, sizeof other, 20); ed_service(); bt_send(dx7, sizeof dx7, 20); ed_service(); bt_take(20);
    ok &= !bt_wire_n && !mbe.ready;                                            /* (a preset file, a DX7 request: USB's) */
    ok &= bt_request(ED_PING, a, 0, 20) == 7;
    bad += check("samples, backups and kits, the diagnostics, BLE_CAL, RES, the pushes, other SysEx: nothing over Bluetooth", ok);
    a[0] = 1; n = request(ED_BACKUP_LIST, a, 1);
    bad += check("... BACKUP_LIST still answers over USB", n > 8u && host_wire[4] == ED_BACKUP_LIST);
    static const uint8_t allowed[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 16, 17, 18, 19, 20, 21, 22, 25, 27, 28, 29, 30, 31, 33, 34, 35,
                                      36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 47, 48, 49, 50, 51, 52, 53, 64, 73, 74, 80, 85, 86,
                                      87, 88};
    for (i = 0, ok = 1; i < 128u; i++) {
        uint32_t in = 0, j;
        for (j = 0; j < sizeof allowed; j++) in |= allowed[j] == i;
        ok &= (i < 96u && (ED_BLE_OK[i >> 3] >> (i & 7u) & 1u)) == in;
    }
    bad += check("the list: the session, sounds, patterns, song and device commands, nothing else", ok);

    /* a page of 1 KB: Prophet's names, as over USB, in 20-byte and in 182-byte packets; the link's notes and clock meanwhile */
    a[0] = ENGI_PROPHET; a[1] = 0; a[2] = 0;
    n = request(ED_NAMES, a, 3); memcpy(usb_reply, host_wire, n);
    bt_wire_n = bt_nseen = 0;
    bt_send(f, bt_frame(ED_NAMES, a, 3, f), 20); ed_service();
    uint32_t note = 0x09u | 0x90u << 8 | 60u << 16 | 100u << 24, off = 0x08u | 0x80u << 8 | 60u << 16;
    ok = ed_ble_busy() && midi_track_emit(note) && midi_track_emit(off);
    midi_sync_emit(0xF8u);
    m = bt_take(20);
    bad += check("NAMES (1 KB) over Bluetooth: byte for byte, 20-byte packets", n > 900u && bt_wire_n == n && !memcmp(bt_wire, usb_reply, n) && m > 40u);
    bad += check("... the link's notes wait for its F7, the clock goes in between", ok && bt_nseen == 3 && (bt_seen[0] & 0xFFFFu) == 0xF80Fu &&
                 bt_seen_at[0] < n && bt_seen[1] == note && bt_seen[2] == off && bt_seen_at[1] == n && !ed_ble_busy());
    bt_wire_n = 0; bt_request(ED_NAMES, a, 3, 182);
    bad += check("... and in 182-byte packets (an ATT MTU of 185)", bt_wire_n == n && !memcmp(bt_wire, usb_reply, n));

    /* one at a time: a reply going out holds the editor (USB too: ed_out is the reply's); the next frame waits, a third is dropped */
    bt_wire_n = 0; host_wire_n = 0;
    bt_send(f, bt_frame(ED_INFO, a, 0, f), 20); ed_service();
    bt_send(f, bt_frame(ED_PING, a, 0, f), 20);
    a[0] = 4; bt_send(f, bt_frame(ED_TRACK_MIX, a, 1, f), 20);                 /* (dropped: a frame waits) */
    ota_frame_done(); sysex_byte(0xF0); sysex_byte(ED_HDR0); sysex_byte(ED_HDR1); sysex_byte(ED_HDR2); sysex_byte(ED_PING); sysex_byte(0xF7);
    ed_service(); ed_service(); host_drain();
    ok = ed_ble_busy() && mbe.ready && sx_ready && !host_wire_n;
    bt_take(20); m = bt_wire_n;
    ed_service(); host_drain();                                              /* USB's first, then Bluetooth's */
    ok &= host_wire_n == 7 && host_wire[4] == ED_PING && mbe.ready;
    ed_service(); bt_take(20); ed_service(); bt_take(20);
    bad += check("a reply going out holds the editor: the next frame and USB's wait, a third is dropped",
                 ok && m == info_n && bt_wire_n == info_n + 7u && bt_wire[info_n + 4u] == ED_PING && !mbe.ready && !ed_ble_busy());

    /* a request cut by a note: the stack's decoder drops the SysEx, the note plays, nothing answers; the next one does */
    static const uint8_t cut1[] = {0x80, 0x81, 0xF0, ED_HDR0, ED_HDR1, ED_HDR2, ED_PING}, cut2[] = {0x80, 0x82, 0x90, 60, 100},
                         cut3[] = {0x80, 0x83, 0xF7};
    before = mb.ln[BLE_LINK_PERIPH].rw;
    bt_wire_n = 0; ble_att_midi_packet(cut1, sizeof cut1); ble_att_midi_packet(cut2, sizeof cut2); ble_att_midi_packet(cut3, sizeof cut3);
    ed_service(); bt_take(20);
    bad += check("a frame cut by a note: no reply, the note goes to the music", !bt_wire_n && !mbe.ready &&
                 mb.ln[BLE_LINK_PERIPH].rw - before == 1 && mb.ln[BLE_LINK_PERIPH].rx[before % MBQ] == (0x9u | 0x90u << 8 | 60u << 16 | 100u << 24));
    mb.ln[BLE_LINK_PERIPH].rr = mb.ln[BLE_LINK_PERIPH].rw;
    bad += check("... and the next frame is answered", bt_request(ED_PING, a, 0, 20) == 7);
#if BLE_LINKS > 1
    uint8_t was = ll_enter(BLE_LINK_CENTRAL);
    bt_wire_n = 0; bt_send(f, bt_frame(ED_PING, a, 0, f), 20);
    ll_leave(was);
    ed_service(); bt_take(20);
    bad += check("SysEx on the device's link (a keyboard Melodee connected out to): not the editor's", !bt_wire_n && !mbe.ready);
#endif

    /* WATCH over Bluetooth: the pushes go there, not to USB; the link coming up again ends it (as a USB reset) */
    a[0] = 1; bt_request(ED_WATCH, a, 1, 20);
    ok = bt_wire_n == 7 && bt_wire[5] == 1 && ed_w.on && ed_w.link == 1;
    TSEL->p[P_LEVEL] = (int16_t)(TSEL->p[P_LEVEL] > 60 ? 40 : 90);
    fm1_ms += 30; ed_w.last_ms = fm1_ms; host_wire_n = 0; bt_wire_n = 0;
    ed_service(); bt_take(20); host_drain();
    bad += check("WATCH over Bluetooth: CHANGED pushed there, nothing on USB", ok && bt_wire_n > 8u && bt_wire[4] == ED_CHANGED &&
                 !bt_wire[5] && bt_wire[6] == P_LEVEL && !host_wire_n);
    mbe.epoch++; fm1_ms += 30; ed_w.last_ms = fm1_ms; ed_service();
    bad += check("... a new connection ends WATCH", !ed_w.on);

    /* the link going while a reply goes out: the rest is dropped, the editor goes on (USB answers) */
    bt_send(f, bt_frame(ED_INFO, a, 0, f), 20); ed_service();
    ok = ed_ble_busy();
    ble_app_state();                                                          /* (no MIDI link in the fake stack: gone) */
    ok &= !ed_ble_busy() && !mb.ln[BLE_LINK_PERIPH].ready;
    bad += check("the computer's link gone mid-reply: the rest dropped, USB answers", ok && request(ED_PING, a, 0) == 7);
    bt_wire_n = 0; bt_send(f, bt_frame(ED_PING, a, 0, f), 20); ed_service(); bt_take(20);
    bad += check("... and a frame without a link gets no reply", !bt_wire_n && !ed_ble_busy() && !mbe.ready);
    mb.ln[BLE_LINK_PERIPH].ready = 1;
    return bad;
}
#endif
int main(void)
{
    int bad = midi_track_protocol() + library_transfers() + native_motion_protocol() + synth_protocol() + bank_protocol() + preferences() + framing() + uart_recovery() + steps() + song_protocol() + malformed_saves() + names_whole() +
              shared_protocol() + user_preset_roundtrip() + concert_pitch() + scale_catalogue() + desc_bulk() + project_scale_protocol() + studio_session() +
              studio_song() + studio_patterns() + studio_device();
#if MELODEE_BLE_EDITOR
    bad += studio_bluetooth();
#endif
    printf("%s\n", bad ? "EDITOR TEST FAILED" : "editor test passed");
    return bad != 0;
}
#endif
