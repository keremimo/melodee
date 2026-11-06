/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The virtual FM-1 (spike): the firmware's editor (editor.c), its storage (storage.c on a 1 MiB NOR in RAM), its
 * projects, kits and presets, its engines, mixer and sequencer, without the UI drawing or any hardware. The web
 * page talks to it in the editor's own SysEx, as it talks to an FM-1 over USB.
 * Composition: tests/editor_test.c's (hostsim.c's engines and sequencer, ui.c, the editor) with backup_test.c's
 * NOR flash under MELODEE_FLASH 1, in melodee.c's order (storage, kits, user presets, projects, CZ/FM6 stores,
 * editor). The clock is web/emu/melodee_web.c's: a millisecond at a time, mix_block every CTL frames at FS as the
 * audio ISR; main.c's loop services (pattern_poll, cz_service, fm6_service, ed_service, glo_poll, settings_poll)
 * every 15 ms, ed_service every millisecond (main.c's wait between frames).
 * A wait of the main loop for the ISR (ed_flash_stop, the backup LIST, a kit's settle) feeds the watchdog: here
 * that is a millisecond of the ISR's time (fm1_wdt_feed below), the sound it makes kept for core_render.
 * Nothing of the firmware changes for this file.
 *   web/core/build.sh  ->  build/core/melodee_core.wasm
 *   cc -DCORE_NATIVE_MAIN ... web/core/melodee_core.c: the same core natively, replaying SysEx frames (stdin) */
#include <stdint.h>
static unsigned char nor[0x100000];                     /* the FM-1's 1 MiB NOR flash, erased at a first boot */
#define DRK_XIP(off) ((const uint8_t *)nor + (off))      /* user kits play from the pool (drum_real.c) */
#define MELODEE_OTA 1
#define MELODEE_FLASH 1
#ifndef MELODEE_VERSION
#define MELODEE_VERSION "CORE"
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main

static void fm1_wdt_feed(void);                         /* the ISR's millisecond (below) */
static void core_drain(void);
static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
static void panel_setup(void) {}

/* the NOR: reads as XIP, 4 KiB erases, programming only clears bits (as the part) */
static uint8_t flash_ok;
static uint32_t nor_erases, nor_progs;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > sizeof nor || n > sizeof nor - off) return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    off &= ~0xFFFu;
    if (off >= sizeof nor) return -1;
    memset(nor + off, 0xFF, 4096);
    nor_erases++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    if (off > sizeof nor || n > sizeof nor - off) return -1;
    for (uint32_t i = 0; i < n; i++) nor[off + i] &= s[i];
    nor_progs++;
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's part: persist_boot finds its flash */
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
static void audio_silence(void) {}
#include "../firmware/src/storage.c"
#include "../firmware/src/kit_store.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#include "../firmware/src/cz_store.c"
#include "../firmware/src/fm6_store.c"
#define ED_BK_FLASH_PTR(off) (nor + (off))
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { core_drain(); }            /* the USB IN endpoint: the page takes every packet */
#include "../firmware/src/editor.c"

/* ------------------------------------------------------------------ the page's side --- */
#define OUTQ 0x40000u                                   /* replies and pushes not read yet (a BACKUP GET: ~300 B) */
static uint8_t out_q[OUTQ];
static uint32_t out_w, out_r, out_lost;
static void core_drain(void)                           /* USB-MIDI SysEx packets -> bytes (editor_test.c host_drain) */
{
    while (so_r != so_w) {
        uint32_t p = sx_out_q[so_r++ % SXQ], cin = p & 15u, i;
        uint32_t n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (i = 0; i < n; i++) {
            if (out_w - out_r >= OUTQ) { out_lost++; continue; }
            out_q[out_w++ % OUTQ] = (uint8_t)(p >> (8u * (i + 1u)));
        }
    }
}

/* the sound: stereo float, made a millisecond at a time and kept until core_render takes it */
#define RING 32768u                                     /* frames: 0.74 s (an ed_flash_stop waits 100 ms at most) */
static float ring[RING * 2u], rendered[4096u * 2u];
static uint32_t ring_w, ring_r, owed, ring_lost;
static uint8_t in_isr, in_loop;
static void isr_ms(void)                               /* TIMER5's millisecond, then the ALNK0 blocks it owes */
{
    fm1_ms++;
    owed += FS;
    while (owed >= 1000u * CTL) {
        int32_t o[2 * CTL];
        owed -= 1000u * CTL;
        in_isr = 1;
        mix_block(o, CTL);
        in_isr = 0;
        for (uint32_t i = 0; i < CTL; i++) {
            if (ring_w - ring_r >= RING) { ring_r++; ring_lost++; }   /* nobody listening: the oldest goes */
            uint32_t r = (ring_w++ % RING) * 2u;
            ring[r] = (float)o[2u * i] / 65536.0f;      /* Q15, 6 dB down as audio.c sends it to the codec */
            ring[r + 1u] = (float)o[2u * i + 1u] / 65536.0f;
        }
    }
}
static uint32_t wdt_feeds;
static void fm1_wdt_feed(void)                         /* a main-loop wait: the ISR keeps running meanwhile */
{
    wdt_feeds++;
    if (!in_isr) isr_ms();
}
static void frame_done(void)                           /* a frame the editor left (not ours): ota_service's, dropped */
{
    if (sx_ready) ota_frame_done();
}
static void loop_services(void)                        /* main.c's loop, without the panel, the screen and the USB */
{
    if (in_loop) return;
    in_loop = 1;
    wire_release();
    pattern_poll();
    cz_service();
    fm6_service();
    ed_service();
    frame_done();
    glo_poll();
    settings_poll();
    in_loop = 0;
}
static void core_tick(void)                            /* one millisecond of the device */
{
    isr_ms();
    if (in_loop) return;
    if (fm1_ms % 15u == 0u) loop_services();
    else { in_loop = 1; ed_service(); frame_done(); in_loop = 0; }   /* (main.c: between two frames) */
}

static void core_melodee_init(void)                    /* main.c melodee_init */
{
    uint32_t i;
    trk_voices_bind();
    midi_insert_init();
    punch_reset();
    chain_defaults(&chain_config);
    pattern_init();
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    undo_depth++;
    fm6_init();
    cz_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, trk_def_engine(i));
        apply_preset_to(t, trk_def_preset(i));
        t->engine = t->eng_req;
        track_defaults_steps(t);
        pat_sig[i] = steps_sig(t);
        pat_last[i] = 0;
    }
    undo_depth--;
    song.sel = 0;
    song.master_q12 = 2048;
    ui.home = 1;
    ui.force = 1;
}

#ifdef __wasm__
#undef __attribute__
#define EXPORT(name) __attribute__((used, export_name(#name)))
#else
#define EXPORT(name)
#endif

/* the page's scratch: requests, records and kits are written here before a call names them */
static uint8_t scratch[0x20000];
EXPORT(core_scratch) uint8_t *core_scratch(void) { return scratch; }
EXPORT(core_scratch_size) uint32_t core_scratch_size(void) { return sizeof scratch; }
EXPORT(core_nor_ptr) uint8_t *core_nor_ptr(void) { return nor; }
EXPORT(core_nor_len) uint32_t core_nor_len(void) { return sizeof nor; }
EXPORT(core_now) uint32_t core_now(void) { return fm1_ms; }
EXPORT(core_fs) uint32_t core_fs(void) { return FS; }

/* power on (fm1_main's order, without the screen, the input and the audio hardware). keep: the NOR holds a
 * previous session's flash (written to core_nor_ptr), else it is erased as a new device's */
EXPORT(core_boot) uint32_t core_boot(uint32_t keep)
{
    if (!keep) memset(nor, 0xFF, sizeof nor);
    settings_init();
    persist_boot();
    settings_init();
    panel_init();
    core_melodee_init();
    glo_restore();
    project_boot();
    usb.config = 1;                                     /* the editor's port is open */
    return flash_ok;
}

/* SysEx from the page: each complete frame goes to the editor at once (as the main loop's wait does) */
EXPORT(core_sysex_in) uint32_t core_sysex_in(const uint8_t *p, uint32_t n)
{
    uint32_t i, frames = 0;
    for (i = 0; i < n; i++) {
        sysex_byte(p[i]);
        if (p[i] == 0xF7u) {
            in_loop = 1;
            ed_service();
            frame_done();
            in_loop = 0;
            frames++;
        }
    }
    core_drain();
    return frames;
}
/* SysEx to the page: up to max bytes of the replies and pushes waiting; the count */
EXPORT(core_sysex_out) uint32_t core_sysex_out(uint8_t *p, uint32_t max)
{
    uint32_t k = 0;
    core_drain();
    while (k < max && out_r != out_w) p[k++] = out_q[out_r++ % OUTQ];
    return k;
}
EXPORT(core_sysex_pending) uint32_t core_sysex_pending(void) { core_drain(); return out_w - out_r; }

/* time without a listener: ms milliseconds of the device (its sound is dropped); ms 0 runs the loop once */
EXPORT(core_poll) void core_poll(uint32_t ms)
{
    if (!ms) loop_services();
    while (ms--) core_tick();
    ring_r = ring_w;
    core_drain();
}
/* n frames (<= 4096) of stereo, interleaved float; the device runs as long as they take */
EXPORT(core_render) float *core_render(uint32_t n)
{
    uint32_t k;
    if (n > 4096u) n = 4096u;
    while (ring_w - ring_r < n) core_tick();
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        rendered[2u * k] = ring[i];
        rendered[2u * k + 1u] = ring[i + 1u];
    }
    core_drain();
    return rendered;
}

/* a project record (the expanded form, BANK_STORE_SIZE bytes; mel_file.js pjExpand) saved to slot as SAVE would,
 * then loaded as LOAD would. 0 = loaded, 1 = not a record or no such slot, 2 = not written, 3 = not loaded, 4 = busy.
 * Slots 0..3 only for now: project.c's host mirrors (proj_slot[4], proj_bank_slot[4], built without FM1_IRQ_TARGET)
 * are indexed by slots up to 31 in proj_write_slot / proj_read_slot / proj_publish_slot, past their end from slot 4 */
#define CORE_SLOTS 4u
EXPORT(core_load_record) uint32_t core_load_record(const uint8_t *p, uint32_t n, uint32_t slot)
{
    if (n != BANK_STORE_SIZE || !bank_valid(p, n) || slot >= CORE_SLOTS) return 1;
    if (ed_flash_stop()) return 4;
    if (proj_write_slot(slot, p, n)) return 2;
    project_load(slot);
    if (proj_cur != slot) return 3;
    song.g[G_SLOT] = (int16_t)(slot + 1u);
    return 0;
}

/* SONG from row (as SONG's PLAY): the internal clock, no loop, so it ends. 0 = started, else chain_prepare_from's */
EXPORT(core_song_prepare) uint32_t core_song_prepare(uint32_t row)
{
    if (ed_flash_stop()) return 2;
    song.g[G_CLOCK] = 0;                                /* CLK INT: nothing else would advance it */
    chain_config.rsv[CHAIN_LOOP] = 0;
    return chain_prepare_from(row);
}
/* bit 0 playing, 1 chain running, 2 armed (starts at the next block), 3 ended; bits 8..15 the row */
EXPORT(core_song_state) uint32_t core_song_state(void)
{
    uint32_t ended = !chain.running && !song.playing && !chain.armed && transport_req != 1u;
    return (uint32_t)song.playing | (uint32_t)chain.running << 1 | (uint32_t)chain.armed << 2 | ended << 3 |
           (uint32_t)chain.row << 8;
}
EXPORT(core_stop) uint32_t core_stop(void) { return (uint32_t)ed_flash_stop(); }

/* counters for the spike's report: [0] ISR ms, [1] watchdog feeds, [2] NOR erases, [3] NOR programs,
 * [4] reply bytes lost, [5] frames dropped unheard, [6] the flash state (flash_ok), [7] projects used */
static uint32_t stats[8];
EXPORT(core_stats) uint32_t *core_stats(void)
{
    uint32_t used = 0;
    for (uint32_t i = 0; i < PROJ_SLOTS; i++) used += proj_meta[i].used != 0;
    stats[0] = fm1_ms; stats[1] = wdt_feeds; stats[2] = nor_erases; stats[3] = nor_progs;
    stats[4] = out_lost; stats[5] = ring_lost; stats[6] = flash_ok; stats[7] = used;
    return stats;
}

#ifdef CORE_NATIVE_MAIN
/* the same core natively:
 *   core_native < frames       SysEx frames (F0 .. F7, one after another), each reply as a hex line: "> frame",
 *                              then "< reply" for every frame the core sent back before the next request
 *   core_native render REC SECONDS OUT   a project record (expanded) loaded to slot 0, its song from row 0, SECONDS
 *                              of it as interleaved float (raw) in OUT, as web/core/spike.mjs renders it in WASM */
static int render_main(const char *rec, uint32_t secs, const char *out)
{
    static uint8_t r[BANK_STORE_SIZE + 1u];
    FILE *f = fopen(rec, "rb");
    if (!f) return 2;
    uint32_t n = (uint32_t)fread(r, 1, sizeof r, f), rc;
    fclose(f);
    core_boot(0);
    core_poll(100);
    if ((rc = core_load_record(r, n, 0)) != 0) { printf("core: load %u\n", (unsigned)rc); return 1; }
    core_poll(30);
    if ((rc = core_song_prepare(0)) != 0) { printf("core: song %u\n", (unsigned)rc); return 1; }
    if (!(f = fopen(out, "wb"))) return 2;
    for (uint32_t left = secs * FS; left;) {
        uint32_t k = left > 1024u ? 1024u : left;
        fwrite(core_render(k), sizeof(float), 2u * k, f);
        left -= k;
    }
    fclose(f);
    printf("core: rendered %u s, state %#x\n", (unsigned)secs, (unsigned)core_song_state());
    return 0;
}
int main(int argc, char **argv)
{
    static uint8_t in[1 << 20], out[1 << 18];
    if (argc == 5 && !strcmp(argv[1], "render")) return render_main(argv[2], (uint32_t)atoi(argv[3]), argv[4]);
    size_t n = fread(in, 1, sizeof in, stdin), at = 0;
    core_boot(0);
    core_poll(100);
    while (at < n) {
        size_t e = at;
        while (e < n && in[e] != 0xF7u) e++;
        if (e == n) break;
        printf(">");
        for (size_t i = at; i <= e; i++) printf(" %02X", in[i]);
        printf("\n");
        core_sysex_in(in + at, (uint32_t)(e + 1u - at));
        uint32_t k = core_sysex_out(out, sizeof out);
        for (uint32_t i = 0; i < k; i++) {
            if (out[i] == 0xF0u) printf("<");
            printf(" %02X", out[i]);
            if (out[i] == 0xF7u) printf("\n");
        }
        at = e + 1u;
    }
    return 0;
}
#endif
